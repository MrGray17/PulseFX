#ifdef _WIN32

#include "AudioSessionMixer.h"
#include <Audiopolicy.h>
#include <Mmdeviceapi.h>
#include <Windows.h>
#include <wrl/client.h>
#include <algorithm>
#include <map>

namespace pulsefx::windows {
namespace {
using Microsoft::WRL::ComPtr;

class ScopedComInit {
public:
    ScopedComInit() noexcept : hr_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ScopedComInit() { if (hr_ == S_OK || hr_ == S_FALSE) CoUninitialize(); }
    bool usable() const noexcept { return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE; }
private:
    HRESULT hr_{};
};

ComPtr<IMMDevice> openDevice(const std::wstring& deviceId) {
    ComPtr<IMMDevice> result;
    if (deviceId.empty()) return result;
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(
            __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(enumerator.GetAddressOf())))) return result;
    enumerator->GetDevice(deviceId.c_str(), result.GetAddressOf());
    return result;
}

std::wstring processName(DWORD processId) {
    if (processId == 0) return L"System Sounds";
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) return L"Process " + std::to_wstring(processId);
    std::wstring path(32768, L'\0');
    DWORD size = static_cast<DWORD>(path.size());
    std::wstring result;
    if (QueryFullProcessImageNameW(process, 0, path.data(), &size) != FALSE) {
        path.resize(size);
        const auto slash = path.find_last_of(L"\\/");
        result = slash == std::wstring::npos ? path : path.substr(slash + 1);
    }
    CloseHandle(process);
    return result.empty() ? L"Process " + std::to_wstring(processId) : result;
}

ComPtr<IAudioSessionManager2> sessionManager(const std::wstring& deviceId) {
    ComPtr<IAudioSessionManager2> manager;
    ComPtr<IMMDevice> device = openDevice(deviceId);
    if (!device) return manager;
    device->Activate(
        __uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
        reinterpret_cast<void**>(manager.GetAddressOf()));
    return manager;
}

template <typename Action>
bool visitProcessSessions(const std::wstring& deviceId, std::uint32_t processId, Action&& action) {
    ScopedComInit com;
    if (!com.usable()) return false;
    ComPtr<IAudioSessionManager2> manager = sessionManager(deviceId);
    if (!manager) return false;
    ComPtr<IAudioSessionEnumerator> sessions;
    if (FAILED(manager->GetSessionEnumerator(sessions.GetAddressOf()))) return false;
    int count = 0;
    if (FAILED(sessions->GetCount(&count))) return false;
    bool changed = false;
    for (int index = 0; index < count; ++index) {
        ComPtr<IAudioSessionControl> control;
        if (FAILED(sessions->GetSession(index, control.GetAddressOf()))) continue;
        ComPtr<IAudioSessionControl2> control2;
        if (FAILED(control.As(&control2))) continue;
        DWORD pid = 0;
        if (FAILED(control2->GetProcessId(&pid)) || pid != processId) continue;
        ComPtr<ISimpleAudioVolume> volume;
        if (FAILED(control.As(&volume))) continue;
        if (action(volume.Get())) changed = true;
    }
    return changed;
}

} // namespace

std::vector<AudioSessionInfo> enumerateAudioSessions(const std::wstring& renderDeviceId) {
    ScopedComInit com;
    if (!com.usable()) return {};
    ComPtr<IAudioSessionManager2> manager = sessionManager(renderDeviceId);
    if (!manager) return {};
    ComPtr<IAudioSessionEnumerator> sessions;
    if (FAILED(manager->GetSessionEnumerator(sessions.GetAddressOf()))) return {};
    int count = 0;
    if (FAILED(sessions->GetCount(&count))) return {};

    std::map<std::uint32_t, AudioSessionInfo> grouped;
    for (int index = 0; index < count; ++index) {
        ComPtr<IAudioSessionControl> control;
        if (FAILED(sessions->GetSession(index, control.GetAddressOf()))) continue;
        ComPtr<IAudioSessionControl2> control2;
        if (FAILED(control.As(&control2))) continue;
        DWORD pid = 0;
        if (FAILED(control2->GetProcessId(&pid))) continue;

        ComPtr<ISimpleAudioVolume> simpleVolume;
        if (FAILED(control.As(&simpleVolume))) continue;
        float volume = 1.0f;
        BOOL muted = FALSE;
        simpleVolume->GetMasterVolume(&volume);
        simpleVolume->GetMute(&muted);
        volume = std::clamp(volume, 0.0f, 1.0f);

        AudioSessionState state = AudioSessionStateInactive;
        control->GetState(&state);

        auto& info = grouped[pid];
        const bool firstSession = info.processName.empty();
        info.processId = pid;
        if (firstSession) {
            info.processName = processName(pid);
            info.volume = volume;
            info.muted = muted != FALSE;
        } else {
            info.volume = std::max(info.volume, volume);
            info.muted = info.muted && muted != FALSE;
        }
        info.active = info.active || state == AudioSessionStateActive;

        if (info.name.empty()) {
            LPWSTR displayName = nullptr;
            if (SUCCEEDED(control->GetDisplayName(&displayName)) && displayName && *displayName) {
                info.name = displayName;
            }
            if (displayName) CoTaskMemFree(displayName);
            if (info.name.empty()) info.name = info.processName;
        }
    }

    std::vector<AudioSessionInfo> result;
    result.reserve(grouped.size());
    for (auto& [_, info] : grouped) result.push_back(std::move(info));
    return result;
}

bool setAudioSessionVolume(const std::wstring& renderDeviceId, std::uint32_t processId, float volume) {
    const float clamped = std::clamp(volume, 0.0f, 1.0f);
    return visitProcessSessions(renderDeviceId, processId, [clamped](ISimpleAudioVolume* session) {
        return SUCCEEDED(session->SetMasterVolume(clamped, nullptr));
    });
}

bool setAudioSessionMuted(const std::wstring& renderDeviceId, std::uint32_t processId, bool muted) {
    return visitProcessSessions(renderDeviceId, processId, [muted](ISimpleAudioVolume* session) {
        return SUCCEEDED(session->SetMute(muted ? TRUE : FALSE, nullptr));
    });
}

} // namespace pulsefx::windows

#endif // _WIN32
