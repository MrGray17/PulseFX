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

bool closeTo(float a, float b, float tolerance = 1.0e-5f) {
    return std::abs(a - b) <= tolerance;
}
}

int main() {
    // Restored settings may arrive before the audio device/format is prepared.
    // Delta application must not optimize that first real application away.
    pulsefx::windows::ApoProcessorBridge restoredBridge;
    pulsefx::windows::ApoControlState restored{};
    restored.processor.preampDb = -2.5f;
    restored.processor.bass = 0.7f;
    restored.processor.clarity = 0.4f;
    restored.processor.pitchSemitones = 1.5f;
    restored.eqDb[17] = 4.0f;
    restoredBridge.applyControlState(restored);
    require(restoredBridge.prepare(48000.0f, 2), "bridge failed to prepare after receiving restored controls");
    require(closeTo(restoredBridge.processor().parameters().preampDb, -2.5f), "pre-prepare preamp was lost");
    require(closeTo(restoredBridge.processor().parameters().bass, 0.7f), "pre-prepare bass was lost");
    require(closeTo(restoredBridge.processor().parameters().clarity, 0.4f), "pre-prepare clarity was lost");
    require(closeTo(restoredBridge.processor().parameters().pitchSemitones, 1.5f), "pre-prepare pitch was lost");
    require(closeTo(restoredBridge.processor().equalizer().bandGain(17), 4.0f), "pre-prepare EQ state was lost");

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

    // Re-applying an identical snapshot is a valid no-op and must not disturb
    // processor state or make output non-finite.
    bridge.applyControlState(state);
    require(closeTo(bridge.processor().equalizer().bandGain(17), 2.0f), "identical control snapshot changed EQ state");

    std::vector<float> stereo(4096 * 2, 0.0f);
    stereo[1000 * 2] = 0.8f;
    stereo[1000 * 2 + 1] = -0.8f;
    bridge.process(stereo.data(), 4096);
    expectFinite(stereo);

    // A precomputed personalized HRTF is revision-published into the stereo
    // bridge. The bridge must install/crossfade it without changing latency or
    // producing non-finite samples. A second revision exercises live switching.
    state = {};
    state.processor.surround = 0.8f;
    state.spatialProfile = pulsefx::SpatialSurround::makeDefaultProfile(48000.0f);
    state.spatialProfileRevision = 1;
    bridge.applyControlState(state);
    const std::size_t latencyBeforeProfileSwap = bridge.latencyFrames();

    std::vector<float> profileAudio(4096 * 2, 0.0f);
    for (std::size_t frame = 0; frame < 4096; ++frame) {
        const float sample = 0.12f * std::sin(2.0f * 3.14159265358979323846f * 997.0f
            * static_cast<float>(frame) / 48000.0f);
        profileAudio[frame * 2] = sample;
        profileAudio[frame * 2 + 1] = sample * 0.35f;
    }
    bridge.process(profileAudio.data(), 4096);
    expectFinite(profileAudio);

    pulsefx::HrtfProfile quieterProfile = state.spatialProfile;
    for (std::size_t index = 0; index < quieterProfile.taps; ++index) {
        quieterProfile.leftToRight[index] *= 0.70f;
        quieterProfile.rightToLeft[index] *= 0.70f;
    }
    state.spatialProfile = quieterProfile;
    state.spatialProfileRevision = 2;
    bridge.applyControlState(state);
    bridge.process(profileAudio.data(), 4096);
    expectFinite(profileAudio);
    require(bridge.latencyFrames() == latencyBeforeProfileSwap, "HRTF profile swap changed declared DSP latency");

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
