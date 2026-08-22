#include "HeadphoneProfileCommand.h"
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    pulsefx::windows::ApoControlState state{};
    std::string error;
    const std::vector<std::string> valid{
        "-6.6", "3",
        "LSC", "105", "0.70", "0.5",
        "PK", "675", "0.94", "3.8",
        "HSC", "10000", "0.70", "6.6",
    };
    require(pulsefx::windows::applyHeadphoneProfileArgs(valid, state, error), "valid profile was rejected");
    require(std::abs(state.headphoneProfile.preampDb + 6.6f) < 0.001f, "profile preamp was lost");
    require(state.headphoneProfile.bands[0].enabled, "first profile band was not enabled");
    require(state.headphoneProfile.bands[0].type == pulsefx::CorrectionFilterType::LowShelf, "low shelf type was lost");
    require(state.headphoneProfile.bands[2].type == pulsefx::CorrectionFilterType::HighShelf, "high shelf type was lost");

    const auto before = state.headphoneProfile;
    require(!pulsefx::windows::applyHeadphoneProfileArgs({"0", "1", "BAD", "1000", "1", "2"}, state, error), "bad filter type was accepted");
    require(state.headphoneProfile.bands[0].frequency == before.bands[0].frequency, "invalid profile partially mutated state");
    require(!pulsefx::windows::applyHeadphoneProfileArgs({"0", "13"}, state, error), "too many bands were accepted");
    require(!pulsefx::windows::applyHeadphoneProfileArgs({"0", "1", "PK", "10", "1", "2"}, state, error), "out-of-range frequency was accepted");
    return 0;
}
