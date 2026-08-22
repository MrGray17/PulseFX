#include "pulsefx/ClarityEnhancer.h"
#include "pulsefx/SpatialSurround.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kSampleRate = 48000.0f;

float toneMagnitude(const std::vector<float>& samples, float sampleRate, float frequency) {
    double real = 0.0;
    double imag = 0.0;
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const double phase = 2.0 * static_cast<double>(kPi) * static_cast<double>(frequency)
            * static_cast<double>(index) / static_cast<double>(sampleRate);
        real += static_cast<double>(samples[index]) * std::cos(phase);
        imag -= static_cast<double>(samples[index]) * std::sin(phase);
    }
    const double scale = 2.0 / static_cast<double>(samples.size());
    return static_cast<float>(std::sqrt(real * real + imag * imag) * scale);
}

void testClarityZeroAmountIsTransparent() {
    for (float rate : {44100.0f, 48000.0f, 96000.0f}) {
        pulsefx::ClarityEnhancer clarity;
        clarity.prepare(rate);
        clarity.setAmount(0.0f);
        clarity.reset();
        for (std::size_t index = 0; index < 12000; ++index) {
            const float time = static_cast<float>(index) / rate;
            const float sourceLeft =
                0.16f * std::sin(2.0f * kPi * 311.0f * time) +
                0.05f * std::sin(2.0f * kPi * 3271.0f * time);
            const float sourceRight = sourceLeft * 0.73f;
            float left = sourceLeft;
            float right = sourceRight;
            clarity.processStereo(left, right);
            assert(std::abs(left - sourceLeft) < 1.0e-7f);
            assert(std::abs(right - sourceRight) < 1.0e-7f);
        }
    }
}

float clarityPresenceGain(bool masked) {
    pulsefx::ClarityEnhancer clarity;
    clarity.prepare(kSampleRate);
    clarity.setAmount(1.0f);
    clarity.reset();

    constexpr std::size_t total = 36000;
    constexpr std::size_t warmup = 8000;
    constexpr float presenceAmplitude = 0.035f;
    std::vector<float> output;
    output.reserve(total - warmup);

    for (std::size_t index = 0; index < total; ++index) {
        const float time = static_cast<float>(index) / kSampleRate;
        float source = presenceAmplitude * std::sin(2.0f * kPi * 2800.0f * time);
        if (masked) source += 0.18f * std::sin(2.0f * kPi * 430.0f * time);
        float left = source;
        float right = source;
        clarity.processStereo(left, right);
        assert(std::isfinite(left));
        assert(std::isfinite(right));
        assert(std::abs(left - right) < 1.0e-6f);
        if (index >= warmup) output.push_back(left);
    }

    return toneMagnitude(output, kSampleRate, 2800.0f) / presenceAmplitude;
}

void testClarityFavorsMaskedDetailOverAlreadyDominantPresence() {
    const float dominantGain = clarityPresenceGain(false);
    const float maskedGain = clarityPresenceGain(true);
    assert(dominantGain > 0.98f);
    assert(dominantGain < 1.14f);
    assert(maskedGain > dominantGain + 0.005f);
}

void testClarityProtectsTransientsAndCenter() {
    pulsefx::ClarityEnhancer clarity;
    clarity.prepare(kSampleRate);
    clarity.setAmount(1.0f);
    clarity.reset();

    float maxPeak = 0.0f;
    for (std::size_t index = 0; index < 4096; ++index) {
        float left = index == 0 ? 1.0f : 0.0f;
        float right = left;
        clarity.processStereo(left, right);
        assert(std::isfinite(left));
        assert(std::isfinite(right));
        assert(std::abs(left - right) < 1.0e-7f);
        maxPeak = std::max(maxPeak, std::abs(left));
    }
    assert(maxPeak < 1.10f);
}

void testClarityToggleIsSmooth() {
    pulsefx::ClarityEnhancer clarity;
    clarity.prepare(kSampleRate);
    clarity.setAmount(0.0f);
    clarity.reset();

    float previousResidual = 0.0f;
    float maxResidualStep = 0.0f;
    for (std::size_t index = 0; index < 14000; ++index) {
        if (index == 6000) clarity.setAmount(1.0f);
        const float time = static_cast<float>(index) / kSampleRate;
        const float source =
            0.12f * std::sin(2.0f * kPi * 520.0f * time) +
            0.04f * std::sin(2.0f * kPi * 3100.0f * time);
        float left = source;
        float right = source;
        clarity.processStereo(left, right);
        const float residual = left - source;
        if (index > 5900) {
            maxResidualStep = std::max(maxResidualStep, std::abs(residual - previousResidual));
        }
        previousResidual = residual;
    }
    assert(maxResidualStep < 0.02f);
}

void testSpatialZeroAmountIsTransparent() {
    for (float rate : {44100.0f, 48000.0f, 96000.0f}) {
        pulsefx::SpatialSurround surround;
        surround.prepare(rate);
        surround.setAmount(0.0f);
        surround.reset();
        for (std::size_t index = 0; index < 14000; ++index) {
            const float time = static_cast<float>(index) / rate;
            const float sourceLeft = 0.18f * std::sin(2.0f * kPi * 997.0f * time);
            const float sourceRight = 0.11f * std::sin(2.0f * kPi * 1553.0f * time);
            float left = sourceLeft;
            float right = sourceRight;
            surround.processStereo(left, right);
            assert(std::abs(left - sourceLeft) < 2.0e-6f);
            assert(std::abs(right - sourceRight) < 2.0e-6f);
        }
    }
}

double oppositeEarEnergy(float frequency) {
    pulsefx::SpatialSurround surround;
    surround.prepare(kSampleRate);
    surround.setAmount(1.0f);
    surround.reset();

    double energy = 0.0;
    for (std::size_t index = 0; index < 28000; ++index) {
        const float source = 0.12f * std::sin(
            2.0f * kPi * frequency * static_cast<float>(index) / kSampleRate);
        float left = source;
        float right = 0.0f;
        surround.processStereo(left, right);
        if (index > 6000) energy += static_cast<double>(right) * static_cast<double>(right);
    }
    return energy;
}

void testSpatialKeepsBassMoreAnchoredThanPresenceBand() {
    const double bassCrossfeed = oppositeEarEnergy(70.0f);
    const double presenceCrossfeed = oppositeEarEnergy(2200.0f);
    assert(bassCrossfeed > 0.0);
    assert(presenceCrossfeed > bassCrossfeed * 1.25);
}

void testSpatialAddsShortLateInterauralField() {
    pulsefx::SpatialSurround surround;
    surround.prepare(kSampleRate);
    surround.setAmount(1.0f);
    surround.reset();

    double lateRightEnergy = 0.0;
    for (std::size_t index = 0; index < 1800; ++index) {
        float left = index == 0 ? 1.0f : 0.0f;
        float right = 0.0f;
        surround.processStereo(left, right);
        if (index > 300) lateRightEnergy += static_cast<double>(right) * static_cast<double>(right);
    }
    // The default HRIR is only 96 taps. Energy hundreds of samples later proves
    // the short externalization field exists without requiring a long reverb.
    assert(lateRightEnergy > 1.0e-7);
}

void testSpatialDoesNotExplodeAlreadyWideAntiPhaseMaterial() {
    pulsefx::SpatialSurround surround;
    surround.prepare(kSampleRate);
    surround.setAmount(1.0f);
    surround.reset();

    double inputEnergy = 0.0;
    double outputEnergy = 0.0;
    for (std::size_t index = 0; index < 32000; ++index) {
        const float source = 0.14f * std::sin(
            2.0f * kPi * 1000.0f * static_cast<float>(index) / kSampleRate);
        float left = source;
        float right = -source;
        surround.processStereo(left, right);
        assert(std::isfinite(left));
        assert(std::isfinite(right));
        if (index > 6000) {
            inputEnergy += static_cast<double>(source) * static_cast<double>(source);
            outputEnergy += 0.5 * (
                static_cast<double>(left) * static_cast<double>(left) +
                static_cast<double>(right) * static_cast<double>(right));
        }
    }
    const double rmsRatio = std::sqrt(outputEnergy / inputEnergy);
    assert(rmsRatio < 1.35);
}

void testSpatialKeepsCenteredMonoCentered() {
    pulsefx::SpatialSurround surround;
    surround.prepare(kSampleRate);
    surround.setAmount(1.0f);
    surround.reset();
    for (std::size_t index = 0; index < 20000; ++index) {
        const float sample = 0.12f * std::sin(
            2.0f * kPi * 730.0f * static_cast<float>(index) / kSampleRate);
        float left = sample;
        float right = sample;
        surround.processStereo(left, right);
        assert(std::abs(left - right) < 1.0e-6f);
    }
}

void testSpatialProfileSwapIsClickFreeAndRapidUpdatesStayFinite() {
    pulsefx::SpatialSurround surround;
    surround.prepare(kSampleRate);
    surround.setAmount(1.0f);
    surround.reset();

    pulsefx::HrtfProfile mutedProfile{};
    mutedProfile.taps = 1;

    auto brightProfile = pulsefx::SpatialSurround::makeDefaultProfile(kSampleRate);
    for (std::size_t index = 0; index < brightProfile.taps; ++index) {
        brightProfile.leftToLeft[index] *= 0.82f;
        brightProfile.rightToRight[index] *= 0.82f;
        brightProfile.leftToRight[index] *= 1.15f;
        brightProfile.rightToLeft[index] *= 1.15f;
    }

    auto finalProfile = pulsefx::SpatialSurround::makeDefaultProfile(kSampleRate);
    for (std::size_t index = 0; index < finalProfile.taps; ++index) {
        finalProfile.leftToRight[index] *= 0.72f;
        finalProfile.rightToLeft[index] *= 0.72f;
    }

    float previousLeft = 0.0f;
    float maxStepAroundFirstSwap = 0.0f;
    for (std::size_t index = 0; index < 26000; ++index) {
        // 997 * 12000 / 48000 = 249.25 cycles: the first swap intentionally
        // occurs near a waveform maximum, where a hard FIR reset is easiest to
        // expose as a discontinuity.
        if (index == 12000) surround.setProfile(mutedProfile);
        if (index == 12100) surround.setProfile(brightProfile);
        if (index == 12200) surround.setProfile(finalProfile); // newest pending wins

        const float source = 0.16f * std::sin(
            2.0f * kPi * 997.0f * static_cast<float>(index) / kSampleRate);
        float left = source;
        float right = source * 0.35f;
        surround.processStereo(left, right);
        assert(std::isfinite(left));
        assert(std::isfinite(right));

        if (index >= 11990 && index <= 12120) {
            maxStepAroundFirstSwap = std::max(maxStepAroundFirstSwap, std::abs(left - previousLeft));
        }
        previousLeft = left;
    }

    // Normal sample-to-sample motion of the 997 Hz tone is already included in
    // this bound. A hard profile reset at the deliberately chosen peak is much
    // larger; the 45 ms bank crossfade must remain comfortably below it.
    assert(maxStepAroundFirstSwap < 0.05f);
}

void testSpatialSanitizesCorruptProfileCoefficients() {
    pulsefx::SpatialSurround surround;
    surround.prepare(kSampleRate);
    surround.setAmount(1.0f);
    surround.reset();

    pulsefx::HrtfProfile corrupt{};
    corrupt.taps = 8;
    corrupt.leftToLeft[0] = std::numeric_limits<float>::quiet_NaN();
    corrupt.leftToRight[2] = std::numeric_limits<float>::infinity();
    corrupt.rightToLeft[3] = -std::numeric_limits<float>::infinity();
    corrupt.rightToRight[0] = 1.0f;
    surround.setProfile(corrupt);

    for (std::size_t index = 0; index < 6000; ++index) {
        float left = index == 0 ? 0.8f : 0.0f;
        float right = 0.0f;
        surround.processStereo(left, right);
        assert(std::isfinite(left));
        assert(std::isfinite(right));
    }
}

void testSpatialRejectsNonFiniteInput() {
    pulsefx::SpatialSurround surround;
    surround.prepare(kSampleRate);
    surround.setAmount(std::numeric_limits<float>::quiet_NaN());
    float left = std::numeric_limits<float>::quiet_NaN();
    float right = std::numeric_limits<float>::infinity();
    surround.processStereo(left, right);
    assert(std::isfinite(left));
    assert(std::isfinite(right));
}

} // namespace

int main() {
    testClarityZeroAmountIsTransparent();
    testClarityFavorsMaskedDetailOverAlreadyDominantPresence();
    testClarityProtectsTransientsAndCenter();
    testClarityToggleIsSmooth();
    testSpatialZeroAmountIsTransparent();
    testSpatialKeepsBassMoreAnchoredThanPresenceBand();
    testSpatialAddsShortLateInterauralField();
    testSpatialDoesNotExplodeAlreadyWideAntiPhaseMaterial();
    testSpatialKeepsCenteredMonoCentered();
    testSpatialProfileSwapIsClickFreeAndRapidUpdatesStayFinite();
    testSpatialSanitizesCorruptProfileCoefficients();
    testSpatialRejectsNonFiniteInput();
    return 0;
}
