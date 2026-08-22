#pragma once
#include "SpatialSurround.h"

namespace pulsefx {

// Compact listener/device adaptation applied to an HRTF profile before it is
// installed into the realtime FIR convolvers. This keeps personalization work
// entirely outside the audio callback.
struct SpatialProfileTuning {
    // Stretch/compress contralateral timing cues. Values above 1 increase the
    // apparent interaural delay; values below 1 reduce it.
    float itdScale{1.0f};

    // Relative strength of the ipsilateral and contralateral HRIR paths.
    float ipsilateralGain{1.0f};
    float contralateralGain{1.0f};

    // Final wet-profile trim before the normal SpatialSurround wet/dry blend.
    float wetTrimDb{0.0f};
};

// Sanitize user/calibration values to conservative perceptual and headroom
// bounds. Non-finite values fall back to neutral rather than poisoning audio.
SpatialProfileTuning sanitizeSpatialProfileTuning(SpatialProfileTuning tuning) noexcept;

// Return a transformed copy of base. The source profile is never mutated.
// Contralateral paths are delay-scaled with linear interpolation, gains are
// bounded, and the result is headroom-normalized against pathological input.
HrtfProfile tuneSpatialProfile(
    const HrtfProfile& base,
    SpatialProfileTuning tuning) noexcept;

} // namespace pulsefx
