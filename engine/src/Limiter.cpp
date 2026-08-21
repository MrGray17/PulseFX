#include "pulsefx/Limiter.h"
#include <algorithm>
#include <cmath>

namespace pulsefx {

void Limiter::prepare(float sampleRate) noexcept { sampleRate_ = sampleRate; }

void Limiter::setCeilingDb(float db) noexcept {
    ceilingLinear_ = std::pow(10.0f, std::clamp(db, -6.0f, -0.1f) / 20.0f);
}

void Limiter::reset() noexcept {
    envelope_ = 1.0f;
    gainReductionDb_ = 0.0f;
}

void Limiter::processStereo(float& left, float& right) noexcept {
    const float peak = std::max(std::abs(left), std::abs(right));
    const float target = peak > ceilingLinear_ ? ceilingLinear_ / peak : 1.0f;

    // Fast attack, smooth release. A look-ahead limiter will replace this in the quality pass.
    const float releaseMs = 80.0f;
    const float releaseCoeff = std::exp(-1.0f / (0.001f * releaseMs * sampleRate_));
    if (target < envelope_) envelope_ = target;
    else envelope_ = releaseCoeff * envelope_ + (1.0f - releaseCoeff) * target;

    left *= envelope_;
    right *= envelope_;
    gainReductionDb_ = envelope_ < 1.0f ? -20.0f * std::log10(std::max(envelope_, 1.0e-8f)) : 0.0f;
}

} // namespace pulsefx
