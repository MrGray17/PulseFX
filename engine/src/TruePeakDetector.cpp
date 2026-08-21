#include "pulsefx/TruePeakDetector.h"
#include <algorithm>
#include <cmath>

namespace pulsefx {
namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr std::size_t kFullTapCount = TruePeakDetector::kOversample * TruePeakDetector::kTapsPerPhase;

float sinc(float x) noexcept {
    if (std::abs(x) < 1.0e-7f) return 1.0f;
    const float pix = kPi * x;
    return std::sin(pix) / pix;
}
}

void TruePeakDetector::prepare() noexcept {
    constexpr float center = static_cast<float>(kFullTapCount - 1) * 0.5f;
    for (std::size_t phase = 0; phase < kOversample; ++phase) {
        float sum = 0.0f;
        for (std::size_t tap = 0; tap < kTapsPerPhase; ++tap) {
            const std::size_t n = tap * kOversample + phase;
            const float nf = static_cast<float>(n);
            const float x = (nf - center) / static_cast<float>(kOversample);
            const float window = 0.42f
                - 0.5f * std::cos(2.0f * kPi * nf / static_cast<float>(kFullTapCount - 1))
                + 0.08f * std::cos(4.0f * kPi * nf / static_cast<float>(kFullTapCount - 1));
            const float coefficient = sinc(x) * window;
            coefficients_[phase][tap] = coefficient;
            sum += coefficient;
        }
        if (std::abs(sum) > 1.0e-8f) {
            for (float& coefficient : coefficients_[phase]) coefficient /= sum;
        }
    }
    prepared_ = true;
    reset();
}

void TruePeakDetector::reset() noexcept {
    historyL_.fill(0.0f);
    historyR_.fill(0.0f);
    writeIndex_ = 0;
}

float TruePeakDetector::processStereo(float left, float right) noexcept {
    if (!prepared_) prepare();
    if (!std::isfinite(left)) left = 0.0f;
    if (!std::isfinite(right)) right = 0.0f;

    historyL_[writeIndex_] = left;
    historyR_[writeIndex_] = right;

    float peak = std::max(std::abs(left), std::abs(right));
    for (std::size_t phase = 0; phase < kOversample; ++phase) {
        float outL = 0.0f;
        float outR = 0.0f;
        std::size_t index = writeIndex_;
        for (std::size_t tap = 0; tap < kTapsPerPhase; ++tap) {
            const float coefficient = coefficients_[phase][tap];
            outL += historyL_[index] * coefficient;
            outR += historyR_[index] * coefficient;
            index = index == 0 ? kTapsPerPhase - 1 : index - 1;
        }
        peak = std::max({peak, std::abs(outL), std::abs(outR)});
    }

    writeIndex_ = (writeIndex_ + 1) % kTapsPerPhase;
    return std::isfinite(peak) ? peak : 0.0f;
}

} // namespace pulsefx
