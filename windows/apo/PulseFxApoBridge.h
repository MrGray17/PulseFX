#pragma once
#include "pulsefx/HeadphoneCorrection.h"
#include "pulsefx/MultichannelBinaural.h"
#include "pulsefx/Processor.h"
#include <array>
#include <cstddef>

namespace pulsefx::windows {

struct ApoControlState {
    ProcessorParameters processor{};
    std::array<float, Equalizer::kFrequencies.size()> eqDb{};
    HeadphoneProfile headphoneProfile{};
    bool headphoneCorrectionEnabled{false};
};

// Portable seam between the Windows audio host and DSP engine. Stereo input is
// processed in place; 5.1/7.1 input is rendered to stereo before the normal
// PulseFX chain so the physical sink never needs to support the source layout.
class ApoProcessorBridge {
public:
    bool prepare(float sampleRate, std::size_t inputChannels) noexcept;
    void reset() noexcept;
    void applyControlState(const ApoControlState& state) noexcept;
    void process(float* interleavedStereo, std::size_t frames) noexcept;
    void processToStereo(
        const float* interleavedInput,
        float* interleavedStereoOutput,
        std::size_t frames) noexcept;

    bool prepared() const noexcept { return prepared_; }
    std::size_t channels() const noexcept { return inputChannels_; }
    std::size_t latencyFrames() const noexcept { return processor_.latencySamples(); }
    Processor& processor() noexcept { return processor_; }

private:
    Processor processor_{};
    MultichannelBinaural multichannel_{};
    ApoControlState control_{};
    std::size_t inputChannels_{0};
    bool prepared_{false};
};

} // namespace pulsefx::windows
