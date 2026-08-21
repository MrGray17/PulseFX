# PulseFX Windows audio layer

This directory is the Windows DSP/APO boundary. The DSP engine must remain independent of COM, registry, PnP, device enumeration and UI code.

## Target architecture

For a Boom-3D-style Windows experience, PulseFX should expose its own **virtual render endpoint** and remain the Windows-selected output device while the app forwards processed audio to a user-selected physical device.

```text
Windows apps
    |
    v
PulseFX virtual render endpoint (SysVAD-derived WaveRT device)
    |
    v
PulseFX APO / DSP bridge
    |
    v
PulseFX user-mode relay
    |
    v
selected physical headphones / speakers / HDMI / USB DAC
```

This matches the product behavior we are targeting more closely than globally attaching PulseFX to an unrelated physical audio driver. It also gives the app explicit control over the downstream device while Windows continues to route applications into the PulseFX endpoint.

## Current state

`ApoProcessorBridge` is the tested seam between a Windows audio shell and `pulsefx::Processor`. It rejects unsupported formats instead of partially processing them. Stereo float32 is the first supported realtime format.

The portable DSP bridge remains useful inside the virtual endpoint even though the product boundary has moved: the virtual driver/APO owns the Windows-facing render graph, while a separate relay owns the physical destination.

## Windows pieces still to implement and validate on a Windows WDK machine

### Virtual endpoint / driver

Use Microsoft's current SysVAD `TabletAudioSample` as the WDM/WaveRT reference. The PulseFX package needs a root-enumerated virtual render device with componentized driver packaging, appropriate INF files, and test signing during development.

### APO component

The componentized APO should provide, at minimum:

- `CBaseAudioProcessingObject`-based COM object.
- `IAudioProcessingObjectRT::APOProcess` forwarding float32 frames to `ApoProcessorBridge::process`.
- Format negotiation and `LockForProcess` that only accept formats PulseFX actually supports.
- `GetLatency` reporting the DSP lookahead latency.
- `IAudioSystemEffects3` state exposure on Windows 11 where appropriate.
- Fail-open behavior: invalid state must bypass rather than mute audio.

### User-mode relay

The relay captures the PulseFX virtual render stream, sends it through the selected processing/output path, and renders to the physical destination with device hot-swap and recovery handling. It must never select the PulseFX virtual endpoint as its own downstream sink.

## Reference implementation

Use Microsoft's `Windows-driver-samples/audio/sysvad` projects as API/packaging references, particularly `TabletAudioSample`, `APO/SwapAPO`, and the componentized INF examples. PulseFX DSP itself remains original.
