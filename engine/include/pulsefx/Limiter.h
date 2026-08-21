#pragma once
#include "TruePeakDetector.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace pulsefx {

class Limiter {
public:
    static constexpr std::size_t kMaxLookaheadFrames = 4096;
    static constexpr std::size_t kQueueCapacity = kMaxLookaheadFrames + 2;

    void prepare(float sampleRate) noexcept;
    void setCeilingDb(float db) noexcept;
    void setReleaseMs(float ms) noexcept;
    void setLookaheadMs(float ms) noexcept;
    void reset() noexcept;
    void processStereo(float& left, float& right) noexcept;
    float gainReductionDb() const noexcept { return gainReductionDb_; }
    std::size_t latencySamples() const noexcept { return lookaheadSamples_; }

private:
    void updateTiming() noexcept;
    void pushPeak(float peak, std::uint64_t index) noexcept;
    void expirePeaks(std::uint64_t minimumIndex) noexcept;
    float futurePeak() const noexcept;

    float sampleRate_{48000.0f};
    float ceilingLinear_{0.89125094f};
    float releaseMs_{110.0f};
    float lookaheadMs_{5.0f};
    float releaseCoeff_{0.0f};
    float envelope_{1.0f};
    float gainReductionDb_{0.0f};
    std::size_t lookaheadSamples_{240};
    std::size_t bufferLength_{241};
    std::size_t writeIndex_{0};
    std::uint64_t sampleIndex_{0};
    std::array<float, kMaxLookaheadFrames + 1> delayL_{};
    std::array<float, kMaxLookaheadFrames + 1> delayR_{};
    std::array<float, kQueueCapacity> peakValues_{};
    std::array<std::uint64_t, kQueueCapacity> peakIndices_{};
    std::size_t peakHead_{0};
    std::size_t peakTail_{0};
    TruePeakDetector truePeakDetector_{};
};

} // namespace pulsefx
