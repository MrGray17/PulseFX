#pragma once
#include "pulsefx/HeadphoneCorrection.h"
#include "pulsefx/MultichannelBinaural.h"
#include "pulsefx/Processor.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace pulsefx::windows {

struct ApoControlState {
    ProcessorParameters processor{};
    std::array<float, Equalizer::kFrequencies.size()> eqDb{};
    HeadphoneProfile headphoneProfile{};
    bool headphoneCorrectionEnabled{false};

    // Optional precomputed stereo HRTF. Calibration/fitting happens outside the
    // audio worker; the bridge only installs a new fixed-size profile when its
    // monotonically changing revision advances. Revision 0 means "use the
    // engine's current/default profile".
    HrtfProfile spatialProfile{};
    std::uint64_t spatialProfileRevision{0};
};

struct BridgeTelemetrySnapshot {
    std::uint32_t sampleRate{0};
    std::uint32_t inputChannels{0};
    std::uint32_t processorLatencyFrames{0};
    float limiterGainReductionDb{0.0f};
    float headroomStress{0.0f};
    float masterWetMix{0.0f};
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
    static BridgeTelemetrySnapshot telemetry() noexcept;

private:
    void publishTelemetry() noexcept;

    Processor processor_{};
    MultichannelBinaural multichannel_{};
    ApoControlState control_{};
    std::size_t inputChannels_{0};
    float sampleRate_{0.0f};
    bool prepared_{false};
};

} // namespace pulsefx::windows
