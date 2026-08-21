#include "pulsefx/SpatialSurround.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace pulsefx {

HrtfProfile SpatialSurround::makeDefaultProfile(float sampleRate) noexcept {
    HrtfProfile profile{};
    profile.taps = HrtfProfile::kMaxTaps;

    // Symmetric analytic fallback: direct ipsilateral energy plus a delayed,
    // spectrally softened contralateral path. This is intentionally not
    // presented as a measured HRTF; measured HRIRs can replace it at runtime.
    profile.leftToLeft[0] = 0.88f;
    profile.leftToLeft[3] = 0.10f;
    profile.leftToLeft[8] = -0.055f;
    profile.leftToLeft[14] = 0.028f;
    profile.rightToRight = profile.leftToLeft;

    const float clampedRate = std::clamp(sampleRate, 8000.0f, 192000.0f);
    const std::size_t maxDelay = HrtfProfile::kMaxTaps - 8;
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
    smoothing_ = 1.0f - std::exp(-1.0f / (0.020f * clampedRate));
    reset();
}

void SpatialSurround::setAmount(float amount) noexcept {
    amountTarget_ = std::clamp(amount, 0.0f, 1.0f);
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
    amountCurrent_ = amountTarget_;
}

void SpatialSurround::processStereo(float& left, float& right) noexcept {
    amountCurrent_ += (amountTarget_ - amountCurrent_) * smoothing_;
    const float dryLeft = left;
    const float dryRight = right;
    const float wetLeft = leftToLeft_.process(dryLeft) + rightToLeft_.process(dryRight);
    const float wetRight = leftToRight_.process(dryLeft) + rightToRight_.process(dryRight);
    left = dryLeft + (wetLeft - dryLeft) * amountCurrent_;
    right = dryRight + (wetRight - dryRight) * amountCurrent_;
}

} // namespace pulsefx
