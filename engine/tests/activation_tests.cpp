#include "pulsefx/Processor.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <vector>

using namespace pulsefx;

namespace {
constexpr float kSampleRate = 48000.0f;
constexpr float kTwoPi = 6.2831853071795864769f;

std::vector<float> makeSine(std::size_t frames, float amplitude, float frequency, float& phase) {
    std::vector<float> result(frames * 2, 0.0f);
    const float step = kTwoPi * frequency / kSampleRate;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const float value = amplitude * std::sin(phase);
        phase += step;
        if (phase >= kTwoPi) phase -= kTwoPi;
        result[frame * 2] = value;
        result[frame * 2 + 1] = value;
    }
    return result;
}

float maxStep(const std::vector<float>& samples) {
    float result = 0.0f;
    for (std::size_t frame = 1; frame < samples.size() / 2; ++frame) {
        result = std::max(result, std::abs(samples[frame * 2] - samples[(frame - 1) * 2]));
        result = std::max(result, std::abs(samples[frame * 2 + 1] - samples[(frame - 1) * 2 + 1]));
    }
    return result;
}

void testLatencyMatchedTransparentBypass() {
    Processor processor;
    ProcessorParameters parameters{};
    parameters.bypass = true;
    processor.setParameters(parameters);
    processor.prepare(kSampleRate);

    const std::size_t latency = processor.latencySamples();
    assert(latency > 0);
    assert(processor.masterWetMix() == 0.0f);

    float phase = 0.0f;
    auto input = makeSine(4096, 0.23f, 733.0f, phase);
    const auto reference = input;
    processor.processInterleaved(input.data(), input.size() / 2, 2);

    for (std::size_t frame = 0; frame < input.size() / 2; ++frame) {
        const float expectedLeft = frame >= latency ? reference[(frame - latency) * 2] : 0.0f;
        const float expectedRight = frame >= latency ? reference[(frame - latency) * 2 + 1] : 0.0f;
        assert(std::abs(input[frame * 2] - expectedLeft) < 1.0e-6f);
        assert(std::abs(input[frame * 2 + 1] - expectedRight) < 1.0e-6f);
    }
}

void testClickFreeMasterRamp() {
    Processor processor;
    ProcessorParameters parameters{};
    parameters.bypass = true;
    parameters.preampDb = 5.0f;
    parameters.fidelity = 0.28f;
    parameters.clarity = 0.16f;
    processor.setParameters(parameters);
    processor.prepare(kSampleRate);

    float phase = 0.0f;
    // Warm every stage and both dry/wet histories while the user hears only dry.
    auto warm = makeSine(8192, 0.08f, 440.0f, phase);
    processor.processInterleaved(warm.data(), warm.size() / 2, 2);
    assert(processor.masterWetMix() < 1.0e-6f);

    parameters.bypass = false;
    processor.setParameters(parameters);
    auto enabling = makeSine(4096, 0.08f, 440.0f, phase);
    processor.processInterleaved(enabling.data(), enabling.size() / 2, 2);
    assert(processor.masterWetMix() > 0.999f);
    // A 440 Hz, 80 mFS sine has a natural adjacent-sample delta below 0.005.
    // Even with the enhanced target, the activation ramp must stay far away
    // from the large one-sample jump characteristic of an audible click.
    assert(maxStep(enabling) < 0.03f);

    parameters.bypass = true;
    processor.setParameters(parameters);
    auto disabling = makeSine(4096, 0.08f, 440.0f, phase);
    processor.processInterleaved(disabling.data(), disabling.size() / 2, 2);
    assert(processor.masterWetMix() < 0.001f);
    assert(maxStep(disabling) < 0.03f);
}

void testRepeatedToggleStress() {
    Processor processor;
    ProcessorParameters parameters{};
    parameters.bypass = false;
    parameters.bass = 0.30f;
    parameters.virtualBass = 0.22f;
    parameters.bassCapability = 0.25f;
    parameters.clarity = 0.18f;
    parameters.fidelity = 0.28f;
    parameters.surround = 0.36f;
    parameters.adaptiveHeadroom = true;
    processor.setParameters(parameters);
    processor.prepare(kSampleRate);

    float phase = 0.0f;
    for (int toggle = 0; toggle < 40; ++toggle) {
        parameters.bypass = (toggle % 2) != 0;
        processor.setParameters(parameters);
        auto block = makeSine(1536, 0.12f, 997.0f, phase);
        processor.processInterleaved(block.data(), block.size() / 2, 2);
        for (float sample : block) assert(std::isfinite(sample));
        assert(std::isfinite(processor.masterWetMix()));
        assert(processor.masterWetMix() >= 0.0f && processor.masterWetMix() <= 1.0f);
    }
}

} // namespace

int main() {
    testLatencyMatchedTransparentBypass();
    testClickFreeMasterRamp();
    testRepeatedToggleStress();
    return 0;
}
