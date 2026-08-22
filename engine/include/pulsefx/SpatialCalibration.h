#pragma once
#include "SpatialProfileTuning.h"

namespace pulsefx {

// Compose the adaptive Signature spatial intent with a listener/headphone
// calibration. Calibration is a multiplicative/relative transform, not a second
// realtime effect. The result is sanitized back into the same conservative
// bounds used by the HRTF transformer.
inline SpatialProfileTuning composeSpatialProfileTuning(
    SpatialProfileTuning signature,
    SpatialProfileTuning calibration) noexcept {
    signature = sanitizeSpatialProfileTuning(signature);
    calibration = sanitizeSpatialProfileTuning(calibration);

    SpatialProfileTuning combined{};
    combined.itdScale = signature.itdScale * calibration.itdScale;
    combined.ipsilateralGain = signature.ipsilateralGain * calibration.ipsilateralGain;
    combined.contralateralGain = signature.contralateralGain * calibration.contralateralGain;
    combined.wetTrimDb = signature.wetTrimDb + calibration.wetTrimDb;
    return sanitizeSpatialProfileTuning(combined);
}

} // namespace pulsefx
