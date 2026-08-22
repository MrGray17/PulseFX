#pragma once

#ifdef _WIN32

#include <cstdint>
#include <string>
#include <vector>

namespace pulsefx::windows {

struct AudioDeviceInfo {
    std::wstring id;
    std::wstring name;
    bool isDefault{false};
    bool isPulseFx{false};
};

std::vector<AudioDeviceInfo> enumerateRenderDevices();
std::wstring defaultRenderDeviceId();
std::wstring findPulseFxOutputId();
std::wstring choosePhysicalOutputId(const std::wstring& preferredId = {});
float endpointVolumeScalar(const std::wstring& deviceId, float fallback = 0.5f) noexcept;
// Returns the endpoint's current shared-mode mix sample rate, or 0 when the
// device/format cannot be resolved. Callers must fail open rather than guess.
std::uint32_t renderDeviceSampleRate(const std::wstring& deviceId) noexcept;
std::string utf8FromWide(const std::wstring& value);
std::wstring wideFromUtf8(const std::string& value);

} // namespace pulsefx::windows

#endif // _WIN32
