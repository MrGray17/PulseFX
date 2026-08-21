#include "pulsefx/HeadphoneCorrection.h"
#include <algorithm>
#include <cmath>

namespace pulsefx {
namespace {
float dbToLinear(float db) noexcept {
    return std::pow(10.0f, db / 20.0f);
}
}

void HeadphoneCorrection::prepare(float sampleRate) noexcept {
    sampleRate_ = std::clamp(sampleRate, 8000.0f, 384000.0f);
    profileGain_.prepare(sampleRate_, 25.0f, dbToLinear(std::clamp(profile_.preampDb, -18.0f, 6.0f)));
    rebuild();
}

void HeadphoneCorrection::setProfile(const HeadphoneProfile& profile) noexcept {
    profile_ = profile;
    profile_.preampDb = std::clamp(profile_.preampDb, -18.0f, 6.0f);
    profileGain_.setTarget(dbToLinear(profile_.preampDb));
    rebuild();
}

void HeadphoneCorrection::rebuild() noexcept {
    for (std::size_t i = 0; i < profile_.bands.size(); ++i) {
        const auto& band = profile_.bands[i];
        const float gain = band.enabled ? std::clamp(band.gainDb, -12.0f, 12.0f) : 0.0f;
        const float q = std::clamp(band.q, 0.1f, 12.0f);
        const float frequency = std::clamp(band.frequency, 20.0f, sampleRate_ * 0.45f);
        switch (band.type) {
            case CorrectionFilterType::LowShelf:
                left_[i].setLowShelf(sampleRate_, frequency, q, gain);
                right_[i].setLowShelf(sampleRate_, frequency, q, gain);
                break;
            case CorrectionFilterType::HighShelf:
                left_[i].setHighShelf(sampleRate_, frequency, q, gain);
                right_[i].setHighShelf(sampleRate_, frequency, q, gain);
                break;
            case CorrectionFilterType::Peaking:
            default:
                left_[i].setPeaking(sampleRate_, frequency, q, gain);
                right_[i].setPeaking(sampleRate_, frequency, q, gain);
                break;
        }
    }
}

void HeadphoneCorrection::reset() noexcept {
    profileGain_.reset(dbToLinear(profile_.preampDb));
    for (auto& filter : left_) filter.reset();
    for (auto& filter : right_) filter.reset();
}

void HeadphoneCorrection::processStereo(float& left, float& right) noexcept {
    if (!enabled_) return;
    const float gain = profileGain_.next();
    left *= gain;
    right *= gain;
    for (std::size_t i = 0; i < profile_.bands.size(); ++i) {
        left = left_[i].process(left);
        right = right_[i].process(right);
    }
}

} // namespace pulsefx
