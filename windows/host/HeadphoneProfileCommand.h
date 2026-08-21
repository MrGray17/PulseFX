#pragma once
#include "../apo/PulseFxApoBridge.h"
#include <string>
#include <vector>

namespace pulsefx::windows {

// args: <preampDb> <count> (<PK|LSC|HSC> <frequency> <q> <gainDb>)*
// Parsing is all-or-nothing: state is unchanged when validation fails.
bool applyHeadphoneProfileArgs(
    const std::vector<std::string>& args,
    ApoControlState& state,
    std::string& error) noexcept;

} // namespace pulsefx::windows
