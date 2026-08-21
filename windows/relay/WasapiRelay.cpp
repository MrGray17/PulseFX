#ifdef _WIN32

#include "WasapiRelay.h"
#include "ClockDriftController.h"
#include "StereoSampleCodec.h"
#include <Audioclient.h>
#include <Mmdeviceapi.h>
#include <Windows.h>
#include <avrt.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace pulsefx::windows {
namespace {

using Microsoft::WRL::ComPtr;

class UniqueHandle {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) : handle_(handle) {}
    ~UniqueHandle() { reset(); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    HANDLE get() const noexcept { return handle_; }
    explicit operator bool() const noexcept { return handle_ != nullptr; }
    void reset(HANDLE next = nullptr) noexcept {
        if (handle_) CloseHandle(handle_);
        handle_ = next;
    }
private:
    HANDLE handle_{nullptr};
};

struct CoTaskMemWaveFormat {
    WAVEFORMATEX* value{nullptr};
    ~CoTaskMemWaveFormat() { if (value) CoTaskMemFree(value); }
};

bool detectStereoEncoding(
    const WAVEFORMATEX* format,
    StereoSampleEncoding& encoding) noexcept {
    if (!format || format->nChannels != 2) return false;

    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT && format->wBitsPerSample == 32) {
        encoding = StereoSampleEncoding::Float32;
        return true;
    }
    if (format->wFormatTag == WAVE_FORMAT_PCM && format->wBitsPerSample == 16) {
        encoding = StereoSampleEncoding::Pcm16;
        return true;
    }
    if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
        format->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) return false;

    const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
    if (IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != FALSE &&
        format->wBitsPerSample == 32) {
        encoding = StereoSampleEncoding::Float32;
        return true;
    }
    if (IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) != FALSE &&
        format->wBitsPerSample == 16) {
        encoding = StereoSampleEncoding::Pcm16;
        return true;
    }
    return false;
}

WAVEFORMATEX makeFloatRenderFormat(DWORD sampleRate) noexcept {
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    format.nChannels = 2;
    format.nSamplesPerSec = sampleRate;
    format.wBitsPerSample = 32;
    format.nBlockAlign = static_cast<WORD>(format.nChannels * sizeof(float));
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    format.cbSize = 0;
    return format;
}

HRESULT resolveRenderDevice(
    IMMDeviceEnumerator* enumerator,
    const std::wstring& deviceId,
    ComPtr<IMMDevice>& device) {
    if (!enumerator) return E_POINTER;
    if (deviceId.empty()) {
        return enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, device.GetAddressOf());
    }
    return enumerator->GetDevice(deviceId.c_str(), device.GetAddressOf());
}

std::wstring readDeviceId(IMMDevice* device) {
    if (!device) return {};
    LPWSTR raw = nullptr;
    if (FAILED(device->GetId(&raw)) || !raw) return {};
    std::wstring value(raw);
    CoTaskMemFree(raw);
    return value;
}

class StereoRingBuffer {
public:
    explicit StereoRingBuffer(std::size_t capacityFrames)
        : samples_(std::max<std::size_t>(capacityFrames, 1) * 2, 0.0f),
          capacityFrames_(std::max<std::size_t>(capacityFrames, 1)) {}

    bool push(const float* interleaved, std::size_t frames) noexcept {
        if (!interleaved || frames == 0) return false;
        bool overflowed = false;
        if (frames >= capacityFrames_) {
            interleaved += (frames - capacityFrames_) * 2;
            frames = capacityFrames_;
            readFrame_ = writeFrame_ = sizeFrames_ = 0;
            overflowed = true;
        }
        const std::size_t freeFrames = capacityFrames_ - sizeFrames_;
        if (frames > freeFrames) {
            const std::size_t drop = frames - freeFrames;
            readFrame_ = (readFrame_ + drop) % capacityFrames_;
            sizeFrames_ -= drop;
            overflowed = true;
        }
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const std::size_t dst = writeFrame_ * 2;
            samples_[dst] = interleaved[frame * 2];
            samples_[dst + 1] = interleaved[frame * 2 + 1];
            writeFrame_ = (writeFrame_ + 1) % capacityFrames_;
        }
        sizeFrames_ += frames;
        return overflowed;
    }

    std::size_t pop(float* interleaved, std::size_t frames) noexcept {
        if (!interleaved || frames == 0) return 0;
        const std::size_t count = std::min(frames, sizeFrames_);
        for (std::size_t frame = 0; frame < count; ++frame) {
            const std::size_t src = readFrame_ * 2;
            interleaved[frame * 2] = samples_[src];
            interleaved[frame * 2 + 1] = samples_[src + 1];
            readFrame_ = (readFrame_ + 1) % capacityFrames_;
        }
        sizeFrames_ -= count;
        return count;
    }

    std::size_t sizeFrames() const noexcept { return sizeFrames_; }

private:
    std::vector<float> samples_;
    std::size_t capacityFrames_{1};
    std::size_t readFrame_{0};
    std::size_t writeFrame_{0};
    std::size_t sizeFrames_{0};
};

} // namespace

struct WasapiRelay::Impl {
    std::thread worker;
    HANDLE stopEvent{nullptr};
    std::atomic<bool> running{false};
    std::atomic<std::uint64_t> underruns{0};
    std::atomic<std::uint64_t> overruns{0};
    std::atomic<std::uint64_t> capturedFrames{0};
    std::atomic<std::uint64_t> renderedFrames{0};
    std::atomic<std::uint64_t> bufferedFrames{0};
    std::atomic<float> clockCorrectionPpm{0.0f};

    std::mutex controlMutex;
    ApoControlState pendingControl{};
    std::atomic<std::uint64_t> requestedControlRevision{0};
    std::atomic<std::uint64_t> appliedControlRevision{0};

    void updateControlState(const ApoControlState& state) noexcept {
        try {
            std::lock_guard<std::mutex> lock(controlMutex);
            pendingControl = state;
            requestedControlRevision.fetch_add(1, std::memory_order_release);
        } catch (...) {
            // Control updates are best effort. The previous valid DSP state
            // remains active if a control-thread runtime failure occurs.
        }
    }

    void applyPendingControlIfAvailable(ApoProcessorBridge& bridge) noexcept {
        const auto requested = requestedControlRevision.load(std::memory_order_acquire);
        if (requested == appliedControlRevision.load(std::memory_order_relaxed)) return;
        if (!controlMutex.try_lock()) return;

        const ApoControlState next = pendingControl;
        const auto revision = requestedControlRevision.load(std::memory_order_acquire);
        controlMutex.unlock();

        bridge.applyControlState(next);
        appliedControlRevision.store(revision, std::memory_order_release);
    }

    bool start(
        const std::wstring& sourceDeviceId,
        const std::wstring& destinationDeviceId,
        const RelayConfig& config) {
        stop();
        if (sourceDeviceId.empty()) return false;
        stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stopEvent) return false;

        underruns.store(0, std::memory_order_relaxed);
        overruns.store(0, std::memory_order_relaxed);
        capturedFrames.store(0, std::memory_order_relaxed);
        renderedFrames.store(0, std::memory_order_relaxed);
        bufferedFrames.store(0, std::memory_order_relaxed);
        clockCorrectionPpm.store(0.0f, std::memory_order_relaxed);
        appliedControlRevision.store(0, std::memory_order_relaxed);
        updateControlState(config.control);

        std::promise<bool> ready;
        auto readyFuture = ready.get_future();
        worker = std::thread(
            [this, sourceDeviceId, destinationDeviceId, config, ready = std::move(ready)]() mutable {
                run(sourceDeviceId, destinationDeviceId, config, std::move(ready));
            });
        const bool started = readyFuture.get();
        if (!started) stop();
        return started;
    }

    void stop() noexcept {
        if (stopEvent) SetEvent(stopEvent);
        if (worker.joinable()) worker.join();
        if (stopEvent) {
            CloseHandle(stopEvent);
            stopEvent = nullptr;
        }
        running.store(false, std::memory_order_release);
    }

    void run(
        const std::wstring& sourceDeviceId,
        const std::wstring& destinationDeviceId,
        const RelayConfig& config,
        std::promise<bool> ready) noexcept {
        bool readySignalled = false;
        auto signalReady = [&](bool value) {
            if (!readySignalled) {
                ready.set_value(value);
                readySignalled = true;
            }
        };

        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(comResult)) {
            signalReady(false);
            return;
        }

        DWORD mmcssTaskIndex = 0;
        HANDLE mmcssHandle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &mmcssTaskIndex);

        UniqueHandle captureEvent(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        UniqueHandle renderEvent(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        if (!captureEvent || !renderEvent || !stopEvent) {
            signalReady(false);
            if (mmcssHandle) AvRevertMmThreadCharacteristics(mmcssHandle);
            CoUninitialize();
            return;
        }

        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDevice> sourceDevice;
        ComPtr<IMMDevice> destinationDevice;
        ComPtr<IAudioClient> sourceClient;
        ComPtr<IAudioClient> destinationClient;
        ComPtr<IAudioCaptureClient> captureClient;
        ComPtr<IAudioRenderClient> renderClient;
        CoTaskMemWaveFormat sourceFormat;
        UINT32 sourceBufferFrames = 0;
        UINT32 destinationBufferFrames = 0;
        StereoSampleEncoding sourceEncoding = StereoSampleEncoding::Float32;

        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(enumerator.GetAddressOf()));
        if (SUCCEEDED(hr)) hr = resolveRenderDevice(enumerator.Get(), sourceDeviceId, sourceDevice);
        if (SUCCEEDED(hr)) hr = resolveRenderDevice(enumerator.Get(), destinationDeviceId, destinationDevice);
        if (SUCCEEDED(hr) && readDeviceId(sourceDevice.Get()) == readDeviceId(destinationDevice.Get())) {
            hr = E_INVALIDARG;
        }
        if (SUCCEEDED(hr)) {
            hr = sourceDevice->Activate(
                __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(sourceClient.GetAddressOf()));
        }
        if (SUCCEEDED(hr)) hr = sourceClient->GetMixFormat(&sourceFormat.value);
        if (SUCCEEDED(hr) && !detectStereoEncoding(sourceFormat.value, sourceEncoding)) {
            hr = AUDCLNT_E_UNSUPPORTED_FORMAT;
        }
        if (SUCCEEDED(hr)) {
            hr = sourceClient->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                0, 0, sourceFormat.value, nullptr);
        }
        if (SUCCEEDED(hr)) hr = sourceClient->SetEventHandle(captureEvent.get());
        if (SUCCEEDED(hr)) hr = sourceClient->GetService(IID_PPV_ARGS(captureClient.GetAddressOf()));
        if (SUCCEEDED(hr)) hr = sourceClient->GetBufferSize(&sourceBufferFrames);

        WAVEFORMATEX renderFormat{};
        if (SUCCEEDED(hr)) renderFormat = makeFloatRenderFormat(sourceFormat.value->nSamplesPerSec);
        if (SUCCEEDED(hr)) {
            hr = destinationDevice->Activate(
                __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(destinationClient.GetAddressOf()));
        }
        if (SUCCEEDED(hr)) {
            hr = destinationClient->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                    AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                    AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY |
                    AUDCLNT_STREAMFLAGS_RATEADJUST,
                0, 0, &renderFormat, nullptr);
        }
        if (SUCCEEDED(hr)) hr = destinationClient->SetEventHandle(renderEvent.get());
        if (SUCCEEDED(hr)) hr = destinationClient->GetService(IID_PPV_ARGS(renderClient.GetAddressOf()));
        if (SUCCEEDED(hr)) hr = destinationClient->GetBufferSize(&destinationBufferFrames);

        ApoProcessorBridge bridge;
        if (SUCCEEDED(hr) && config.processInRelay) {
            if (!bridge.prepare(static_cast<float>(sourceFormat.value->nSamplesPerSec), 2)) {
                hr = AUDCLNT_E_UNSUPPORTED_FORMAT;
            } else {
                applyPendingControlIfAvailable(bridge);
            }
        }

        if (FAILED(hr)) {
            signalReady(false);
            if (mmcssHandle) AvRevertMmThreadCharacteristics(mmcssHandle);
            CoUninitialize();
            return;
        }

        const std::size_t ringFrames = std::max<std::size_t>(
            static_cast<std::size_t>(sourceFormat.value->nSamplesPerSec / 2),
            std::max<std::size_t>(sourceBufferFrames, destinationBufferFrames) * 8);
        const std::size_t targetBufferedFrames = std::max<std::size_t>(
            static_cast<std::size_t>(sourceFormat.value->nSamplesPerSec * 0.060),
            static_cast<std::size_t>(destinationBufferFrames) * 4);
        StereoRingBuffer ring(ringFrames);
        std::vector<float> captureScratch(static_cast<std::size_t>(sourceBufferFrames) * 2, 0.0f);

        BYTE* initialBuffer = nullptr;
        if (SUCCEEDED(renderClient->GetBuffer(destinationBufferFrames, &initialBuffer))) {
            renderClient->ReleaseBuffer(destinationBufferFrames, AUDCLNT_BUFFERFLAGS_SILENT);
        }

        hr = destinationClient->Start();
        if (SUCCEEDED(hr)) hr = sourceClient->Start();
        if (FAILED(hr)) {
            destinationClient->Stop();
            signalReady(false);
            if (mmcssHandle) AvRevertMmThreadCharacteristics(mmcssHandle);
            CoUninitialize();
            return;
        }

        ComPtr<IAudioClient> clockClient = destinationClient;
        const float nominalSampleRate = static_cast<float>(sourceFormat.value->nSamplesPerSec);
        std::thread clockWorker([
            this,
            clockClient,
            nominalSampleRate,
            targetBufferedFrames]() mutable {
            const HRESULT clockCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(clockCom)) return;

            ComPtr<IAudioClockAdjustment> adjustment;
            const HRESULT serviceResult = clockClient->GetService(
                IID_PPV_ARGS(adjustment.GetAddressOf()));
            if (SUCCEEDED(serviceResult) && adjustment) {
                ClockDriftController controller;
                controller.prepare(nominalSampleRate, targetBufferedFrames);
                while (WaitForSingleObject(stopEvent, 200) == WAIT_TIMEOUT) {
                    const auto buffered = static_cast<std::size_t>(
                        bufferedFrames.load(std::memory_order_relaxed));
                    const float adjustedRate = controller.update(buffered);
                    if (SUCCEEDED(adjustment->SetSampleRate(adjustedRate))) {
                        clockCorrectionPpm.store(
                            controller.correctionPpm(),
                            std::memory_order_relaxed);
                    }
                }
                adjustment->SetSampleRate(nominalSampleRate);
                clockCorrectionPpm.store(0.0f, std::memory_order_relaxed);
            }
            CoUninitialize();
        });

        running.store(true, std::memory_order_release);
        signalReady(true);

        HANDLE waits[3]{stopEvent, captureEvent.get(), renderEvent.get()};
        bool keepRunning = true;
        while (keepRunning) {
            const DWORD waitResult = WaitForMultipleObjects(3, waits, FALSE, INFINITE);
            if (waitResult == WAIT_OBJECT_0) {
                keepRunning = false;
                continue;
            }
            if (waitResult == WAIT_FAILED || waitResult > WAIT_OBJECT_0 + 2) {
                SetEvent(stopEvent);
                keepRunning = false;
                continue;
            }

            if (waitResult == WAIT_OBJECT_0 + 1) {
                UINT32 packetFrames = 0;
                while (SUCCEEDED(captureClient->GetNextPacketSize(&packetFrames)) && packetFrames > 0) {
                    BYTE* rawData = nullptr;
                    DWORD flags = 0;
                    UINT32 frames = 0;
                    if (FAILED(captureClient->GetBuffer(&rawData, &frames, &flags, nullptr, nullptr))) break;
                    const std::size_t sampleCount = static_cast<std::size_t>(frames) * 2;
                    if (sampleCount > captureScratch.size()) {
                        captureClient->ReleaseBuffer(frames);
                        overruns.fetch_add(1, std::memory_order_relaxed);
                        break;
                    }
                    if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || !rawData) {
                        std::fill_n(captureScratch.data(), sampleCount, 0.0f);
                    } else {
                        decodeStereoSamples(rawData, frames, sourceEncoding, captureScratch.data());
                    }
                    captureClient->ReleaseBuffer(frames);

                    if (config.processInRelay) {
                        applyPendingControlIfAvailable(bridge);
                        bridge.process(captureScratch.data(), frames);
                    }
                    if (ring.push(captureScratch.data(), frames)) {
                        overruns.fetch_add(1, std::memory_order_relaxed);
                    }
                    bufferedFrames.store(ring.sizeFrames(), std::memory_order_relaxed);
                    capturedFrames.fetch_add(frames, std::memory_order_relaxed);
                }
            }

            if (waitResult == WAIT_OBJECT_0 + 2) {
                UINT32 padding = 0;
                if (FAILED(destinationClient->GetCurrentPadding(&padding))) continue;
                const UINT32 available = destinationBufferFrames > padding
                    ? destinationBufferFrames - padding : 0;
                if (available == 0) continue;

                BYTE* rawOutput = nullptr;
                if (FAILED(renderClient->GetBuffer(available, &rawOutput)) || !rawOutput) continue;
                auto* output = reinterpret_cast<float*>(rawOutput);
                const std::size_t copied = ring.pop(output, available);
                bufferedFrames.store(ring.sizeFrames(), std::memory_order_relaxed);
                if (copied < available) {
                    std::fill(
                        output + copied * 2,
                        output + static_cast<std::size_t>(available) * 2,
                        0.0f);
                    underruns.fetch_add(1, std::memory_order_relaxed);
                }
                renderClient->ReleaseBuffer(available, 0);
                renderedFrames.fetch_add(copied, std::memory_order_relaxed);
            }
        }

        SetEvent(stopEvent);
        if (clockWorker.joinable()) clockWorker.join();
        sourceClient->Stop();
        destinationClient->Stop();
        running.store(false, std::memory_order_release);
        if (mmcssHandle) AvRevertMmThreadCharacteristics(mmcssHandle);
        CoUninitialize();
    }
};

WasapiRelay::WasapiRelay() : impl_(std::make_unique<Impl>()) {}
WasapiRelay::~WasapiRelay() { stop(); }

bool WasapiRelay::start(
    const std::wstring& sourceDeviceId,
    const std::wstring& destinationDeviceId,
    const RelayConfig& config) {
    return impl_->start(sourceDeviceId, destinationDeviceId, config);
}

void WasapiRelay::updateControlState(const ApoControlState& state) noexcept {
    impl_->updateControlState(state);
}

void WasapiRelay::stop() noexcept { impl_->stop(); }

bool WasapiRelay::running() const noexcept {
    return impl_->running.load(std::memory_order_acquire);
}

RelayStats WasapiRelay::stats() const noexcept {
    return {
        impl_->underruns.load(std::memory_order_relaxed),
        impl_->overruns.load(std::memory_order_relaxed),
        impl_->capturedFrames.load(std::memory_order_relaxed),
        impl_->renderedFrames.load(std::memory_order_relaxed),
        impl_->bufferedFrames.load(std::memory_order_relaxed),
        impl_->appliedControlRevision.load(std::memory_order_relaxed),
        impl_->clockCorrectionPpm.load(std::memory_order_relaxed),
    };
}

} // namespace pulsefx::windows

#endif // _WIN32
