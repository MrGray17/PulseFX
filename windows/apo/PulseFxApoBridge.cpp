#include "PulseFxApoBridge.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace pulsefx::windows {

bool ApoProcessorBridge::prepare(float sampleRate, std::size_t inputChannels) noexcept {
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0f || sampleRate > 384000.0f ||
        (inputChannels != 2 && inputChannels != 6 && inputChannels != 8)) {
        prepared_ = false;
        inputChannels_ = 0;
        return false;
    }
    inputChannels_ = inputChannels;
    processor_.prepare(sampleRate);
    if (!multichannel_.prepare(sampleRate, inputChannels_)) {
        prepared_ = false;
        inputChannels_ = 0;
        return false;
    }
    prepared_ = true;
    applyControlState(control_);
    return true;
}

void ApoProcessorBridge::reset() noexcept {
    if (!prepared_) return;
    multichannel_.reset();
    processor_.reset();
}

void ApoProcessorBridge::applyControlState(const ApoControlState& state) noexcept {
    control_ = state;
    if (!prepared_) return;

    ProcessorParameters processorState = state.processor;
    if (inputChannels_ > 2) {
        // Multichannel sources already receive directional rendering before the
        // stereo chain. Applying the stereo HRTF effect again would double-
        // spatialize positional cues.
        multichannel_.setAmount(processorState.surround);
        processorState.surround = 0.0f;
    } else {
        multichannel_.setAmount(0.0f);
    }

    processor_.setParameters(processorState);
    for (std::size_t band = 0; band < state.eqDb.size(); ++band) {
        processor_.equalizer().setBandGain(band, state.eqDb[band]);
    }
    processor_.headphoneCorrection().setProfile(state.headphoneProfile);
    processor_.headphoneCorrection().setEnabled(state.headphoneCorrectionEnabled);
}

void ApoProcessorBridge::process(float* interleavedStereo, std::size_t frames) noexcept {
    if (!prepared_ || inputChannels_ != 2 || !interleavedStereo || frames == 0) return;
    processor_.processInterleaved(interleavedStereo, frames, 2);
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
}

} // namespace pulsefx::windows
