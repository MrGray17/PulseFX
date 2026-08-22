#include "pulsefx/VirtualBassEnhancer.h"
#include <algorithm>
#include <cmath>

namespace pulsefx {
namespace {

float onePoleCoeff(float sampleRate, float frequency) noexcept {
    const float rate = std::clamp(sampleRate, 8000.0f, 384000.0f);
    const float clampedFrequency = std::clamp(frequency, 5.0f, rate * 0.20f);
    return 1.0f - std::exp(-2.0f * 3.14159265358979323846f * clampedFrequency / rate);
}

float smoothingCoeff(float sampleRate, float milliseconds) noexcept {
    const float rate = std::clamp(sampleRate, 8000.0f, 384000.0f);
    const float seconds = std::clamp(milliseconds, 1.0f, 250.0f) * 0.001f;
    return 1.0f - std::exp(-1.0f / (seconds * rate));
}

float finiteOrZero(float value) noexcept {
    return std::isfinite(value) ? value : 0.0f;
}

} // namespace

void VirtualBassEnhancer::prepare(float sampleRate) noexcept {
    sampleRate_ = std::clamp(sampleRate, 8000.0f, 384000.0f);
    low110Coeff_ = onePoleCoeff(sampleRate_, 110.0f);
    low42Coeff_ = onePoleCoeff(sampleRate_, 42.0f);
    dcCoeff_ = onePoleCoeff(sampleRate_, 12.0f);
    hp90Coeff_ = onePoleCoeff(sampleRate_, 90.0f);
    lp360Coeff_ = onePoleCoeff(sampleRate_, 360.0f);
    envelopeRelease_ = std::exp(-1.0f / (0.055f * sampleRate_));
    controlSmoothingCoeff_ = smoothingCoeff(sampleRate_, 24.0f);
    reset();
}

void VirtualBassEnhancer::setAmount(float amount) noexcept {
    amountTarget_ = std::clamp(std::isfinite(amount) ? amount : 0.0f, 0.0f, 1.0f);
}

void VirtualBassEnhancer::setBassCapability(float capability) noexcept {
    bassCapabilityTarget_ = std::clamp(std::isfinite(capability) ? capability : 1.0f, 0.0f, 1.0f);
}

void VirtualBassEnhancer::reset() noexcept {
    amountCurrent_ = amountTarget_;
    bassCapabilityCurrent_ = bassCapabilityTarget_;
    low110_ = 0.0f;
    low42_ = 0.0f;
    squareDc_ = 0.0f;
    harmonicLow90_ = 0.0f;
    harmonicLow360_ = 0.0f;
    envelope_ = 0.0f;
}

void VirtualBassEnhancer::smoothControls() noexcept {
    amountCurrent_ += (amountTarget_ - amountCurrent_) * controlSmoothingCoeff_;
    bassCapabilityCurrent_ += (bassCapabilityTarget_ - bassCapabilityCurrent_) * controlSmoothingCoeff_;
}

float VirtualBassEnhancer::processLowBand(float mono) noexcept {
    low110_ += (mono - low110_) * low110Coeff_;
    low42_ += (mono - low42_) * low42Coeff_;
    return low110_ - low42_;
}

float VirtualBassEnhancer::processHarmonics(float lowBand) noexcept {
    const float absolute = std::abs(lowBand);
    envelope_ = std::max(absolute, envelope_ * envelopeRelease_);
    if (envelope_ < 1.0e-5f) return 0.0f;

    // Normalize only for waveshaping, then restore the tracked envelope. This
    // prevents the harmonic balance from disappearing on quiet bass while the
    // final synthesis level still follows the source amplitude.
    const float normalizer = std::max(envelope_, 0.015f);
    const float x = std::clamp(lowBand / normalizer, -1.0f, 1.0f);

    // Chebyshev-like harmonic generation. T2 contributes a second harmonic and
    // T3 a third; a slow DC tracker removes T2's offset for arbitrary content.
    const float square = (2.0f * x * x - 1.0f) * envelope_;
    squareDc_ += (square - squareDc_) * dcCoeff_;
    const float second = square - squareDc_;
    const float third = (4.0f * x * x * x - 3.0f * x) * envelope_;
    float harmonic = second * 0.62f + third * 0.24f;

    // Keep synthesized energy mainly above the weak-fundamental region and
    // below the lower midrange where it would become obvious coloration.
    harmonicLow90_ += (harmonic - harmonicLow90_) * hp90Coeff_;
    harmonic -= harmonicLow90_;
    harmonicLow360_ += (harmonic - harmonicLow360_) * lp360Coeff_;
    return harmonicLow360_;
}

void VirtualBassEnhancer::processStereo(float& left, float& right) noexcept {
    left = finiteOrZero(left);
    right = finiteOrZero(right);
    smoothControls();

    const float effective = amountCurrent_ * (1.0f - bassCapabilityCurrent_);
    if (effective <= 1.0e-5f) return;

    // Bass synthesis is deliberately mono. It strengthens bass perception
    // without pulling the low end away from the center or widening sub-bass.
    const float mono = (left + right) * 0.5f;
    const float lowBand = processLowBand(mono);
    const float harmonics = processHarmonics(lowBand);

    // Conservative ceiling: even maximum virtual-bass mode only injects a
    // fraction of the generated harmonic signal; the true-peak limiter remains
    // the final safety stage in Processor.
    const float added = harmonics * (0.18f * effective);
    left += added;
    right += added;
}

} // namespace pulsefx
