#pragma once
#include "SmoothedValue.h"

namespace pulsefx {

class StereoEnhancer {
public:
    void prepare(float sampleRate) noexcept;
    void setAmount(float amount) noexcept;
    void reset() noexcept;
    void processStereo(float& left, float& right) noexcept;

private:
    float alpha_{0.0f};
    float lowL_{0.0f};
    float lowR_{0.0f};
    SmoothedValue amount_{};
};

} // namespace pulsefx
