#pragma once
#include "SpatialCalibration.h"
#include "SpatialProfileTuning.h"
#include <cmath>
#include <cstdint>

namespace pulsefx {

// Precomputes the calibrated Manual and calibrated+adaptive Signature HRTF
// profiles for one exact stream sample rate. All FIR generation/tuning happens
// on the control thread; the realtime renderer receives fixed-size profiles plus
// non-zero revisions only.
class SignatureSpatialProfileBank {
public:
    bool update(
        float sampleRate,
        SpatialProfileTuning signatureTuning,
        SpatialProfileTuning calibrationTuning = {}) noexcept {
        if (!std::isfinite(sampleRate) || sampleRate < 8000.0f || sampleRate > 384000.0f) {
            return false;
        }
        signatureTuning = sanitizeSpatialProfileTuning(signatureTuning);
        calibrationTuning = sanitizeSpatialProfileTuning(calibrationTuning);
        if (valid_ && sampleRate_ == sampleRate &&
            sameTuning(signatureTuning_, signatureTuning) &&
            sameTuning(calibrationTuning_, calibrationTuning)) return false;

        sampleRate_ = sampleRate;
        signatureTuning_ = signatureTuning;
        calibrationTuning_ = calibrationTuning;
        const auto base = SpatialSurround::makeDefaultProfile(sampleRate_);
        manualProfile_ = tuneSpatialProfile(base, calibrationTuning_);
        signatureProfile_ = tuneSpatialProfile(
            base,
            composeSpatialProfileTuning(signatureTuning_, calibrationTuning_));
        manualRevision_ = nextRevision();
        signatureRevision_ = nextRevision();
        valid_ = true;
        return true;
    }

    bool valid() const noexcept { return valid_; }
    float sampleRate() const noexcept { return sampleRate_; }
    const SpatialProfileTuning& tuning() const noexcept { return signatureTuning_; }
    const SpatialProfileTuning& calibrationTuning() const noexcept { return calibrationTuning_; }
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
    SpatialProfileTuning signatureTuning_{};
    SpatialProfileTuning calibrationTuning_{};
    HrtfProfile manualProfile_{};
    HrtfProfile signatureProfile_{};
    std::uint64_t revisionCounter_{0};
    std::uint64_t manualRevision_{0};
    std::uint64_t signatureRevision_{0};
};

} // namespace pulsefx
