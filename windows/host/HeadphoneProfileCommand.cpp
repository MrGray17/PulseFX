#include "HeadphoneProfileCommand.h"
#include <cmath>

namespace pulsefx::windows {
namespace {

bool parseFloatStrict(const std::string& text, float& value) noexcept {
    try {
        std::size_t used = 0;
        value = std::stof(text, &used);
        return used == text.size() && std::isfinite(value);
    } catch (...) {
        return false;
    }
}

bool parseCount(const std::string& text, std::size_t& value) noexcept {
    try {
        std::size_t used = 0;
        const auto parsed = std::stoull(text, &used);
        if (used != text.size() || parsed > HeadphoneProfile::kMaxBands) return false;
        value = static_cast<std::size_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool parseType(const std::string& text, CorrectionFilterType& type) noexcept {
    if (text == "PK") { type = CorrectionFilterType::Peaking; return true; }
    if (text == "LSC") { type = CorrectionFilterType::LowShelf; return true; }
    if (text == "HSC") { type = CorrectionFilterType::HighShelf; return true; }
    return false;
}

} // namespace

bool applyHeadphoneProfileArgs(
    const std::vector<std::string>& args,
    ApoControlState& state,
    std::string& error) noexcept {
    if (args.size() < 2) {
        error = "headphone_profile expects preamp, count and filter tuples";
        return false;
    }

    float preamp = 0.0f;
    std::size_t count = 0;
    if (!parseFloatStrict(args[0], preamp) || preamp < -18.0f || preamp > 6.0f) {
        error = "invalid headphone profile preamp";
        return false;
    }
    if (!parseCount(args[1], count)) {
        error = "invalid headphone profile filter count";
        return false;
    }
    if (args.size() != 2 + count * 4) {
        error = "headphone profile filter tuple count mismatch";
        return false;
    }

    HeadphoneProfile profile{};
    profile.preampDb = preamp;
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t offset = 2 + index * 4;
        CorrectionFilterType type{};
        float frequency = 0.0f;
        float q = 0.0f;
        float gain = 0.0f;
        if (!parseType(args[offset], type)) {
            error = "unsupported headphone filter type";
            return false;
        }
        if (!parseFloatStrict(args[offset + 1], frequency) || frequency < 20.0f || frequency > 20000.0f) {
            error = "invalid headphone filter frequency";
            return false;
        }
        if (!parseFloatStrict(args[offset + 2], q) || q < 0.1f || q > 12.0f) {
            error = "invalid headphone filter Q";
            return false;
        }
        if (!parseFloatStrict(args[offset + 3], gain) || gain < -12.0f || gain > 12.0f) {
            error = "invalid headphone filter gain";
            return false;
        }
        profile.bands[index] = {frequency, q, gain, type, true};
    }

    state.headphoneProfile = profile;
    error.clear();
    return true;
}

} // namespace pulsefx::windows
