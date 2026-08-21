#include "pulsefx/Limiter.h"
#include <algorithm>
#include <cmath>

namespace pulsefx {

void Limiter::prepare(float sampleRate) noexcept {
    sampleRate_ = std::clamp(sampleRate, 8000.0f, 384000.0f);
    updateTiming();
    reset();
}

void Limiter::setCeilingDb(float db) noexcept {
    ceilingLinear_ = std::pow(10.0f, std::clamp(db, -6.0f, -0.1f) / 20.0f);
}

void Limiter::setReleaseMs(float ms) noexcept {
    releaseMs_ = std::clamp(ms, 20.0f, 500.0f);
    updateTiming();
}

void Limiter::setLookaheadMs(float ms) noexcept {
    lookaheadMs_ = std::clamp(ms, 1.0f, 10.0f);
    updateTiming();
    reset();
}

void Limiter::updateTiming() noexcept {
    releaseCoeff_ = std::exp(-1.0f / (0.001f * releaseMs_ * sampleRate_));
    const auto requested = static_cast<std::size_t>(std::lround(sampleRate_ * lookaheadMs_ * 0.001f));
    lookaheadSamples_ = std::clamp<std::size_t>(requested, 1, kMaxLookaheadFrames);
    bufferLength_ = lookaheadSamples_ + 1;
}

void Limiter::reset() noexcept {
    envelope_ = 1.0f;
    gainReductionDb_ = 0.0f;
    writeIndex_ = 0;
    sampleIndex_ = 0;
    peakHead_ = peakTail_ = 0;
    delayL_.fill(0.0f);
    delayR_.fill(0.0f);
    peakValues_.fill(0.0f);
    peakIndices_.fill(0);
}

void Limiter::pushPeak(float peak, std::uint64_t index) noexcept {
    while (peakHead_ != peakTail_) {
        const std::size_t back = (peakTail_ + kQueueCapacity - 1) % kQueueCapacity;
        if (peakValues_[back] > peak) break;
        peakTail_ = back;
    }
    peakValues_[peakTail_] = peak;
    peakIndices_[peakTail_] = index;
    peakTail_ = (peakTail_ + 1) % kQueueCapacity;
    if (peakTail_ == peakHead_) peakHead_ = (peakHead_ + 1) % kQueueCapacity;
}

void Limiter::expirePeaks(std::uint64_t minimumIndex) noexcept {
    while (peakHead_ != peakTail_ && peakIndices_[peakHead_] < minimumIndex) {
        peakHead_ = (peakHead_ + 1) % kQueueCapacity;
    }
}

float Limiter::futurePeak() const noexcept {
    return peakHead_ != peakTail_ ? peakValues_[peakHead_] : 0.0f;
}

void Limiter::processStereo(float& left, float& right) noexcept {
    if (!std::isfinite(left)) left = 0.0f;
    if (!std::isfinite(right)) right = 0.0f;

    const float inputLeft = left;
    const float inputRight = right;
    const float peak = std::max(std::abs(inputLeft), std::abs(inputRight));
    pushPeak(peak, sampleIndex_);

    const std::uint64_t oldestVisible = sampleIndex_ > lookaheadSamples_
        ? sampleIndex_ - lookaheadSamples_
        : 0;
    expirePeaks(oldestVisible);

    const float aheadPeak = futurePeak();
    const float target = aheadPeak > ceilingLinear_
        ? ceilingLinear_ / std::max(aheadPeak, 1.0e-12f)
        : 1.0f;

    if (target < envelope_) envelope_ = target;
    else envelope_ = releaseCoeff_ * envelope_ + (1.0f - releaseCoeff_) * target;

    delayL_[writeIndex_] = inputLeft;
    delayR_[writeIndex_] = inputRight;
    const std::size_t readIndex = (writeIndex_ + 1) % bufferLength_;
    left = delayL_[readIndex] * envelope_;
    right = delayR_[readIndex] * envelope_;
    writeIndex_ = readIndex;
    ++sampleIndex_;

    gainReductionDb_ = envelope_ < 1.0f
        ? -20.0f * std::log10(std::max(envelope_, 1.0e-8f))
        : 0.0f;
}

} // namespace pulsefx
