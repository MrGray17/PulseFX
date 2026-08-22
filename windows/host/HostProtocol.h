#pragma once

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace pulsefx::windows {

struct HostCommand {
    std::string name;
    std::vector<std::string> args;
};

HostCommand parseHostCommand(std::string_view line);
std::string jsonEscape(std::string_view value);

// Parse one complete finite floating-point token. NaN and infinities are
// rejected at the native-control boundary instead of relying on downstream
// clamping or DSP stages to recover from poisoned state. Header-inline keeps
// this tiny validator reusable by isolated host/profile test targets.
inline bool parseFiniteFloat(std::string_view text, float& value) noexcept {
    if (text.empty()) return false;
    try {
        const std::string token(text);
        std::size_t used = 0;
        const float parsed = std::stof(token, &used);
        if (used != token.size() || !std::isfinite(parsed)) return false;
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace pulsefx::windows
