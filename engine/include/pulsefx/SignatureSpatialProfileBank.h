#pragma once
#include "SpatialProfileTuning.h"
#include <cmath>
#include <cstdint>

namespace pulsefx {

// Precomputes the neutral and Signature HRTF profiles for one exact stream
// sample rate. All FIR generation/tuning happens on the control thread; the
// realtime renderer only receives fixed-size profiles plus non-zero revisions.
class SignatureSpatialProfileBank {
public:
    bool update(float sampleRate, SpatialProfileTuning tuning) noexcept {
        if (!std::isfinite(sampleRate) || sampleRate < 8000.0f || sampleRate > 384000.0f) {
            return false;
        }
        tuning = sanitizeSpatialProfileTuning(tuning);
        if (valid_ && sampleRate_ == sampleRate && sameTuning(tuning_, tuning)) return false;

        sampleRate_ = sampleRate;
        tuning_ = tuning;
        manualProfile_ = SpatialSurround::makeDefaultProfile(sampleRate_);
        signatureProfile_ = tuneSpatialProfile(manualProfile_, tuning_);
        manualRevision_ = nextRevision();
        signatureRevision_ = nextRevision();
        valid_ = true;
        return true;
    }

    bool valid() const noexcept { return valid_; }
    float sampleRate() const noexcept { return sampleRate_; }
    const SpatialProfileTuning& tuning() const noexcept { return tuning_; }
    const HrtfProfile& manualProfile() const noexcept { return manualProfile_; }
    const HrtfProfile& signatureProfile() const noexcept { return signatureProfile_; }
    std::uint64_t manualRevision() const noexcept { return manualRevision_; }
    std::uint64_t signatureRevision() const noexcept { return signatureRevision_; }

private:
    static bool sameTuning(
        const SpatialProfileTuning& a,
        const SpatialProfileTuning& b) noexcept {
        return a.itdScale == b.itdScale &&
            a.ipsilateralGain == b.ipsilateralGain &&
            a.contralateralGain == b.contralateralGain &&
            a.wetTrimDb == b.wetTrimDb;
    }

    std::uint64_t nextRevision() noexcept {
        ++revisionCounter_;
        if (revisionCounter_ == 0) ++revisionCounter_; // revision 0 means no published profile
        return revisionCounter_;
    }

    bool valid_{false};
    float sampleRate_{0.0f};
    SpatialProfileTuning tuning_{};
    HrtfProfile manualProfile_{};
    HrtfProfile signatureProfile_{};
    std::uint64_t revisionCounter_{0};
    std::uint64_t manualRevision_{0};
    std::uint64_t signatureRevision_{0};
};

} // namespace pulsefx
