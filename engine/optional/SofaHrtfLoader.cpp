#include "pulsefx/SofaHrtfLoader.h"
#include <mysofa.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace pulsefx {
namespace {

std::size_t copyWithDelay(
    const std::vector<float>& source,
    float delaySeconds,
    float sampleRate,
    std::array<float, HrtfProfile::kMaxTaps>& destination) noexcept {
    const long roundedDelay = std::lround(std::max(0.0f, delaySeconds) * sampleRate);
    const std::size_t delaySamples = std::min<std::size_t>(
        static_cast<std::size_t>(std::max<long>(0, roundedDelay)),
        HrtfProfile::kMaxTaps - 1);
    const std::size_t copyCount = std::min<std::size_t>(
        source.size(),
        HrtfProfile::kMaxTaps - delaySamples);
    for (std::size_t i = 0; i < copyCount; ++i) {
        destination[delaySamples + i] = source[i];
    }
    return delaySamples + copyCount;
}

bool fetchVirtualSpeaker(
    MYSOFA_EASY* easy,
    int filterLength,
    float azimuthDegrees,
    float elevationDegrees,
    float distanceMeters,
    float sampleRate,
    std::array<float, HrtfProfile::kMaxTaps>& toLeftEar,
    std::array<float, HrtfProfile::kMaxTaps>& toRightEar,
    std::size_t& usedTaps) {
    if (!easy || filterLength <= 0) return false;

    std::vector<float> left(static_cast<std::size_t>(filterLength));
    std::vector<float> right(static_cast<std::size_t>(filterLength));
    float leftDelay = 0.0f;
    float rightDelay = 0.0f;
    float coordinate[3]{azimuthDegrees, elevationDegrees, distanceMeters};
    mysofa_s2c(coordinate);
    mysofa_getfilter_float(
        easy,
        coordinate[0], coordinate[1], coordinate[2],
        left.data(), right.data(),
        &leftDelay, &rightDelay);

    usedTaps = std::max(
        copyWithDelay(left, leftDelay, sampleRate, toLeftEar),
        copyWithDelay(right, rightDelay, sampleRate, toRightEar));
    return usedTaps > 0;
}

} // namespace

SofaHrtfLoadResult loadStereoHrtfProfileFromSofa(
    const char* path,
    float sampleRate,
    HrtfProfile& destination,
    float speakerAzimuthDegrees,
    float speakerElevationDegrees,
    float speakerDistanceMeters) noexcept {
    SofaHrtfLoadResult result{};
    if (!path || sampleRate < 8000.0f || speakerDistanceMeters <= 0.0f) {
        result.errorCode = MYSOFA_INVALID_FORMAT;
        return result;
    }

    int filterLength = 0;
    int error = MYSOFA_OK;
    MYSOFA_EASY* easy = mysofa_open(path, sampleRate, &filterLength, &error);
    result.errorCode = error;
    result.sourceFilterLength = filterLength;
    if (!easy || error != MYSOFA_OK || filterLength <= 0) {
        if (easy) mysofa_close(easy);
        return result;
    }

    HrtfProfile profile{};
    std::size_t leftUsed = 0;
    std::size_t rightUsed = 0;
    const float azimuth = std::clamp(std::abs(speakerAzimuthDegrees), 5.0f, 85.0f);
    const bool leftOk = fetchVirtualSpeaker(
        easy, filterLength,
        azimuth, speakerElevationDegrees, speakerDistanceMeters, sampleRate,
        profile.leftToLeft, profile.leftToRight, leftUsed);
    const bool rightOk = fetchVirtualSpeaker(
        easy, filterLength,
        -azimuth, speakerElevationDegrees, speakerDistanceMeters, sampleRate,
        profile.rightToLeft, profile.rightToRight, rightUsed);
    mysofa_close(easy);

    if (!leftOk || !rightOk) {
        result.errorCode = MYSOFA_READ_ERROR;
        return result;
    }

    profile.taps = std::clamp<std::size_t>(std::max(leftUsed, rightUsed), 1, HrtfProfile::kMaxTaps);
    destination = profile;
    result.ok = true;
    result.errorCode = MYSOFA_OK;
    result.usedFilterLength = static_cast<int>(profile.taps);
    return result;
}

} // namespace pulsefx
