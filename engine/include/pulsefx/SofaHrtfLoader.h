#pragma once
#include "SpatialSurround.h"

namespace pulsefx {

struct SofaHrtfLoadResult {
    bool ok{false};
    int errorCode{0};
    int sourceFilterLength{0};
    int usedFilterLength{0};
};

// Loads a stereo virtual-speaker HRTF profile from an AES69/SOFA file.
// This function performs file IO, allocation, coordinate lookup and optional
// resampling; call it from control/setup code, never from the realtime callback.
SofaHrtfLoadResult loadStereoHrtfProfileFromSofa(
    const char* path,
    float sampleRate,
    HrtfProfile& destination,
    float speakerAzimuthDegrees = 30.0f,
    float speakerElevationDegrees = 0.0f,
    float speakerDistanceMeters = 1.0f) noexcept;

} // namespace pulsefx
