#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace pulsefx::windows {

// Slow PI controller for keeping a relay ring buffer near its target fill.
// It produces a tiny render-rate correction; no samples are dropped/duplicated.
class ClockDriftController {
public:
    void prepare(float nominalSampleRate, std::size_t targetBufferedFrames) noexcept {
        nominalSampleRate_ = std::clamp(nominalSampleRate, 8000.0f, 384000.0f);
        targetBufferedFrames_ = std::max<std::size_t>(targetBufferedFrames, 1);
        reset();
    }

    void reset() noexcept {
        integral_ = 0.0f;
        correction_ = 0.0f;
    }

    float update(std::size_t bufferedFrames) noexcept {
        const float target = static_cast<float>(targetBufferedFrames_);
        float error = (static_cast<float>(bufferedFrames) - target) / target;
        if (std::abs(error) < 0.005f) error = 0.0f;

        constexpr float kProportional = 0.0015f;
        constexpr float kIntegral = 0.00002f;
        constexpr float kMaxCorrection = 0.0020f; // +/- 2000 ppm hard recovery bound.

        integral_ = std::clamp(
            integral_ + error * kIntegral,
            -kMaxCorrection,
            kMaxCorrection);
        const float requested = std::clamp(
            error * kProportional + integral_,
            -kMaxCorrection,
            kMaxCorrection);
        correction_ += (requested - correction_) * 0.18f;
        return nominalSampleRate_ * (1.0f + correction_);
    }

    float correctionPpm() const noexcept { return correction_ * 1'000'000.0f; }
    std::size_t targetBufferedFrames() const noexcept { return targetBufferedFrames_; }

private:
    float nominalSampleRate_{48000.0f};
    std::size_t targetBufferedFrames_{4800};
    float integral_{0.0f};
    float correction_{0.0f};
};

} // namespace pulsefx::windows
