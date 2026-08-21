#pragma once

#ifdef _WIN32

#include "../apo/PulseFxApoBridge.h"
#include <cstdint>
#include <memory>
#include <string>

namespace pulsefx::windows {

struct RelayConfig {
    bool processInRelay{true};
    ApoControlState control{};
};

struct RelayStats {
    std::uint64_t underruns{0};
    std::uint64_t overruns{0};
    std::uint64_t capturedFrames{0};
    std::uint64_t renderedFrames{0};
};

class WasapiRelay {
public:
    WasapiRelay();
    ~WasapiRelay();

    WasapiRelay(const WasapiRelay&) = delete;
    WasapiRelay& operator=(const WasapiRelay&) = delete;

    // sourceDeviceId should be the PulseFX virtual render endpoint. An empty
    // destinationDeviceId selects the current default physical render device.
    bool start(
        const std::wstring& sourceDeviceId,
        const std::wstring& destinationDeviceId,
        const RelayConfig& config);

    void stop() noexcept;
    bool running() const noexcept;
    RelayStats stats() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace pulsefx::windows

#endif // _WIN32
