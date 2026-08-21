#include "pulsefx/Ambience.h"
#include "pulsefx/Equalizer.h"
#include "pulsefx/FidelityEnhancer.h"
#include "pulsefx/HeadphoneCorrection.h"
#include "pulsefx/Processor.h"
#include "pulsefx/SpatialSurround.h"
#include "pulsefx/TruePeakDetector.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace {
constexpr float kSampleRate = 48000.0f;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void expectFinite(const std::vector<float>& audio) {
    for (float sample : audio) require(std::isfinite(sample), "non-finite sample");
}

void testBypassIsBitTransparent() {
    pulsefx::Processor processor;
    processor.prepare(kSampleRate);
    pulsefx::ProcessorParameters params{};
    params.bypass = true;
    processor.setParameters(params);
    std::vector<float> audio{0.25f, -0.5f, 0.75f, -0.125f, -0.99f, 0.99f};
    const auto original = audio;
    processor.processInterleaved(audio.data(), audio.size() / 2, 2);
    require(audio == original, "bypass changed samples");
}

void testFlatChainIsTransparentApartFromDeclaredLatency() {
    pulsefx::Processor processor;
    processor.prepare(kSampleRate);
    const std::size_t latency = processor.limiter().latencySamples();
    constexpr std::size_t frames = 8192;
    std::vector<float> audio(frames * 2);
    std::vector<float> original = audio;
    for (std::size_t i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        const float s = 0.15f * std::sin(2.0f * std::numbers::pi_v<float> * 997.0f * t);
        audio[i*2] = s;
        audio[i*2+1] = -0.7f*s;
    }
    original = audio;
    processor.processInterleaved(audio.data(), frames, 2);
    float maxError = 0.0f;
    for (std::size_t i = latency + 2048; i < frames; ++i) {
        maxError = std::max(maxError, std::abs(audio[i*2] - original[(i-latency)*2]));
        maxError = std::max(maxError, std::abs(audio[i*2+1] - original[(i-latency)*2+1]));
    }
    require(maxError < 2.0e-4f, "flat chain is not transparent after latency compensation");
}

void testLimiterContainsAHotTransient() {
    pulsefx::Processor processor;
    processor.prepare(kSampleRate);
    pulsefx::ProcessorParameters params{};
    params.preampDb = 9.0f;
    processor.setParameters(params);
    constexpr std::size_t frames = 4096;
    std::vector<float> audio(frames * 2, 0.0f);
    audio[1200*2] = 1.0f;
    audio[1200*2+1] = -1.0f;
    processor.processInterleaved(audio.data(), frames, 2);
    const float peak = *std::max_element(audio.begin(), audio.end(), [](float a, float b) {
        return std::abs(a) < std::abs(b);
    });
    require(std::abs(peak) <= 0.892f, "limiter exceeded ceiling");
}

void test31BandEqualizerActuallyBoostsOneKhz() {
    pulsefx::Equalizer eq;
    eq.prepare(kSampleRate);
    constexpr std::size_t oneKhzBand = 17;
    eq.setBandGain(oneKhzBand, 6.0f);
    float inputEnergy = 0.0f;
    float outputEnergy = 0.0f;
    for (std::size_t i = 0; i < 12000; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        const float input = 0.05f * std::sin(2.0f * std::numbers::pi_v<float> * 1000.0f * t);
        float left = input;
        float right = input;
        eq.processStereo(left, right);
        if (i > 4000) {
            inputEnergy += input * input;
            outputEnergy += left * left;
        }
    }
    require(outputEnergy > inputEnergy * 2.5f, "1 kHz EQ band did not provide expected boost");
}

void testSpaceKeepsMonoCentered() {
    pulsefx::Processor processor;
    processor.prepare(kSampleRate);
    pulsefx::ProcessorParameters params{};
    params.space = 1.0f;
    processor.setParameters(params);
    std::vector<float> audio(4096 * 2, 0.12f);
    processor.processInterleaved(audio.data(), 4096, 2);
    for (std::size_t i = 0; i < 4096; ++i) {
        require(std::abs(audio[i*2] - audio[i*2+1]) < 1.0e-5f, "space effect moved centered mono content");
    }
}

void testSpatialSurroundCrossfeedsOppositeEar() {
    pulsefx::SpatialSurround surround;
    surround.prepare(kSampleRate);
    surround.setAmount(1.0f);
    surround.reset();
    float oppositeEarEnergy = 0.0f;
    for (std::size_t i = 0; i < 256; ++i) {
        float left = i == 0 ? 1.0f : 0.0f;
        float right = 0.0f;
        surround.processStereo(left, right);
        oppositeEarEnergy += right * right;
    }
    require(oppositeEarEnergy > 0.01f, "surround failed to create a contralateral HRTF path");
}

void testSpatialSurroundKeepsMonoCentered() {
    pulsefx::SpatialSurround surround;
    surround.prepare(kSampleRate);
    surround.setAmount(1.0f);
    surround.reset();
    for (std::size_t i = 0; i < 4096; ++i) {
        const float sample = 0.1f * std::sin(2.0f * std::numbers::pi_v<float> * 700.0f * static_cast<float>(i) / kSampleRate);
        float left = sample;
        float right = sample;
        surround.processStereo(left, right);
        require(std::abs(left - right) < 1.0e-5f, "surround moved symmetric mono content off center");
    }
}

void testAmbienceCreatesCrossChannelEarlyReflections() {
    pulsefx::Ambience ambience;
    ambience.prepare(kSampleRate);
    ambience.setAmount(1.0f);
    ambience.reset();
    float rightEnergy = 0.0f;
    for (std::size_t i = 0; i < 4096; ++i) {
        float left = i == 0 ? 1.0f : 0.0f;
        float right = 0.0f;
        ambience.processStereo(left, right);
        if (i > 400) rightEnergy += right * right;
    }
    require(rightEnergy > 0.001f, "ambience produced no cross-channel early reflections");
}

void testFidelityLiftsQuietHighFrequencyDetail() {
    pulsefx::FidelityEnhancer fidelity;
    fidelity.prepare(kSampleRate);
    fidelity.setAmount(1.0f);
    fidelity.reset();
    float inputEnergy = 0.0f;
    float outputEnergy = 0.0f;
    for (std::size_t i = 0; i < 12000; ++i) {
        const float input = 0.025f * std::sin(2.0f * std::numbers::pi_v<float> * 8000.0f * static_cast<float>(i) / kSampleRate);
        float left = input;
        float right = input;
        fidelity.processStereo(left, right);
        if (i > 4000) {
            inputEnergy += input * input;
            outputEnergy += left * left;
        }
    }
    require(outputEnergy > inputEnergy * 1.08f, "fidelity did not lift quiet high-frequency detail");
}

void testReferenceEffectCompatibility() {
    pulsefx::Processor processor;
    processor.prepare(kSampleRate);
    pulsefx::ProcessorParameters params{};
    params.surround = 0.8f;
    params.space = 0.7f;
    params.ambience = 0.6f;
    params.fidelity = 0.5f;
    params.nightMode = true;
    processor.setParameters(params);
    const auto surroundState = processor.parameters();
    require(surroundState.surround > 0.79f, "surround was unexpectedly disabled");
    require(surroundState.space == 0.0f, "spatial remained enabled with surround");
    require(surroundState.ambience == 0.0f, "ambience remained enabled with surround");
    require(!surroundState.nightMode, "night mode remained enabled with surround");
    require(surroundState.fidelity > 0.49f, "fidelity should remain compatible with surround");

    params = {};
    params.ambience = 0.8f;
    params.space = 0.8f;
    params.nightMode = true;
    processor.setParameters(params);
    const auto ambienceState = processor.parameters();
    require(ambienceState.ambience > 0.79f, "ambience was unexpectedly disabled");
    require(ambienceState.space == 0.0f, "spatial remained enabled with ambience");
    require(!ambienceState.nightMode, "night mode remained enabled with ambience");
}

void testHeadphoneCorrectionProfileIsStable() {
    pulsefx::Processor processor;
    processor.prepare(44100.0f);
    pulsefx::HeadphoneProfile profile{};
    profile.bands[0] = {90.0f, 0.7f, -3.0f, true};
    profile.bands[1] = {2800.0f, 1.4f, 2.5f, true};
    profile.bands[2] = {7600.0f, 2.2f, -2.0f, true};
    processor.headphoneCorrection().setProfile(profile);
    processor.headphoneCorrection().setEnabled(true);
    std::vector<float> audio(8192 * 2, 0.0f);
    for (std::size_t i = 0; i < 8192; ++i) {
        const float t = static_cast<float>(i) / 44100.0f;
        audio[i*2] = 0.1f * std::sin(2.0f * std::numbers::pi_v<float> * 440.0f * t);
        audio[i*2+1] = audio[i*2];
    }
    processor.processInterleaved(audio.data(), 8192, 2);
    expectFinite(audio);
}

void testTruePeakDetectorCatchesInterSamplePeak() {
    pulsefx::TruePeakDetector detector;
    detector.prepare();
    float samplePeak = 0.0f;
    float reconstructedPeak = 0.0f;
    for (std::size_t i = 0; i < 4096; ++i) {
        const float phase = 2.0f * std::numbers::pi_v<float> * 12000.0f * static_cast<float>(i) / kSampleRate
            + std::numbers::pi_v<float> * 0.25f;
        const float sample = std::sin(phase);
        samplePeak = std::max(samplePeak, std::abs(sample));
        const float peak = detector.processStereo(sample, sample);
        if (i > 128) reconstructedPeak = std::max(reconstructedPeak, peak);
    }
    require(samplePeak < 0.72f, "test signal does not create the expected inter-sample peak condition");
    require(reconstructedPeak > 0.94f, "4x true-peak detector missed a known inter-sample peak");
}

void testLimiterContainsInterSamplePeak() {
    pulsefx::Processor processor;
    processor.prepare(kSampleRate);
    constexpr std::size_t frames = 8192;
    std::vector<float> audio(frames * 2, 0.0f);
    for (std::size_t i = 0; i < frames; ++i) {
        const float phase = 2.0f * std::numbers::pi_v<float> * 12000.0f * static_cast<float>(i) / kSampleRate
            + std::numbers::pi_v<float> * 0.25f;
        const float sample = std::sin(phase);
        audio[i*2] = sample;
        audio[i*2+1] = sample;
    }
    processor.processInterleaved(audio.data(), frames, 2);

    pulsefx::TruePeakDetector outputDetector;
    outputDetector.prepare();
    float outputTruePeak = 0.0f;
    const std::size_t latency = processor.limiter().latencySamples();
    for (std::size_t i = 0; i < frames; ++i) {
        const float peak = outputDetector.processStereo(audio[i*2], audio[i*2+1]);
        if (i > latency + 256) outputTruePeak = std::max(outputTruePeak, peak);
    }
    require(outputTruePeak <= 0.905f, "limiter allowed an inter-sample peak beyond the safety margin");
}

void testExtremeControlsNeverProduceNan() {
    for (float sampleRate : {44100.0f, 48000.0f, 96000.0f}) {
        pulsefx::Processor processor;
        processor.prepare(sampleRate);
        pulsefx::ProcessorParameters params{};
        params.preampDb = 9.0f;
        params.bass = 1.0f;
        params.clarity = 1.0f;
        params.fidelity = 1.0f;
        params.surround = 1.0f;
        params.dynamics = 1.0f;
        processor.setParameters(params);
        for (std::size_t band = 0; band < pulsefx::Equalizer::kFrequencies.size(); ++band) {
            processor.equalizer().setBandGain(band, band % 2 == 0 ? 12.0f : -12.0f);
        }
        std::vector<float> audio(16384 * 2, 0.95f);
        processor.processInterleaved(audio.data(), 16384, 2);
        expectFinite(audio);
        for (float sample : audio) require(std::abs(sample) <= 0.893f, "extreme controls exceeded limiter ceiling");
    }
}
}

int main() {
    testBypassIsBitTransparent();
    testFlatChainIsTransparentApartFromDeclaredLatency();
    testLimiterContainsAHotTransient();
    test31BandEqualizerActuallyBoostsOneKhz();
    testSpaceKeepsMonoCentered();
    testSpatialSurroundCrossfeedsOppositeEar();
    testSpatialSurroundKeepsMonoCentered();
    testAmbienceCreatesCrossChannelEarlyReflections();
    testFidelityLiftsQuietHighFrequencyDetail();
    testReferenceEffectCompatibility();
    testHeadphoneCorrectionProfileIsStable();
    testTruePeakDetectorCatchesInterSamplePeak();
    testLimiterContainsInterSamplePeak();
    testExtremeControlsNeverProduceNan();
    std::cout << "PulseFX DSP quality tests passed\n";
}
