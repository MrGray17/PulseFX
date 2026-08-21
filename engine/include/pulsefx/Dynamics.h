#pragma once
#include "SmoothedValue.h"

namespace pulsefx {

class Dynamics {
public:
    void prepare(float sampleRate) noexcept;
    void setAmount(float amount) noexcept;
    void setNightMode(bool enabled) noexcept;
    void reset() noexcept;
    void processStereo(float& left, float& right) noexcept;
    float gainReductionDb() const noexcept { return gainReductionDb_; }

private:
    void updateTiming() noexcept;

    float sampleRate_{48000.0f};
    float detector_{0.0f};
    float gain_{1.0f};
    float gainReductionDb_{0.0f};
    bool nightMode_{false};
    float detectorAttackCoeff_{0.0f};
    float detectorReleaseCoeff_{0.0f};
    float gainAttackCoeff_{0.0f};
    float gainReleaseCoeff_{0.0f};
    SmoothedValue amount_{};
};

} // namespace pulsefx
