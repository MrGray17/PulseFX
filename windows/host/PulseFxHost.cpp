#ifdef _WIN32

#include "AudioDeviceCatalog.h"
#include "AudioSessionMixer.h"
#include "HeadphoneProfileCommand.h"
#include "HostProtocol.h"
#include "../relay/WasapiRelay.h"
#include "pulsefx/SignatureControls.h"
#include "pulsefx/SignatureDeviceAnalysis.h"
#include "pulsefx/SignatureSpatialProfileBank.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
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

enum class ProcessingMode {
    Signature,
    Manual,
};

const char* processingModeName(ProcessingMode mode) noexcept {
    return mode == ProcessingMode::Signature ? "signature" : "manual";
}

const char* knowledgeName(DeviceKnowledge knowledge) noexcept {
    switch (knowledge) {
        case DeviceKnowledge::Measured: return "measured";
        case DeviceKnowledge::Personalized: return "personalized";
        case DeviceKnowledge::Unknown: break;
    }
    return "unknown";
}

class PulseFxHost {
public:
    PulseFxHost() {
        manualControl_.processor.bypass = false;
        manualControl_.processor.fidelity = 0.42f;
        manualControl_.processor.space = 0.34f;
        manualControl_.processor.dynamics = 0.20f;
        manualControl_.processor.pitchSemitones = 0.0f;
        control_ = manualControl_;
        applySignatureLocked(0.5f, 0, false);
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
        if (command.name == "mode") return handleModeLocked(command);
        if (command.name == "output") return handleOutputLocked(command);
        if (command.name == "enabled") return handleBoolControlLocked(command, "enabled");
        if (command.name == "night") return handleBoolControlLocked(command, "night");
        if (command.name == "headphone_enable") return handleBoolControlLocked(command, "headphone_enable");
        if (command.name == "headphone_profile") return handleHeadphoneProfileLocked(command);
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
    void pushControlLocked() {
        relay_.updateControlState(control_);
    }

    void activateManualLocked() {
        mode_ = ProcessingMode::Manual;
        control_ = manualControl_;
    }

    void clearSpatialPublicationLocked() noexcept {
        publishedSpatialSampleRate_ = 0;
        manualControl_.spatialProfileRevision = 0;
        control_.spatialProfileRevision = 0;
    }

    void syncSpatialProfilesLocked(
        std::uint32_t sampleRate,
        const SpatialProfileTuning& signatureTuning) {
        if (sampleRate == 0) return;
        spatialProfiles_.update(static_cast<float>(sampleRate), signatureTuning);
        if (!spatialProfiles_.valid() ||
            static_cast<std::uint32_t>(std::lround(spatialProfiles_.sampleRate())) != sampleRate) return;

        manualControl_.spatialProfile = spatialProfiles_.manualProfile();
        manualControl_.spatialProfileRevision = spatialProfiles_.manualRevision();
        publishedSpatialSampleRate_ = sampleRate;
    }

    std::uint32_t activeSourceSampleRateLocked() const noexcept {
        if (sourceId_.empty()) return 0;
        const auto resolved = renderDeviceSampleRate(sourceId_);
        return resolved != 0 ? resolved : publishedSpatialSampleRate_;
    }

    void applySignatureLocked(
        float endpointVolume,
        std::uint32_t sampleRate,
        bool push) {
        SignatureInputs inputs = makeSignatureInputsFromHeadphoneProfile(
            manualControl_.headphoneProfile,
            endpointVolume);
        inputs.content = ContentClass::General;
        inputs.lowLatency = false;

        const auto compiled = compileSignatureControls(inputs, manualControl_.processor);
        syncSpatialProfilesLocked(sampleRate, compiled.spatial);

        ApoControlState next = manualControl_;
        next.processor = compiled.processor;
        // Signature owns enhancement, not user tone shaping. Manual EQ is kept
        // intact in manualControl_ so switching back to Manual restores it.
        next.eqDb.fill(0.0f);
        if (spatialProfiles_.valid() && publishedSpatialSampleRate_ != 0) {
            next.spatialProfile = spatialProfiles_.signatureProfile();
            next.spatialProfileRevision = spatialProfiles_.signatureRevision();
        } else {
            // Never publish a stale profile for a stream rate we could not
            // resolve. A fresh bridge then keeps its own rate-correct default.
            next.spatialProfileRevision = 0;
        }
        control_ = next;
        signatureInputs_ = inputs;
        signaturePlan_ = makeAdaptiveSignature(inputs);
        lastSignatureEndpointVolume_ = inputs.endpointVolume;
        if (push) pushControlLocked();
    }

    void refreshSignatureForCurrentOutputLocked() {
        if (mode_ != ProcessingMode::Signature || destinationId_.empty()) return;
        const float volume = endpointVolumeScalar(destinationId_, lastSignatureEndpointVolume_);
        const auto sampleRate = activeSourceSampleRateLocked();
        const bool rateChanged = sampleRate != 0 && sampleRate != publishedSpatialSampleRate_;
        if (std::abs(volume - lastSignatureEndpointVolume_) < 0.04f && !rateChanged) return;
        applySignatureLocked(volume, sampleRate, true);
    }

    bool ensureRelayLocked() {
        const std::wstring nextSource = findPulseFxOutputId();
        if (nextSource.empty()) {
            relay_.stop();
            sourceId_.clear();
            destinationId_.clear();
            clearSpatialPublicationLocked();
            lastError_ = "PulseFX Output is not installed or active";
            return false;
        }

        // In Auto mode, follow a real Windows physical default while one
        // exists. Once PulseFX Output itself becomes the Windows default there
        // is no longer a physical endpoint carrying the default flag, so keep
        // the already-working physical sink as the temporary fallback instead
        // of jumping to whichever endpoint happens to enumerate first. If that
        // sink disappears, choosePhysicalOutputId() still falls through to the
        // remaining available physical device.
        std::wstring routingPreference = preferredDestinationId_;
        if (routingPreference.empty() && !destinationId_.empty() && defaultRenderDeviceId() == nextSource) {
            routingPreference = destinationId_;
        }
        const std::wstring nextDestination = choosePhysicalOutputId(routingPreference);
        if (nextDestination.empty() || nextDestination == nextSource) {
            relay_.stop();
            sourceId_ = nextSource;
            destinationId_.clear();
            lastError_ = "No physical playback device is available";
            return false;
        }

        if (relay_.running() && sourceId_ == nextSource && destinationId_ == nextDestination) {
            refreshSignatureForCurrentOutputLocked();
            return true;
        }

        const std::uint32_t nextSampleRate = renderDeviceSampleRate(nextSource);
        relay_.stop();
        if (nextSampleRate == 0) {
            // A new relay must never inherit an HRTF designed for an old stream
            // rate. Revision zero lets the freshly prepared bridge use its own
            // exact-rate default until the control thread can resolve the mix.
            clearSpatialPublicationLocked();
        }

        if (mode_ == ProcessingMode::Signature) {
            applySignatureLocked(
                endpointVolumeScalar(nextDestination, lastSignatureEndpointVolume_),
                nextSampleRate,
                false);
        } else {
            syncSpatialProfilesLocked(nextSampleRate, signaturePlan_.spatial);
            control_ = manualControl_;
        }

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

    std::string handleModeLocked(const HostCommand& command) {
        if (command.args.size() != 1) return errorJson("mode expects signature or manual");
        if (command.args[0] == "signature") {
            mode_ = ProcessingMode::Signature;
            const float volume = destinationId_.empty()
                ? lastSignatureEndpointVolume_
                : endpointVolumeScalar(destinationId_, lastSignatureEndpointVolume_);
            applySignatureLocked(volume, activeSourceSampleRateLocked(), true);
            return ackJson("mode");
        }
        if (command.args[0] == "manual") {
            syncSpatialProfilesLocked(activeSourceSampleRateLocked(), signaturePlan_.spatial);
            activateManualLocked();
            pushControlLocked();
            return ackJson("mode");
        }
        return errorJson("mode expects signature or manual");
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

        if (name == "enabled") {
            manualControl_.processor.bypass = !value;
            control_.processor.bypass = !value;
            pushControlLocked();
            return ackJson(name);
        }
        if (name == "headphone_enable") {
            manualControl_.headphoneCorrectionEnabled = value;
            control_.headphoneCorrectionEnabled = value;
            pushControlLocked();
            return ackJson(name);
        }
        if (name == "night") {
            manualControl_.processor.nightMode = value;
            activateManualLocked();
            pushControlLocked();
            return ackJson(name);
        }
        return errorJson("unknown boolean control");
    }

    std::string handleFloatControlLocked(const HostCommand& command) {
        if (command.args.size() != 1) return errorJson("numeric control expects one value");
        float value = 0.0f;
        if (!parseFiniteFloat(command.args[0], value)) return errorJson("invalid numeric value");

        if (command.name == "pitch") {
            value = std::clamp(value, -5.0f, 5.0f);
            manualControl_.processor.pitchSemitones = value;
            control_.processor.pitchSemitones = value;
            pushControlLocked();
            return ackJson(command.name);
        }

        if (command.name == "preamp") {
            manualControl_.processor.preampDb = std::clamp(value, -18.0f, 9.0f);
        } else {
            value = std::clamp(value, 0.0f, 1.0f);
            if (command.name == "bass") manualControl_.processor.bass = value;
            else if (command.name == "clarity") manualControl_.processor.clarity = value;
            else if (command.name == "fidelity") manualControl_.processor.fidelity = value;
            else if (command.name == "spatial") manualControl_.processor.space = value;
            else if (command.name == "surround") manualControl_.processor.surround = value;
            else if (command.name == "ambience") manualControl_.processor.ambience = value;
            else if (command.name == "dynamics") manualControl_.processor.dynamics = value;
        }
        activateManualLocked();
        pushControlLocked();
        return ackJson(command.name);
    }

    std::string handleEqLocked(const HostCommand& command) {
        if (command.args.size() != 2) return errorJson("eq expects band index and gain dB");
        std::uint32_t band = 0;
        float gain = 0.0f;
        if (!parseUint32(command.args[0], band) || band >= manualControl_.eqDb.size() || !parseFiniteFloat(command.args[1], gain)) {
            return errorJson("invalid EQ band or gain");
        }
        manualControl_.eqDb[band] = std::clamp(gain, -12.0f, 12.0f);
        activateManualLocked();
        pushControlLocked();
        return ackJson("eq");
    }

    std::string handleHeadphoneProfileLocked(const HostCommand& command) {
        // Parse into a copy first. Malformed remote/profile data can never leave
        // half of a new filter bank active on the realtime thread.
        ApoControlState next = manualControl_;
        std::string error;
        if (!applyHeadphoneProfileArgs(command.args, next, error)) return errorJson(error);
        manualControl_ = next;
        if (mode_ == ProcessingMode::Signature) {
            const float volume = destinationId_.empty()
                ? lastSignatureEndpointVolume_
                : endpointVolumeScalar(destinationId_, lastSignatureEndpointVolume_);
            applySignatureLocked(volume, activeSourceSampleRateLocked(), true);
        } else {
            control_ = manualControl_;
            pushControlLocked();
        }
        return ackJson("headphone_profile");
    }

    std::string handleAppVolumeLocked(const HostCommand& command) {
        if (command.args.size() != 2 || sourceId_.empty()) return errorJson("app_volume expects pid and volume");
        std::uint32_t pid = 0;
        float volume = 0.0f;
        if (!parseUint32(command.args[0], pid) || !parseFiniteFloat(command.args[1], volume)) return errorJson("invalid app volume arguments");
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
        const bool routingActive = !sourceId_.empty() && defaultRenderDeviceId() == sourceId_;
        std::string effectiveError = lastError_;
        if (effectiveError.empty() && relay_.running() && !routingActive) {
            effectiveError = "PulseFX Output is not the Windows default playback device; system audio may bypass processing";
        }

        std::ostringstream out;
        out << "{\"type\":\"status\",\"ok\":true"
            << ",\"running\":" << (relay_.running() ? "true" : "false")
            << ",\"routingActive\":" << (routingActive ? "true" : "false")
            << ",\"sourceId\":" << quoted(utf8FromWide(sourceId_))
            << ",\"destinationId\":" << quoted(utf8FromWide(destinationId_))
            << ",\"preferredDestinationId\":" << quoted(utf8FromWide(preferredDestinationId_))
            << ",\"error\":" << quoted(effectiveError)
            << ",\"controls\":{"
            << "\"mode\":" << quoted(processingModeName(mode_)) << ','
            << "\"enabled\":" << (!control_.processor.bypass ? "true" : "false") << ','
            << "\"pitchSemitones\":" << control_.processor.pitchSemitones << ','
            << "\"preampDb\":" << control_.processor.preampDb << ','
            << "\"bass\":" << control_.processor.bass << ','
            << "\"virtualBass\":" << control_.processor.virtualBass << ','
            << "\"clarity\":" << control_.processor.clarity << ','
            << "\"fidelity\":" << control_.processor.fidelity << ','
            << "\"surround\":" << control_.processor.surround << ','
            << "\"dynamics\":" << control_.processor.dynamics
            << "},\"signature\":{"
            << "\"knowledge\":" << quoted(knowledgeName(signatureInputs_.knowledge)) << ','
            << "\"lowFrequencyCapability\":" << signatureInputs_.lowFrequencyCapability << ','
            << "\"correctionDemand\":" << signatureInputs_.correctionDemand << ','
            << "\"harshnessRisk\":" << signatureInputs_.harshnessRisk << ','
            << "\"endpointVolume\":" << signatureInputs_.endpointVolume << ','
            << "\"plannedSurround\":" << signaturePlan_.surround << ','
            << "\"spatialSampleRate\":" << publishedSpatialSampleRate_ << ','
            << "\"spatialProfileRevision\":" << control_.spatialProfileRevision << ','
            << "\"itdScale\":" << signaturePlan_.spatial.itdScale << ','
            << "\"contralateralGain\":" << signaturePlan_.spatial.contralateralGain
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
    ApoControlState manualControl_{};
    ApoControlState control_{};
    ProcessingMode mode_{ProcessingMode::Signature};
    SignatureInputs signatureInputs_{};
    SignaturePlan signaturePlan_{};
    SignatureSpatialProfileBank spatialProfiles_{};
    float lastSignatureEndpointVolume_{0.5f};
    std::uint32_t publishedSpatialSampleRate_{0};
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
