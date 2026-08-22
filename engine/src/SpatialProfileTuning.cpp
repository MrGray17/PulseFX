#include "pulsefx/SpatialProfileTuning.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace pulsefx {
namespace {

float finiteOr(float value, float fallback) noexcept {
    return std::isfinite(value) ? value : fallback;
}

float dbToGain(float db) noexcept {
    return std::pow(10.0f, db / 20.0f);
}

template <std::size_t N>
void scalePath(std::array<float, N>& path, std::size_t taps, float gain) noexcept {
    for (std::size_t index = 0; index < taps; ++index) {
        const float sample = finiteOr(path[index], 0.0f);
        path[index] = sample * gain;
    }
    for (std::size_t index = taps; index < N; ++index) path[index] = 0.0f;
}

template <std::size_t N>
void resampleDelayPath(
    const std::array<float, N>& source,
    std::array<float, N>& destination,
    std::size_t taps,
    float scale) noexcept {
    destination.fill(0.0f);
    if (taps == 0) return;

    // Map destination sample time back into the source timeline. Linear
    // interpolation avoids the staircase timing changes produced by integer
    // tap shifting during calibration.
    for (std::size_t outIndex = 0; outIndex < taps; ++outIndex) {
        const float sourcePosition = static_cast<float>(outIndex) / scale;
        const auto lower = static_cast<std::size_t>(sourcePosition);
        if (lower >= taps) break;
        const std::size_t upper = std::min(lower + 1, taps - 1);
        const float fraction = sourcePosition - static_cast<float>(lower);
        const float a = finiteOr(source[lower], 0.0f);
        const float b = finiteOr(source[upper], 0.0f);
        destination[outIndex] = a + (b - a) * fraction;
    }
}

float outputL1(const HrtfProfile& profile, bool leftEar) noexcept {
    float total = 0.0f;
    for (std::size_t index = 0; index < profile.taps; ++index) {
        if (leftEar) {
            total += std::abs(profile.leftToLeft[index]);
            total += std::abs(profile.rightToLeft[index]);
        } else {
            total += std::abs(profile.leftToRight[index]);
            total += std::abs(profile.rightToRight[index]);
        }
    }
    return total;
}

void capOutputEnergy(HrtfProfile& profile) noexcept {
    // The analytic fallback is already around this region. This is not a
    // loudness target; it is a final guard against pathological imported HRIRs
    // or extreme calibration values producing excessive FIR path energy.
    constexpr float kMaxEarL1 = 2.0f;
    const float maximum = std::max(outputL1(profile, true), outputL1(profile, false));
    if (!std::isfinite(maximum) || maximum <= 0.0f) return;
    if (maximum <= kMaxEarL1) return;
    const float scale = kMaxEarL1 / maximum;
    scalePath(profile.leftToLeft, profile.taps, scale);
    scalePath(profile.leftToRight, profile.taps, scale);
    scalePath(profile.rightToLeft, profile.taps, scale);
    scalePath(profile.rightToRight, profile.taps, scale);
}

} // namespace

SpatialProfileTuning sanitizeSpatialProfileTuning(SpatialProfileTuning tuning) noexcept {
    tuning.itdScale = std::clamp(finiteOr(tuning.itdScale, 1.0f), 0.70f, 1.40f);
    tuning.ipsilateralGain = std::clamp(finiteOr(tuning.ipsilateralGain, 1.0f), 0.70f, 1.25f);
    tuning.contralateralGain = std::clamp(finiteOr(tuning.contralateralGain, 1.0f), 0.45f, 1.35f);
    tuning.wetTrimDb = std::clamp(finiteOr(tuning.wetTrimDb, 0.0f), -6.0f, 3.0f);
    return tuning;
}

HrtfProfile tuneSpatialProfile(
    const HrtfProfile& base,
    SpatialProfileTuning tuning) noexcept {
    tuning = sanitizeSpatialProfileTuning(tuning);

    HrtfProfile result{};
    result.taps = std::clamp<std::size_t>(base.taps, 1, HrtfProfile::kMaxTaps);

    for (std::size_t index = 0; index < result.taps; ++index) {
        result.leftToLeft[index] = finiteOr(base.leftToLeft[index], 0.0f);
        result.rightToRight[index] = finiteOr(base.rightToRight[index], 0.0f);
    }

    resampleDelayPath(base.leftToRight, result.leftToRight, result.taps, tuning.itdScale);
    resampleDelayPath(base.rightToLeft, result.rightToLeft, result.taps, tuning.itdScale);

    const float trim = dbToGain(tuning.wetTrimDb);
    scalePath(result.leftToLeft, result.taps, tuning.ipsilateralGain * trim);
    scalePath(result.rightToRight, result.taps, tuning.ipsilateralGain * trim);
    scalePath(result.leftToRight, result.taps, tuning.contralateralGain * trim);
    scalePath(result.rightToLeft, result.taps, tuning.contralateralGain * trim);

    capOutputEnergy(result);
    return result;
}

} // namespace pulsefx
