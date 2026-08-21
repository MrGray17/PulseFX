#include "pulsefx/Ambience.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace pulsefx {

void Ambience::prepare(float sampleRate) noexcept {
    const float clampedRate = std::clamp(sampleRate, 8000.0f, 384000.0f);
    constexpr std::array<float, 4> delayMs{11.0f, 23.0f, 37.0f, 53.0f};
    for (std::size_t i = 0; i < delayMs.size(); ++i) {
        delays_[i] = std::clamp<std::size_t>(
            static_cast<std::size_t>(std::lround(clampedRate * delayMs[i] / 1000.0f)),
            1,
            kBufferSize - 1);
    }
    smoothing_ = 1.0f - std::exp(-1.0f / (0.030f * clampedRate));
    reset();
}

void Ambience::setAmount(float amount) noexcept {
    amountTarget_ = std::clamp(amount, 0.0f, 1.0f);
}

void Ambience::reset() noexcept {
    leftBuffer_.fill(0.0f);
    rightBuffer_.fill(0.0f);
    writeIndex_ = 0;
    amountCurrent_ = amountTarget_;
}

void Ambience::processStereo(float& left, float& right) noexcept {
    amountCurrent_ += (amountTarget_ - amountCurrent_) * smoothing_;
    const float dryLeft = left;
    const float dryRight = right;
    const auto read = [this](const auto& buffer, std::size_t delay) noexcept {
        return buffer[(writeIndex_ + kBufferSize - delay) % kBufferSize];
    };

    const float earlyLeft =
        0.34f * read(leftBuffer_, delays_[0]) +
        0.22f * read(rightBuffer_, delays_[1]) +
        0.15f * read(leftBuffer_, delays_[2]) +
        0.10f * read(rightBuffer_, delays_[3]);
    const float earlyRight =
        0.34f * read(rightBuffer_, delays_[0]) +
        0.22f * read(leftBuffer_, delays_[1]) +
        0.15f * read(rightBuffer_, delays_[2]) +
        0.10f * read(leftBuffer_, delays_[3]);

    leftBuffer_[writeIndex_] = dryLeft;
    rightBuffer_[writeIndex_] = dryRight;
    writeIndex_ = (writeIndex_ + 1) % kBufferSize;

    const float mix = 0.42f * amountCurrent_;
    left = dryLeft + earlyLeft * mix;
    right = dryRight + earlyRight * mix;
}

} // namespace pulsefx
