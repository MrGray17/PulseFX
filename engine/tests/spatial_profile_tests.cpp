#include "pulsefx/SpatialProfileTuning.h"
#include "pulsefx/SpatialSurround.h"
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>

namespace {

bool near(float a, float b, float epsilon = 1.0e-5f) {
    return std::abs(a - b) <= epsilon;
}

std::size_t firstMeaningfulTap(const std::array<float, pulsefx::HrtfProfile::kMaxTaps>& path, std::size_t taps) {
    for (std::size_t index = 0; index < taps; ++index) {
        if (std::abs(path[index]) > 1.0e-4f) return index;
    }
    return taps;
}

void testNeutralPreservesProfile() {
    const auto base = pulsefx::SpatialSurround::makeDefaultProfile(48000.0f);
    const auto tuned = pulsefx::tuneSpatialProfile(base, {});
    assert(tuned.taps == base.taps);
    for (std::size_t index = 0; index < base.taps; ++index) {
        assert(near(tuned.leftToLeft[index], base.leftToLeft[index]));
        assert(near(tuned.leftToRight[index], base.leftToRight[index]));
        assert(near(tuned.rightToLeft[index], base.rightToLeft[index]));
        assert(near(tuned.rightToRight[index], base.rightToRight[index]));
    }
}

void testItdScaleMovesContralateralDelay() {
    const auto base = pulsefx::SpatialSurround::makeDefaultProfile(48000.0f);
    const auto neutral = pulsefx::tuneSpatialProfile(base, {});
    pulsefx::SpatialProfileTuning wider{};
    wider.itdScale = 1.35f;
    const auto tuned = pulsefx::tuneSpatialProfile(base, wider);
    assert(firstMeaningfulTap(tuned.leftToRight, tuned.taps) > firstMeaningfulTap(neutral.leftToRight, neutral.taps));
    assert(firstMeaningfulTap(tuned.rightToLeft, tuned.taps) > firstMeaningfulTap(neutral.rightToLeft, neutral.taps));
}

void testSymmetryIsPreserved() {
    const auto base = pulsefx::SpatialSurround::makeDefaultProfile(48000.0f);
    pulsefx::SpatialProfileTuning tuning{};
    tuning.itdScale = 1.2f;
    tuning.ipsilateralGain = 1.1f;
    tuning.contralateralGain = 0.8f;
    tuning.wetTrimDb = -1.0f;
    const auto tuned = pulsefx::tuneSpatialProfile(base, tuning);
    for (std::size_t index = 0; index < tuned.taps; ++index) {
        assert(near(tuned.leftToLeft[index], tuned.rightToRight[index]));
        assert(near(tuned.leftToRight[index], tuned.rightToLeft[index]));
    }
}

void testInvalidTuningFallsBackAndClamps() {
    pulsefx::SpatialProfileTuning tuning{};
    tuning.itdScale = std::numeric_limits<float>::quiet_NaN();
    tuning.ipsilateralGain = 99.0f;
    tuning.contralateralGain = -99.0f;
    tuning.wetTrimDb = std::numeric_limits<float>::infinity();
    const auto safe = pulsefx::sanitizeSpatialProfileTuning(tuning);
    assert(near(safe.itdScale, 1.0f));
    assert(near(safe.ipsilateralGain, 1.25f));
    assert(near(safe.contralateralGain, 0.45f));
    assert(near(safe.wetTrimDb, 0.0f));
}

void testNonFiniteHrirSamplesCannotEscape() {
    auto base = pulsefx::SpatialSurround::makeDefaultProfile(48000.0f);
    base.leftToLeft[0] = std::numeric_limits<float>::quiet_NaN();
    base.leftToRight[10] = std::numeric_limits<float>::infinity();
    const auto tuned = pulsefx::tuneSpatialProfile(base, {});
    for (std::size_t index = 0; index < tuned.taps; ++index) {
        assert(std::isfinite(tuned.leftToLeft[index]));
        assert(std::isfinite(tuned.leftToRight[index]));
        assert(std::isfinite(tuned.rightToLeft[index]));
        assert(std::isfinite(tuned.rightToRight[index]));
    }
}

void testPathologicalProfileGetsEnergyCapped() {
    pulsefx::HrtfProfile base{};
    base.taps = 64;
    for (std::size_t index = 0; index < base.taps; ++index) {
        base.leftToLeft[index] = 10.0f;
        base.leftToRight[index] = 10.0f;
        base.rightToLeft[index] = 10.0f;
        base.rightToRight[index] = 10.0f;
    }
    const auto tuned = pulsefx::tuneSpatialProfile(base, {1.4f, 1.25f, 1.35f, 3.0f});
    float leftEarL1 = 0.0f;
    float rightEarL1 = 0.0f;
    for (std::size_t index = 0; index < tuned.taps; ++index) {
        leftEarL1 += std::abs(tuned.leftToLeft[index]) + std::abs(tuned.rightToLeft[index]);
        rightEarL1 += std::abs(tuned.leftToRight[index]) + std::abs(tuned.rightToRight[index]);
    }
    assert(leftEarL1 <= 2.0001f);
    assert(rightEarL1 <= 2.0001f);
}

} // namespace

int main() {
    testNeutralPreservesProfile();
    testItdScaleMovesContralateralDelay();
    testSymmetryIsPreserved();
    testInvalidTuningFallsBackAndClamps();
    testNonFiniteHrirSamplesCannotEscape();
    testPathologicalProfileGetsEnergyCapped();
    return 0;
}
