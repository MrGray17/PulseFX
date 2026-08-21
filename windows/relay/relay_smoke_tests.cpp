#ifdef _WIN32
#include "ClockDriftController.h"
#include "StereoSampleCodec.h"
#include "WasapiRelay.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testRelayStartsIdle() {
    pulsefx::windows::WasapiRelay relay;
    require(!relay.running(), "new relay unexpectedly running");
    const auto stats = relay.stats();
    require(
        stats.underruns == 0 && stats.overruns == 0 &&
        stats.capturedFrames == 0 && stats.renderedFrames == 0 &&
        stats.bufferedFrames == 0 && std::abs(stats.clockCorrectionPpm) < 0.001f,
        "new relay has non-zero statistics");
    relay.stop();
}

void testClockControllerStaysNominalAtTarget() {
    pulsefx::windows::ClockDriftController controller;
    controller.prepare(48000.0f, 4800);
    float rate = 0.0f;
    for (int i = 0; i < 100; ++i) rate = controller.update(4800);
    require(std::abs(rate - 48000.0f) < 0.01f, "clock controller moved a centered buffer");
}

void testClockControllerCorrectsBothDirections() {
    pulsefx::windows::ClockDriftController high;
    high.prepare(48000.0f, 4800);
    float highRate = 48000.0f;
    for (int i = 0; i < 250; ++i) highRate = high.update(5600);
    require(highRate > 48000.0f, "high buffer fill did not speed up render clock");
    require(high.correctionPpm() <= 2000.1f, "positive clock correction exceeded hard bound");

    pulsefx::windows::ClockDriftController low;
    low.prepare(48000.0f, 4800);
    float lowRate = 48000.0f;
    for (int i = 0; i < 250; ++i) lowRate = low.update(4000);
    require(lowRate < 48000.0f, "low buffer fill did not slow render clock");
    require(low.correctionPpm() >= -2000.1f, "negative clock correction exceeded hard bound");
}

void testPcm16DecodeCoversFullScaleAndChannelOrder() {
    const std::array<std::int16_t, 6> input{
        -32768, 32767,
        0, 16384,
        -16384, 1,
    };
    std::array<float, 6> output{};
    pulsefx::windows::decodeStereoSamples(
        input.data(), 3, pulsefx::windows::StereoSampleEncoding::Pcm16, output.data());
    require(std::abs(output[0] + 1.0f) < 1.0e-7f, "PCM16 negative full scale decoded incorrectly");
    require(output[1] > 0.9999f && output[1] < 1.0f, "PCM16 positive full scale decoded incorrectly");
    require(output[2] == 0.0f, "PCM16 silence decoded incorrectly");
    require(std::abs(output[3] - 0.5f) < 1.0e-7f, "PCM16 right channel decoded incorrectly");
    require(std::abs(output[4] + 0.5f) < 1.0e-7f, "PCM16 left channel order changed");
    require(output[5] > 0.0f, "PCM16 least-significant positive sample lost");
}

void testFloatDecodeIsBitPreserving() {
    const std::array<float, 4> input{0.25f, -0.5f, 0.75f, -1.0f};
    std::array<float, 4> output{};
    pulsefx::windows::decodeStereoSamples(
        input.data(), 2, pulsefx::windows::StereoSampleEncoding::Float32, output.data());
    require(input == output, "float32 relay decode changed samples");
}

} // namespace

int main() {
    testRelayStartsIdle();
    testClockControllerStaysNominalAtTarget();
    testClockControllerCorrectsBothDirections();
    testPcm16DecodeCoversFullScaleAndChannelOrder();
    testFloatDecodeIsBitPreserving();
    return 0;
}
#endif
