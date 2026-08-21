#pragma once
#include "Biquad.h"
#include "SmoothedValue.h"
#include <array>
#include <cstddef>

namespace pulsefx {

enum class CorrectionFilterType {
    Peaking,
    LowShelf,
    HighShelf,
};

struct CorrectionBand {
    float frequency{1000.0f};
    float q{1.0f};
    float gainDb{0.0f};
    CorrectionFilterType type{CorrectionFilterType::Peaking};
    bool enabled{false};
};

struct HeadphoneProfile {
    static constexpr std::size_t kMaxBands = 12;
    std::array<CorrectionBand, kMaxBands> bands{};
    float preampDb{0.0f};
};

class HeadphoneCorrection {
public:
    void prepare(float sampleRate) noexcept;
    void setProfile(const HeadphoneProfile& profile) noexcept;
    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }
    void reset() noexcept;
    void processStereo(float& left, float& right) noexcept;

private:
    void rebuild() noexcept;
    float sampleRate_{48000.0f};
    bool enabled_{false};
    HeadphoneProfile profile_{};
    SmoothedValue profileGain_{};
    std::array<Biquad, HeadphoneProfile::kMaxBands> left_{};
    std::array<Biquad, HeadphoneProfile::kMaxBands> right_{};
};

} // namespace pulsefx
