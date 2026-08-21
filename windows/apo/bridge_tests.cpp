#include "PulseFxApoBridge.h"
#include <cmath>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void expectFinite(const std::vector<float>& audio) {
    for (float sample : audio) require(std::isfinite(sample), "bridge produced non-finite output");
}
}

int main() {
    pulsefx::windows::ApoProcessorBridge bridge;
    require(!bridge.prepare(48000.0f, 5), "bridge accepted unsupported five-channel layout");
    require(bridge.prepare(48000.0f, 2), "bridge rejected supported stereo layout");
    require(bridge.latencyFrames() > 0, "bridge failed to expose DSP latency");

    pulsefx::windows::ApoControlState state{};
    state.processor.bass = 0.5f;
    state.processor.clarity = 0.3f;
    state.processor.space = 0.25f;
    state.eqDb[17] = 2.0f;
    bridge.applyControlState(state);

    std::vector<float> stereo(4096 * 2, 0.0f);
    stereo[1000 * 2] = 0.8f;
    stereo[1000 * 2 + 1] = -0.8f;
    bridge.process(stereo.data(), 4096);
    expectFinite(stereo);

    require(bridge.prepare(48000.0f, 6), "bridge rejected supported 5.1 layout");
    state = {};
    state.processor.surround = 1.0f;
    bridge.applyControlState(state);
    std::vector<float> surround51(4096 * 6, 0.0f);
    std::vector<float> downmixed51(4096 * 2, 0.0f);
    surround51[1000 * 6 + 4] = 0.7f;
    bridge.processToStereo(surround51.data(), downmixed51.data(), 4096);
    expectFinite(downmixed51);

    require(bridge.prepare(48000.0f, 8), "bridge rejected supported 7.1 layout");
    bridge.applyControlState(state);
    std::vector<float> surround71(4096 * 8, 0.0f);
    std::vector<float> downmixed71(4096 * 2, 0.0f);
    surround71[1000 * 8 + 6] = 0.7f;
    bridge.processToStereo(surround71.data(), downmixed71.data(), 4096);
    expectFinite(downmixed71);
}
