#include "pulsefx/Processor.h"
#include <algorithm>
#include <cmath>

namespace pulsefx {

void Processor::prepare(float sampleRate) noexcept {
    equalizer_.prepare(sampleRate);
    limiter_.prepare(sampleRate);
}

void Processor::reset() noexcept {
    equalizer_.reset();
    limiter_.reset();
}

void Processor::processInterleaved(float* samples, std::size_t frames, std::size_t channels) noexcept {
    if (!samples || channels < 2) return;
    const float preamp = std::pow(10.0f, std::clamp(parameters_.preampDb, -24.0f, 12.0f) / 20.0f);
    const float width = std::clamp(parameters_.stereoWidth, 0.0f, 2.0f);

    for (std::size_t frame = 0; frame < frames; ++frame) {
        float& leftOut = samples[frame * channels];
        float& rightOut = samples[frame * channels + 1];
        if (parameters_.bypass) continue;

        float left = leftOut * preamp;
        float right = rightOut * preamp;
        equalizer_.processStereo(left, right);

        const float mid = 0.5f * (left + right);
        const float side = 0.5f * (left - right) * width;
        left = mid + side;
        right = mid - side;

        limiter_.processStereo(left, right);
        leftOut = left;
        rightOut = right;
    }
}

} // namespace pulsefx
