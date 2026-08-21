#pragma once
#include <array>
#include <cstddef>

namespace pulsefx {

class Ambience {
public:
    static constexpr std::size_t kBufferSize = 32768;

    void prepare(float sampleRate) noexcept;
    void setAmount(float amount) noexcept;
    void reset() noexcept;
    void processStereo(float& left, float& right) noexcept;

private:
    std::array<float, kBufferSize> leftBuffer_{};
    std::array<float, kBufferSize> rightBuffer_{};
    std::array<std::size_t, 4> delays_{};
    std::size_t writeIndex_{0};
    float amountTarget_{0.0f};
    float amountCurrent_{0.0f};
    float smoothing_{0.002f};
};

} // namespace pulsefx
