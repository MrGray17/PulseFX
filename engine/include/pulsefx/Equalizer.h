#pragma once
#include "Biquad.h"
#include <array>
#include <cstddef>

namespace pulsefx {

class Equalizer {
public:
    static constexpr std::array<float, 31> kFrequencies{
        20.0f, 25.0f, 31.5f, 40.0f, 50.0f, 63.0f, 80.0f, 100.0f,
        125.0f, 160.0f, 200.0f, 250.0f, 315.0f, 400.0f, 500.0f, 630.0f,
        800.0f, 1000.0f, 1250.0f, 1600.0f, 2000.0f, 2500.0f, 3150.0f,
        4000.0f, 5000.0f, 6300.0f, 8000.0f, 10000.0f, 12500.0f,
        16000.0f, 20000.0f
    };

    void prepare(float sampleRate) noexcept;
    void setBandGain(std::size_t band, float gainDb) noexcept;
    float bandGain(std::size_t band) const noexcept;
    void setFlat() noexcept;
    void reset() noexcept;
    void processStereo(float& left, float& right) noexcept;

private:
    void rebuild(std::size_t band) noexcept;
    float sampleRate_{48000.0f};
    std::array<float, 31> gains_{};
    std::array<Biquad, 31> left_{};
    std::array<Biquad, 31> right_{};
};

} // namespace pulsefx
