#include "pulsefx/Processor.h"
#include <algorithm>
#include <cmath>

namespace pulsefx {
namespace {
float dbToLinear(float db) noexcept { return std::pow(10.0f, db / 20.0f); }
float finiteOr(float value, float fallback) noexcept {
    return std::isfinite(value) ? value : fallback;
}
float safePrevious(float value, float neutral) noexcept {
    return std::isfinite(value) ? value : neutral;
}
}

void Processor::prepare(float sampleRate) noexcept {
    const ProcessorParameters desired = parameters_;
    sampleRate_ = std::clamp(finiteOr(sampleRate, 48000.0f), 8000.0f, 384000.0f);
    headroomAttackCoeff_ = std::exp(-1.0f / (0.35f * sampleRate_));
    headroomReleaseCoeff_ = std::exp(-1.0f / (2.5f * sampleRate_));
    headroomStress_ = 0.0f;
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
    const ProcessorParameters previous = parameters_;
    ProcessorParameters next = parameters;

    // Treat every control snapshot as untrusted, even after host validation.
    // A non-finite field preserves the last finite setting (or a neutral value
    // if legacy/corrupt state somehow poisoned the previous snapshot).
    next.preampDb = std::clamp(
        finiteOr(next.preampDb, safePrevious(previous.preampDb, 0.0f)), -18.0f, 9.0f);
    next.bass = std::clamp(
        finiteOr(next.bass, safePrevious(previous.bass, 0.0f)), 0.0f, 1.0f);
    next.virtualBass = std::clamp(
        finiteOr(next.virtualBass, safePrevious(previous.virtualBass, 0.0f)), 0.0f, 1.0f);
    next.bassCapability = std::clamp(
        finiteOr(next.bassCapability, safePrevious(previous.bassCapability, 1.0f)), 0.0f, 1.0f);
    next.clarity = std::clamp(
        finiteOr(next.clarity, safePrevious(previous.clarity, 0.0f)), 0.0f, 1.0f);
    next.fidelity = std::clamp(
        finiteOr(next.fidelity, safePrevious(previous.fidelity, 0.0f)), 0.0f, 1.0f);
    next.space = std::clamp(
        finiteOr(next.space, safePrevious(previous.space, 0.0f)), 0.0f, 1.0f);
    next.surround = std::clamp(
        finiteOr(next.surround, safePrevious(previous.surround, 0.0f)), 0.0f, 1.0f);
    next.ambience = std::clamp(
        finiteOr(next.ambience, safePrevious(previous.ambience, 0.0f)), 0.0f, 1.0f);
    next.dynamics = std::clamp(
        finiteOr(next.dynamics, safePrevious(previous.dynamics, 0.0f)), 0.0f, 1.0f);
    next.pitchSemitones = std::clamp(
        finiteOr(next.pitchSemitones, safePrevious(previous.pitchSemitones, 0.0f)), -5.0f, 5.0f);

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

    parameters_ = next;

    // The headroom governor belongs only to Signature. Reset its memory when
    // entering/leaving adaptive mode or when processing is explicitly bypassed
    // so a later re-enable cannot inherit stale stress from an old signal.
    if (previous.adaptiveHeadroom != next.adaptiveHeadroom ||
        (!previous.bypass && next.bypass)) {
        headroomStress_ = 0.0f;
    }

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
    headroomStress_ = 0.0f;
}

float Processor::headroomEnhancementBlend() const noexcept {
    if (!parameters_.adaptiveHeadroom) return 1.0f;
    const float stress = std::clamp(
        std::isfinite(headroomStress_) ? headroomStress_ : 0.0f,
        0.0f,
        1.0f);
    // Never collapse Signature into dry sound. Even under prolonged limiting,
    // keep 38% of the enrichment field while correction/EQ remain untouched.
    return std::clamp(1.0f - 0.62f * stress, 0.38f, 1.0f);
}

void Processor::observeLimiterStress(float gainReductionDb) noexcept {
    if (!parameters_.adaptiveHeadroom) {
        headroomStress_ = 0.0f;
        return;
    }

    const float reduction = std::clamp(
        std::isfinite(gainReductionDb) ? gainReductionDb : 0.0f,
        0.0f,
        12.0f);
    // Ignore tiny true-peak catches. Continuous reduction above roughly 0.75 dB
    // is treated as evidence that optional enhancement is spending too much
    // headroom; 5 dB or more maps to full stress.
    const float target = std::clamp((reduction - 0.75f) / 4.25f, 0.0f, 1.0f);
    const float coeff = target > headroomStress_ ? headroomAttackCoeff_ : headroomReleaseCoeff_;
    headroomStress_ = coeff * headroomStress_ + (1.0f - coeff) * target;
    if (!std::isfinite(headroomStress_)) headroomStress_ = 0.0f;
    headroomStress_ = std::clamp(headroomStress_, 0.0f, 1.0f);
}

std::size_t Processor::latencySamples() const noexcept {
    return limiter_.latencySamples() + pitchShifter_.latencySamples();
}

void Processor::processInterleaved(float* samples, std::size_t frames, std::size_t channels) noexcept {
    if (!samples || frames == 0 || channels < 2 || parameters_.bypass) return;

    // The governor changes slowly and is sampled once per host block. This keeps
    // enrichment weighting constant inside a packet while the limiter updates
    // the stress envelope sample-by-sample for the next packet.
    const float enrichmentBlend = headroomEnhancementBlend();

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

        // Correction and user tone shaping define the protected baseline.
        // Signature enrichment is allowed to retreat toward it under sustained
        // limiter pressure, but the baseline itself is never dynamically EQ'd.
        const float baselineLeft = left;
        const float baselineRight = right;
        bass_.processStereo(left, right);
        virtualBass_.processStereo(left, right);
        fidelity_.processStereo(left, right);
        clarity_.processStereo(left, right);
        left = baselineLeft + (left - baselineLeft) * enrichmentBlend;
        right = baselineRight + (right - baselineRight) * enrichmentBlend;

        // Keep dynamics outside the enrichment blend: compression can reduce
        // peak pressure and should not be weakened precisely when headroom is
        // scarce.
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
        const float spatialBaselineLeft = left;
        const float spatialBaselineRight = right;
        spatialSurround_.processStereo(left, right);
        ambience_.processStereo(left, right);
        stereo_.processStereo(left, right);
        left = spatialBaselineLeft + (left - spatialBaselineLeft) * enrichmentBlend;
        right = spatialBaselineRight + (right - spatialBaselineRight) * enrichmentBlend;
        limiter_.processStereo(left, right);
        observeLimiterStress(limiter_.gainReductionDb());
        leftOut = left;
        rightOut = right;
    }
}

} // namespace pulsefx
