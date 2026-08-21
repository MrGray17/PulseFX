#pragma once
#include <algorithm>
#include <array>
#include <cstddef>

namespace pulsefx {

class FirConvolver {
public:
    static constexpr std::size_t kMaxTaps = 128;

    void setImpulse(const float* impulse, std::size_t taps) noexcept {
        taps_ = std::clamp<std::size_t>(taps, 1, kMaxTaps);
        coefficients_.fill(0.0f);
        for (std::size_t i = 0; i < taps_; ++i) {
            coefficients_[i] = impulse ? impulse[i] : (i == 0 ? 1.0f : 0.0f);
        }
        reset();
    }

    void reset() noexcept {
        history_.fill(0.0f);
        writeIndex_ = 0;
    }

    float process(float input) noexcept {
        history_[writeIndex_] = input;
        float output = 0.0f;
        std::size_t index = writeIndex_;
        for (std::size_t tap = 0; tap < taps_; ++tap) {
            output += coefficients_[tap] * history_[index];
            index = index == 0 ? kMaxTaps - 1 : index - 1;
        }
        writeIndex_ = (writeIndex_ + 1) % kMaxTaps;
        return output;
    }

private:
    std::array<float, kMaxTaps> coefficients_{};
    std::array<float, kMaxTaps> history_{};
    std::size_t taps_{1};
    std::size_t writeIndex_{0};
};

} // namespace pulsefx
