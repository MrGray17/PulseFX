#include "pulsefx/Dynamics.h"
#include <algorithm>
#include <cmath>

namespace pulsefx {
namespace {
float dbToLinear(float db) noexcept { return std::pow(10.0f, db / 20.0f); }
float linearToDb(float x) noexcept { return 20.0f * std::log10(std::max(x, 1.0e-9f)); }
float timeCoeff(float sampleRate, float ms) noexcept {
    return std::exp(-1.0f / (0.001f * ms * sampleRate));
}

float softKneeGainDb(float inputDb, float thresholdDb, float ratio, float kneeDb) noexcept {
    if (ratio <= 1.0f) return 0.0f;
    const float x = inputDb - thresholdDb;
    const float slope = 1.0f / ratio - 1.0f;
    if (x <= -0.5f * kneeDb) return 0.0f;
    if (x >= 0.5f * kneeDb) return slope * x;
    const float y = x + 0.5f * kneeDb;
    return slope * y * y / (2.0f * kneeDb);
}
}

void Dynamics::prepare(float sampleRate) noexcept {
    sampleRate_ = std::clamp(sampleRate, 8000.0f, 384000.0f);
    amount_.prepare(sampleRate_, 45.0f, 0.0f);
    updateTiming();
    reset();
}

void Dynamics::updateTiming() noexcept {
    detectorAttackCoeff_ = timeCoeff(sampleRate_, nightMode_ ? 8.0f : 14.0f);
    detectorReleaseCoeff_ = timeCoeff(sampleRate_, nightMode_ ? 220.0f : 150.0f);
    gainAttackCoeff_ = timeCoeff(sampleRate_, 5.0f);
    gainReleaseCoeff_ = timeCoeff(sampleRate_, nightMode_ ? 180.0f : 120.0f);
}

void Dynamics::setAmount(float amount) noexcept {
    amount_.setTarget(std::clamp(amount, 0.0f, 1.0f));
}

void Dynamics::setNightMode(bool enabled) noexcept {
    if (nightMode_ == enabled) return;
    nightMode_ = enabled;
    updateTiming();
}

void Dynamics::reset() noexcept {
    detector_ = 0.0f;
    gain_ = 1.0f;
    gainReductionDb_ = 0.0f;
}

void Dynamics::processStereo(float& left, float& right) noexcept {
    const float amount = amount_.next();
    if (amount <= 1.0e-5f && !nightMode_) return;

    const float peak = std::max(std::abs(left), std::abs(right));
    const float detectorCoeff = peak > detector_ ? detectorAttackCoeff_ : detectorReleaseCoeff_;
    detector_ = detectorCoeff * detector_ + (1.0f - detectorCoeff) * peak;

    const float detectorDb = linearToDb(detector_);
    const float thresholdDb = nightMode_ ? -24.0f : (-10.0f - 8.0f * amount);
    const float ratio = nightMode_ ? 3.5f : (1.0f + 1.4f * amount);
    const float kneeDb = nightMode_ ? 10.0f : 7.0f;
    const float desiredReductionDb = softKneeGainDb(detectorDb, thresholdDb, ratio, kneeDb);
    const float makeupDb = nightMode_ ? 3.0f : 1.3f * amount;
    const float targetGain = dbToLinear(desiredReductionDb + makeupDb);

    const float gainCoeff = targetGain < gain_ ? gainAttackCoeff_ : gainReleaseCoeff_;
    gain_ = gainCoeff * gain_ + (1.0f - gainCoeff) * targetGain;

    left *= gain_;
    right *= gain_;
    gainReductionDb_ = std::max(0.0f, -linearToDb(std::min(gain_, 1.0f)));
}

} // namespace pulsefx
