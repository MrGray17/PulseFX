#include "pulsefx/Equalizer.h"
#include "pulsefx/HeadphoneCorrection.h"
#include "pulsefx/Processor.h"
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

void testExtremeControlsNeverProduceNan() {
    for (float sampleRate : {44100.0f, 48000.0f, 96000.0f}) {
        pulsefx::Processor processor;
        processor.prepare(sampleRate);
        pulsefx::ProcessorParameters params{};
        params.preampDb = 9.0f;
        params.bass = 1.0f;
        params.clarity = 1.0f;
        params.space = 1.0f;
        params.dynamics = 1.0f;
        params.nightMode = true;
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
    testHeadphoneCorrectionProfileIsStable();
    testExtremeControlsNeverProduceNan();
    std::cout << "PulseFX DSP quality tests passed\n";
}
