#ifdef _WIN32
#include "ClockDriftController.h"
#include "WasapiRelay.h"
#include <cmath>
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

} // namespace

int main() {
    testRelayStartsIdle();
    testClockControllerStaysNominalAtTarget();
    testClockControllerCorrectsBothDirections();
    return 0;
}
#endif
