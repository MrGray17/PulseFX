#ifdef _WIN32
#include "WasapiRelay.h"
#include <stdexcept>

int main() {
    pulsefx::windows::WasapiRelay relay;
    if (relay.running()) throw std::runtime_error("new relay unexpectedly running");
    const auto stats = relay.stats();
    if (stats.underruns != 0 || stats.overruns != 0 ||
        stats.capturedFrames != 0 || stats.renderedFrames != 0) {
        throw std::runtime_error("new relay has non-zero statistics");
    }
    relay.stop();
    return 0;
}
#endif
