#pragma once

#ifdef _WIN32

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
std::string utf8FromWide(const std::wstring& value);
std::wstring wideFromUtf8(const std::string& value);

} // namespace pulsefx::windows

#endif // _WIN32
