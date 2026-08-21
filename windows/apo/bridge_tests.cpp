#include "PulseFxApoBridge.h"
#include <cmath>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    pulsefx::windows::ApoProcessorBridge bridge;
    require(!bridge.prepare(48000.0f, 6), "bridge accepted unsupported multichannel layout");
    require(bridge.prepare(48000.0f, 2), "bridge rejected supported stereo layout");
    require(bridge.latencyFrames() > 0, "bridge failed to expose DSP latency");

    pulsefx::windows::ApoControlState state{};
    state.processor.bass = 0.5f;
    state.processor.clarity = 0.3f;
    state.processor.space = 0.25f;
    state.eqDb[17] = 2.0f;
    bridge.applyControlState(state);

    std::vector<float> audio(4096 * 2, 0.0f);
    audio[1000 * 2] = 0.8f;
    audio[1000 * 2 + 1] = -0.8f;
    bridge.process(audio.data(), 4096);
    for (float sample : audio) require(std::isfinite(sample), "bridge produced non-finite output");
}
