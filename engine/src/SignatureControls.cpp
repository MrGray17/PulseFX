#include "pulsefx/SignatureControls.h"

namespace pulsefx {

CompiledSignatureControls compileSignatureControls(
    const SignatureInputs& inputs,
    const ProcessorParameters& base) noexcept {
    const SignaturePlan plan = makeAdaptiveSignature(inputs);

    CompiledSignatureControls compiled{};
    compiled.processor = base;

    // Preserve engine lifecycle/pitch state. Signature owns enhancement stages.
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
