#include "pulsefx/Processor.h"
#include <algorithm>
#include <cmath>

namespace pulsefx {
namespace {
float dbToLinear(float db) noexcept { return std::pow(10.0f, db / 20.0f); }
}

void Processor::prepare(float sampleRate) noexcept {
    sampleRate_ = std::clamp(sampleRate, 8000.0f, 384000.0f);
    preampGain_.prepare(sampleRate_, 30.0f, 1.0f);
    equalizer_.prepare(sampleRate_);
    headphoneCorrection_.prepare(sampleRate_);
    bass_.prepare(sampleRate_);
    fidelity_.prepare(sampleRate_);
    clarity_.prepare(sampleRate_);
    dynamics_.prepare(sampleRate_);
    pitchShifter_.prepare(sampleRate_);
    spatialSurround_.prepare(sampleRate_);
    ambience_.prepare(sampleRate_);
    stereo_.prepare(sampleRate_);
    limiter_.prepare(sampleRate_);
    limiter_.setCeilingDb(-1.0f);
    limiter_.setLookaheadMs(5.0f);
    limiter_.setReleaseMs(110.0f);
    setParameters(parameters_);
}

void Processor::setParameters(const ProcessorParameters& parameters) noexcept {
    parameters_ = parameters;
    parameters_.preampDb = std::clamp(parameters_.preampDb, -18.0f, 9.0f);
    parameters_.bass = std::clamp(parameters_.bass, 0.0f, 1.0f);
    parameters_.clarity = std::clamp(parameters_.clarity, 0.0f, 1.0f);
    parameters_.fidelity = std::clamp(parameters_.fidelity, 0.0f, 1.0f);
    parameters_.space = std::clamp(parameters_.space, 0.0f, 1.0f);
    parameters_.surround = std::clamp(parameters_.surround, 0.0f, 1.0f);
    parameters_.ambience = std::clamp(parameters_.ambience, 0.0f, 1.0f);
    parameters_.dynamics = std::clamp(parameters_.dynamics, 0.0f, 1.0f);
    parameters_.pitchSemitones = std::clamp(parameters_.pitchSemitones, -5.0f, 5.0f);

    // Match the reference product's documented effect compatibility while
    // keeping transitions smoothed internally.
    if (parameters_.surround > 0.0f) {
        parameters_.space = 0.0f;
        parameters_.ambience = 0.0f;
        parameters_.nightMode = false;
    } else if (parameters_.ambience > 0.0f) {
        parameters_.space = 0.0f;
        parameters_.nightMode = false;
    }

    preampGain_.setTarget(dbToLinear(parameters_.preampDb));
    bass_.setAmount(parameters_.bass);
    fidelity_.setAmount(parameters_.fidelity);
    clarity_.setAmount(parameters_.clarity);
    pitchShifter_.setSemitones(parameters_.pitchSemitones);
    spatialSurround_.setAmount(parameters_.surround);
    ambience_.setAmount(parameters_.ambience);
    stereo_.setAmount(parameters_.space);
    dynamics_.setAmount(parameters_.dynamics);
    dynamics_.setNightMode(parameters_.nightMode);
}

void Processor::reset() noexcept {
    preampGain_.reset(dbToLinear(parameters_.preampDb));
    equalizer_.reset();
    headphoneCorrection_.reset();
    bass_.reset();
    fidelity_.reset();
    clarity_.reset();
    dynamics_.reset();
    pitchShifter_.reset();
    spatialSurround_.reset();
    ambience_.reset();
    stereo_.reset();
    limiter_.reset();
}

std::size_t Processor::latencySamples() const noexcept {
    return limiter_.latencySamples() + pitchShifter_.latencySamples();
}

void Processor::processInterleaved(float* samples, std::size_t frames, std::size_t channels) noexcept {
    if (!samples || frames == 0 || channels < 2 || parameters_.bypass) return;

    // Tone/dynamics stage. Pitch is block-based, so the chain is deliberately
    // split around it rather than allocating or buffering inside a per-sample
    // processor.
    for (std::size_t frame = 0; frame < frames; ++frame) {
        float& leftOut = samples[frame * channels];
        float& rightOut = samples[frame * channels + 1];
        const float gain = preampGain_.next();
        float left = std::isfinite(leftOut) ? leftOut * gain : 0.0f;
        float right = std::isfinite(rightOut) ? rightOut * gain : 0.0f;
        equalizer_.processStereo(left, right);
        headphoneCorrection_.processStereo(left, right);
        bass_.processStereo(left, right);
        fidelity_.processStereo(left, right);
        clarity_.processStereo(left, right);
        dynamics_.processStereo(left, right);
        leftOut = left;
        rightOut = right;
    }

    if (channels == 2 && pitchShifter_.active()) {
        pitchShifter_.processInterleaved(samples, frames);
    }

    // Spatial/output-protection stage. Keeping the limiter last means every
    // effect, including pitch, is covered by the same true-peak ceiling.
    for (std::size_t frame = 0; frame < frames; ++frame) {
        float& leftOut = samples[frame * channels];
        float& rightOut = samples[frame * channels + 1];
        float left = std::isfinite(leftOut) ? leftOut : 0.0f;
        float right = std::isfinite(rightOut) ? rightOut : 0.0f;
        spatialSurround_.processStereo(left, right);
        ambience_.processStereo(left, right);
        stereo_.processStereo(left, right);
        limiter_.processStereo(left, right);
        leftOut = left;
        rightOut = right;
    }
}

} // namespace pulsefx
