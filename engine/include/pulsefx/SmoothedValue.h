#pragma once
#include <algorithm>
#include <cmath>

namespace pulsefx {

class SmoothedValue {
public:
    void prepare(float sampleRate, float timeMs, float initial = 0.0f) noexcept {
        sampleRate_ = std::clamp(sampleRate, 8000.0f, 384000.0f);
        setTimeMs(timeMs);
        current_ = target_ = initial;
    }

    void setTimeMs(float timeMs) noexcept {
        const float seconds = std::max(timeMs, 0.01f) * 0.001f;
        coeff_ = std::exp(-1.0f / (seconds * sampleRate_));
    }

    void setTarget(float target) noexcept { target_ = target; }
    void reset(float value) noexcept { current_ = target_ = value; }

    float next() noexcept {
        current_ = coeff_ * current_ + (1.0f - coeff_) * target_;
        if (std::abs(current_ - target_) < 1.0e-6f) current_ = target_;
        return current_;
    }

    float current() const noexcept { return current_; }

private:
    float sampleRate_{48000.0f};
    float coeff_{0.0f};
    float current_{0.0f};
    float target_{0.0f};
};

} // namespace pulsefx
