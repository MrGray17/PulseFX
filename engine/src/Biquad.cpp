#include "pulsefx/Biquad.h"
#include <algorithm>
#include <cmath>

namespace pulsefx {
namespace {
constexpr float kPi = 3.14159265358979323846f;

BiquadCoefficients normalize(float b0, float b1, float b2, float a0, float a1, float a2) noexcept {
    if (std::abs(a0) < 1.0e-12f || !std::isfinite(a0)) return {};
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

float safeSampleRate(float sampleRate) noexcept {
    return std::clamp(sampleRate, 8000.0f, 384000.0f);
}

float maxCoeffDelta(const BiquadCoefficients& a, const BiquadCoefficients& b) noexcept {
    return std::max({std::abs(a.b0-b.b0), std::abs(a.b1-b.b1), std::abs(a.b2-b.b2),
                     std::abs(a.a1-b.a1), std::abs(a.a2-b.a2)});
}
}

void Biquad::setTarget(BiquadCoefficients coefficients, float sampleRate) noexcept {
    target_ = coefficients;
    sampleRate = safeSampleRate(sampleRate);
    smoothingCoeff_ = std::exp(-1.0f / (0.018f * sampleRate));
    smoothing_ = maxCoeffDelta(c_, target_) > 1.0e-7f;
}

void Biquad::setPeaking(float sampleRate, float frequency, float q, float gainDb) noexcept {
    sampleRate = safeSampleRate(sampleRate);
    frequency = std::clamp(frequency, 10.0f, sampleRate * 0.45f);
    q = std::max(q, 0.05f);
    gainDb = std::clamp(gainDb, -24.0f, 24.0f);
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * kPi * frequency / sampleRate;
    const float alpha = std::sin(w0) / (2.0f * q);
    const float c = std::cos(w0);
    setTarget(normalize(1.0f + alpha * A, -2.0f * c, 1.0f - alpha * A,
                        1.0f + alpha / A, -2.0f * c, 1.0f - alpha / A), sampleRate);
}

void Biquad::setLowShelf(float sampleRate, float frequency, float q, float gainDb) noexcept {
    sampleRate = safeSampleRate(sampleRate);
    frequency = std::clamp(frequency, 10.0f, sampleRate * 0.45f);
    q = std::max(q, 0.05f);
    gainDb = std::clamp(gainDb, -24.0f, 24.0f);
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * kPi * frequency / sampleRate;
    const float c = std::cos(w0);
    const float s = std::sin(w0);
    const float alpha = s / (2.0f * q);
    const float sqrtA = std::sqrt(A);
    const float two = 2.0f * sqrtA * alpha;
    setTarget(normalize(A*((A+1)-(A-1)*c+two), 2*A*((A-1)-(A+1)*c), A*((A+1)-(A-1)*c-two),
                        (A+1)+(A-1)*c+two, -2*((A-1)+(A+1)*c), (A+1)+(A-1)*c-two), sampleRate);
}

void Biquad::setHighShelf(float sampleRate, float frequency, float q, float gainDb) noexcept {
    sampleRate = safeSampleRate(sampleRate);
    frequency = std::clamp(frequency, 10.0f, sampleRate * 0.45f);
    q = std::max(q, 0.05f);
    gainDb = std::clamp(gainDb, -24.0f, 24.0f);
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * kPi * frequency / sampleRate;
    const float c = std::cos(w0);
    const float s = std::sin(w0);
    const float alpha = s / (2.0f * q);
    const float sqrtA = std::sqrt(A);
    const float two = 2.0f * sqrtA * alpha;
    setTarget(normalize(A*((A+1)+(A-1)*c+two), -2*A*((A-1)+(A+1)*c), A*((A+1)+(A-1)*c-two),
                        (A+1)-(A-1)*c+two, 2*((A-1)-(A+1)*c), (A+1)-(A-1)*c-two), sampleRate);
}

void Biquad::smoothCoefficients() noexcept {
    if (!smoothing_) return;
    const float a = smoothingCoeff_;
    const float b = 1.0f - a;
    c_.b0 = a*c_.b0 + b*target_.b0;
    c_.b1 = a*c_.b1 + b*target_.b1;
    c_.b2 = a*c_.b2 + b*target_.b2;
    c_.a1 = a*c_.a1 + b*target_.a1;
    c_.a2 = a*c_.a2 + b*target_.a2;
    if (maxCoeffDelta(c_, target_) < 1.0e-6f) {
        c_ = target_;
        smoothing_ = false;
    }
}

void Biquad::reset() noexcept { z1_ = z2_ = 0.0f; }

float Biquad::process(float x) noexcept {
    smoothCoefficients();
    if (!std::isfinite(x)) x = 0.0f;
    const float y = c_.b0 * x + z1_;
    z1_ = c_.b1 * x - c_.a1 * y + z2_;
    z2_ = c_.b2 * x - c_.a2 * y;
    return std::isfinite(y) ? y : 0.0f;
}

} // namespace pulsefx
