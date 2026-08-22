#include "pulsefx/Processor.h"
#include <algorithm>
#include <cmath>

namespace pulsefx {
namespace {
float dbToLinear(float db) noexcept { return std::pow(10.0f, db / 20.0f); }
}

void Processor::prepare(float sampleRate) noexcept {
    const ProcessorParameters desired = parameters_;
    sampleRate_ = std::clamp(sampleRate, 8000.0f, 384000.0f);
    preampGain_.prepare(sampleRate_, 30.0f, 1.0f);
    equalizer_.prepare(sampleRate_);
    headphoneCorrection_.prepare(sampleRate_);
    bass_.prepare(sampleRate_);
    virtualBass_.prepare(sampleRate_);
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

    // prepare() resets some stage internals (notably preamp and pitch). Make the
    // first post-prepare parameter application compare against a neutral logical
    // baseline so desired non-default controls are always restored. Later live
    // updates remain delta-only.
    parameters_ = ProcessorParameters{};
    setParameters(desired);
}

void Processor::setParameters(const ProcessorParameters& parameters) noexcept {
    ProcessorParameters next = parameters;
    next.preampDb = std::clamp(next.preampDb, -18.0f, 9.0f);
    next.bass = std::clamp(next.bass, 0.0f, 1.0f);
    next.virtualBass = std::clamp(next.virtualBass, 0.0f, 1.0f);
    next.bassCapability = std::clamp(next.bassCapability, 0.0f, 1.0f);
    next.clarity = std::clamp(next.clarity, 0.0f, 1.0f);
    next.fidelity = std::clamp(next.fidelity, 0.0f, 1.0f);
    next.space = std::clamp(next.space, 0.0f, 1.0f);
    next.surround = std::clamp(next.surround, 0.0f, 1.0f);
    next.ambience = std::clamp(next.ambience, 0.0f, 1.0f);
    next.dynamics = std::clamp(next.dynamics, 0.0f, 1.0f);
    next.pitchSemitones = std::clamp(next.pitchSemitones, -5.0f, 5.0f);

    // Match the reference product's documented effect compatibility while
    // keeping transitions smoothed internally.
    if (next.surround > 0.0f) {
        next.space = 0.0f;
        next.ambience = 0.0f;
        next.nightMode = false;
    } else if (next.ambience > 0.0f) {
        next.space = 0.0f;
        next.nightMode = false;
    }

    const ProcessorParameters previous = parameters_;
    parameters_ = next;

    // Control snapshots arrive at audio packet boundaries. Avoid touching DSP
    // modules whose effective parameter did not change: several modules rebuild
    // biquad targets and therefore perform trig/pow work in their setters.
    // Delta-application keeps normal live UI automation extremely small while
    // preserving the existing coefficient smoothing/state of unchanged stages.
    if (previous.preampDb != next.preampDb) preampGain_.setTarget(dbToLinear(next.preampDb));
    if (previous.bass != next.bass) bass_.setAmount(next.bass);
    if (previous.virtualBass != next.virtualBass) virtualBass_.setAmount(next.virtualBass);
    if (previous.bassCapability != next.bassCapability) virtualBass_.setBassCapability(next.bassCapability);
    if (previous.fidelity != next.fidelity) fidelity_.setAmount(next.fidelity);
    if (previous.clarity != next.clarity) clarity_.setAmount(next.clarity);
    if (previous.pitchSemitones != next.pitchSemitones) pitchShifter_.setSemitones(next.pitchSemitones);
    if (previous.surround != next.surround) spatialSurround_.setAmount(next.surround);
    if (previous.ambience != next.ambience) ambience_.setAmount(next.ambience);
    if (previous.space != next.space) stereo_.setAmount(next.space);
    if (previous.dynamics != next.dynamics) dynamics_.setAmount(next.dynamics);
    if (previous.nightMode != next.nightMode) dynamics_.setNightMode(next.nightMode);
}

void Processor::reset() noexcept {
    preampGain_.reset(dbToLinear(parameters_.preampDb));
    equalizer_.reset();
    headphoneCorrection_.reset();
    bass_.reset();
    virtualBass_.reset();
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
        virtualBass_.processStereo(left, right);
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
