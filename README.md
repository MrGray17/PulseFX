# PulseFX

PulseFX is a Windows system-wide audio enhancement project with a native realtime DSP engine, virtual playback endpoint, multichannel-to-binaural rendering, headphone correction, per-app volume control, and a desktop control surface.

The current target is feature parity with the Windows surface of Boom 3D 2.3.0 while keeping PulseFX's implementation, visual identity, and code original. Boom's proprietary DSP source, assets, and private headphone database are not copied.

> **Status:** advanced pre-release hardening. Native DSP/relay tests, desktop builds, reference-analysis tooling, x64/ARM64 virtual-driver builds, architecture-specific installers, and hardware validation are treated as release gates. Exact proprietary-DSP equivalence is **not** claimed until repeatable Boom captures and volume-matched comparisons meet the criteria in [`docs/BOOM_PARITY_MATRIX.md`](docs/BOOM_PARITY_MATRIX.md) and [`docs/WINDOWS_VALIDATION.md`](docs/WINDOWS_VALIDATION.md).

## Current architecture

```text
Windows apps / games / browsers
          │
          ▼
   PulseFX Output
  virtual WDM endpoint
          │
          ▼
 event-driven WASAPI relay
          │
          ├─ stereo / 5.1 / 7.1 decode
          ├─ multichannel binaural render
          ├─ headphone correction
          ├─ 31-band EQ
          ├─ Fidelity / Spatial / Ambience / Night / Surround
          ├─ ±5 semitone spectral pitch shift
          └─ true-peak limiter
          │
          ▼
 physical Windows output
 headphones / speakers / USB / Bluetooth / HDMI
```

The Electron desktop controller is intentionally kept outside the realtime audio path. It communicates with the native host through a bounded command protocol and provides output selection, effects, EQ, a searchable pinned headphone-profile catalog, app-volume control, local player/playlists, Internet radio, tray Quick Controls, and configurable global hotkeys.

## Engineering principles

- Audio quality before feature count.
- Bit-transparent master bypass apart from declared fixed pipeline latency.
- No UI/network/filesystem work in the realtime callback.
- Bounded, validated control messages across the desktop/native boundary.
- No silent routing failures: bad device/profile states fail open and surface an error.
- True-peak output protection under aggressive presets and gain.
- Reproducible dependency/data revisions for audio algorithms and headphone profiles.
- Loudness-matched A/B analysis before calling one signal “better.”
- No weakening Secure Boot, Windows driver-signature enforcement, or OS security to make installation appear successful.

## Release gates

Automated CI covers native DSP/host tests on Windows and Linux, the reference A/B analyzer, desktop syntax/build checks, AutoEq profile parsing, radio URL sanitization, x64/ARM64 virtual-driver packages, and architecture-specific NSIS installers.

Automated tests are necessary but not sufficient for a system-wide audio product. A production release must additionally pass clean-machine driver installation, device hot-plug/sleep/reboot/long-run tests, kernel-policy signature verification, and the Boom black-box reference-matching matrix documented under `docs/`.
