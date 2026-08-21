#pragma once
#include "FirConvolver.h"
#include <array>
#include <cstddef>

namespace pulsefx {

struct HrtfProfile {
    static constexpr std::size_t kMaxTaps = FirConvolver::kMaxTaps;
    std::array<float, kMaxTaps> leftToLeft{};
    std::array<float, kMaxTaps> leftToRight{};
    std::array<float, kMaxTaps> rightToLeft{};
    std::array<float, kMaxTaps> rightToRight{};
    std::size_t taps{1};
};

class SpatialSurround {
public:
    void prepare(float sampleRate) noexcept;
    void setAmount(float amount) noexcept;
    void setProfile(const HrtfProfile& profile) noexcept;
    void reset() noexcept;
    void processStereo(float& left, float& right) noexcept;

    // Safe fallback only. Production-quality spatialization should load
    // measured HRIR data into HrtfProfile outside the realtime callback.
    static HrtfProfile makeDefaultProfile(float sampleRate) noexcept;

private:
    FirConvolver leftToLeft_{};
    FirConvolver leftToRight_{};
    FirConvolver rightToLeft_{};
    FirConvolver rightToRight_{};
    float amountTarget_{0.0f};
    float amountCurrent_{0.0f};
    float smoothing_{0.002f};
};

} // namespace pulsefx
