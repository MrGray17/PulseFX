#ifdef _WIN32

#include "AudioDeviceCatalog.h"
#include <Mmdeviceapi.h>
#include <Windows.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cwctype>

namespace pulsefx::windows {
namespace {
using Microsoft::WRL::ComPtr;

class ScopedComInit {
public:
    ScopedComInit() noexcept : hr_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ScopedComInit() {
        if (hr_ == S_OK || hr_ == S_FALSE) CoUninitialize();
    }
    bool usable() const noexcept { return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE; }
private:
    HRESULT hr_{};
};

std::wstring deviceId(IMMDevice* device) {
    if (!device) return {};
    LPWSTR raw = nullptr;
    if (FAILED(device->GetId(&raw)) || !raw) return {};
    std::wstring result(raw);
    CoTaskMemFree(raw);
    return result;
}

std::wstring deviceName(IMMDevice* device) {
    if (!device) return {};
    ComPtr<IPropertyStore> store;
    if (FAILED(device->OpenPropertyStore(STGM_READ, store.GetAddressOf()))) return {};
    PROPVARIANT value;
    PropVariantInit(&value);
    std::wstring result;
    if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR && value.pwszVal) {
        result = value.pwszVal;
    }
    PropVariantClear(&value);
    return result;
}

bool containsCaseInsensitive(std::wstring value, std::wstring needle) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return std::towlower(c); });
    std::transform(needle.begin(), needle.end(), needle.begin(), [](wchar_t c) { return std::towlower(c); });
    return value.find(needle) != std::wstring::npos;
}

} // namespace

std::vector<AudioDeviceInfo> enumerateRenderDevices() {
    ScopedComInit com;
    if (!com.usable()) return {};

    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(
            __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(enumerator.GetAddressOf())))) return {};

    std::wstring defaultId;
    ComPtr<IMMDevice> defaultDevice;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, defaultDevice.GetAddressOf()))) {
        defaultId = deviceId(defaultDevice.Get());
    }

    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, collection.GetAddressOf()))) return {};

    UINT count = 0;
    if (FAILED(collection->GetCount(&count))) return {};

    std::vector<AudioDeviceInfo> result;
    result.reserve(count);
    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(index, device.GetAddressOf()))) continue;
        AudioDeviceInfo info;
        info.id = deviceId(device.Get());
        info.name = deviceName(device.Get());
        info.isDefault = !defaultId.empty() && info.id == defaultId;
        info.isPulseFx = containsCaseInsensitive(info.name, L"pulsefx") ||
            containsCaseInsensitive(info.id, L"pulsefxvirtualaudio");
        if (!info.id.empty()) result.push_back(std::move(info));
    }
    return result;
}

std::wstring defaultRenderDeviceId() {
    for (const auto& device : enumerateRenderDevices()) {
        if (device.isDefault) return device.id;
    }
    return {};
}

std::wstring findPulseFxOutputId() {
    for (const auto& device : enumerateRenderDevices()) {
        if (device.isPulseFx) return device.id;
    }
    return {};
}

std::wstring choosePhysicalOutputId(const std::wstring& preferredId) {
    const auto devices = enumerateRenderDevices();
    if (!preferredId.empty()) {
        for (const auto& device : devices) {
            if (!device.isPulseFx && device.id == preferredId) return device.id;
        }
    }
    for (const auto& device : devices) {
        if (!device.isPulseFx && device.isDefault) return device.id;
    }
    for (const auto& device : devices) {
        if (!device.isPulseFx) return device.id;
    }
    return {};
}

float endpointVolumeScalar(const std::wstring& requestedDeviceId, float fallback) noexcept {
    fallback = std::isfinite(fallback) ? std::clamp(fallback, 0.0f, 1.0f) : 0.5f;
    if (requestedDeviceId.empty()) return fallback;

    ScopedComInit com;
    if (!com.usable()) return fallback;

    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(
            __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(enumerator.GetAddressOf())))) return fallback;

    ComPtr<IMMDevice> device;
    if (FAILED(enumerator->GetDevice(requestedDeviceId.c_str(), device.GetAddressOf()))) return fallback;

    ComPtr<IAudioEndpointVolume> endpointVolume;
    if (FAILED(device->Activate(
            __uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(endpointVolume.GetAddressOf())))) return fallback;

    float volume = fallback;
    if (FAILED(endpointVolume->GetMasterVolumeLevelScalar(&volume)) || !std::isfinite(volume)) return fallback;
    return std::clamp(volume, 0.0f, 1.0f);
}

std::string utf8FromWide(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring wideFromUtf8(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

} // namespace pulsefx::windows

#endif // _WIN32
