#pragma once
#include "Biquad.h"
#include <array>
#include <cstddef>

namespace pulsefx {

class Equalizer {
public:
    static constexpr std::array<float, 10> kFrequencies{31.0f, 62.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f};

    void prepare(float sampleRate) noexcept;
    void setBandGain(std::size_t band, float gainDb) noexcept;
    void reset() noexcept;
    void processStereo(float& left, float& right) noexcept;

private:
    void rebuild(std::size_t band) noexcept;
    float sampleRate_{48000.0f};
    std::array<float, 10> gains_{};
    std::array<Biquad, 10> left_{};
    std::array<Biquad, 10> right_{};
};

} // namespace pulsefx
