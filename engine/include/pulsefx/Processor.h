#pragma once
#include "Ambience.h"
#include "BassEnhancer.h"
#include "ClarityEnhancer.h"
#include "Dynamics.h"
#include "Equalizer.h"
#include "FidelityEnhancer.h"
#include "HeadphoneCorrection.h"
#include "Limiter.h"
#include "PitchShifter.h"
#include "SmoothedValue.h"
#include "SpatialSurround.h"
#include "StereoEnhancer.h"
#include "VirtualBassEnhancer.h"
#include <cstddef>

namespace pulsefx {

struct ProcessorParameters {
    bool bypass{false};
    float preampDb{0.0f};
    float bass{0.0f};
    float virtualBass{0.0f};       // psychoacoustic bass synthesis amount
    float bassCapability{1.0f};    // 0 = limited transducer, 1 = full LF capability
    float clarity{0.0f};
    float fidelity{0.0f};
    float space{0.0f};       // stereo-image widening
    float surround{0.0f};    // HRTF/binaural virtualization
    float ambience{0.0f};    // early reflections
    float dynamics{0.0f};
    float pitchSemitones{0.0f};
    bool nightMode{false};
};

class Processor {
public:
    void prepare(float sampleRate) noexcept;
    void reset() noexcept;
    void setParameters(const ProcessorParameters& parameters) noexcept;
    const ProcessorParameters& parameters() const noexcept { return parameters_; }
    Equalizer& equalizer() noexcept { return equalizer_; }
    HeadphoneCorrection& headphoneCorrection() noexcept { return headphoneCorrection_; }
    SpatialSurround& spatialSurround() noexcept { return spatialSurround_; }
    PitchShifter& pitchShifter() noexcept { return pitchShifter_; }
    const PitchShifter& pitchShifter() const noexcept { return pitchShifter_; }
    VirtualBassEnhancer& virtualBassEnhancer() noexcept { return virtualBass_; }
    const VirtualBassEnhancer& virtualBassEnhancer() const noexcept { return virtualBass_; }
    Limiter& limiter() noexcept { return limiter_; }
    const Limiter& limiter() const noexcept { return limiter_; }
    std::size_t latencySamples() const noexcept;
    void processInterleaved(float* samples, std::size_t frames, std::size_t channels) noexcept;

private:
    ProcessorParameters parameters_{};
    float sampleRate_{48000.0f};
    SmoothedValue preampGain_{};
    Equalizer equalizer_{};
    HeadphoneCorrection headphoneCorrection_{};
    BassEnhancer bass_{};
    VirtualBassEnhancer virtualBass_{};
    FidelityEnhancer fidelity_{};
    ClarityEnhancer clarity_{};
    Dynamics dynamics_{};
    PitchShifter pitchShifter_{};
    SpatialSurround spatialSurround_{};
    Ambience ambience_{};
    StereoEnhancer stereo_{};
    Limiter limiter_{};
};

} // namespace pulsefx
