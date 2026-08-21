#pragma once
#include <array>
#include <cstddef>

namespace pulsefx {

// 4x polyphase windowed-sinc reconstruction used for conservative
// inter-sample peak estimation. The implementation is fixed-size and
// allocation-free so it can live on the realtime audio path.
class TruePeakDetector {
public:
    static constexpr std::size_t kOversample = 4;
    static constexpr std::size_t kTapsPerPhase = 12;

    void prepare() noexcept;
    void reset() noexcept;
    float processStereo(float left, float right) noexcept;

private:
    using PhaseCoefficients = std::array<float, kTapsPerPhase>;
    std::array<PhaseCoefficients, kOversample> coefficients_{};
    std::array<float, kTapsPerPhase> historyL_{};
    std::array<float, kTapsPerPhase> historyR_{};
    std::size_t writeIndex_{0};
    bool prepared_{false};
};

} // namespace pulsefx
