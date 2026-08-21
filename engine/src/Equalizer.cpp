#include "pulsefx/Equalizer.h"
#include <algorithm>

namespace pulsefx {

void Equalizer::prepare(float sampleRate) noexcept {
    sampleRate_ = std::clamp(sampleRate, 8000.0f, 384000.0f);
    for (std::size_t i = 0; i < kFrequencies.size(); ++i) rebuild(i);
}

void Equalizer::setBandGain(std::size_t band, float gainDb) noexcept {
    if (band >= gains_.size()) return;
    gains_[band] = std::clamp(gainDb, -12.0f, 12.0f);
    rebuild(band);
}

float Equalizer::bandGain(std::size_t band) const noexcept {
    return band < gains_.size() ? gains_[band] : 0.0f;
}

void Equalizer::setFlat() noexcept {
    gains_.fill(0.0f);
    for (std::size_t i = 0; i < kFrequencies.size(); ++i) rebuild(i);
}

void Equalizer::rebuild(std::size_t band) noexcept {
    const float f = kFrequencies[band];
    const float gain = gains_[band];
    constexpr float kGraphicQ = 3.8f;
    if (band == 0) {
        left_[band].setLowShelf(sampleRate_, f, 0.707f, gain);
        right_[band].setLowShelf(sampleRate_, f, 0.707f, gain);
    } else if (band == kFrequencies.size() - 1) {
        left_[band].setHighShelf(sampleRate_, f, 0.707f, gain);
        right_[band].setHighShelf(sampleRate_, f, 0.707f, gain);
    } else {
        left_[band].setPeaking(sampleRate_, f, kGraphicQ, gain);
        right_[band].setPeaking(sampleRate_, f, kGraphicQ, gain);
    }
}

void Equalizer::reset() noexcept {
    for (auto& filter : left_) filter.reset();
    for (auto& filter : right_) filter.reset();
}

void Equalizer::processStereo(float& left, float& right) noexcept {
    for (std::size_t i = 0; i < kFrequencies.size(); ++i) {
        left = left_[i].process(left);
        right = right_[i].process(right);
    }
}

} // namespace pulsefx
