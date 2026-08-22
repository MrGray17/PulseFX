# PulseFX

PulseFX is a Windows system-wide audio enhancement project with a native realtime DSP engine, virtual playback endpoint, multichannel-to-binaural rendering, headphone correction, per-app volume control, and a desktop control surface.

The target is feature parity with the Windows surface of Boom 3D 2.3.0 while keeping PulseFX's implementation, visual identity, and code original. Boom's proprietary DSP source, assets, and private headphone database are not copied.

> **Status:** code-complete pre-release. The full repository implementation has passed the automated Windows/Linux native suite, desktop/Playwright interaction suite, x64 + ARM64 virtual-driver builds, A/B analysis tests, and verified x64 + ARM64 NSIS packaging. The remaining work is release validation outside hosted CI: trusted driver signing, real Windows hardware/lifecycle testing, and Boom-vs-PulseFX reference matching before claiming proprietary-DSP equivalence.

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

The Electron desktop controller is intentionally kept outside the realtime audio path. It communicates with the native host through a bounded command protocol and provides output selection, effects, EQ, a searchable pinned headphone-profile catalog, app-volume control, local player/playlists, Internet radio, tray Quick Controls, configurable global hotkeys, Windows Default Apps handoff, and Explorer audio-file launch/autoplay.

## What is already done

- [x] Native realtime DSP engine
- [x] System-wide `PulseFX Output` virtual playback endpoint architecture
- [x] Event-driven WASAPI relay with clock-drift correction and recovery
- [x] Stereo, 5.1, and 7.1 input handling
- [x] Multichannel-to-binaural rendering
- [x] 31-band EQ and preset surface
- [x] Fidelity, Spatial, Ambience, Night Mode, and 3D/HRTF Surround
- [x] ±5 semitone pitch shifting
- [x] True-peak output protection
- [x] Pinned AutoEq headphone-correction workflow with thousands of searchable models
- [x] Per-application Windows volume and mute controls
- [x] Output-device selection and routing telemetry
- [x] Realtime-safe live control updates
- [x] Native-host crash/protocol recovery
- [x] Persistent desktop settings
- [x] System tray Quick Controls and global hotkeys
- [x] Local audio player and playlists
- [x] Explorer file associations, cold/warm launch handoff, deduplication, and autoplay
- [x] Internet radio search plus Popular / Local / Country browsing
- [x] Windows Default Apps handoff
- [x] x64 virtual-audio-driver build
- [x] ARM64 virtual-audio-driver build
- [x] SetupAPI root-device creation and safe uninstall flow
- [x] Fail-closed driver installation and device-health verification
- [x] x64 NSIS installer packaging
- [x] ARM64 NSIS installer packaging
- [x] Full Playwright UI interaction tests, including all major controls and 31 EQ bands
- [x] Windows and Linux native regression tests
- [x] Objective Boom/PulseFX A/B analysis tooling

The exact hardening head passed the complete CI matrix end-to-end, including both verified Windows installer artifacts.

## What's left to do

The repository implementation is no longer waiting on ordinary feature development. The remaining work is release proof that requires a trusted Windows environment and real reference audio.

### 1. Production driver signing 🔐

The virtual audio driver built by CI is currently **unsigned**. Windows is expected to reject it on a normal Secure-Boot-enabled machine.

Before a normal public/private release:

- [ ] Obtain the required Windows code-signing / driver-signing credentials
- [ ] Sign the PulseFX driver package using the proper production Windows driver-signing path
- [ ] Verify the resulting `.sys` / `.cat` package with kernel-policy signature verification
- [ ] Confirm the installer succeeds without disabling Secure Boot or driver-signature enforcement

PulseFX must **not** rely on test-signing mode, disabled signature enforcement, disabled Secure Boot, or other security downgrades for normal installation.

### 2. Clean Windows installation + real hardware validation 🖥️

Run the signed installer on a normal physical Windows machine and complete the matrix in [`docs/WINDOWS_VALIDATION.md`](docs/WINDOWS_VALIDATION.md).

At minimum:

- [ ] Fresh install on a clean Windows machine
- [ ] Confirm `PulseFX Output` appears and starts correctly
- [ ] Set `PulseFX Output` as the Windows playback device
- [ ] Confirm routing to built-in speakers/headphone jack
- [ ] Test USB headphones / DAC
- [ ] Test Bluetooth headphones
- [ ] Test HDMI / monitor output where available
- [ ] Switch physical outputs while audio is playing
- [ ] Unplug/replug devices during playback
- [ ] Test Windows default-device changes
- [ ] Test application volume/mute controls with real apps
- [ ] Test stereo sources
- [ ] Test real 5.1 and 7.1 sources
- [ ] Test sleep → resume
- [ ] Test reboot → relaunch → settings restore
- [ ] Kill/restart the native host and verify recovery
- [ ] Run long playback sessions and confirm no accumulating clock drift
- [ ] Confirm zero unacceptable underruns/overruns/dropouts
- [ ] Test install → upgrade → uninstall → reinstall lifecycle

### 3. Boom 3D reference matching 🎧

Feature parity is implemented, but PulseFX must **not** be described as acoustically identical to Boom 3D until black-box testing proves it.

Use the same lossless source material through both processors:

```text
same source audio
      │
      ├──────────────► Boom 3D capture
      │
      └──────────────► PulseFX capture
                            │
                            ▼
                     automatic alignment
                            │
                     loudness matching
                            │
          spectrum / dynamics / stereo / peaks
                            │
                       residual error
                            │
                      tune PulseFX DSP
                            │
                          repeat
```

Required work:

- [ ] Capture Boom 3D and PulseFX output from identical lossless test material
- [ ] Test every major Boom effect independently
- [ ] Test documented compatible effect combinations
- [ ] Compare 31-band EQ behavior and presets
- [ ] Compare stereo width and HRTF/spatial behavior
- [ ] Compare transient response and dynamics
- [ ] Compare true-peak/headroom behavior
- [ ] Compare quiet/detail behavior of Fidelity and Night Mode
- [ ] Test multichannel surround material
- [ ] Tune PulseFX where objective deltas remain meaningful
- [ ] Perform volume-matched blind listening tests
- [ ] Repeat across multiple music genres, speech, film/game material, and headphones

Acceptance criteria and the current parity matrix live in [`docs/BOOM_PARITY_MATRIX.md`](docs/BOOM_PARITY_MATRIX.md).

### 4. Release polish after validation 🚀

Once the three gates above pass:

- [ ] Produce trusted signed x64 and ARM64 installers
- [ ] Run one final release-candidate CI matrix
- [ ] Publish versioned release artifacts and checksums
- [ ] Add installation/troubleshooting documentation based on the real clean-machine test
- [ ] Mark the first validated build as the PulseFX release candidate / v1.0

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

Automated CI covers native DSP/host tests on Windows and Linux, the reference A/B analyzer, desktop syntax/build checks, the full Playwright UI interaction suite, AutoEq profile parsing, radio URL/input sanitization, x64/ARM64 virtual-driver packages, and architecture-specific NSIS installers.

Automated tests are necessary but not sufficient for a system-wide audio product. A production release must additionally pass trusted kernel-driver signing, clean-machine driver installation, physical device hot-plug/sleep/reboot/long-run testing, and the Boom black-box reference-matching matrix documented under `docs/`.
