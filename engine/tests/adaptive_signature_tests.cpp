#include "pulsefx/AdaptiveSignature.h"
#include <cassert>
#include <cmath>
#include <limits>

using namespace pulsefx;

namespace {

bool finitePlan(const SignaturePlan& plan) {
    return std::isfinite(plan.preampDb) &&
        std::isfinite(plan.physicalBass) &&
        std::isfinite(plan.virtualBass) &&
        std::isfinite(plan.virtualBassCapability) &&
        std::isfinite(plan.clarity) &&
        std::isfinite(plan.fidelity) &&
        std::isfinite(plan.surround) &&
        std::isfinite(plan.ambience) &&
        std::isfinite(plan.dynamics) &&
        std::isfinite(plan.spatial.itdScale) &&
        std::isfinite(plan.spatial.ipsilateralGain) &&
        std::isfinite(plan.spatial.contralateralGain) &&
        std::isfinite(plan.spatial.wetTrimDb);
}

void assertBounds(const SignaturePlan& plan) {
    assert(finitePlan(plan));
    assert(plan.preampDb >= -9.0f && plan.preampDb <= -0.5f);
    assert(plan.physicalBass >= 0.0f && plan.physicalBass <= 0.42f);
    assert(plan.virtualBass >= 0.0f && plan.virtualBass <= 0.55f);
    assert(plan.virtualBassCapability >= 0.0f && plan.virtualBassCapability <= 1.0f);
    assert(plan.clarity >= 0.0f && plan.clarity <= 0.38f);
    assert(plan.fidelity >= 0.0f && plan.fidelity <= 0.44f);
    assert(plan.surround >= 0.0f && plan.surround <= 0.62f);
    assert(plan.ambience >= 0.0f && plan.ambience <= 1.0f);
    assert(plan.dynamics >= 0.0f && plan.dynamics <= 0.24f);
    assert(plan.spatial.itdScale >= 0.75f && plan.spatial.itdScale <= 1.25f);
    assert(plan.spatial.ipsilateralGain >= 0.65f && plan.spatial.ipsilateralGain <= 1.20f);
    assert(plan.spatial.contralateralGain >= 0.45f && plan.spatial.contralateralGain <= 1.15f);
    assert(plan.spatial.wetTrimDb >= -6.0f && plan.spatial.wetTrimDb <= 1.5f);
}

} // namespace

int main() {
    SignatureInputs unknown{};
    const auto unknownPlan = makeAdaptiveSignature(unknown);
    assertBounds(unknownPlan);

    // Better LF capability should move the plan away from synthesized bass and
    // toward the real low-frequency path.
    SignatureInputs weakLf{};
    weakLf.knowledge = DeviceKnowledge::Measured;
    weakLf.lowFrequencyCapability = 0.05f;
    weakLf.content = ContentClass::Music;
    const auto weakPlan = makeAdaptiveSignature(weakLf);

    SignatureInputs strongLf = weakLf;
    strongLf.lowFrequencyCapability = 0.95f;
    const auto strongPlan = makeAdaptiveSignature(strongLf);
    assert(weakPlan.virtualBass > strongPlan.virtualBass);
    assert(weakPlan.physicalBass < strongPlan.physicalBass);

    // Harshness risk must never be answered by adding even more upper-mid/high
    // detail enhancement.
    SignatureInputs smooth{};
    smooth.harshnessRisk = 0.0f;
    const auto smoothPlan = makeAdaptiveSignature(smooth);
    SignatureInputs harsh = smooth;
    harsh.harshnessRisk = 1.0f;
    const auto harshPlan = makeAdaptiveSignature(harsh);
    assert(harshPlan.clarity < smoothPlan.clarity);
    assert(harshPlan.fidelity < smoothPlan.fidelity);

    // Sustained limiter activity is a safety signal: gain-producing stages must
    // back off monotonically and reserve more preamp headroom.
    SignatureInputs relaxed{};
    relaxed.lowFrequencyCapability = 0.2f;
    const auto relaxedPlan = makeAdaptiveSignature(relaxed);
    SignatureInputs stressed = relaxed;
    stressed.limiterStress = 1.0f;
    const auto stressedPlan = makeAdaptiveSignature(stressed);
    assert(stressedPlan.preampDb < relaxedPlan.preampDb);
    assert(stressedPlan.physicalBass < relaxedPlan.physicalBass);
    assert(stressedPlan.virtualBass < relaxedPlan.virtualBass);
    assert(stressedPlan.clarity < relaxedPlan.clarity);
    assert(stressedPlan.fidelity < relaxedPlan.fidelity);

    // Low endpoint volume should receive modest compensation, never an
    // unbounded loudness boost.
    SignatureInputs lowVolume{};
    lowVolume.endpointVolume = 0.05f;
    const auto lowVolumePlan = makeAdaptiveSignature(lowVolume);
    SignatureInputs highVolume = lowVolume;
    highVolume.endpointVolume = 0.95f;
    const auto highVolumePlan = makeAdaptiveSignature(highVolume);
    assert(lowVolumePlan.physicalBass >= highVolumePlan.physicalBass);
    assert(lowVolumePlan.clarity >= highVolumePlan.clarity);

    // Personalization earns more spatial confidence than an unknown device, but
    // both must stay inside the conservative renderer bounds.
    SignatureInputs personalized{};
    personalized.knowledge = DeviceKnowledge::Personalized;
    const auto personalizedPlan = makeAdaptiveSignature(personalized);
    assert(personalizedPlan.surround > unknownPlan.surround);
    assert(personalizedPlan.spatial.itdScale >= unknownPlan.spatial.itdScale);

    // Voice mode protects the phantom center and avoids bass/spatial spectacle.
    SignatureInputs voice{};
    voice.content = ContentClass::Voice;
    voice.lowFrequencyCapability = 0.1f;
    const auto voicePlan = makeAdaptiveSignature(voice);
    SignatureInputs movie = voice;
    movie.content = ContentClass::Movie;
    const auto moviePlan = makeAdaptiveSignature(movie);
    assert(voicePlan.surround < moviePlan.surround);
    assert(voicePlan.virtualBass < moviePlan.virtualBass);

    // Low-latency mode must not make the plan more expensive/aggressive.
    SignatureInputs normalGame{};
    normalGame.content = ContentClass::Game;
    const auto normalGamePlan = makeAdaptiveSignature(normalGame);
    SignatureInputs fastGame = normalGame;
    fastGame.lowLatency = true;
    const auto fastGamePlan = makeAdaptiveSignature(fastGame);
    assert(fastGamePlan.surround <= normalGamePlan.surround);
    assert(fastGamePlan.fidelity <= normalGamePlan.fidelity);
    assert(fastGamePlan.dynamics <= normalGamePlan.dynamics);

    // Adversarial/non-finite policy inputs must collapse to finite, bounded
    // defaults rather than leaking NaN/Inf into DSP controls.
    SignatureInputs hostile{};
    hostile.lowFrequencyCapability = std::numeric_limits<float>::quiet_NaN();
    hostile.correctionDemand = std::numeric_limits<float>::infinity();
    hostile.harshnessRisk = -std::numeric_limits<float>::infinity();
    hostile.endpointVolume = std::numeric_limits<float>::quiet_NaN();
    hostile.limiterStress = std::numeric_limits<float>::infinity();
    assertBounds(makeAdaptiveSignature(hostile));

    // Exhaust the edge cube for every policy input so future formula changes
    // cannot accidentally escape bounds at combinations not covered above.
    constexpr float edges[] = {-10.0f, 0.0f, 0.5f, 1.0f, 10.0f};
    for (float lf : edges) {
        for (float correction : edges) {
            for (float harshness : edges) {
                for (float volume : edges) {
                    for (float stress : edges) {
                        SignatureInputs input{};
                        input.lowFrequencyCapability = lf;
                        input.correctionDemand = correction;
                        input.harshnessRisk = harshness;
                        input.endpointVolume = volume;
                        input.limiterStress = stress;
                        input.knowledge = DeviceKnowledge::Personalized;
                        input.content = ContentClass::Movie;
                        assertBounds(makeAdaptiveSignature(input));
                    }
                }
            }
        }
    }

    return 0;
}
