#pragma once
#include "pulsefx/HeadphoneCorrection.h"
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

// Portable seam between the Windows APO COM shell and the DSP engine.
// The COM wrapper owns Windows lifetime/format negotiation; this object owns
// only deterministic, allocation-free processing after prepare().
class ApoProcessorBridge {
public:
    bool prepare(float sampleRate, std::size_t channels) noexcept;
    void reset() noexcept;
    void applyControlState(const ApoControlState& state) noexcept;
    void process(float* interleaved, std::size_t frames) noexcept;

    bool prepared() const noexcept { return prepared_; }
    std::size_t channels() const noexcept { return channels_; }
    std::size_t latencyFrames() const noexcept { return processor_.limiter().latencySamples(); }
    Processor& processor() noexcept { return processor_; }

private:
    Processor processor_{};
    std::size_t channels_{0};
    bool prepared_{false};
};

} // namespace pulsefx::windows
