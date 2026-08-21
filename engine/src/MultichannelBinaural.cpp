#include "pulsefx/MultichannelBinaural.h"
#include <algorithm>
#include <cmath>

namespace pulsefx {
namespace {
constexpr float kCentre = 0.70710678f;
constexpr float kLfe = 0.35f;

std::size_t msToSamples(float milliseconds, float sampleRate) noexcept {
    const float raw = milliseconds * sampleRate * 0.001f;
    return static_cast<std::size_t>(std::clamp(
        raw,
        0.0f,
        static_cast<float>(MultichannelBinaural::kHistorySamples - 1)));
}
}

bool MultichannelBinaural::prepare(float sampleRate, std::size_t channels) noexcept {
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0f || sampleRate > 384000.0f ||
        (channels != 2 && channels != 6 && channels != 8)) {
        channels_ = 0;
        return false;
    }
    sampleRate_ = sampleRate;
    channels_ = channels;
    const float x = 2.0f * 3.14159265358979323846f * 3600.0f / sampleRate_;
    shadowAlpha_ = 1.0f - std::exp(-x);
    configurePaths();
    reset();
    return true;
}

void MultichannelBinaural::setAmount(float amount) noexcept {
    if (!std::isfinite(amount)) amount = 0.0f;
    amountTarget_ = std::clamp(amount, 0.0f, 1.0f);
}

void MultichannelBinaural::reset() noexcept {
    for (auto& channel : history_) channel.fill(0.0f);
    leftShadowState_.fill(0.0f);
    rightShadowState_.fill(0.0f);
    writeIndex_ = 0;
    amountCurrent_ = amountTarget_;
}

void MultichannelBinaural::configurePaths() noexcept {
    paths_.fill({});
    paths_[0] = {1.00f, 0.28f, 0.00f, 0.55f, false, true};
    paths_[1] = {0.28f, 1.00f, 0.55f, 0.00f, true, false};
    if (channels_ >= 6) {
        paths_[2] = {0.68f, 0.68f, 0.08f, 0.08f, false, false};
        paths_[3] = {0.30f, 0.30f, 0.00f, 0.00f, false, false};
        if (channels_ == 6) {
            paths_[4] = {0.84f, 0.22f, 0.18f, 0.78f, false, true};
            paths_[5] = {0.22f, 0.84f, 0.78f, 0.18f, true, false};
        } else {
            paths_[4] = {0.70f, 0.30f, 0.35f, 0.88f, false, true};
            paths_[5] = {0.30f, 0.70f, 0.88f, 0.35f, true, false};
            paths_[6] = {0.84f, 0.22f, 0.18f, 0.78f, false, true};
            paths_[7] = {0.22f, 0.84f, 0.78f, 0.18f, true, false};
        }
    }

    for (std::size_t channel = 0; channel < kMaxChannels; ++channel) {
        leftDelaySamples_[channel] = msToSamples(paths_[channel].leftDelayMs, sampleRate_);
        rightDelaySamples_[channel] = msToSamples(paths_[channel].rightDelayMs, sampleRate_);
    }
}

float MultichannelBinaural::delayed(std::size_t channel, std::size_t delaySamples) const noexcept {
    const std::size_t index = (writeIndex_ + kHistorySamples - delaySamples) % kHistorySamples;
    return history_[channel][index];
}

void MultichannelBinaural::processInterleavedToStereo(
    const float* input,
    float* stereoOutput,
    std::size_t frames) noexcept {
    if (!input || !stereoOutput || frames == 0 || channels_ == 0) return;

    if (channels_ == 2) {
        if (input == stereoOutput) return;
        for (std::size_t frame = 0; frame < frames; ++frame) {
            stereoOutput[frame * 2] = std::isfinite(input[frame * 2]) ? input[frame * 2] : 0.0f;
            stereoOutput[frame * 2 + 1] = std::isfinite(input[frame * 2 + 1]) ? input[frame * 2 + 1] : 0.0f;
        }
        return;
    }

    const float dryNormalize = channels_ == 6 ? 0.56f : 0.47f;
    const float wetNormalize = channels_ == 6 ? 0.50f : 0.42f;

    for (std::size_t frame = 0; frame < frames; ++frame) {
        const float* source = input + frame * channels_;
        for (std::size_t channel = 0; channel < channels_; ++channel) {
            const float sample = std::isfinite(source[channel]) ? source[channel] : 0.0f;
            history_[channel][writeIndex_] = sample;
        }
        const auto current = [&](std::size_t channel) noexcept {
            return history_[channel][writeIndex_];
        };

        float dryLeft = current(0) + kCentre * current(2) + kLfe * current(3);
        float dryRight = current(1) + kCentre * current(2) + kLfe * current(3);
        if (channels_ == 6) {
            dryLeft += kCentre * current(4);
            dryRight += kCentre * current(5);
        } else {
            dryLeft += 0.50f * current(4) + kCentre * current(6);
            dryRight += 0.50f * current(5) + kCentre * current(7);
        }
        dryLeft *= dryNormalize;
        dryRight *= dryNormalize;

        float wetLeft = 0.0f;
        float wetRight = 0.0f;
        for (std::size_t channel = 0; channel < channels_; ++channel) {
            float left = delayed(channel, leftDelaySamples_[channel]);
            float right = delayed(channel, rightDelaySamples_[channel]);
            if (paths_[channel].shadowLeft) {
                leftShadowState_[channel] += shadowAlpha_ * (left - leftShadowState_[channel]);
                left = leftShadowState_[channel];
            }
            if (paths_[channel].shadowRight) {
                rightShadowState_[channel] += shadowAlpha_ * (right - rightShadowState_[channel]);
                right = rightShadowState_[channel];
            }
            wetLeft += left * paths_[channel].leftGain;
            wetRight += right * paths_[channel].rightGain;
        }
        wetLeft *= wetNormalize;
        wetRight *= wetNormalize;

        amountCurrent_ += (amountTarget_ - amountCurrent_) * 0.0025f;
        const float dryMix = 1.0f - amountCurrent_;
        stereoOutput[frame * 2] = dryLeft * dryMix + wetLeft * amountCurrent_;
        stereoOutput[frame * 2 + 1] = dryRight * dryMix + wetRight * amountCurrent_;
        writeIndex_ = (writeIndex_ + 1) % kHistorySamples;
    }
}

} // namespace pulsefx
