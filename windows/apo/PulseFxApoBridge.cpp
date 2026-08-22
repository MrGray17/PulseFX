#include "PulseFxApoBridge.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace pulsefx::windows {
namespace {

std::atomic<std::uint32_t> gSampleRate{0};
std::atomic<std::uint32_t> gInputChannels{0};
std::atomic<std::uint32_t> gProcessorLatencyFrames{0};
std::atomic<float> gLimiterGainReductionDb{0.0f};
std::atomic<float> gHeadroomStress{0.0f};
std::atomic<float> gMasterWetMix{0.0f};

bool sameCorrectionBand(const CorrectionBand& a, const CorrectionBand& b) noexcept {
    return a.frequency == b.frequency &&
        a.q == b.q &&
        a.gainDb == b.gainDb &&
        a.type == b.type &&
        a.enabled == b.enabled;
}

bool sameHeadphoneProfile(const HeadphoneProfile& a, const HeadphoneProfile& b) noexcept {
    if (a.preampDb != b.preampDb) return false;
    for (std::size_t index = 0; index < a.bands.size(); ++index) {
        if (!sameCorrectionBand(a.bands[index], b.bands[index])) return false;
    }
    return true;
}

} // namespace

bool ApoProcessorBridge::prepare(float sampleRate, std::size_t inputChannels) noexcept {
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0f || sampleRate > 384000.0f ||
        (inputChannels != 2 && inputChannels != 6 && inputChannels != 8)) {
        prepared_ = false;
        inputChannels_ = 0;
        sampleRate_ = 0.0f;
        gSampleRate.store(0, std::memory_order_relaxed);
        gInputChannels.store(0, std::memory_order_relaxed);
        return false;
    }
    sampleRate_ = sampleRate;
    inputChannels_ = inputChannels;
    processor_.prepare(sampleRate);
    if (!multichannel_.prepare(sampleRate, inputChannels_)) {
        prepared_ = false;
        inputChannels_ = 0;
        sampleRate_ = 0.0f;
        gSampleRate.store(0, std::memory_order_relaxed);
        gInputChannels.store(0, std::memory_order_relaxed);
        return false;
    }

    // applyControlState() may have been called before prepare. Reset the applied
    // baseline while preserving that pending snapshot so the first prepared
    // application cannot be accidentally optimized away by delta detection.
    const ApoControlState pending = control_;
    control_ = ApoControlState{};
    prepared_ = true;
    applyControlState(pending);
    publishTelemetry();
    return true;
}

void ApoProcessorBridge::reset() noexcept {
    if (!prepared_) return;
    multichannel_.reset();
    processor_.reset();
    publishTelemetry();
}

void ApoProcessorBridge::applyControlState(const ApoControlState& state) noexcept {
    const ApoControlState previous = control_;
    control_ = state;
    if (!prepared_) return;

    ProcessorParameters processorState = state.processor;
    if (inputChannels_ > 2) {
        // Multichannel sources already receive directional rendering before the
        // stereo chain. Applying the stereo HRTF effect again would double-
        // spatialize positional cues.
        if (previous.processor.surround != state.processor.surround) {
            multichannel_.setAmount(processorState.surround);
        }
        processorState.surround = 0.0f;
    } else {
        if (previous.processor.surround != 0.0f) multichannel_.setAmount(0.0f);
    }

    // Processor::setParameters is itself delta-aware. Calling it for every
    // snapshot is therefore cheap for unchanged stages and preserves the
    // smoothed state of every filter that did not actually move.
    processor_.setParameters(processorState);

    // Stereo spatial calibration is precomputed on a non-realtime/control
    // thread. The audio worker only sees a fixed-size HRTF plus a revision and
    // starts the renderer's click-free bank crossfade. Multichannel uses its
    // dedicated directional renderer and is intentionally not faked through a
    // stereo calibration profile.
    if (inputChannels_ == 2 &&
        state.spatialProfileRevision != 0 &&
        state.spatialProfileRevision != previous.spatialProfileRevision) {
        processor_.spatialSurround().setProfile(state.spatialProfile);
    }

    // EQ updates used to rebuild all 31 bands for every UI command, including
    // unrelated controls such as Pitch. Only changed bands may touch their
    // coefficient calculators on the realtime packet boundary now.
    for (std::size_t band = 0; band < state.eqDb.size(); ++band) {
        if (state.eqDb[band] != previous.eqDb[band]) {
            processor_.equalizer().setBandGain(band, state.eqDb[band]);
        }
    }

    // Loading a headphone model can legitimately rebuild its small filter bank,
    // but ordinary control changes must never redo that work.
    if (!sameHeadphoneProfile(state.headphoneProfile, previous.headphoneProfile)) {
        processor_.headphoneCorrection().setProfile(state.headphoneProfile);
    }
    if (state.headphoneCorrectionEnabled != previous.headphoneCorrectionEnabled) {
        processor_.headphoneCorrection().setEnabled(state.headphoneCorrectionEnabled);
    }
    publishTelemetry();
}

void ApoProcessorBridge::publishTelemetry() noexcept {
    const auto latency = processor_.latencySamples();
    gSampleRate.store(
        static_cast<std::uint32_t>(std::clamp(sampleRate_, 0.0f, 384000.0f)),
        std::memory_order_relaxed);
    gInputChannels.store(static_cast<std::uint32_t>(inputChannels_), std::memory_order_relaxed);
    gProcessorLatencyFrames.store(
        static_cast<std::uint32_t>(std::min<std::size_t>(latency, 0xffffffffu)),
        std::memory_order_relaxed);
    const float reduction = processor_.limiter().gainReductionDb();
    const float stress = processor_.headroomStress();
    const float wet = processor_.masterWetMix();
    gLimiterGainReductionDb.store(std::isfinite(reduction) ? reduction : 0.0f, std::memory_order_relaxed);
    gHeadroomStress.store(std::isfinite(stress) ? stress : 0.0f, std::memory_order_relaxed);
    gMasterWetMix.store(std::isfinite(wet) ? wet : 0.0f, std::memory_order_relaxed);
}

BridgeTelemetrySnapshot ApoProcessorBridge::telemetry() noexcept {
    return {
        gSampleRate.load(std::memory_order_relaxed),
        gInputChannels.load(std::memory_order_relaxed),
        gProcessorLatencyFrames.load(std::memory_order_relaxed),
        gLimiterGainReductionDb.load(std::memory_order_relaxed),
        gHeadroomStress.load(std::memory_order_relaxed),
        gMasterWetMix.load(std::memory_order_relaxed),
    };
}

void ApoProcessorBridge::process(float* interleavedStereo, std::size_t frames) noexcept {
    if (!prepared_ || inputChannels_ != 2 || !interleavedStereo || frames == 0) return;
    processor_.processInterleaved(interleavedStereo, frames, 2);
    publishTelemetry();
}

void ApoProcessorBridge::processToStereo(
    const float* interleavedInput,
    float* interleavedStereoOutput,
    std::size_t frames) noexcept {
    if (!prepared_ || !interleavedInput || !interleavedStereoOutput || frames == 0) return;

    if (inputChannels_ == 2) {
        if (interleavedInput != interleavedStereoOutput) {
            std::memcpy(
                interleavedStereoOutput,
                interleavedInput,
                frames * 2 * sizeof(float));
        }
    } else {
        multichannel_.processInterleavedToStereo(
            interleavedInput,
            interleavedStereoOutput,
            frames);
    }
    processor_.processInterleaved(interleavedStereoOutput, frames, 2);
    publishTelemetry();
}

} // namespace pulsefx::windows
