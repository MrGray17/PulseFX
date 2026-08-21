#pragma once
#include <array>
#include <cstddef>

namespace pulsefx {

// Real-time 5.1/7.1 -> stereo renderer. The dry path follows conventional
// centre/surround downmix gains; the wet path adds per-speaker ITD, head-shadow
// filtering and directional weighting to produce a headphone binaural image.
class MultichannelBinaural {
public:
    static constexpr std::size_t kMaxChannels = 8;
    static constexpr std::size_t kHistorySamples = 384;

    bool prepare(float sampleRate, std::size_t channels) noexcept;
    void reset() noexcept;
    void setAmount(float amount) noexcept;
    float amount() const noexcept { return amountTarget_; }
    std::size_t channels() const noexcept { return channels_; }

    void processInterleavedToStereo(
        const float* input,
        float* stereoOutput,
        std::size_t frames) noexcept;

private:
    struct Path {
        float leftGain{0.0f};
        float rightGain{0.0f};
        float leftDelayMs{0.0f};
        float rightDelayMs{0.0f};
        bool shadowLeft{false};
        bool shadowRight{false};
    };

    float delayed(std::size_t channel, std::size_t delaySamples) const noexcept;
    void configurePaths() noexcept;

    float sampleRate_{48000.0f};
    std::size_t channels_{0};
    std::array<Path, kMaxChannels> paths_{};
    std::array<std::size_t, kMaxChannels> leftDelaySamples_{};
    std::array<std::size_t, kMaxChannels> rightDelaySamples_{};
    std::array<std::array<float, kHistorySamples>, kMaxChannels> history_{};
    std::array<float, kMaxChannels> leftShadowState_{};
    std::array<float, kMaxChannels> rightShadowState_{};
    std::size_t writeIndex_{0};
    float shadowAlpha_{0.35f};
    float amountTarget_{0.0f};
    float amountCurrent_{0.0f};
};

} // namespace pulsefx
