#include "pulsefx/PitchShifter.h"
#include <cstring>
#include <signalsmith-stretch/signalsmith-stretch.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace pulsefx {
namespace {
constexpr std::size_t kChunkFrames = 2048;
constexpr float kInactiveThreshold = 0.001f;
}

struct PitchShifter::Impl {
    signalsmith::stretch::SignalsmithStretch<float> stretch{0x50554658L};
    std::array<float, kChunkFrames> leftIn{};
    std::array<float, kChunkFrames> rightIn{};
    std::array<float, kChunkFrames> leftOut{};
    std::array<float, kChunkFrames> rightOut{};
    float sampleRate{48000.0f};
    float targetSemitones{0.0f};
    float currentSemitones{0.0f};
    std::size_t latency{0};
    bool prepared{false};
};

PitchShifter::PitchShifter() : impl_(std::make_unique<Impl>()) {}
PitchShifter::~PitchShifter() = default;

bool PitchShifter::prepare(float sampleRate) noexcept {
    if (!impl_ || !std::isfinite(sampleRate) || sampleRate < 8000.0f || sampleRate > 384000.0f) {
        return false;
    }
    try {
        impl_->sampleRate = sampleRate;
        // Split computation avoids large periodic CPU spikes in the realtime
        // relay at the cost of one additional interval of pitch-only latency.
        impl_->stretch.presetCheaper(2, sampleRate, true);
        impl_->stretch.setTransposeSemitones(0.0f, 8000.0f / sampleRate);
        impl_->latency = static_cast<std::size_t>(
            std::max(0, impl_->stretch.inputLatency()) +
            std::max(0, impl_->stretch.outputLatency()));
        impl_->targetSemitones = 0.0f;
        impl_->currentSemitones = 0.0f;
        impl_->prepared = true;
        impl_->stretch.reset();
        return true;
    } catch (...) {
        impl_->prepared = false;
        impl_->latency = 0;
        return false;
    }
}

void PitchShifter::reset() noexcept {
    if (!impl_ || !impl_->prepared) return;
    try {
        impl_->stretch.reset();
        impl_->currentSemitones = impl_->targetSemitones;
        impl_->stretch.setTransposeSemitones(
            impl_->currentSemitones,
            8000.0f / impl_->sampleRate);
    } catch (...) {
        impl_->prepared = false;
    }
}

void PitchShifter::setSemitones(float semitones) noexcept {
    if (!impl_) return;
    if (!std::isfinite(semitones)) semitones = 0.0f;
    impl_->targetSemitones = std::clamp(semitones, -5.0f, 5.0f);
    if (std::abs(impl_->targetSemitones) < kInactiveThreshold) {
        impl_->targetSemitones = 0.0f;
    }
}

float PitchShifter::semitones() const noexcept {
    return impl_ ? impl_->targetSemitones : 0.0f;
}

bool PitchShifter::active() const noexcept {
    return impl_ && impl_->prepared && std::abs(impl_->targetSemitones) >= kInactiveThreshold;
}

std::size_t PitchShifter::latencySamples() const noexcept {
    return active() ? preparedLatencySamples() : 0;
}

std::size_t PitchShifter::preparedLatencySamples() const noexcept {
    return impl_ && impl_->prepared ? impl_->latency : 0;
}

void PitchShifter::processInterleaved(float* stereo, std::size_t frames) noexcept {
    if (!stereo || frames == 0 || !active() || !impl_) return;

    try {
        std::size_t offset = 0;
        while (offset < frames) {
            const std::size_t count = std::min(kChunkFrames, frames - offset);
            for (std::size_t i = 0; i < count; ++i) {
                const float left = stereo[(offset + i) * 2];
                const float right = stereo[(offset + i) * 2 + 1];
                impl_->leftIn[i] = std::isfinite(left) ? left : 0.0f;
                impl_->rightIn[i] = std::isfinite(right) ? right : 0.0f;
            }

            // Smooth automation in spectral-block sized steps. This prevents
            // zippering when a user drags the realtime ±5 semitone control.
            const float delta = impl_->targetSemitones - impl_->currentSemitones;
            impl_->currentSemitones += std::clamp(delta, -0.20f, 0.20f);
            impl_->stretch.setTransposeSemitones(
                impl_->currentSemitones,
                8000.0f / impl_->sampleRate);

            std::array<float*, 2> inputs{impl_->leftIn.data(), impl_->rightIn.data()};
            std::array<float*, 2> outputs{impl_->leftOut.data(), impl_->rightOut.data()};
            impl_->stretch.process(
                inputs,
                static_cast<int>(count),
                outputs,
                static_cast<int>(count));

            for (std::size_t i = 0; i < count; ++i) {
                const float left = impl_->leftOut[i];
                const float right = impl_->rightOut[i];
                stereo[(offset + i) * 2] = std::isfinite(left) ? left : 0.0f;
                stereo[(offset + i) * 2 + 1] = std::isfinite(right) ? right : 0.0f;
            }
            offset += count;
        }
    } catch (...) {
        // A third-party spectral processor must never tear down the system
        // audio callback. Disable pitch until the next prepare/restart.
        impl_->prepared = false;
    }
}

} // namespace pulsefx
