#include "pulsefx/ClarityEnhancer.h"
#include <algorithm>
#include <cmath>

namespace pulsefx {
namespace {
constexpr float kPi = 3.14159265358979323846f;

float onePoleCoeff(float sampleRate, float frequency) noexcept {
    const float rate = std::clamp(sampleRate, 8000.0f, 384000.0f);
    const float clampedFrequency = std::clamp(frequency, 5.0f, rate * 0.20f);
    return 1.0f - std::exp(-2.0f * kPi * clampedFrequency / rate);
}

float envelopeCoeff(float sampleRate, float milliseconds) noexcept {
    const float rate = std::clamp(sampleRate, 8000.0f, 384000.0f);
    return 1.0f - std::exp(-1.0f / (0.001f * milliseconds * rate));
}

float finiteOrZero(float value) noexcept {
    return std::isfinite(value) ? value : 0.0f;
}
} // namespace

void ClarityEnhancer::prepare(float sampleRate) noexcept {
    sampleRate_ = std::clamp(sampleRate, 8000.0f, 384000.0f);
    low250Coeff_ = onePoleCoeff(sampleRate_, 250.0f);
    low900Coeff_ = onePoleCoeff(sampleRate_, 900.0f);
    low5800Coeff_ = onePoleCoeff(sampleRate_, 5800.0f);
    amountSmoothing_ = envelopeCoeff(sampleRate_, 35.0f);
    detectorAttack_ = envelopeCoeff(sampleRate_, 7.0f);
    detectorRelease_ = envelopeCoeff(sampleRate_, 100.0f);
    fastAttack_ = envelopeCoeff(sampleRate_, 1.5f);
    fastRelease_ = envelopeCoeff(sampleRate_, 24.0f);
    slowAttack_ = envelopeCoeff(sampleRate_, 32.0f);
    slowRelease_ = envelopeCoeff(sampleRate_, 180.0f);
    reset();
}

void ClarityEnhancer::setAmount(float amount) noexcept {
    amountTarget_ = std::clamp(std::isfinite(amount) ? amount : 0.0f, 0.0f, 1.0f);
}

void ClarityEnhancer::reset() noexcept {
    amountCurrent_ = amountTarget_;
    leftLow250_ = rightLow250_ = 0.0f;
    leftLow900_ = rightLow900_ = 0.0f;
    leftLow5800_ = rightLow5800_ = 0.0f;
    broadbandEnvelope_ = presenceEnvelope_ = mudEnvelope_ = 0.0f;
    fastEnvelope_ = slowEnvelope_ = 0.0f;
}

void ClarityEnhancer::updateEnvelope(
    float level,
    float& envelope,
    float attack,
    float release) noexcept {
    const float coefficient = level > envelope ? attack : release;
    envelope += coefficient * (level - envelope);
}

void ClarityEnhancer::processStereo(float& left, float& right) noexcept {
    left = finiteOrZero(left);
    right = finiteOrZero(right);
    amountCurrent_ += (amountTarget_ - amountCurrent_) * amountSmoothing_;

    const float dryLeft = left;
    const float dryRight = right;

    leftLow250_ += (dryLeft - leftLow250_) * low250Coeff_;
    rightLow250_ += (dryRight - rightLow250_) * low250Coeff_;
    leftLow900_ += (dryLeft - leftLow900_) * low900Coeff_;
    rightLow900_ += (dryRight - rightLow900_) * low900Coeff_;
    leftLow5800_ += (dryLeft - leftLow5800_) * low5800Coeff_;
    rightLow5800_ += (dryRight - rightLow5800_) * low5800Coeff_;

    const float lowMidLeft = leftLow900_ - leftLow250_;
    const float lowMidRight = rightLow900_ - rightLow250_;
    const float presenceLeft = leftLow5800_ - leftLow900_;
    const float presenceRight = rightLow5800_ - rightLow900_;
    const float airLeft = dryLeft - leftLow5800_;
    const float airRight = dryRight - rightLow5800_;

    const float broadbandLevel = 0.5f * (std::abs(dryLeft) + std::abs(dryRight));
    const float presenceLevel = 0.5f * (std::abs(presenceLeft) + std::abs(presenceRight));
    const float mudLevel = 0.5f * (std::abs(lowMidLeft) + std::abs(lowMidRight));

    updateEnvelope(broadbandLevel, broadbandEnvelope_, detectorAttack_, detectorRelease_);
    updateEnvelope(presenceLevel, presenceEnvelope_, detectorAttack_, detectorRelease_);
    updateEnvelope(mudLevel, mudEnvelope_, detectorAttack_, detectorRelease_);
    updateEnvelope(broadbandLevel, fastEnvelope_, fastAttack_, fastRelease_);
    updateEnvelope(broadbandLevel, slowEnvelope_, slowAttack_, slowRelease_);

    const float harshnessRatio = presenceEnvelope_ / std::max(broadbandEnvelope_, 1.0e-4f);
    const float harshnessGuard = std::clamp((harshnessRatio - 0.30f) / 0.62f, 0.0f, 1.0f);

    const float transientRatio = fastEnvelope_ / std::max(slowEnvelope_ + 0.012f, 0.012f);
    const float transientGuard = std::clamp((transientRatio - 1.25f) / 1.75f, 0.0f, 1.0f);

    const float mudRatio = mudEnvelope_ / std::max(broadbandEnvelope_, 1.0e-4f);
    const float mudGuard = std::clamp((mudRatio - 0.38f) / 0.62f, 0.0f, 1.0f);

    // Presence is strongest when useful detail is masked by the rest of the
    // mix, and deliberately restrained when the presence band already dominates.
    const float safeDetail = amountCurrent_
        * (1.0f - 0.72f * harshnessGuard)
        * (1.0f - 0.58f * transientGuard);
    const float presenceGain = 0.17f * safeDetail;
    const float airGain = 0.060f * safeDetail * (1.0f - 0.80f * harshnessGuard);

    // A small adaptive low-mid subtraction reduces masking without turning the
    // enhancer into a fixed smile-curve EQ. It only engages on mud-dominant
    // material and remains linked across channels.
    const float mudCut = 0.045f * amountCurrent_ * mudGuard * (1.0f - 0.35f * transientGuard);

    left = dryLeft + presenceLeft * presenceGain + airLeft * airGain - lowMidLeft * mudCut;
    right = dryRight + presenceRight * presenceGain + airRight * airGain - lowMidRight * mudCut;
}

} // namespace pulsefx
