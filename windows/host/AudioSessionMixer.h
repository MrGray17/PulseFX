#pragma once

#ifdef _WIN32

#include <cstdint>
#include <string>
#include <vector>

namespace pulsefx::windows {

struct AudioSessionInfo {
    std::uint32_t processId{0};
    std::wstring processName;
    std::wstring name;
    float volume{1.0f};
    bool muted{false};
    bool active{false};
};

std::vector<AudioSessionInfo> enumerateAudioSessions(const std::wstring& renderDeviceId);
bool setAudioSessionVolume(const std::wstring& renderDeviceId, std::uint32_t processId, float volume);
bool setAudioSessionMuted(const std::wstring& renderDeviceId, std::uint32_t processId, bool muted);

} // namespace pulsefx::windows

#endif // _WIN32
