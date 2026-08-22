#pragma once
#include "SpatialProfileTuning.h"

namespace pulsefx {

enum class DeviceKnowledge {
    Unknown,
    Measured,
    Personalized,
};

enum class ContentClass {
    General,
    Music,
    Movie,
    Game,
    Voice,
};

struct SignatureInputs {
    DeviceKnowledge knowledge{DeviceKnowledge::Unknown};
    ContentClass content{ContentClass::General};

    // 0 = cannot reproduce deep bass cleanly, 1 = excellent LF extension.
    float lowFrequencyCapability{0.5f};

    // 0 = already close to target, 1 = large correction is required.
    float correctionDemand{0.0f};

    // 0 = smooth/low-risk treble, 1 = strong resonance/harshness risk.
    float harshnessRisk{0.0f};

    // Windows endpoint volume normalized to [0, 1]. This is not acoustic SPL.
    float endpointVolume{0.5f};

    // Recent limiter activity normalized to [0, 1]. High values mean the
    // enhancement chain is consuming too much headroom and should back off.
    float limiterStress{0.0f};

    // User preference, bounded by policy. 1 = reference Signature tuning;
    // values below/above 1 make optional enhancement subtler/more expansive
    // without changing headphone correction or escaping limiter/headroom rules.
    float strength{1.0f};

    bool lowLatency{false};
};

struct SignaturePlan {
    float preampDb{-1.5f};
    float physicalBass{0.0f};
    float virtualBass{0.0f};
    float virtualBassCapability{1.0f};
    float clarity{0.0f};
    float fidelity{0.0f};
    float surround{0.0f};
    float ambience{0.0f};
    float dynamics{0.0f};
    SpatialProfileTuning spatial{};
};

// Pure, allocation-free policy. It does not inspect audio samples and must be
// called outside the realtime callback. All outputs are finite and bounded.
SignaturePlan makeAdaptiveSignature(const SignatureInputs& inputs) noexcept;

} // namespace pulsefx
