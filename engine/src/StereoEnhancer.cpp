#include "pulsefx/StereoEnhancer.h"
#include <algorithm>
#include <cmath>

namespace pulsefx {
namespace { constexpr float kPi = 3.14159265358979323846f; }

void StereoEnhancer::prepare(float sampleRate) noexcept {
    sampleRate = std::clamp(sampleRate, 8000.0f, 384000.0f);
    constexpr float crossoverHz = 180.0f;
    alpha_ = 1.0f - std::exp(-2.0f * kPi * crossoverHz / sampleRate);
    amount_.prepare(sampleRate, 45.0f, 0.0f);
}

void StereoEnhancer::setAmount(float amount) noexcept {
    amount_.setTarget(std::clamp(amount, 0.0f, 1.0f));
}

void StereoEnhancer::reset() noexcept {
    lowL_ = lowR_ = 0.0f;
}

void StereoEnhancer::processStereo(float& left, float& right) noexcept {
    const float amount = amount_.next();
    if (amount <= 1.0e-5f) return;

    lowL_ += alpha_ * (left - lowL_);
    lowR_ += alpha_ * (right - lowR_);

    const float highL = left - lowL_;
    const float highR = right - lowR_;
    const float highMid = 0.5f * (highL + highR);
    const float highSide = 0.5f * (highL - highR);
    const float width = 1.0f + 0.65f * amount;
    const float widenedL = highMid + highSide * width;
    const float widenedR = highMid - highSide * width;

    const float lowMid = 0.5f * (lowL_ + lowR_);
    const float monoBlend = 0.12f * amount;
    const float stableLowL = lowL_ * (1.0f - monoBlend) + lowMid * monoBlend;
    const float stableLowR = lowR_ * (1.0f - monoBlend) + lowMid * monoBlend;

    left = stableLowL + widenedL;
    right = stableLowR + widenedR;
}

} // namespace pulsefx
