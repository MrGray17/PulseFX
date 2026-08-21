#include "pulsefx/Processor.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using pulsefx::Processor;

static void testBypassIsTransparent() {
    Processor processor;
    processor.prepare(48000.0f);
    pulsefx::ProcessorParameters params{};
    params.bypass = true;
    processor.setParameters(params);
    std::vector<float> audio{0.25f, -0.5f, 0.75f, -0.125f};
    const auto original = audio;
    processor.processInterleaved(audio.data(), 2, 2);
    assert(audio == original);
}

static void testLimiterContainsPeaks() {
    Processor processor;
    processor.prepare(48000.0f);
    pulsefx::ProcessorParameters params{};
    params.preampDb = 12.0f;
    processor.setParameters(params);
    std::vector<float> audio(2048 * 2, 0.95f);
    processor.processInterleaved(audio.data(), 2048, 2);
    for (float sample : audio) assert(std::abs(sample) <= 0.900f);
}

static void testNoNanOnSilence() {
    Processor processor;
    processor.prepare(44100.0f);
    std::vector<float> audio(4096 * 2, 0.0f);
    processor.processInterleaved(audio.data(), 4096, 2);
    for (float sample : audio) assert(std::isfinite(sample));
}

int main() {
    testBypassIsTransparent();
    testLimiterContainsPeaks();
    testNoNanOnSilence();
    std::cout << "PulseFX DSP tests passed\n";
}
