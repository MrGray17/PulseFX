#include "pulsefx/Biquad.h"
#include <algorithm>
#include <cmath>

namespace pulsefx {
namespace {
constexpr float kPi = 3.14159265358979323846f;

BiquadCoefficients normalize(float b0, float b1, float b2, float a0, float a1, float a2) noexcept {
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}
}

void Biquad::setPeaking(float sampleRate, float frequency, float q, float gainDb) noexcept {
    frequency = std::clamp(frequency, 10.0f, sampleRate * 0.45f);
    q = std::max(q, 0.05f);
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * kPi * frequency / sampleRate;
    const float alpha = std::sin(w0) / (2.0f * q);
    const float c = std::cos(w0);
    c_ = normalize(1.0f + alpha * A, -2.0f * c, 1.0f - alpha * A,
                   1.0f + alpha / A, -2.0f * c, 1.0f - alpha / A);
}

void Biquad::setLowShelf(float sampleRate, float frequency, float q, float gainDb) noexcept {
    frequency = std::clamp(frequency, 10.0f, sampleRate * 0.45f);
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * kPi * frequency / sampleRate;
    const float c = std::cos(w0);
    const float s = std::sin(w0);
    const float alpha = s / (2.0f * std::max(q, 0.05f));
    const float sqrtA = std::sqrt(A);
    const float two = 2.0f * sqrtA * alpha;
    c_ = normalize(A*((A+1)-(A-1)*c+two), 2*A*((A-1)-(A+1)*c), A*((A+1)-(A-1)*c-two),
                   (A+1)+(A-1)*c+two, -2*((A-1)+(A+1)*c), (A+1)+(A-1)*c-two);
}

void Biquad::setHighShelf(float sampleRate, float frequency, float q, float gainDb) noexcept {
    frequency = std::clamp(frequency, 10.0f, sampleRate * 0.45f);
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * kPi * frequency / sampleRate;
    const float c = std::cos(w0);
    const float s = std::sin(w0);
    const float alpha = s / (2.0f * std::max(q, 0.05f));
    const float sqrtA = std::sqrt(A);
    const float two = 2.0f * sqrtA * alpha;
    c_ = normalize(A*((A+1)+(A-1)*c+two), -2*A*((A-1)+(A+1)*c), A*((A+1)+(A-1)*c-two),
                   (A+1)-(A-1)*c+two, 2*((A-1)-(A+1)*c), (A+1)-(A-1)*c-two);
}

void Biquad::reset() noexcept { z1_ = z2_ = 0.0f; }

float Biquad::process(float x) noexcept {
    const float y = c_.b0 * x + z1_;
    z1_ = c_.b1 * x - c_.a1 * y + z2_;
    z2_ = c_.b2 * x - c_.a2 * y;
    return y;
}

} // namespace pulsefx
