# PulseFX Windows APO layer

This directory is the Windows integration boundary. The DSP engine must remain independent of COM, registry, PnP and UI code.

## Intended insertion point

PulseFX targets a **render endpoint effect (EFX)** for the user's selected playback endpoint. Windows applies an EFX to every stream using that endpoint, after the render mix. This gives the system-wide behavior PulseFX needs while keeping the DSP in a user-mode APO.

The current `ApoProcessorBridge` is the tested seam between the Windows shell and `pulsefx::Processor`. It rejects unsupported formats instead of partially processing them. Stereo float32 is the first supported graph format; multichannel virtualization will be added deliberately rather than faked.

## Windows shell still to implement and validate on Windows

The COM/WDK wrapper must be built from the current Microsoft SysVAD APO pattern and must provide, at minimum:

- `CBaseAudioProcessingObject`-based COM object.
- `IAudioProcessingObjectRT::APOProcess` forwarding float32 frames to `ApoProcessorBridge::process`.
- Format negotiation and `LockForProcess` that call `ApoProcessorBridge::prepare` only for formats we really support.
- `GetLatency` reporting the lookahead limiter latency.
- `IAudioSystemEffects3` state exposure on Windows 11.
- Componentized APO registration (`Class=AudioProcessingObject`) and an audio-driver extension association for the machine's actual output device.
- Fail-open behavior: unsupported format or invalid state must bypass, never mute the endpoint.

Microsoft's componentized model does not permit one globally registered APO to attach itself to unrelated audio drivers. The install package therefore has to associate PulseFX with the target audio device/driver on this PC.

## Reference implementation

Use Microsoft's `Windows-driver-samples/audio/sysvad/APO/SwapAPO` and `TabletAudioSample/ComponentizedApoSample.inx` as API/packaging references. Do not copy their sample effect logic into the DSP engine.
