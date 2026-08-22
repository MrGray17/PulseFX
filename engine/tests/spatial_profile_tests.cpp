#include "pulsefx/SignatureSpatialProfileBank.h"
#include "pulsefx/SpatialCalibration.h"
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

bool sameProfile(const pulsefx::HrtfProfile& a, const pulsefx::HrtfProfile& b) {
    if (a.taps != b.taps) return false;
    for (std::size_t index = 0; index < a.taps; ++index) {
        if (!near(a.leftToLeft[index], b.leftToLeft[index]) ||
            !near(a.leftToRight[index], b.leftToRight[index]) ||
            !near(a.rightToLeft[index], b.rightToLeft[index]) ||
            !near(a.rightToRight[index], b.rightToRight[index])) return false;
    }
    return true;
}

void testNeutralPreservesProfile() {
    const auto base = pulsefx::SpatialSurround::makeDefaultProfile(48000.0f);
    const auto tuned = pulsefx::tuneSpatialProfile(base, {});
    assert(sameProfile(tuned, base));
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

void testCalibrationCompositionIsBounded() {
    pulsefx::SpatialProfileTuning signature{1.08f, 1.03f, 0.92f, -0.6f};
    pulsefx::SpatialProfileTuning neutral{};
    const auto same = pulsefx::composeSpatialProfileTuning(signature, neutral);
    assert(near(same.itdScale, signature.itdScale));
    assert(near(same.ipsilateralGain, signature.ipsilateralGain));
    assert(near(same.contralateralGain, signature.contralateralGain));
    assert(near(same.wetTrimDb, signature.wetTrimDb));

    pulsefx::SpatialProfileTuning calibration{1.10f, 1.05f, 0.88f, -0.7f};
    const auto combined = pulsefx::composeSpatialProfileTuning(signature, calibration);
    assert(combined.itdScale > signature.itdScale);
    assert(combined.contralateralGain < signature.contralateralGain);
    assert(combined.wetTrimDb < signature.wetTrimDb);

    pulsefx::SpatialProfileTuning hostile{};
    hostile.itdScale = std::numeric_limits<float>::infinity();
    hostile.ipsilateralGain = -100.0f;
    hostile.contralateralGain = 100.0f;
    hostile.wetTrimDb = std::numeric_limits<float>::quiet_NaN();
    const auto safe = pulsefx::composeSpatialProfileTuning(hostile, hostile);
    assert(std::isfinite(safe.itdScale));
    assert(std::isfinite(safe.ipsilateralGain));
    assert(std::isfinite(safe.contralateralGain));
    assert(std::isfinite(safe.wetTrimDb));
    assert(safe.itdScale >= 0.75f && safe.itdScale <= 1.40f);
    assert(safe.ipsilateralGain >= 0.65f && safe.ipsilateralGain <= 1.25f);
    assert(safe.contralateralGain >= 0.45f && safe.contralateralGain <= 1.35f);
    assert(safe.wetTrimDb >= -6.0f && safe.wetTrimDb <= 3.0f);
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

void testSignatureProfileBankUsesExactRateAndStableRevisions() {
    pulsefx::SignatureSpatialProfileBank bank;
    assert(!bank.valid());
    assert(!bank.update(std::numeric_limits<float>::quiet_NaN(), {}));
    assert(!bank.valid());

    assert(bank.update(44100.0f, {}));
    assert(bank.valid());
    assert(near(bank.sampleRate(), 44100.0f));
    assert(bank.manualRevision() != 0 && bank.signatureRevision() != 0);
    assert(bank.manualRevision() != bank.signatureRevision());
    const auto manual441 = pulsefx::SpatialSurround::makeDefaultProfile(44100.0f);
    assert(sameProfile(bank.manualProfile(), manual441));
    assert(sameProfile(bank.signatureProfile(), manual441));

    const auto manualRevision = bank.manualRevision();
    const auto signatureRevision = bank.signatureRevision();
    assert(!bank.update(44100.0f, {}));
    assert(bank.manualRevision() == manualRevision);
    assert(bank.signatureRevision() == signatureRevision);

    pulsefx::SpatialProfileTuning expansive{};
    expansive.itdScale = 1.12f;
    expansive.ipsilateralGain = 1.02f;
    expansive.contralateralGain = 0.82f;
    expansive.wetTrimDb = -0.8f;
    assert(bank.update(48000.0f, expansive));
    assert(near(bank.sampleRate(), 48000.0f));
    assert(bank.manualRevision() != manualRevision);
    assert(bank.signatureRevision() != signatureRevision);
    const auto base48 = pulsefx::SpatialSurround::makeDefaultProfile(48000.0f);
    assert(sameProfile(bank.manualProfile(), base48));
    assert(!sameProfile(bank.signatureProfile(), bank.manualProfile()));

    // A listener calibration lives underneath both modes. With neutral adaptive
    // tuning, calibrated Signature and calibrated Manual are identical. With an
    // expansive Signature tuning, Signature then builds on top of calibration.
    pulsefx::SpatialProfileTuning calibration{};
    calibration.itdScale = 1.06f;
    calibration.ipsilateralGain = 1.03f;
    calibration.contralateralGain = 0.90f;
    calibration.wetTrimDb = -0.4f;
    assert(bank.update(48000.0f, {}, calibration));
    assert(!sameProfile(bank.manualProfile(), base48));
    assert(sameProfile(bank.signatureProfile(), bank.manualProfile()));
    const auto calibratedManual = bank.manualProfile();
    assert(bank.update(48000.0f, expansive, calibration));
    assert(sameProfile(bank.manualProfile(), calibratedManual));
    assert(!sameProfile(bank.signatureProfile(), bank.manualProfile()));

    const auto cross48 = firstMeaningfulTap(bank.manualProfile().leftToRight, bank.manualProfile().taps);
    assert(bank.update(96000.0f, expansive, calibration));
    const auto cross96 = firstMeaningfulTap(bank.manualProfile().leftToRight, bank.manualProfile().taps);
    assert(cross96 > cross48);

    const auto stableManualRevision = bank.manualRevision();
    const auto stableSignatureRevision = bank.signatureRevision();
    assert(!bank.update(1000.0f, expansive, calibration));
    assert(bank.manualRevision() == stableManualRevision);
    assert(bank.signatureRevision() == stableSignatureRevision);
    assert(near(bank.sampleRate(), 96000.0f));
}

} // namespace

int main() {
    testNeutralPreservesProfile();
    testItdScaleMovesContralateralDelay();
    testSymmetryIsPreserved();
    testInvalidTuningFallsBackAndClamps();
    testCalibrationCompositionIsBounded();
    testNonFiniteHrirSamplesCannotEscape();
    testPathologicalProfileGetsEnergyCapped();
    testSignatureProfileBankUsesExactRateAndStableRevisions();
    return 0;
}
