#include "pulsefx/AdaptiveSignature.h"
#include <algorithm>
#include <cmath>

namespace pulsefx {
namespace {

float finiteOr(float value, float fallback) noexcept {
    return std::isfinite(value) ? value : fallback;
}

float unit(float value, float fallback) noexcept {
    return std::clamp(finiteOr(value, fallback), 0.0f, 1.0f);
}

float mix(float a, float b, float amount) noexcept {
    return a + (b - a) * std::clamp(amount, 0.0f, 1.0f);
}

} // namespace

SignaturePlan makeAdaptiveSignature(const SignatureInputs& raw) noexcept {
    const float lf = unit(raw.lowFrequencyCapability, 0.5f);
    const float correction = unit(raw.correctionDemand, 0.0f);
    const float harshness = unit(raw.harshnessRisk, 0.0f);
    const float volume = unit(raw.endpointVolume, 0.5f);
    const float limiterStress = unit(raw.limiterStress, 0.0f);
    const float strength = std::clamp(finiteOr(raw.strength, 1.0f), 0.50f, 1.25f);

    // Endpoint volume is only a relative control signal, never claimed as SPL.
    // At low settings, modest bass/detail compensation counters the normal
    // reduction in perceived low-frequency weight without chasing content RMS.
    const float lowVolumeComp = std::clamp((0.45f - volume) / 0.45f, 0.0f, 1.0f);

    // Sustained limiter work is evidence that the enhancement plan is spending
    // too much headroom. Back off musical gain-producing stages before relying
    // on the limiter as a permanent tone shaper.
    const float headroomScale = 1.0f - 0.62f * limiterStress;
    const float detailSafety = 1.0f - 0.72f * harshness;

    SignaturePlan plan{};

    // Reserve headroom for correction and enhancement up front. Stronger user
    // preference spends a little more preamp headroom before any boost happens.
    plan.preampDb = -1.25f - 3.0f * correction - 1.5f * limiterStress
        - std::max(0.0f, strength - 1.0f) * 1.8f;

    // Real LF boost is most useful when the transducer can reproduce it.
    // Weak-LF devices instead receive more missing-fundamental synthesis.
    const float realBassTrust = 0.25f + 0.75f * lf;
    plan.physicalBass = (0.14f + 0.13f * lowVolumeComp) * realBassTrust;

    float virtualBassTrust = 1.0f - lf;
    if (raw.knowledge == DeviceKnowledge::Unknown) {
        // Unknown devices get a smaller virtual-bass ceiling because capability
        // is uncertain; measured weak devices can be helped more confidently.
        virtualBassTrust *= 0.58f;
    }
    if (raw.knowledge == DeviceKnowledge::Personalized && lf > 0.72f) {
        virtualBassTrust *= 0.25f;
    }
    plan.virtualBass = (0.08f + 0.42f * virtualBassTrust + 0.08f * lowVolumeComp) * virtualBassTrust;
    plan.virtualBassCapability = lf;

    // Detail enhancement is explicitly de-rated by harshness/resonance risk.
    plan.clarity = (0.22f + 0.07f * lowVolumeComp) * detailSafety;
    plan.fidelity = (0.30f + 0.06f * lowVolumeComp) * (0.55f + 0.45f * detailSafety);
    plan.dynamics = 0.14f;

    // Generic HRTFs should be useful but not overconfident. Measured and
    // personalized devices earn progressively stronger externalization.
    switch (raw.knowledge) {
        case DeviceKnowledge::Unknown:
            plan.surround = 0.40f;
            plan.spatial = SpatialProfileTuning{1.00f, 1.00f, 0.92f, -0.8f};
            break;
        case DeviceKnowledge::Measured:
            plan.surround = 0.46f;
            plan.spatial = SpatialProfileTuning{1.02f, 1.00f, 0.96f, -0.7f};
            break;
        case DeviceKnowledge::Personalized:
            plan.surround = 0.52f;
            plan.spatial = SpatialProfileTuning{1.04f, 1.00f, 0.99f, -0.6f};
            break;
    }

    // Content intent changes the plan without creating completely different
    // DSP chains. Voice remains centered; movies favor externalization; games
    // preserve transient/directional cues and avoid heavy dynamics.
    switch (raw.content) {
        case ContentClass::Music:
            plan.physicalBass += 0.03f;
            plan.fidelity += 0.04f * detailSafety;
            break;
        case ContentClass::Movie:
            plan.surround += 0.08f;
            plan.dynamics += 0.07f;
            break;
        case ContentClass::Game:
            plan.surround += 0.05f;
            plan.dynamics = 0.07f;
            plan.physicalBass *= 0.88f;
            break;
        case ContentClass::Voice:
            plan.surround = 0.10f;
            plan.physicalBass *= 0.25f;
            plan.virtualBass *= 0.10f;
            plan.clarity = std::min(0.34f, 0.27f * detailSafety + 0.04f);
            plan.fidelity *= 0.45f;
            plan.dynamics = 0.10f;
            plan.spatial = SpatialProfileTuning{};
            break;
        case ContentClass::General:
            break;
    }

    if (raw.lowLatency) {
        // Keep EQ/correction/limiter and lightweight HRTF work available, but
        // reduce nonessential enhancement so interactive audio retains clean
        // transients and a generous CPU/latency safety margin.
        plan.surround *= 0.88f;
        plan.dynamics *= 0.55f;
        plan.fidelity *= 0.85f;
    }

    // Strength scales only optional enhancement. It never scales correction or
    // bypasses headroom protection. Spatial timing/gain tuning moves gently so
    // the upper strength range becomes more externalized rather than simply
    // louder or more bass-heavy.
    plan.physicalBass *= strength;
    plan.virtualBass *= strength;
    plan.clarity *= strength;
    plan.fidelity *= strength;
    plan.dynamics *= mix(0.78f, 1.08f, (strength - 0.50f) / 0.75f);
    plan.surround *= mix(0.72f, 1.10f, (strength - 0.50f) / 0.75f);
    const float spatialStrength = std::clamp((strength - 1.0f) * 0.12f, -0.06f, 0.03f);
    plan.spatial.itdScale *= 1.0f + spatialStrength;
    plan.spatial.contralateralGain *= 1.0f - spatialStrength * 0.70f;

    plan.physicalBass *= headroomScale;
    plan.virtualBass *= headroomScale;
    plan.clarity *= mix(1.0f, 0.72f, limiterStress);
    plan.fidelity *= mix(1.0f, 0.74f, limiterStress);
    plan.dynamics *= mix(1.0f, 0.82f, limiterStress);

    plan.preampDb = std::clamp(plan.preampDb, -9.0f, -0.5f);
    plan.physicalBass = std::clamp(plan.physicalBass, 0.0f, 0.42f);
    plan.virtualBass = std::clamp(plan.virtualBass, 0.0f, 0.55f);
    plan.virtualBassCapability = std::clamp(plan.virtualBassCapability, 0.0f, 1.0f);
    plan.clarity = std::clamp(plan.clarity, 0.0f, 0.38f);
    plan.fidelity = std::clamp(plan.fidelity, 0.0f, 0.44f);
    plan.surround = std::clamp(plan.surround, 0.0f, 0.62f);
    plan.ambience = 0.0f; // early reflections are integrated into the spatial renderer
    plan.dynamics = std::clamp(plan.dynamics, 0.0f, 0.24f);
    plan.spatial = sanitizeSpatialProfileTuning(plan.spatial);
    return plan;
}

} // namespace pulsefx
