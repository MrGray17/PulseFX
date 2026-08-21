#pragma once
#include <array>
#include <cstddef>

namespace pulsefx {

struct BiquadCoefficients {
    float b0{1.0f};
    float b1{0.0f};
    float b2{0.0f};
    float a1{0.0f};
    float a2{0.0f};
};

class Biquad {
public:
    void setPeaking(float sampleRate, float frequency, float q, float gainDb) noexcept;
    void setLowShelf(float sampleRate, float frequency, float q, float gainDb) noexcept;
    void setHighShelf(float sampleRate, float frequency, float q, float gainDb) noexcept;
    void reset() noexcept;
    float process(float x) noexcept;

private:
    BiquadCoefficients c_{};
    float z1_{0.0f};
    float z2_{0.0f};
};

} // namespace pulsefx
