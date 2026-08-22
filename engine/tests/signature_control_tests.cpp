#include "pulsefx/SignatureControls.h"
#include <cassert>
#include <cmath>

using namespace pulsefx;

int main() {
    ProcessorParameters base{};
    base.bypass = true;
    base.pitchSemitones = -2.5f;
    base.space = 0.9f;
    base.nightMode = true;
    base.bass = 0.9f;
    base.virtualBass = 0.9f;

    SignatureInputs inputs{};
    inputs.knowledge = DeviceKnowledge::Measured;
    inputs.content = ContentClass::Music;
    inputs.lowFrequencyCapability = 0.15f;
    inputs.correctionDemand = 0.35f;
    inputs.harshnessRisk = 0.2f;
    inputs.endpointVolume = 0.25f;

    const SignaturePlan plan = makeAdaptiveSignature(inputs);
    const auto compiled = compileSignatureControls(inputs, base);

    // Signature never takes ownership of engine lifecycle or pitch.
    assert(compiled.processor.bypass == base.bypass);
    assert(compiled.processor.pitchSemitones == base.pitchSemitones);

    // Enhancement stages are deterministically replaced by the policy plan.
    assert(compiled.processor.preampDb == plan.preampDb);
    assert(compiled.processor.bass == plan.physicalBass);
    assert(compiled.processor.virtualBass == plan.virtualBass);
    assert(compiled.processor.bassCapability == plan.virtualBassCapability);
    assert(compiled.processor.clarity == plan.clarity);
    assert(compiled.processor.fidelity == plan.fidelity);
    assert(compiled.processor.surround == plan.surround);
    assert(compiled.processor.ambience == plan.ambience);
    assert(compiled.processor.dynamics == plan.dynamics);
    assert(compiled.processor.space == 0.0f);
    assert(!compiled.processor.nightMode);

    // The spatial plan remains separate so the caller can transform an HRIR
    // outside the realtime callback before atomically installing it.
    assert(compiled.spatial.itdScale == plan.spatial.itdScale);
    assert(compiled.spatial.ipsilateralGain == plan.spatial.ipsilateralGain);
    assert(compiled.spatial.contralateralGain == plan.spatial.contralateralGain);
    assert(compiled.spatial.wetTrimDb == plan.spatial.wetTrimDb);

    // A capable personalized headphone must not accidentally inherit the
    // aggressive weak-transducer bass settings from a previous manual state.
    SignatureInputs premium{};
    premium.knowledge = DeviceKnowledge::Personalized;
    premium.content = ContentClass::Music;
    premium.lowFrequencyCapability = 0.98f;
    const auto premiumControls = compileSignatureControls(premium, base);
    assert(premiumControls.processor.virtualBass < compiled.processor.virtualBass);
    assert(premiumControls.processor.surround > compiled.processor.surround);
    assert(premiumControls.processor.bass < 0.42f);

    // All compiled processor controls must remain finite after hostile inputs.
    SignatureInputs hostile{};
    hostile.lowFrequencyCapability = NAN;
    hostile.correctionDemand = INFINITY;
    hostile.harshnessRisk = -INFINITY;
    hostile.endpointVolume = NAN;
    hostile.limiterStress = INFINITY;
    const auto safe = compileSignatureControls(hostile, base);
    assert(std::isfinite(safe.processor.preampDb));
    assert(std::isfinite(safe.processor.bass));
    assert(std::isfinite(safe.processor.virtualBass));
    assert(std::isfinite(safe.processor.clarity));
    assert(std::isfinite(safe.processor.fidelity));
    assert(std::isfinite(safe.processor.surround));
    assert(std::isfinite(safe.processor.dynamics));

    return 0;
}
