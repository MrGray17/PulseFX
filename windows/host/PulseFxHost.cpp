#ifdef _WIN32

#include "AudioDeviceCatalog.h"
#include "AudioSessionMixer.h"
#include "HeadphoneProfileCommand.h"
#include "HostProtocol.h"
#include "../relay/WasapiRelay.h"
#include "pulsefx/ScenePolicy.h"
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
#include <vector>

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

bool parseContentClass(const std::string& value, ContentClass& result) noexcept {
    if (value == "general") { result = ContentClass::General; return true; }
    if (value == "music") { result = ContentClass::Music; return true; }
    if (value == "movie") { result = ContentClass::Movie; return true; }
    if (value == "game") { result = ContentClass::Game; return true; }
    if (value == "voice") { result = ContentClass::Voice; return true; }
    return false;
}

const char* contentClassName(ContentClass content) noexcept {
    switch (content) {
        case ContentClass::Music: return "music";
        case ContentClass::Movie: return "movie";
        case ContentClass::Game: return "game";
        case ContentClass::Voice: return "voice";
        case ContentClass::General: break;
    }
    return "general";
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

bool sameSceneSelection(const SceneSelection& a, const SceneSelection& b) noexcept {
    return a.matched == b.matched &&
        a.processKey == b.processKey &&
        a.processId == b.processId &&
        a.content == b.content &&
        a.lowLatency == b.lowLatency &&
        a.priority == b.priority;
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
        if (command.name == "content") return handleContentLocked(command);
        if (command.name == "low_latency") return handleLowLatencyLocked(command);
        if (command.name == "signature_strength") return handleSignatureStrengthLocked(command);
        if (command.name == "scene_enable") return handleSceneEnableLocked(command);
        if (command.name == "scene_set") return handleSceneSetLocked(command);
        if (command.name == "scene_remove") return handleSceneRemoveLocked(command);
        if (command.name == "scene_clear") return handleSceneClearLocked(command);
        if (command.name == "spatial_calibration") return handleSpatialCalibrationLocked(command);
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

    SpatialProfileTuning activeCalibrationLocked() const noexcept {
        return calibrationEnabled_ ? calibrationTuning_ : SpatialProfileTuning{};
    }

    void syncSpatialProfilesLocked(
        std::uint32_t sampleRate,
        const SpatialProfileTuning& signatureTuning) {
        if (sampleRate == 0) return;
        spatialProfiles_.update(
            static_cast<float>(sampleRate),
            signatureTuning,
            activeCalibrationLocked());
        if (!spatialProfiles_.valid() ||
            static_cast<std::uint32_t>(std::lround(spatialProfiles_.sampleRate())) != sampleRate) return;

        manualControl_.spatialProfile = spatialProfiles_.manualProfile();
        manualControl_.spatialProfileRevision = spatialProfiles_.manualRevision();
        publishedSpatialSampleRate_ = sampleRate;
    }

    std::uint32_t activeSourceSampleRateLocked() const noexcept {
        const auto telemetry = ApoProcessorBridge::telemetry();
        if (telemetry.sampleRate != 0) return telemetry.sampleRate;
        if (sourceId_.empty()) return publishedSpatialSampleRate_;
        const auto resolved = renderDeviceSampleRate(sourceId_);
        return resolved != 0 ? resolved : publishedSpatialSampleRate_;
    }

    ContentClass effectiveContentLocked() const noexcept {
        return sceneAutomationEnabled_ && activeScene_.matched
            ? activeScene_.content
            : defaultContent_;
    }

    bool effectiveLowLatencyLocked() const noexcept {
        return sceneAutomationEnabled_ && activeScene_.matched
            ? activeScene_.lowLatency
            : defaultLowLatency_;
    }

    void applySignatureLocked(
        float endpointVolume,
        std::uint32_t sampleRate,
        bool push) {
        SignatureInputs inputs = makeSignatureInputsFromHeadphoneProfile(
            manualControl_.headphoneProfile,
            endpointVolume);
        if (calibrationEnabled_) inputs.knowledge = DeviceKnowledge::Personalized;
        inputs.content = effectiveContentLocked();
        inputs.lowLatency = effectiveLowLatencyLocked();
        inputs.strength = signatureStrength_;

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

    void refreshSceneLocked() {
        SceneSelection next{};
        if (sceneAutomationEnabled_ && !sceneRules_.empty() && !sourceId_.empty()) {
            const auto sessions = enumerateAudioSessions(sourceId_);
            std::vector<SceneSession> candidates;
            candidates.reserve(sessions.size());
            for (const auto& session : sessions) {
                candidates.push_back({
                    utf8FromWide(session.processName),
                    session.processId,
                    session.active,
                    session.muted,
                    session.volume,
                });
            }
            next = chooseScene(sceneRules_, candidates);
        }
        if (sameSceneSelection(next, activeScene_)) return;
        activeScene_ = next;
        if (mode_ == ProcessingMode::Signature) {
            const float volume = destinationId_.empty()
                ? lastSignatureEndpointVolume_
                : endpointVolumeScalar(destinationId_, lastSignatureEndpointVolume_);
            applySignatureLocked(volume, activeSourceSampleRateLocked(), true);
        }
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
        if (nextSampleRate == 0) clearSpatialPublicationLocked();

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
                refreshSceneLocked();
            } catch (...) {
                // The next watchdog pass retries. Never terminate the host from
                // an environmental device/session-enumeration failure.
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

    std::string handleContentLocked(const HostCommand& command) {
        if (command.args.size() != 1) return errorJson("content expects general, music, movie, game, or voice");
        ContentClass content{};
        if (!parseContentClass(command.args[0], content)) return errorJson("invalid content class");
        defaultContent_ = content;
        if (mode_ == ProcessingMode::Signature && !(sceneAutomationEnabled_ && activeScene_.matched)) {
            applySignatureLocked(lastSignatureEndpointVolume_, activeSourceSampleRateLocked(), true);
        }
        return ackJson("content");
    }

    std::string handleLowLatencyLocked(const HostCommand& command) {
        if (command.args.size() != 1) return errorJson("low_latency expects one boolean");
        bool enabled = false;
        if (!parseBool(command.args[0], enabled)) return errorJson("invalid low_latency value");
        defaultLowLatency_ = enabled;
        if (mode_ == ProcessingMode::Signature && !(sceneAutomationEnabled_ && activeScene_.matched)) {
            applySignatureLocked(lastSignatureEndpointVolume_, activeSourceSampleRateLocked(), true);
        }
        return ackJson("low_latency");
    }

    std::string handleSignatureStrengthLocked(const HostCommand& command) {
        if (command.args.size() != 1) return errorJson("signature_strength expects one value");
        float value = 1.0f;
        if (!parseFiniteFloat(command.args[0], value)) return errorJson("invalid Signature strength");
        signatureStrength_ = std::clamp(value, 0.50f, 1.25f);
        if (mode_ == ProcessingMode::Signature) {
            applySignatureLocked(lastSignatureEndpointVolume_, activeSourceSampleRateLocked(), true);
        }
        return ackJson("signature_strength");
    }

    std::string handleSceneEnableLocked(const HostCommand& command) {
        if (command.args.size() != 1) return errorJson("scene_enable expects one boolean");
        bool enabled = false;
        if (!parseBool(command.args[0], enabled)) return errorJson("invalid scene_enable value");
        sceneAutomationEnabled_ = enabled;
        refreshSceneLocked();
        if (!enabled && mode_ == ProcessingMode::Signature) {
            applySignatureLocked(lastSignatureEndpointVolume_, activeSourceSampleRateLocked(), true);
        }
        return ackJson("scene_enable");
    }

    std::string handleSceneSetLocked(const HostCommand& command) {
        if (command.args.size() != 4) return errorJson("scene_set expects process, content, low_latency, priority");
        const std::string processKey = normalizeSceneProcessKey(command.args[0]);
        if (processKey.empty()) return errorJson("invalid scene process");
        ContentClass content{};
        if (!parseContentClass(command.args[1], content)) return errorJson("invalid scene content");
        bool lowLatency = false;
        if (!parseBool(command.args[2], lowLatency)) return errorJson("invalid scene low_latency");
        std::uint32_t priority = 0;
        if (!parseUint32(command.args[3], priority) || priority > 100) return errorJson("scene priority must be 0..100");

        auto found = std::find_if(sceneRules_.begin(), sceneRules_.end(), [&](const SceneRule& rule) {
            return normalizeSceneProcessKey(rule.processKey) == processKey;
        });
        SceneRule next{processKey, content, lowLatency, static_cast<int>(priority)};
        if (found != sceneRules_.end()) *found = next;
        else {
            if (sceneRules_.size() >= 128) return errorJson("scene rule limit reached");
            sceneRules_.push_back(std::move(next));
        }
        refreshSceneLocked();
        return ackJson("scene_set");
    }

    std::string handleSceneRemoveLocked(const HostCommand& command) {
        if (command.args.size() != 1) return errorJson("scene_remove expects one process");
        const std::string processKey = normalizeSceneProcessKey(command.args[0]);
        if (processKey.empty()) return errorJson("invalid scene process");
        sceneRules_.erase(
            std::remove_if(sceneRules_.begin(), sceneRules_.end(), [&](const SceneRule& rule) {
                return normalizeSceneProcessKey(rule.processKey) == processKey;
            }),
            sceneRules_.end());
        refreshSceneLocked();
        return ackJson("scene_remove");
    }

    std::string handleSceneClearLocked(const HostCommand& command) {
        if (!command.args.empty()) return errorJson("scene_clear expects no arguments");
        sceneRules_.clear();
        refreshSceneLocked();
        return ackJson("scene_clear");
    }

    std::string handleSpatialCalibrationLocked(const HostCommand& command) {
        if (command.args.size() != 5) {
            return errorJson("spatial_calibration expects enabled, itd, ipsilateral, contralateral, trim_db");
        }
        bool enabled = false;
        float itd = 1.0f;
        float ipsilateral = 1.0f;
        float contralateral = 1.0f;
        float trimDb = 0.0f;
        if (!parseBool(command.args[0], enabled) ||
            !parseFiniteFloat(command.args[1], itd) ||
            !parseFiniteFloat(command.args[2], ipsilateral) ||
            !parseFiniteFloat(command.args[3], contralateral) ||
            !parseFiniteFloat(command.args[4], trimDb)) {
            return errorJson("invalid spatial calibration values");
        }
        calibrationEnabled_ = enabled;
        calibrationTuning_ = sanitizeSpatialProfileTuning({itd, ipsilateral, contralateral, trimDb});

        const auto sampleRate = activeSourceSampleRateLocked();
        if (mode_ == ProcessingMode::Signature) {
            applySignatureLocked(lastSignatureEndpointVolume_, sampleRate, true);
        } else {
            syncSpatialProfilesLocked(sampleRate, signaturePlan_.spatial);
            control_ = manualControl_;
            pushControlLocked();
        }
        return ackJson("spatial_calibration");
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
        refreshSceneLocked();
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
                << ",\"processName\":" << quoted(utf8FromWide(session.processName))
                << ",\"name\":" << quoted(utf8FromWide(session.name))
                << ",\"volume\":" << session.volume
                << ",\"muted\":" << (session.muted ? "true" : "false")
                << ",\"active\":" << (session.active ? "true" : "false") << '}';
        }
        out << "]}";
        return out.str();
    }

    std::string statusJsonLocked() const {
        const RelayStats stats = relay_.stats();
        const BridgeTelemetrySnapshot telemetry = ApoProcessorBridge::telemetry();
        const bool routingActive = !sourceId_.empty() && defaultRenderDeviceId() == sourceId_;
        std::string effectiveError = lastError_;
        if (effectiveError.empty() && relay_.running() && !routingActive) {
            effectiveError = "PulseFX Output is not the Windows default playback device; system audio may bypass processing";
        }

        const float processorLatencyMs = telemetry.sampleRate > 0
            ? 1000.0f * static_cast<float>(telemetry.processorLatencyFrames) / static_cast<float>(telemetry.sampleRate)
            : 0.0f;
        const float bufferedLatencyMs = telemetry.sampleRate > 0
            ? 1000.0f * static_cast<float>(stats.bufferedFrames) / static_cast<float>(telemetry.sampleRate)
            : 0.0f;

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
            << "\"content\":" << quoted(contentClassName(effectiveContentLocked())) << ','
            << "\"lowLatency\":" << (effectiveLowLatencyLocked() ? "true" : "false") << ','
            << "\"signatureStrength\":" << signatureStrength_ << ','
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
            << "},\"calibration\":{"
            << "\"enabled\":" << (calibrationEnabled_ ? "true" : "false") << ','
            << "\"itdScale\":" << calibrationTuning_.itdScale << ','
            << "\"ipsilateralGain\":" << calibrationTuning_.ipsilateralGain << ','
            << "\"contralateralGain\":" << calibrationTuning_.contralateralGain << ','
            << "\"wetTrimDb\":" << calibrationTuning_.wetTrimDb
            << "},\"scene\":{"
            << "\"automationEnabled\":" << (sceneAutomationEnabled_ ? "true" : "false") << ','
            << "\"matched\":" << (activeScene_.matched ? "true" : "false") << ','
            << "\"processName\":" << quoted(activeScene_.processKey) << ','
            << "\"pid\":" << activeScene_.processId << ','
            << "\"content\":" << quoted(contentClassName(activeScene_.content)) << ','
            << "\"lowLatency\":" << (activeScene_.lowLatency ? "true" : "false") << ','
            << "\"priority\":" << activeScene_.priority << ','
            << "\"ruleCount\":" << sceneRules_.size()
            << "},\"stats\":{"
            << "\"underruns\":" << stats.underruns << ','
            << "\"overruns\":" << stats.overruns << ','
            << "\"capturedFrames\":" << stats.capturedFrames << ','
            << "\"renderedFrames\":" << stats.renderedFrames << ','
            << "\"bufferedFrames\":" << stats.bufferedFrames << ','
            << "\"controlRevision\":" << stats.controlRevision << ','
            << "\"clockCorrectionPpm\":" << stats.clockCorrectionPpm << ','
            << "\"sampleRate\":" << telemetry.sampleRate << ','
            << "\"inputChannels\":" << telemetry.inputChannels << ','
            << "\"processorLatencyFrames\":" << telemetry.processorLatencyFrames << ','
            << "\"processorLatencyMs\":" << processorLatencyMs << ','
            << "\"bufferedLatencyMs\":" << bufferedLatencyMs << ','
            << "\"internalLatencyMs\":" << (processorLatencyMs + bufferedLatencyMs) << ','
            << "\"limiterGainReductionDb\":" << telemetry.limiterGainReductionDb << ','
            << "\"headroomStress\":" << telemetry.headroomStress << ','
            << "\"masterWetMix\":" << telemetry.masterWetMix
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
    float signatureStrength_{1.0f};
    float lastSignatureEndpointVolume_{0.5f};
    std::uint32_t publishedSpatialSampleRate_{0};
    ContentClass defaultContent_{ContentClass::General};
    bool defaultLowLatency_{false};
    bool sceneAutomationEnabled_{true};
    std::vector<SceneRule> sceneRules_{};
    SceneSelection activeScene_{};
    bool calibrationEnabled_{false};
    SpatialProfileTuning calibrationTuning_{};
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
