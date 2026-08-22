#ifdef _WIN32

#include <Windows.h>
#include <SetupAPI.h>
#include <cfgmgr32.h>
#include <devguid.h>
#include <algorithm>
#include <cwchar>
#include <iostream>
#include <string>
#include <vector>

namespace {
constexpr wchar_t kHardwareId[] = L"ROOT\\PulseFXVirtualAudio";
constexpr wchar_t kDeviceName[] = L"PulseFX Virtual Audio Device";

class DeviceInfoSet {
public:
    explicit DeviceInfoSet(HDEVINFO value = INVALID_HANDLE_VALUE) noexcept : value_(value) {}
    ~DeviceInfoSet() {
        if (value_ != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(value_);
    }
    DeviceInfoSet(const DeviceInfoSet&) = delete;
    DeviceInfoSet& operator=(const DeviceInfoSet&) = delete;
    HDEVINFO get() const noexcept { return value_; }
    bool valid() const noexcept { return value_ != INVALID_HANDLE_VALUE; }
private:
    HDEVINFO value_;
};

std::wstring windowsError(const wchar_t* operation) {
    const DWORD code = GetLastError();
    wchar_t* message = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        0,
        reinterpret_cast<wchar_t*>(&message),
        0,
        nullptr);
    std::wstring result(operation);
    result += L" failed (" + std::to_wstring(code) + L")";
    if (length && message) {
        result += L": ";
        result.append(message, length);
        while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) result.pop_back();
    }
    if (message) LocalFree(message);
    return result;
}

bool hardwareIdsContain(const std::vector<BYTE>& bytes, DWORD type) {
    if (type != REG_MULTI_SZ || bytes.size() < sizeof(wchar_t) * 2) return false;
    const auto* cursor = reinterpret_cast<const wchar_t*>(bytes.data());
    const auto* end = reinterpret_cast<const wchar_t*>(bytes.data() + bytes.size());
    while (cursor < end && *cursor != L'\0') {
        const std::size_t remaining = static_cast<std::size_t>(end - cursor);
        const std::size_t length = wcsnlen_s(cursor, remaining);
        if (length == remaining) return false;
        if (_wcsicmp(std::wstring(cursor, length).c_str(), kHardwareId) == 0) return true;
        cursor += length + 1;
    }
    return false;
}

bool deviceMatches(HDEVINFO set, SP_DEVINFO_DATA& device) {
    DWORD type = 0;
    DWORD required = 0;
    SetupDiGetDeviceRegistryPropertyW(
        set, &device, SPDRP_HARDWAREID, &type, nullptr, 0, &required);
    if (required == 0 || GetLastError() != ERROR_INSUFFICIENT_BUFFER) return false;

    std::vector<BYTE> buffer(required + sizeof(wchar_t) * 2, 0);
    if (!SetupDiGetDeviceRegistryPropertyW(
            set,
            &device,
            SPDRP_HARDWAREID,
            &type,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            nullptr)) {
        return false;
    }
    return hardwareIdsContain(buffer, type);
}

std::vector<SP_DEVINFO_DATA> matchingDevices(HDEVINFO set) {
    std::vector<SP_DEVINFO_DATA> matches;
    for (DWORD index = 0;; ++index) {
        SP_DEVINFO_DATA device{};
        device.cbSize = sizeof(device);
        if (!SetupDiEnumDeviceInfo(set, index, &device)) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS) break;
            continue;
        }
        if (deviceMatches(set, device)) matches.push_back(device);
    }
    return matches;
}

DeviceInfoSet allMediaDevices() {
    return DeviceInfoSet(SetupDiGetClassDevsW(&GUID_DEVCLASS_MEDIA, nullptr, nullptr, 0));
}

bool deviceExists() {
    auto set = allMediaDevices();
    if (!set.valid()) return false;
    return !matchingDevices(set.get()).empty();
}

bool readServiceName(HDEVINFO set, SP_DEVINFO_DATA& device, std::wstring& service) {
    DWORD type = 0;
    DWORD required = 0;
    SetupDiGetDeviceRegistryPropertyW(set, &device, SPDRP_SERVICE, &type, nullptr, 0, &required);
    if (required == 0 || GetLastError() != ERROR_INSUFFICIENT_BUFFER) return false;

    std::vector<BYTE> buffer(required + sizeof(wchar_t), 0);
    if (!SetupDiGetDeviceRegistryPropertyW(
            set,
            &device,
            SPDRP_SERVICE,
            &type,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            nullptr) || type != REG_SZ) {
        return false;
    }
    const auto* value = reinterpret_cast<const wchar_t*>(buffer.data());
    service.assign(value);
    return !service.empty();
}

bool deviceIsHealthy(HDEVINFO set, SP_DEVINFO_DATA& device, bool verbose) {
    std::wstring service;
    if (!readServiceName(set, device, service)) {
        if (verbose) std::wcerr << L"PulseFX device exists but has no bound driver service.\n";
        return false;
    }

    ULONG status = 0;
    ULONG problem = 0;
    const CONFIGRET result = CM_Get_DevNode_Status(&status, &problem, device.DevInst, 0);
    if (result != CR_SUCCESS) {
        if (verbose) std::wcerr << L"CM_Get_DevNode_Status failed with CONFIGRET " << result << L".\n";
        return false;
    }
    if (problem != 0) {
        if (verbose) std::wcerr << L"PulseFX device problem code: " << problem << L".\n";
        return false;
    }
    if ((status & DN_STARTED) == 0) {
        if (verbose) std::wcerr << L"PulseFX device is bound to service '" << service << L"' but is not started.\n";
        return false;
    }

    if (verbose) {
        std::wcout << L"PulseFX device healthy. Service: " << service << L".\n";
    }
    return true;
}

bool deviceHealthy() {
    auto set = allMediaDevices();
    if (!set.valid()) return false;
    for (auto device : matchingDevices(set.get())) {
        if (deviceIsHealthy(set.get(), device, true)) return true;
    }
    return false;
}

bool createRootDevice() {
    if (deviceExists()) {
        std::wcout << L"PulseFX root device already exists.\n";
        return true;
    }

    DeviceInfoSet set(SetupDiCreateDeviceInfoList(&GUID_DEVCLASS_MEDIA, nullptr));
    if (!set.valid()) {
        std::wcerr << windowsError(L"SetupDiCreateDeviceInfoList") << L'\n';
        return false;
    }

    SP_DEVINFO_DATA device{};
    device.cbSize = sizeof(device);
    if (!SetupDiCreateDeviceInfoW(
            set.get(),
            kDeviceName,
            &GUID_DEVCLASS_MEDIA,
            nullptr,
            nullptr,
            DICD_GENERATE_ID,
            &device)) {
        std::wcerr << windowsError(L"SetupDiCreateDeviceInfo") << L'\n';
        return false;
    }

    std::wstring hardwareIds(kHardwareId);
    hardwareIds.push_back(L'\0');
    hardwareIds.push_back(L'\0');
    const DWORD hardwareBytes = static_cast<DWORD>(hardwareIds.size() * sizeof(wchar_t));
    if (!SetupDiSetDeviceRegistryPropertyW(
            set.get(),
            &device,
            SPDRP_HARDWAREID,
            reinterpret_cast<const BYTE*>(hardwareIds.data()),
            hardwareBytes)) {
        std::wcerr << windowsError(L"SetupDiSetDeviceRegistryProperty(HardwareID)") << L'\n';
        return false;
    }

    if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE, set.get(), &device)) {
        std::wcerr << windowsError(L"DIF_REGISTERDEVICE") << L'\n';
        return false;
    }

    if (!deviceExists()) {
        std::wcerr << L"PulseFX root device registration returned success but the hardware ID was not enumerated.\n";
        return false;
    }
    std::wcout << L"Created ROOT\\PulseFXVirtualAudio device.\n";
    return true;
}

bool removeRootDevices() {
    auto set = allMediaDevices();
    if (!set.valid()) {
        std::wcerr << windowsError(L"SetupDiGetClassDevs") << L'\n';
        return false;
    }

    const auto matches = matchingDevices(set.get());
    bool success = true;
    for (auto device : matches) {
        SP_REMOVEDEVICE_PARAMS remove{};
        remove.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
        remove.ClassInstallHeader.InstallFunction = DIF_REMOVE;
        remove.Scope = DI_REMOVEDEVICE_GLOBAL;
        remove.HwProfile = 0;
        if (!SetupDiSetClassInstallParamsW(
                set.get(),
                &device,
                &remove.ClassInstallHeader,
                sizeof(remove))) {
            std::wcerr << windowsError(L"SetupDiSetClassInstallParams(DIF_REMOVE)") << L'\n';
            success = false;
            continue;
        }
        if (!SetupDiCallClassInstaller(DIF_REMOVE, set.get(), &device)) {
            std::wcerr << windowsError(L"DIF_REMOVE") << L'\n';
            success = false;
        }
    }

    if (success && !deviceExists()) {
        std::wcout << L"PulseFX root device removed.\n";
        return true;
    }
    if (matches.empty()) {
        std::wcout << L"PulseFX root device was not present.\n";
        return true;
    }
    return false;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::wcerr << L"Usage: pulsefx_device_setup.exe <install|remove|check|exists>\n";
        return 64;
    }

    const std::wstring command(argv[1]);
    if (command == L"install") return createRootDevice() ? 0 : 1;
    if (command == L"remove") return removeRootDevices() ? 0 : 1;
    if (command == L"check") return deviceHealthy() ? 0 : 2;
    if (command == L"exists") return deviceExists() ? 0 : 2;

    std::wcerr << L"Unknown command. Expected install, remove, check, or exists.\n";
    return 64;
}

#endif // _WIN32
