# PulseFX virtual render endpoint

PulseFX needs its own Windows playback endpoint so applications can keep routing to **PulseFX Output** while PulseFX forwards processed audio to the physical device selected in the control app.

## Upstream strategy

The kernel driver is prepared from Microsoft's `audio/simpleaudiosample` at the pinned Windows Driver Samples revision recorded in `bootstrap-driver.ps1`.

We intentionally do not vendor Microsoft's full sample into this repository. The bootstrap script fetches the pinned upstream source and applies a very small PulseFX customization:

- keep the sample's simple WaveRT speaker/render endpoint;
- disable capture endpoints at runtime;
- do not publish the sample microphone interfaces;
- change the visible hardware ID and friendly names to PulseFX;
- keep the render topology that has no fake hardware loopback pin and no offload path.

Generated upstream/build files live under `.work/` and `out/` and are ignored by Git.

## Prepare the source

From PowerShell on Windows:

```powershell
.\windows\driver\bootstrap-driver.ps1
```

This only prepares the pinned source under `windows/driver/.work/`.

## Build the driver

On a Windows development environment with Visual Studio C++ tooling and the Windows Driver Kit installed:

```powershell
.\windows\driver\bootstrap-driver.ps1 -Build -Configuration Debug -Platform x64
```

A successful package is copied to `windows/driver/out/`.

The build script deliberately does **not** enable test-signing, change Secure Boot/BitLocker settings, install certificates, or install the driver. Those system-level deployment steps should only happen after the generated package has been reviewed and the Windows build has passed.

## Why this endpoint shape

The upstream speaker filter already exposes a single 48 kHz stereo render stream and a bridge pin. There is no sample loopback pin in that filter. PulseFX then uses Windows software loopback capture from this endpoint and relays the captured stream to the selected physical output.

The user-mode relay lives in `windows/relay/`. The portable DSP/APO seam lives in `windows/apo/`.

## Remaining Windows validation

Before calling the system-wide path production-ready we still need to validate on the actual target PC:

- WDK build and driver package signing;
- endpoint creation as `PulseFX Output`;
- software loopback returns the real render mix, never a sample-generated tone;
- event-driven loopback + physical rendering across sleep/resume and device changes;
- no feedback loop if the default device changes;
- clock-drift correction remains stable over long playback sessions;
- latency and A/V sync with the final DSP chain.
