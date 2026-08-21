#include "pulsefx/ClarityEnhancer.h"
#include <algorithm>

namespace pulsefx {

void ClarityEnhancer::prepare(float sampleRate) noexcept {
    sampleRate_ = std::clamp(sampleRate, 8000.0f, 384000.0f);
    setAmount(amount_);
}

void ClarityEnhancer::setAmount(float amount) noexcept {
    amount_ = std::clamp(amount, 0.0f, 1.0f);
    const float presenceDb = 1.7f * amount_;
    const float airDb = 2.2f * amount_;
    leftPresence_.setPeaking(sampleRate_, 2600.0f, 0.8f, presenceDb);
    rightPresence_.setPeaking(sampleRate_, 2600.0f, 0.8f, presenceDb);
    leftAir_.setHighShelf(sampleRate_, 8500.0f, 0.707f, airDb);
    rightAir_.setHighShelf(sampleRate_, 8500.0f, 0.707f, airDb);
}

void ClarityEnhancer::reset() noexcept {
    leftPresence_.reset();
    rightPresence_.reset();
    leftAir_.reset();
    rightAir_.reset();
}

void ClarityEnhancer::processStereo(float& left, float& right) noexcept {
    left = leftAir_.process(leftPresence_.process(left));
    right = rightAir_.process(rightPresence_.process(right));
}

} // namespace pulsefx
