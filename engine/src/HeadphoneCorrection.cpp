#include "pulsefx/HeadphoneCorrection.h"
#include <algorithm>

namespace pulsefx {

void HeadphoneCorrection::prepare(float sampleRate) noexcept {
    sampleRate_ = std::clamp(sampleRate, 8000.0f, 384000.0f);
    rebuild();
}

void HeadphoneCorrection::setProfile(const HeadphoneProfile& profile) noexcept {
    profile_ = profile;
    rebuild();
}

void HeadphoneCorrection::rebuild() noexcept {
    for (std::size_t i = 0; i < profile_.bands.size(); ++i) {
        const auto& band = profile_.bands[i];
        const float gain = band.enabled ? std::clamp(band.gainDb, -12.0f, 12.0f) : 0.0f;
        const float q = std::clamp(band.q, 0.1f, 12.0f);
        const float frequency = std::clamp(band.frequency, 20.0f, sampleRate_ * 0.45f);
        left_[i].setPeaking(sampleRate_, frequency, q, gain);
        right_[i].setPeaking(sampleRate_, frequency, q, gain);
    }
}

void HeadphoneCorrection::reset() noexcept {
    for (auto& filter : left_) filter.reset();
    for (auto& filter : right_) filter.reset();
}

void HeadphoneCorrection::processStereo(float& left, float& right) noexcept {
    if (!enabled_) return;
    for (std::size_t i = 0; i < profile_.bands.size(); ++i) {
        left = left_[i].process(left);
        right = right_[i].process(right);
    }
}

} // namespace pulsefx
