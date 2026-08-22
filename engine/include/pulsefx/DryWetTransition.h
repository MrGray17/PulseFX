#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace pulsefx {

// Keeps the transparent/dry path time-aligned with the processed path and
// performs bounded master wet/dry transitions. Storage is allocated only from
// prepare(); process() is allocation-free and noexcept.
class DryWetTransition {
public:
    bool prepare(float sampleRate, std::size_t latencySamples, bool wet) noexcept {
        sampleRate_ = std::clamp(
            std::isfinite(sampleRate) ? sampleRate : 48000.0f,
            8000.0f,
            384000.0f);
        try {
            // Half a second is intentionally generous for limiter + optional
            // spectral pitch latency while keeping realtime storage on the heap.
            const auto requested = static_cast<std::size_t>(std::ceil(sampleRate_ * 0.5f)) + 8;
            capacity_ = std::clamp<std::size_t>(requested, 8192, 262144);
            left_.assign(capacity_, 0.0f);
            right_.assign(capacity_, 0.0f);
        } catch (...) {
            left_.clear();
            right_.clear();
            capacity_ = 0;
            return false;
        }

        wetStep_ = 1.0f / std::max(1.0f, sampleRate_ * 0.025f);
        delayStep_ = 1.0f / std::max(1.0f, sampleRate_ * 0.040f);
        writeIndex_ = 0;
        wetCurrent_ = wetTarget_ = wet ? 1.0f : 0.0f;
        currentDelay_ = targetDelay_ = clampDelay(latencySamples);
        delayTransition_ = 1.0f;
        return true;
    }

    void reset(bool wet) noexcept {
        std::fill(left_.begin(), left_.end(), 0.0f);
        std::fill(right_.begin(), right_.end(), 0.0f);
        writeIndex_ = 0;
        wetCurrent_ = wetTarget_ = wet ? 1.0f : 0.0f;
        currentDelay_ = targetDelay_;
        delayTransition_ = 1.0f;
    }

    void setWet(bool wet) noexcept { wetTarget_ = wet ? 1.0f : 0.0f; }

    void setLatencySamples(std::size_t latencySamples) noexcept {
        const auto next = clampDelay(latencySamples);
        if (next == targetDelay_) return;
        targetDelay_ = next;
        delayTransition_ = 0.0f;
    }

    std::size_t latencySamples() const noexcept { return targetDelay_; }
    float wetMix() const noexcept { return wetCurrent_; }

    void process(
        float inputLeft,
        float inputRight,
        float& delayedDryLeft,
        float& delayedDryRight,
        float& wetMix) noexcept {
        if (!std::isfinite(inputLeft)) inputLeft = 0.0f;
        if (!std::isfinite(inputRight)) inputRight = 0.0f;

        if (capacity_ == 0 || left_.empty() || right_.empty()) {
            delayedDryLeft = inputLeft;
            delayedDryRight = inputRight;
            advanceWet();
            wetMix = wetCurrent_;
            return;
        }

        left_[writeIndex_] = inputLeft;
        right_[writeIndex_] = inputRight;

        const float oldLeft = read(left_, currentDelay_);
        const float oldRight = read(right_, currentDelay_);
        float dryLeft = oldLeft;
        float dryRight = oldRight;

        if (delayTransition_ < 1.0f) {
            const float nextLeft = read(left_, targetDelay_);
            const float nextRight = read(right_, targetDelay_);
            const float amount = std::clamp(delayTransition_, 0.0f, 1.0f);
            dryLeft = oldLeft + (nextLeft - oldLeft) * amount;
            dryRight = oldRight + (nextRight - oldRight) * amount;
            delayTransition_ = std::min(1.0f, delayTransition_ + delayStep_);
            if (delayTransition_ >= 1.0f) currentDelay_ = targetDelay_;
        }

        writeIndex_ = (writeIndex_ + 1) % capacity_;
        advanceWet();
        delayedDryLeft = std::isfinite(dryLeft) ? dryLeft : 0.0f;
        delayedDryRight = std::isfinite(dryRight) ? dryRight : 0.0f;
        wetMix = wetCurrent_;
    }

private:
    std::size_t clampDelay(std::size_t value) const noexcept {
        if (capacity_ <= 1) return 0;
        return std::min(value, capacity_ - 1);
    }

    float read(const std::vector<float>& buffer, std::size_t delay) const noexcept {
        if (capacity_ == 0 || buffer.empty()) return 0.0f;
        const std::size_t clamped = std::min(delay, capacity_ - 1);
        const std::size_t index = (writeIndex_ + capacity_ - clamped) % capacity_;
        return buffer[index];
    }

    void advanceWet() noexcept {
        if (wetCurrent_ < wetTarget_) wetCurrent_ = std::min(wetTarget_, wetCurrent_ + wetStep_);
        else if (wetCurrent_ > wetTarget_) wetCurrent_ = std::max(wetTarget_, wetCurrent_ - wetStep_);
        if (!std::isfinite(wetCurrent_)) wetCurrent_ = wetTarget_;
    }

    float sampleRate_{48000.0f};
    float wetCurrent_{1.0f};
    float wetTarget_{1.0f};
    float wetStep_{1.0f};
    float delayTransition_{1.0f};
    float delayStep_{1.0f};
    std::size_t capacity_{0};
    std::size_t writeIndex_{0};
    std::size_t currentDelay_{0};
    std::size_t targetDelay_{0};
    std::vector<float> left_{};
    std::vector<float> right_{};
};

} // namespace pulsefx
