#include "PulseFxApoBridge.h"
#include <algorithm>
#include <cmath>

namespace pulsefx::windows {

bool ApoProcessorBridge::prepare(float sampleRate, std::size_t channels) noexcept {
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0f || sampleRate > 384000.0f || channels != 2) {
        prepared_ = false;
        channels_ = 0;
        return false;
    }
    channels_ = channels;
    processor_.prepare(sampleRate);
    prepared_ = true;
    return true;
}

void ApoProcessorBridge::reset() noexcept {
    if (prepared_) processor_.reset();
}

void ApoProcessorBridge::applyControlState(const ApoControlState& state) noexcept {
    if (!prepared_) return;
    processor_.setParameters(state.processor);
    for (std::size_t band = 0; band < state.eqDb.size(); ++band) {
        processor_.equalizer().setBandGain(band, state.eqDb[band]);
    }
    processor_.headphoneCorrection().setProfile(state.headphoneProfile);
    processor_.headphoneCorrection().setEnabled(state.headphoneCorrectionEnabled);
}

void ApoProcessorBridge::process(float* interleaved, std::size_t frames) noexcept {
    if (!prepared_ || !interleaved || frames == 0) return;
    processor_.processInterleaved(interleaved, frames, channels_);
}

} // namespace pulsefx::windows
