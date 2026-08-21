#ifdef _WIN32

#include "AudioDeviceCatalog.h"
#include "AudioSessionMixer.h"
#include "HostProtocol.h"
#include "../relay/WasapiRelay.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace pulsefx::windows {
namespace {

bool parseBool(const std::string& value, bool& result) {
    if (value == "1" || value == "true" || value == "on") { result = true; return true; }
    if (value == "0" || value == "false" || value == "off") { result = false; return true; }
    return false;
}

bool parseFloat(const std::string& value, float& result) {
    try {
        std::size_t used = 0;
        result = std::stof(value, &used);
        return used == value.size();
    } catch (...) {
        return false;
    }
}

bool parseUint32(const std::string& value, std::uint32_t& result) {
    try {
        std::size_t used = 0;
        const auto parsed = std::stoull(value, &used);
        if (used != value.size() || parsed > 0xffffffffULL) return false;
        result = static_cast<std::uint32_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

std::string quoted(const std::string& value) {
    return "\"" + jsonEscape(value) + "\"";
}

class PulseFxHost {
public:
    PulseFxHost() {
        control_.processor.bypass = false;
        control_.processor.fidelity = 0.42f;
        control_.processor.space = 0.34f;
        control_.processor.dynamics = 0.20f;
        control_.processor.pitchSemitones = 0.0f;
    }

    ~PulseFxHost() { stop(); }

    void start() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ensureRelayLocked();
        }
        watchdog_ = std::thread([this] { watchdogLoop(); });
    }

    void stop() noexcept {
        stopRequested_.store(true, std::memory_order_release);
        if (watchdog_.joinable()) watchdog_.join();
        std::lock_guard<std::mutex> lock(mutex_);
        relay_.stop();
    }

    bool shouldStop() const noexcept { return stopRequested_.load(std::memory_order_acquire); }

    std::string handle(const HostCommand& command) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (command.name.empty()) return errorJson("empty command");
        if (command.name == "ping") return "{\"type\":\"pong\",\"ok\":true}";
        if (command.name == "status") return statusJsonLocked();
        if (command.name == "devices") return devicesJsonLocked();
        if (command.name == "apps") return appsJsonLocked();
        if (command.name == "quit") {
            stopRequested_.store(true, std::memory_order_release);
            return ackJson("quit");
        }
        if (command.name == "output") return handleOutputLocked(command);
        if (command.name == "enabled") return handleBoolControlLocked(command, "enabled");
        if (command.name == "night") return handleBoolControlLocked(command, "night");
        if (command.name == "headphone_enable") return handleBoolControlLocked(command, "headphone_enable");
        if (command.name == "preamp" || command.name == "bass" || command.name == "clarity" ||
            command.name == "fidelity" || command.name == "spatial" || command.name == "surround" ||
            command.name == "ambience" || command.name == "dynamics" || command.name == "pitch") {
            return handleFloatControlLocked(command);
        }
        if (command.name == "eq") return handleEqLocked(command);
        if (command.name == "app_volume") return handleAppVolumeLocked(command);
        if (command.name == "app_mute") return handleAppMuteLocked(command);
        return errorJson("unknown command");
    }

private:
    bool ensureRelayLocked() {
        const std::wstring nextSource = findPulseFxOutputId();
        if (nextSource.empty()) {
            relay_.stop();
            sourceId_.clear();
            destinationId_.clear();
            lastError_ = "PulseFX Output is not installed or active";
            return false;
        }

        const std::wstring nextDestination = choosePhysicalOutputId(preferredDestinationId_);
        if (nextDestination.empty() || nextDestination == nextSource) {
            relay_.stop();
            sourceId_ = nextSource;
            destinationId_.clear();
            lastError_ = "No physical playback device is available";
            return false;
        }

        if (relay_.running() && sourceId_ == nextSource && destinationId_ == nextDestination) return true;

        relay_.stop();
        RelayConfig config;
        config.processInRelay = true;
        config.control = control_;
        if (!relay_.start(nextSource, nextDestination, config)) {
            sourceId_ = nextSource;
            destinationId_ = nextDestination;
            lastError_ = "WASAPI relay failed to start";
            return false;
        }

        sourceId_ = nextSource;
        destinationId_ = nextDestination;
        lastError_.clear();
        return true;
    }

    void watchdogLoop() noexcept {
        while (!stopRequested_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
            if (stopRequested_.load(std::memory_order_acquire)) break;
            try {
                std::lock_guard<std::mutex> lock(mutex_);
                ensureRelayLocked();
            } catch (...) {
                // The next watchdog pass retries. Never terminate the host from
                // an environmental device-enumeration failure.
            }
        }
    }

    void pushControlLocked() {
        relay_.updateControlState(control_);
    }

    std::string handleOutputLocked(const HostCommand& command) {
        if (command.args.size() != 1) return errorJson("output expects one device id or auto");
        const std::wstring previous = preferredDestinationId_;
        preferredDestinationId_ = command.args[0] == "auto" ? std::wstring{} : wideFromUtf8(command.args[0]);
        if (!preferredDestinationId_.empty()) {
            const auto resolved = choosePhysicalOutputId(preferredDestinationId_);
            if (resolved != preferredDestinationId_) {
                preferredDestinationId_ = previous;
                return errorJson("requested physical output is unavailable");
            }
        }
        relay_.stop();
        ensureRelayLocked();
        return statusJsonLocked();
    }

    std::string handleBoolControlLocked(const HostCommand& command, const char* controlName) {
        if (command.args.size() != 1) return errorJson("boolean control expects one value");
        bool value = false;
        if (!parseBool(command.args[0], value)) return errorJson("invalid boolean value");
        const std::string name(controlName);
        if (name == "enabled") control_.processor.bypass = !value;
        else if (name == "night") control_.processor.nightMode = value;
        else if (name == "headphone_enable") control_.headphoneCorrectionEnabled = value;
        pushControlLocked();
        return ackJson(name);
    }

    std::string handleFloatControlLocked(const HostCommand& command) {
        if (command.args.size() != 1) return errorJson("numeric control expects one value");
        float value = 0.0f;
        if (!parseFloat(command.args[0], value)) return errorJson("invalid numeric value");

        if (command.name == "preamp") {
            control_.processor.preampDb = std::clamp(value, -18.0f, 9.0f);
        } else if (command.name == "pitch") {
            control_.processor.pitchSemitones = std::clamp(value, -5.0f, 5.0f);
        } else {
            value = std::clamp(value, 0.0f, 1.0f);
            if (command.name == "bass") control_.processor.bass = value;
            else if (command.name == "clarity") control_.processor.clarity = value;
            else if (command.name == "fidelity") control_.processor.fidelity = value;
            else if (command.name == "spatial") control_.processor.space = value;
            else if (command.name == "surround") control_.processor.surround = value;
            else if (command.name == "ambience") control_.processor.ambience = value;
            else if (command.name == "dynamics") control_.processor.dynamics = value;
        }
        pushControlLocked();
        return ackJson(command.name);
    }

    std::string handleEqLocked(const HostCommand& command) {
        if (command.args.size() != 2) return errorJson("eq expects band index and gain dB");
        std::uint32_t band = 0;
        float gain = 0.0f;
        if (!parseUint32(command.args[0], band) || band >= control_.eqDb.size() || !parseFloat(command.args[1], gain)) {
            return errorJson("invalid EQ band or gain");
        }
        control_.eqDb[band] = std::clamp(gain, -12.0f, 12.0f);
        pushControlLocked();
        return ackJson("eq");
    }

    std::string handleAppVolumeLocked(const HostCommand& command) {
        if (command.args.size() != 2 || sourceId_.empty()) return errorJson("app_volume expects pid and volume");
        std::uint32_t pid = 0;
        float volume = 0.0f;
        if (!parseUint32(command.args[0], pid) || !parseFloat(command.args[1], volume)) return errorJson("invalid app volume arguments");
        if (!setAudioSessionVolume(sourceId_, pid, std::clamp(volume, 0.0f, 1.0f))) return errorJson("audio session was not found");
        return ackJson("app_volume");
    }

    std::string handleAppMuteLocked(const HostCommand& command) {
        if (command.args.size() != 2 || sourceId_.empty()) return errorJson("app_mute expects pid and boolean");
        std::uint32_t pid = 0;
        bool muted = false;
        if (!parseUint32(command.args[0], pid) || !parseBool(command.args[1], muted)) return errorJson("invalid app mute arguments");
        if (!setAudioSessionMuted(sourceId_, pid, muted)) return errorJson("audio session was not found");
        return ackJson("app_mute");
    }

    std::string devicesJsonLocked() const {
        const auto devices = enumerateRenderDevices();
        std::ostringstream out;
        out << "{\"type\":\"devices\",\"ok\":true,\"devices\":[";
        for (std::size_t index = 0; index < devices.size(); ++index) {
            if (index) out << ',';
            const auto& device = devices[index];
            out << "{\"id\":" << quoted(utf8FromWide(device.id))
                << ",\"name\":" << quoted(utf8FromWide(device.name))
                << ",\"default\":" << (device.isDefault ? "true" : "false")
                << ",\"pulsefx\":" << (device.isPulseFx ? "true" : "false") << '}';
        }
        out << "]}";
        return out.str();
    }

    std::string appsJsonLocked() const {
        const auto sessions = sourceId_.empty() ? std::vector<AudioSessionInfo>{} : enumerateAudioSessions(sourceId_);
        std::ostringstream out;
        out << "{\"type\":\"apps\",\"ok\":true,\"apps\":[";
        for (std::size_t index = 0; index < sessions.size(); ++index) {
            if (index) out << ',';
            const auto& session = sessions[index];
            out << "{\"pid\":" << session.processId
                << ",\"name\":" << quoted(utf8FromWide(session.name))
                << ",\"volume\":" << session.volume
                << ",\"muted\":" << (session.muted ? "true" : "false") << '}';
        }
        out << "]}";
        return out.str();
    }

    std::string statusJsonLocked() const {
        const RelayStats stats = relay_.stats();
        std::ostringstream out;
        out << "{\"type\":\"status\",\"ok\":true"
            << ",\"running\":" << (relay_.running() ? "true" : "false")
            << ",\"sourceId\":" << quoted(utf8FromWide(sourceId_))
            << ",\"destinationId\":" << quoted(utf8FromWide(destinationId_))
            << ",\"preferredDestinationId\":" << quoted(utf8FromWide(preferredDestinationId_))
            << ",\"error\":" << quoted(lastError_)
            << ",\"controls\":{"
            << "\"enabled\":" << (!control_.processor.bypass ? "true" : "false") << ','
            << "\"pitchSemitones\":" << control_.processor.pitchSemitones
            << "},\"stats\":{"
            << "\"underruns\":" << stats.underruns << ','
            << "\"overruns\":" << stats.overruns << ','
            << "\"capturedFrames\":" << stats.capturedFrames << ','
            << "\"renderedFrames\":" << stats.renderedFrames << ','
            << "\"bufferedFrames\":" << stats.bufferedFrames << ','
            << "\"controlRevision\":" << stats.controlRevision << ','
            << "\"clockCorrectionPpm\":" << stats.clockCorrectionPpm
            << "}}";
        return out.str();
    }

    static std::string ackJson(const std::string& command) {
        return "{\"type\":\"ack\",\"ok\":true,\"command\":" + quoted(command) + '}';
    }

    static std::string errorJson(const std::string& message) {
        return "{\"type\":\"error\",\"ok\":false,\"error\":" + quoted(message) + '}';
    }

    mutable std::mutex mutex_;
    WasapiRelay relay_{};
    ApoControlState control_{};
    std::wstring sourceId_{};
    std::wstring destinationId_{};
    std::wstring preferredDestinationId_{};
    std::string lastError_{};
    std::atomic<bool> stopRequested_{false};
    std::thread watchdog_{};
};

} // namespace
} // namespace pulsefx::windows

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    pulsefx::windows::PulseFxHost host;
    host.start();
    std::cout << host.handle(pulsefx::windows::parseHostCommand("status")) << std::endl;

    std::string line;
    while (!host.shouldStop() && std::getline(std::cin, line)) {
        const auto command = pulsefx::windows::parseHostCommand(line);
        std::cout << host.handle(command) << std::endl;
    }
    host.stop();
    return 0;
}

#endif // _WIN32
