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

    const float clampedRate = std::clamp(
        std::isfinite(sampleRate) ? sampleRate : 48000.0f,
        8000.0f,
        192000.0f);
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
    const float clampedRate = std::clamp(
        std::isfinite(sampleRate) ? sampleRate : 48000.0f,
        8000.0f,
        384000.0f);

    activeHrtfBank_ = 0;
    targetHrtfBank_ = 1;
    pendingHrtfBank_ = 2;
    profileInitialized_ = false;
    profileTransitionActive_ = false;
    pendingProfileReady_ = false;
    profileTransition_ = 1.0f;
    profileTransitionStep_ = 1.0f / std::max(1.0f, clampedRate * 0.045f);
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

void SpatialSurround::loadProfile(std::size_t bankIndex, const HrtfProfile& profile) noexcept {
    if (bankIndex >= hrtfBanks_.size()) return;
    const std::size_t taps = std::clamp<std::size_t>(profile.taps, 1, HrtfProfile::kMaxTaps);
    auto& bank = hrtfBanks_[bankIndex];
    bank.leftToLeft.setImpulse(profile.leftToLeft.data(), taps);
    bank.leftToRight.setImpulse(profile.leftToRight.data(), taps);
    bank.rightToLeft.setImpulse(profile.rightToLeft.data(), taps);
    bank.rightToRight.setImpulse(profile.rightToRight.data(), taps);
}

void SpatialSurround::startProfileTransition(std::size_t targetBank) noexcept {
    targetHrtfBank_ = targetBank;
    profileTransition_ = 0.0f;
    profileTransitionActive_ = true;
}

void SpatialSurround::setProfile(const HrtfProfile& profile) noexcept {
    if (!profileInitialized_) {
        loadProfile(activeHrtfBank_, profile);
        profileInitialized_ = true;
        profileTransitionActive_ = false;
        pendingProfileReady_ = false;
        profileTransition_ = 1.0f;
        return;
    }

    if (!profileTransitionActive_) {
        std::size_t target = 0;
        for (; target < hrtfBanks_.size(); ++target) {
            if (target != activeHrtfBank_) break;
        }
        loadProfile(target, profile);
        pendingProfileReady_ = false;
        startProfileTransition(target);
        return;
    }

    // While one crossfade is active, preload only the newest requested profile
    // into the third, currently inaudible bank. Multiple rapid calibration
    // updates collapse to the newest one without coefficient loading from the
    // realtime process method.
    std::size_t spare = 0;
    for (; spare < hrtfBanks_.size(); ++spare) {
        if (spare != activeHrtfBank_ && spare != targetHrtfBank_) break;
    }
    if (spare >= hrtfBanks_.size()) return;
    loadProfile(spare, profile);
    pendingHrtfBank_ = spare;
    pendingProfileReady_ = true;
}

void SpatialSurround::reset() noexcept {
    for (auto& bank : hrtfBanks_) bank.reset();
    reflectionLeft_.fill(0.0f);
    reflectionRight_.fill(0.0f);
    reflectionWriteIndex_ = 0;
    dryLowLeft_ = dryLowRight_ = 0.0f;
    wetLowLeft_ = wetLowRight_ = 0.0f;
    reflectionDampedLeft_ = reflectionDampedRight_ = 0.0f;
    amountCurrent_ = amountTarget_;
}

void SpatialSurround::processHrtfBank(
    std::size_t bankIndex,
    float dryLeft,
    float dryRight,
    float& wetLeft,
    float& wetRight) noexcept {
    if (bankIndex >= hrtfBanks_.size()) {
        wetLeft = dryLeft;
        wetRight = dryRight;
        return;
    }
    auto& bank = hrtfBanks_[bankIndex];
    wetLeft = bank.leftToLeft.process(dryLeft) + bank.rightToLeft.process(dryRight);
    wetRight = bank.leftToRight.process(dryLeft) + bank.rightToRight.process(dryRight);
    wetLeft = finiteOrZero(wetLeft);
    wetRight = finiteOrZero(wetRight);
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

    float activeWetLeft = 0.0f;
    float activeWetRight = 0.0f;
    processHrtfBank(activeHrtfBank_, dryLeft, dryRight, activeWetLeft, activeWetRight);

    float wetLeft = activeWetLeft;
    float wetRight = activeWetRight;
    if (profileTransitionActive_) {
        float targetWetLeft = 0.0f;
        float targetWetRight = 0.0f;
        processHrtfBank(targetHrtfBank_, dryLeft, dryRight, targetWetLeft, targetWetRight);
        const float transition = std::clamp(profileTransition_, 0.0f, 1.0f);
        wetLeft = activeWetLeft + (targetWetLeft - activeWetLeft) * transition;
        wetRight = activeWetRight + (targetWetRight - activeWetRight) * transition;

        profileTransition_ += profileTransitionStep_;
        if (profileTransition_ >= 1.0f) {
            activeHrtfBank_ = targetHrtfBank_;
            profileTransition_ = 1.0f;
            profileTransitionActive_ = false;

            // A pending bank was fully loaded by setProfile(), outside this
            // sample loop. Starting the next crossfade only changes indices.
            if (pendingProfileReady_) {
                const std::size_t nextTarget = pendingHrtfBank_;
                pendingProfileReady_ = false;
                startProfileTransition(nextTarget);
            }
        }
    }

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
    left = finiteOrZero(outputLeft * outputTrim);
    right = finiteOrZero(outputRight * outputTrim);
}

} // namespace pulsefx
