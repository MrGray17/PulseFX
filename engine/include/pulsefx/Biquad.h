#pragma once

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
    void setTarget(BiquadCoefficients coefficients, float sampleRate) noexcept;
    void smoothCoefficients() noexcept;

    BiquadCoefficients c_{};
    BiquadCoefficients target_{};
    float smoothingCoeff_{0.0f};
    bool smoothing_{false};
    float z1_{0.0f};
    float z2_{0.0f};
};

} // namespace pulsefx
