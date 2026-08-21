#include "pulsefx/BassEnhancer.h"
#include <algorithm>

namespace pulsefx {

void BassEnhancer::prepare(float sampleRate) noexcept {
    sampleRate_ = std::clamp(sampleRate, 8000.0f, 384000.0f);
    setAmount(amount_);
}

void BassEnhancer::setAmount(float amount) noexcept {
    amount_ = std::clamp(amount, 0.0f, 1.0f);
    const float gainDb = amount_ * 5.5f;
    leftShelf_.setLowShelf(sampleRate_, 92.0f, 0.75f, gainDb);
    rightShelf_.setLowShelf(sampleRate_, 92.0f, 0.75f, gainDb);
}

void BassEnhancer::reset() noexcept {
    leftShelf_.reset();
    rightShelf_.reset();
}

void BassEnhancer::processStereo(float& left, float& right) noexcept {
    left = leftShelf_.process(left);
    right = rightShelf_.process(right);
}

} // namespace pulsefx
