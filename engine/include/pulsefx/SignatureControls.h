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
CompiledSignatureControls compileSignatureControls(
    const SignatureInputs& inputs,
    const ProcessorParameters& base) noexcept;

} // namespace pulsefx
