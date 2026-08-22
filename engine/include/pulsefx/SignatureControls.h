#pragma once
#include "AdaptiveSignature.h"
#include "Processor.h"

namespace pulsefx {

struct CompiledSignatureControls {
    ProcessorParameters processor{};
    SpatialProfileTuning spatial{};
};

// Compile a policy decision into the same ProcessorParameters used by manual
// controls. Non-Signature state (bypass and pitch) is intentionally preserved
// from base so enabling adaptive enhancement cannot unexpectedly start/stop the
// engine or alter playback pitch.
inline CompiledSignatureControls compileSignatureControls(
    const SignatureInputs& inputs,
    const ProcessorParameters& base) noexcept {
    const SignaturePlan plan = makeAdaptiveSignature(inputs);

    CompiledSignatureControls compiled{};
    compiled.processor = base;
    compiled.processor.preampDb = plan.preampDb;
    compiled.processor.bass = plan.physicalBass;
    compiled.processor.virtualBass = plan.virtualBass;
    compiled.processor.bassCapability = plan.virtualBassCapability;
    compiled.processor.clarity = plan.clarity;
    compiled.processor.fidelity = plan.fidelity;
    compiled.processor.space = 0.0f;
    compiled.processor.surround = plan.surround;
    compiled.processor.ambience = plan.ambience;
    compiled.processor.dynamics = plan.dynamics;
    compiled.processor.nightMode = false;
    compiled.spatial = plan.spatial;
    return compiled;
}

} // namespace pulsefx
