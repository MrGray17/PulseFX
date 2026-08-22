#include "pulsefx/MultichannelBinaural.h"
#include "pulsefx/PitchShifter.h"
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

float goertzel(const std::vector<float>& stereo, std::size_t startFrame, float frequency) {
    const float omega = 2.0f * std::numbers::pi_v<float> * frequency / kSampleRate;
    const float coeff = 2.0f * std::cos(omega);
    float s1 = 0.0f;
    float s2 = 0.0f;
    const std::size_t frames = stereo.size() / 2;
    for (std::size_t frame = std::min(startFrame, frames); frame < frames; ++frame) {
        const float sample = 0.5f * (stereo[frame * 2] + stereo[frame * 2 + 1]);
        const float s0 = sample + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return std::sqrt(std::max(0.0f, s1 * s1 + s2 * s2 - coeff * s1 * s2));
}

void testPitchRangeAndLatency() {
    pulsefx::PitchShifter shifter;
    require(shifter.prepare(kSampleRate), "pitch shifter failed to prepare");
    shifter.setSemitones(99.0f);
    require(std::abs(shifter.semitones() - 5.0f) < 1.0e-6f, "pitch upper range was not clamped to +5");
    require(shifter.latencySamples() > 0, "active pitch did not report latency");
    shifter.setSemitones(-99.0f);
    require(std::abs(shifter.semitones() + 5.0f) < 1.0e-6f, "pitch lower range was not clamped to -5");
    shifter.setSemitones(0.0f);
    require(shifter.latencySamples() == 0, "disabled pitch still reported latency");
}

void testPitchRaisesA440ByFiveSemitones() {
    pulsefx::PitchShifter shifter;
    require(shifter.prepare(kSampleRate), "pitch shifter failed to prepare");
    shifter.setSemitones(5.0f);
    shifter.reset();

    constexpr std::size_t frames = 65536;
    std::vector<float> audio(frames * 2, 0.0f);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const float sample = 0.12f * std::sin(
            2.0f * std::numbers::pi_v<float> * 440.0f * static_cast<float>(frame) / kSampleRate);
        audio[frame * 2] = sample;
        audio[frame * 2 + 1] = sample;
    }
    shifter.processInterleaved(audio.data(), frames);

    const float target = 440.0f * std::pow(2.0f, 5.0f / 12.0f);
    const std::size_t analysisStart = std::min<std::size_t>(
        shifter.latencySamples() + 8192, frames / 2);
    const float shiftedEnergy = goertzel(audio, analysisStart, target);
    const float originalEnergy = goertzel(audio, analysisStart, 440.0f);
    require(shiftedEnergy > originalEnergy * 2.0f, "pitch shifter did not move A440 toward +5 semitones");
}

void testFiveOneSideChannelIsDirectional() {
    pulsefx::MultichannelBinaural renderer;
    require(renderer.prepare(kSampleRate, 6), "5.1 renderer failed to prepare");
    renderer.setAmount(1.0f);
    renderer.reset();

    std::vector<float> input(4096 * 6, 0.0f);
    std::vector<float> output(4096 * 2, 0.0f);
    input[128 * 6 + 4] = 1.0f; // SL in KSAUDIO_SPEAKER_5POINT1_SURROUND order
    renderer.processInterleavedToStereo(input.data(), output.data(), 4096);

    float leftEnergy = 0.0f;
    float rightEnergy = 0.0f;
    for (std::size_t frame = 0; frame < 4096; ++frame) {
        leftEnergy += output[frame * 2] * output[frame * 2];
        rightEnergy += output[frame * 2 + 1] * output[frame * 2 + 1];
    }
    require(leftEnergy > rightEnergy * 2.0f, "left surround speaker was not localized to the left hemisphere");
    require(rightEnergy > 1.0e-5f, "binaural renderer produced no contralateral ear energy");
}

void testSevenOneRearChannelIsDirectional() {
    pulsefx::MultichannelBinaural renderer;
    require(renderer.prepare(kSampleRate, 8), "7.1 renderer failed to prepare");
    renderer.setAmount(1.0f);
    renderer.reset();

    std::vector<float> input(4096 * 8, 0.0f);
    std::vector<float> output(4096 * 2, 0.0f);
    input[128 * 8 + 4] = 1.0f; // BL
    renderer.processInterleavedToStereo(input.data(), output.data(), 4096);

    float leftEnergy = 0.0f;
    float rightEnergy = 0.0f;
    for (std::size_t frame = 0; frame < 4096; ++frame) {
        leftEnergy += output[frame * 2] * output[frame * 2];
        rightEnergy += output[frame * 2 + 1] * output[frame * 2 + 1];
        require(std::isfinite(output[frame * 2]) && std::isfinite(output[frame * 2 + 1]),
            "7.1 renderer produced non-finite output");
    }
    require(leftEnergy > rightEnergy * 1.5f, "left rear speaker lost left-side localization");
}

void testDryFiveOneCentreStaysCentered() {
    pulsefx::MultichannelBinaural renderer;
    require(renderer.prepare(kSampleRate, 6), "5.1 renderer failed to prepare");
    renderer.setAmount(0.0f);
    renderer.reset();

    std::vector<float> input(8192 * 6, 0.0f);
    std::vector<float> output(8192 * 2, 0.0f);
    for (std::size_t frame = 0; frame < 8192; ++frame) {
        input[frame * 6 + 2] = 0.1f * std::sin(
            2.0f * std::numbers::pi_v<float> * 700.0f * static_cast<float>(frame) / kSampleRate);
    }
    renderer.processInterleavedToStereo(input.data(), output.data(), 8192);
    for (std::size_t frame = 0; frame < 8192; ++frame) {
        require(std::abs(output[frame * 2] - output[frame * 2 + 1]) < 1.0e-6f,
            "dry centre channel moved off centre");
    }
}

void testPrepareClearsStaleSurroundAmount() {
    pulsefx::MultichannelBinaural renderer;
    require(renderer.prepare(kSampleRate, 6), "initial 5.1 renderer prepare failed");
    renderer.setAmount(1.0f);
    require(std::abs(renderer.amount() - 1.0f) < 1.0e-6f, "test could not enable multichannel surround");

    require(renderer.prepare(kSampleRate, 8), "7.1 re-prepare failed");
    require(std::abs(renderer.amount()) < 1.0e-6f,
        "device re-prepare leaked the previous multichannel surround amount");

    renderer.setAmount(0.63f);
    require(renderer.prepare(kSampleRate, 6), "second 5.1 re-prepare failed");
    require(std::abs(renderer.amount()) < 1.0e-6f,
        "channel-layout re-prepare did not return renderer to neutral control state");
}
}

int main() {
    testPitchRangeAndLatency();
    testPitchRaisesA440ByFiveSemitones();
    testFiveOneSideChannelIsDirectional();
    testSevenOneRearChannelIsDirectional();
    testDryFiveOneCentreStaysCentered();
    testPrepareClearsStaleSurroundAmount();
    std::cout << "PulseFX Boom-parity DSP tests passed\n";
}