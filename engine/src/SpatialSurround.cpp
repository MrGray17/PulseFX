#include "pulsefx/SpatialSurround.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace pulsefx {
namespace {
constexpr float kPi = 3.14159265358979323846f;

float onePoleCoeff(float sampleRate, float frequency) noexcept {
    const float rate = std::clamp(sampleRate, 8000.0f, 384000.0f);
    const float clampedFrequency = std::clamp(frequency, 5.0f, rate * 0.20f);
    return 1.0f - std::exp(-2.0f * kPi * clampedFrequency / rate);
}

float finiteOrZero(float value) noexcept {
    return std::isfinite(value) ? value : 0.0f;
}
} // namespace

HrtfProfile SpatialSurround::makeDefaultProfile(float sampleRate) noexcept {
    HrtfProfile profile{};
    profile.taps = 96;

    // Symmetric analytic fallback: direct ipsilateral energy plus a delayed,
    // spectrally softened contralateral path. Measured HRIRs replace this at
    // runtime when available.
    profile.leftToLeft[0] = 0.88f;
    profile.leftToLeft[3] = 0.10f;
    profile.leftToLeft[8] = -0.055f;
    profile.leftToLeft[14] = 0.028f;
    profile.rightToRight = profile.leftToLeft;

    const float clampedRate = std::clamp(sampleRate, 8000.0f, 192000.0f);
    const std::size_t maxDelay = profile.taps - 8;
    const std::size_t delay = std::min<std::size_t>(
        maxDelay,
        static_cast<std::size_t>(std::lround(clampedRate * 0.00042f)));
    constexpr std::array<float, 6> headShadow{0.24f, 0.18f, 0.12f, 0.075f, 0.04f, 0.02f};
    for (std::size_t i = 0; i < headShadow.size() && delay + i < profile.taps; ++i) {
        profile.leftToRight[delay + i] = headShadow[i];
        profile.rightToLeft[delay + i] = headShadow[i];
    }
    return profile;
}

void SpatialSurround::prepare(float sampleRate) noexcept {
    const float clampedRate = std::clamp(sampleRate, 8000.0f, 384000.0f);
    setProfile(makeDefaultProfile(clampedRate));

    // Low bass stays predominantly on the original stereo image. Most spatial
    // manipulation happens above this anchor region.
    lowAnchorCoeff_ = onePoleCoeff(clampedRate, 180.0f);

    // Early reflections are intentionally short and damped. Their delays are
    // kept inside the fixed realtime buffer even at the highest supported rate.
    constexpr std::array<float, kReflectionCount> delayMs{6.8f, 12.7f, 19.0f};
    for (std::size_t i = 0; i < reflectionDelays_.size(); ++i) {
        reflectionDelays_[i] = std::clamp<std::size_t>(
            static_cast<std::size_t>(std::lround(clampedRate * delayMs[i] / 1000.0f)),
            1,
            kReflectionBufferSize - 1);
    }
    reflectionDampingCoeff_ = onePoleCoeff(clampedRate, 5200.0f);
    smoothing_ = 1.0f - std::exp(-1.0f / (0.026f * clampedRate));
    reset();
}

void SpatialSurround::setAmount(float amount) noexcept {
    amountTarget_ = std::clamp(std::isfinite(amount) ? amount : 0.0f, 0.0f, 1.0f);
}

void SpatialSurround::setProfile(const HrtfProfile& profile) noexcept {
    const std::size_t taps = std::clamp<std::size_t>(profile.taps, 1, HrtfProfile::kMaxTaps);
    leftToLeft_.setImpulse(profile.leftToLeft.data(), taps);
    leftToRight_.setImpulse(profile.leftToRight.data(), taps);
    rightToLeft_.setImpulse(profile.rightToLeft.data(), taps);
    rightToRight_.setImpulse(profile.rightToRight.data(), taps);
}

void SpatialSurround::reset() noexcept {
    leftToLeft_.reset();
    leftToRight_.reset();
    rightToLeft_.reset();
    rightToRight_.reset();
    reflectionLeft_.fill(0.0f);
    reflectionRight_.fill(0.0f);
    reflectionWriteIndex_ = 0;
    dryLowLeft_ = dryLowRight_ = 0.0f;
    wetLowLeft_ = wetLowRight_ = 0.0f;
    reflectionDampedLeft_ = reflectionDampedRight_ = 0.0f;
    amountCurrent_ = amountTarget_;
}

float SpatialSurround::readReflection(
    const std::array<float, kReflectionBufferSize>& buffer,
    std::size_t delay) const noexcept {
    return buffer[(reflectionWriteIndex_ + kReflectionBufferSize - delay) % kReflectionBufferSize];
}

void SpatialSurround::processStereo(float& left, float& right) noexcept {
    left = finiteOrZero(left);
    right = finiteOrZero(right);
    amountCurrent_ += (amountTarget_ - amountCurrent_) * smoothing_;

    const float dryLeft = left;
    const float dryRight = right;

    // Keep convolution histories warm even at zero mix so enabling the effect
    // mid-stream does not begin from an artificial empty HRTF state.
    const float wetLeft = leftToLeft_.process(dryLeft) + rightToLeft_.process(dryRight);
    const float wetRight = leftToRight_.process(dryLeft) + rightToRight_.process(dryRight);

    dryLowLeft_ += (dryLeft - dryLowLeft_) * lowAnchorCoeff_;
    dryLowRight_ += (dryRight - dryLowRight_) * lowAnchorCoeff_;
    wetLowLeft_ += (wetLeft - wetLowLeft_) * lowAnchorCoeff_;
    wetLowRight_ += (wetRight - wetLowRight_) * lowAnchorCoeff_;

    const float dryHighLeft = dryLeft - dryLowLeft_;
    const float dryHighRight = dryRight - dryLowRight_;
    const float wetHighLeft = wetLeft - wetLowLeft_;
    const float wetHighRight = wetRight - wetLowRight_;

    const float mid = 0.5f * (dryLeft + dryRight);
    const float side = 0.5f * (dryLeft - dryRight);
    const float imageEnergy = std::abs(mid) + std::abs(side) + 1.0e-6f;
    const float centerConfidence = std::clamp(std::abs(mid) / imageEnergy, 0.0f, 1.0f);
    const float sideDominance = std::clamp(std::abs(side) / imageEnergy, 0.0f, 1.0f);

    // Strong phantom-center material receives less HRTF wet energy so vocals
    // stay focused. Already extreme/anti-phase stereo also backs the renderer
    // off rather than widening unstable material further.
    const float wideGuard = std::clamp((sideDominance - 0.72f) / 0.28f, 0.0f, 1.0f);
    const float imageGuard = std::clamp(
        1.0f - 0.36f * centerConfidence - 0.45f * wideGuard,
        0.42f,
        1.0f);
    const float highMix = amountCurrent_ * imageGuard;
    const float lowMix = amountCurrent_ * 0.18f;

    float outputLeft =
        dryLowLeft_ + (wetLowLeft_ - dryLowLeft_) * lowMix +
        dryHighLeft + (wetHighLeft - dryHighLeft) * highMix;
    float outputRight =
        dryLowRight_ + (wetLowRight_ - dryLowRight_) * lowMix +
        dryHighRight + (wetHighRight - dryHighRight) * highMix;

    // The early-reflection field is generated from the high-passed dry signal
    // to avoid smearing/relocating bass. Cross-ear asymmetry creates useful
    // interaural variation without turning the stage into a long reverb.
    const float rawReflectionLeft =
        0.42f * readReflection(reflectionLeft_, reflectionDelays_[0]) +
        0.25f * readReflection(reflectionRight_, reflectionDelays_[1]) -
        0.12f * readReflection(reflectionLeft_, reflectionDelays_[2]);
    const float rawReflectionRight =
        0.42f * readReflection(reflectionRight_, reflectionDelays_[0]) +
        0.25f * readReflection(reflectionLeft_, reflectionDelays_[1]) -
        0.12f * readReflection(reflectionRight_, reflectionDelays_[2]);

    reflectionDampedLeft_ += (rawReflectionLeft - reflectionDampedLeft_) * reflectionDampingCoeff_;
    reflectionDampedRight_ += (rawReflectionRight - reflectionDampedRight_) * reflectionDampingCoeff_;

    reflectionLeft_[reflectionWriteIndex_] = dryHighLeft;
    reflectionRight_[reflectionWriteIndex_] = dryHighRight;
    reflectionWriteIndex_ = (reflectionWriteIndex_ + 1) % kReflectionBufferSize;

    const float reflectionMix = 0.052f * amountCurrent_ * (1.0f - 0.35f * wideGuard);
    outputLeft += reflectionDampedLeft_ * reflectionMix;
    outputRight += reflectionDampedRight_ * reflectionMix;

    // A small deterministic trim reduces the usual "spatial sounds better
    // because it is louder" bias while leaving exact zero-amount transparency.
    const float outputTrim = 1.0f - 0.035f * amountCurrent_;
    left = outputLeft * outputTrim;
    right = outputRight * outputTrim;
}

} // namespace pulsefx
