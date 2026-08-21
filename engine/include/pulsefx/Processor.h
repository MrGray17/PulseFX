#pragma once
#include "BassEnhancer.h"
#include "ClarityEnhancer.h"
#include "Dynamics.h"
#include "Equalizer.h"
#include "HeadphoneCorrection.h"
#include "Limiter.h"
#include "SmoothedValue.h"
#include "StereoEnhancer.h"
#include <cstddef>

namespace pulsefx {

struct ProcessorParameters {
    bool bypass{false};
    float preampDb{0.0f};
    float bass{0.0f};
    float clarity{0.0f};
    float space{0.0f};
    float dynamics{0.0f};
    bool nightMode{false};
};

class Processor {
public:
    void prepare(float sampleRate) noexcept;
    void reset() noexcept;
    void setParameters(const ProcessorParameters& parameters) noexcept;
    Equalizer& equalizer() noexcept { return equalizer_; }
    HeadphoneCorrection& headphoneCorrection() noexcept { return headphoneCorrection_; }
    Limiter& limiter() noexcept { return limiter_; }
    const Limiter& limiter() const noexcept { return limiter_; }
    void processInterleaved(float* samples, std::size_t frames, std::size_t channels) noexcept;

private:
    ProcessorParameters parameters_{};
    float sampleRate_{48000.0f};
    SmoothedValue preampGain_{};
    Equalizer equalizer_{};
    HeadphoneCorrection headphoneCorrection_{};
    BassEnhancer bass_{};
    ClarityEnhancer clarity_{};
    Dynamics dynamics_{};
    StereoEnhancer stereo_{};
    Limiter limiter_{};
};

} // namespace pulsefx
