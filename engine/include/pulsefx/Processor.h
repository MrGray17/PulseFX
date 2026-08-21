#pragma once
#include "Equalizer.h"
#include "Limiter.h"
#include <cstddef>

namespace pulsefx {

struct ProcessorParameters {
    bool bypass{false};
    float preampDb{0.0f};
    float stereoWidth{1.0f};
};

class Processor {
public:
    void prepare(float sampleRate) noexcept;
    void reset() noexcept;
    void setParameters(const ProcessorParameters& parameters) noexcept { parameters_ = parameters; }
    Equalizer& equalizer() noexcept { return equalizer_; }
    Limiter& limiter() noexcept { return limiter_; }
    void processInterleaved(float* samples, std::size_t frames, std::size_t channels) noexcept;

private:
    ProcessorParameters parameters_{};
    Equalizer equalizer_{};
    Limiter limiter_{};
};

} // namespace pulsefx
