#include "pulsefx/FidelityEnhancer.h"
#include <algorithm>
#include <cmath>

namespace pulsefx {
namespace {
constexpr float kPi = 3.14159265358979323846f;
}

void FidelityEnhancer::prepare(float sampleRate) noexcept {
    const float clampedRate = std::clamp(sampleRate, 8000.0f, 384000.0f);
    lowpassCoeff_ = 1.0f - std::exp(-2.0f * kPi * 3200.0f / clampedRate);
    smoothing_ = 1.0f - std::exp(-1.0f / (0.030f * clampedRate));
    envelopeAttack_ = 1.0f - std::exp(-1.0f / (0.008f * clampedRate));
    envelopeRelease_ = 1.0f - std::exp(-1.0f / (0.120f * clampedRate));
    reset();
}

void FidelityEnhancer::setAmount(float amount) noexcept {
    amountTarget_ = std::clamp(amount, 0.0f, 1.0f);
}

void FidelityEnhancer::reset() noexcept {
    amountCurrent_ = amountTarget_;
    lowpassLeft_ = 0.0f;
    lowpassRight_ = 0.0f;
    envelope_ = 0.0f;
}

void FidelityEnhancer::processStereo(float& left, float& right) noexcept {
    amountCurrent_ += (amountTarget_ - amountCurrent_) * smoothing_;
    lowpassLeft_ += lowpassCoeff_ * (left - lowpassLeft_);
    lowpassRight_ += lowpassCoeff_ * (right - lowpassRight_);

    const float level = 0.5f * (std::abs(left) + std::abs(right));
    const float envelopeCoeff = level > envelope_ ? envelopeAttack_ : envelopeRelease_;
    envelope_ += envelopeCoeff * (level - envelope_);

    // Lift low-level high-frequency detail more than already-loud material.
    // This creates perceived articulation without applying a static treble shelf.
    const float quietFactor = 1.0f - std::clamp(envelope_ / 0.32f, 0.0f, 1.0f);
    const float detailGain = amountCurrent_ * (0.08f + 0.18f * quietFactor);
    left += (left - lowpassLeft_) * detailGain;
    right += (right - lowpassRight_) * detailGain;
}

} // namespace pulsefx
