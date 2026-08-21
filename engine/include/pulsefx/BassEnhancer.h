#pragma once
#include "Biquad.h"

namespace pulsefx {

class BassEnhancer {
public:
    void prepare(float sampleRate) noexcept;
    void setAmount(float amount) noexcept;
    void reset() noexcept;
    void processStereo(float& left, float& right) noexcept;

private:
    float sampleRate_{48000.0f};
    float amount_{0.0f};
    Biquad leftShelf_{};
    Biquad rightShelf_{};
};

} // namespace pulsefx
