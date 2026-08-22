#pragma once
#include "SpatialSurround.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace pulsefx {

// Compact listener-specific calibration. These values are deliberately small
// and bounded so a desktop calibration wizard can explore them safely without
// changing realtime callback complexity.
struct SpatialCalibration {
    float itdScale{1.0f};            // 0.65 .. 1.45, scales cross-ear HRIR timing
    float contralateralGain{1.0f};   // 0.35 .. 1.60, head-shadow/crossfeed energy
    float ipsilateralGain{1.0f};     // 0.75 .. 1.25, same-side HRIR energy
    float wetTrimDb{0.0f};           // -6 .. +3 dB, calibrated profile level trim
};

inline SpatialCalibration sanitizeSpatialCalibration(SpatialCalibration value) noexcept {
    if (!std::isfinite(value.itdScale)) value.itdScale = 1.0f;
    if (!std::isfinite(value.contralateralGain)) value.contralateralGain = 1.0f;
    if (!std::isfinite(value.ipsilateralGain)) value.ipsilateralGain = 1.0f;
    if (!std::isfinite(value.wetTrimDb)) value.wetTrimDb = 0.0f;
    value.itdScale = std::clamp(value.itdScale, 0.65f, 1.45f);
    value.contralateralGain = std::clamp(value.contralateralGain, 0.35f, 1.60f);
    value.ipsilateralGain = std::clamp(value.ipsilateralGain, 0.75f, 1.25f);
    value.wetTrimDb = std::clamp(value.wetTrimDb, -6.0f, 3.0f);
    return value;
}

namespace detail {

inline void scaleImpulse(const std::array<float, HrtfProfile::kMaxTaps>& input,
                         std::array<float, HrtfProfile::kMaxTaps>& output,
                         std::size_t taps,
                         float timeScale,
                         float gain) noexcept {
    output.fill(0.0f);
    if (taps == 0) return;
    const std::size_t safeTaps = std::min(taps, HrtfProfile::kMaxTaps);

    // Linear energy deposition keeps the transform allocation-free and avoids
    // the coarse timing jumps that integer-only tap shifting would introduce.
    for (std::size_t index = 0; index < safeTaps; ++index) {
        const float sample = input[index] * gain;
        if (sample == 0.0f) continue;
        const float target = static_cast<float>(index) * timeScale;
        const auto lower = static_cast<std::size_t>(target);
        if (lower >= HrtfProfile::kMaxTaps) continue;
        const float fraction = target - static_cast<float>(lower);
        output[lower] += sample * (1.0f - fraction);
        if (fraction > 0.0f && lower + 1 < HrtfProfile::kMaxTaps) {
            output[lower + 1] += sample * fraction;
        }
    }
}

inline std::size_t transformedTapCount(std::size_t taps, float timeScale) noexcept {
    if (taps <= 1) return std::max<std::size_t>(1, taps);
    const float last = static_cast<float>(taps - 1) * timeScale;
    return std::clamp<std::size_t>(
        static_cast<std::size_t>(std::ceil(last)) + 1,
        1,
        HrtfProfile::kMaxTaps);
}

} // namespace detail

// Transform a measured or analytic base HRIR outside the realtime callback.
// The resulting HrtfProfile can be atomically installed through
// SpatialSurround::setProfile without adding work to processStereo().
inline HrtfProfile personalizeHrtfProfile(const HrtfProfile& base,
                                          SpatialCalibration calibration) noexcept {
    calibration = sanitizeSpatialCalibration(calibration);
    HrtfProfile result{};
    const std::size_t taps = std::clamp<std::size_t>(base.taps, 1, HrtfProfile::kMaxTaps);
    const float trim = std::pow(10.0f, calibration.wetTrimDb / 20.0f);

    detail::scaleImpulse(base.leftToLeft, result.leftToLeft, taps, 1.0f,
                         calibration.ipsilateralGain * trim);
    detail::scaleImpulse(base.rightToRight, result.rightToRight, taps, 1.0f,
                         calibration.ipsilateralGain * trim);
    detail::scaleImpulse(base.leftToRight, result.leftToRight, taps, calibration.itdScale,
                         calibration.contralateralGain * trim);
    detail::scaleImpulse(base.rightToLeft, result.rightToLeft, taps, calibration.itdScale,
                         calibration.contralateralGain * trim);

    result.taps = std::max(
        taps,
        detail::transformedTapCount(taps, calibration.itdScale));
    return result;
}

} // namespace pulsefx
