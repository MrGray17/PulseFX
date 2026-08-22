#pragma once
#include "AdaptiveSignature.h"
#include "HeadphoneCorrection.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace pulsefx {

// Conservative device evidence inferred from a measured headphone-correction
// profile. AutoEq tells us how much frequency-response correction is required;
// it does not tell us distortion, acoustic SPL, or a personalized HRTF, so this
// analysis deliberately avoids claiming any of those things.
struct HeadphoneSignatureEvidence {
    DeviceKnowledge knowledge{DeviceKnowledge::Unknown};
    float lowFrequencyCapability{0.5f};
    float correctionDemand{0.0f};
    float harshnessRisk{0.0f};
};

inline HeadphoneSignatureEvidence analyzeHeadphoneProfile(
    const HeadphoneProfile& profile) noexcept {
    HeadphoneSignatureEvidence evidence{};

    double gainSquareSum = 0.0;
    float lowBoostSum = 0.0f;
    float lowCutSum = 0.0f;
    float highCutSum = 0.0f;
    std::size_t enabledCount = 0;
    std::size_t lowCount = 0;
    std::size_t highCount = 0;

    for (const auto& band : profile.bands) {
        if (!band.enabled || !std::isfinite(band.frequency) ||
            !std::isfinite(band.gainDb) || band.frequency < 10.0f) {
            continue;
        }

        ++enabledCount;
        const float gain = std::clamp(band.gainDb, -18.0f, 18.0f);
        gainSquareSum += static_cast<double>(gain) * static_cast<double>(gain);

        if (band.frequency <= 250.0f) {
            ++lowCount;
            if (gain > 0.0f) lowBoostSum += gain;
            else lowCutSum += -gain;
        }
        if (band.frequency >= 2500.0f) {
            ++highCount;
            if (gain < 0.0f) highCutSum += -gain;
        }
    }

    if (enabledCount == 0) return evidence;
    evidence.knowledge = DeviceKnowledge::Measured;

    const float rmsCorrection = static_cast<float>(std::sqrt(
        gainSquareSum / static_cast<double>(enabledCount)));
    const float profilePreamp = std::isfinite(profile.preampDb)
        ? std::abs(profile.preampDb)
        : 0.0f;
    evidence.correctionDemand = std::clamp(
        0.72f * (rmsCorrection / 8.0f) + 0.28f * (profilePreamp / 12.0f),
        0.0f,
        1.0f);

    if (lowCount > 0) {
        const float lowBoost = lowBoostSum / static_cast<float>(lowCount);
        const float lowCut = lowCutSum / static_cast<float>(lowCount);
        // Positive LF correction is evidence that the raw headphone response
        // needs help in the bass region; negative correction is weak evidence
        // of stronger native LF output. Keep the estimate intentionally broad.
        evidence.lowFrequencyCapability = std::clamp(
            0.60f - 0.085f * lowBoost + 0.035f * lowCut,
            0.08f,
            0.92f);
    }

    if (highCount > 0) {
        const float highCut = highCutSum / static_cast<float>(highCount);
        // A required treble cut is useful evidence of excess upper-frequency
        // energy. Positive treble correction is not treated as proof of safety.
        evidence.harshnessRisk = std::clamp(highCut / 7.5f, 0.0f, 1.0f);
    }
    return evidence;
}

inline SignatureInputs makeSignatureInputsFromHeadphoneProfile(
    const HeadphoneProfile& profile,
    float endpointVolume = 0.5f) noexcept {
    const auto evidence = analyzeHeadphoneProfile(profile);
    SignatureInputs inputs{};
    inputs.knowledge = evidence.knowledge;
    inputs.lowFrequencyCapability = evidence.lowFrequencyCapability;
    inputs.correctionDemand = evidence.correctionDemand;
    inputs.harshnessRisk = evidence.harshnessRisk;
    inputs.endpointVolume = std::clamp(
        std::isfinite(endpointVolume) ? endpointVolume : 0.5f,
        0.0f,
        1.0f);
    return inputs;
}

} // namespace pulsefx
