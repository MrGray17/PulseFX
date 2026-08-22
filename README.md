# PulseFX

PulseFX is a Windows system-wide audio enhancement project built around a native realtime DSP engine, a virtual playback endpoint, device-aware enhancement, binaural rendering, headphone correction, per-app controls, and a desktop control surface.

The engineering goal is no longer to merely copy a competitor feature list. PulseFX is being built as an original enhancement engine that aims to combine the strongest parts of products such as Boom 3D and FxSound while improving adaptability, transparency, realtime safety, and testability. Boom's proprietary DSP source/assets and FxSound's AGPL implementation are not copied into PulseFX.

> **Status:** code-complete pre-release candidate. The repository implementation now includes the adaptive Signature engine and has automated coverage across Windows/Linux native DSP, the Windows host, desktop/Playwright interactions, x64 + ARM64 virtual-driver builds, objective A/B tooling, and architecture-specific NSIS packaging. A public-quality release still requires trusted production driver signing, physical Windows lifecycle/hardware validation, and real volume-matched competitor/reference listening tests.

## The default experience

PulseFX is designed around one simple product test:

> **Music is already playing → enable PulseFX → the same audio immediately feels clearer, larger, more separated and more externalized, without relying on a fake loudness jump.**

Fresh installs start in **Signature Mode** with a Flat user EQ baseline. Existing saved users migrate safely to **Manual Mode** so their previous tone/effect choices are preserved.

### Signature Mode

Signature is a bounded native policy that compiles into the same tested DSP controls used by Manual Mode. It does not run an opaque AI model in the realtime callback.

It adapts enhancement using conservative evidence including:

- selected headphone-correction profile demand
- estimated low-frequency capability from measured correction data
- treble/harshness risk inferred from correction requirements
- Windows physical-endpoint volume as a relative listening-level signal
- device knowledge level (unknown / measured / genuinely personalized spatial profile)
- content intent and low-latency policy hooks
- explicit headroom limits and final true-peak protection

AutoEq data is treated as headphone-response evidence, **not** falsely labeled as a personalized HRTF.

### Manual Mode

Manual Mode remains deterministic and fully user-controlled. Moving a sound-shaping control such as EQ, preamp, bass, clarity, fidelity, spatial, surround, ambience, dynamics, or Night Mode intentionally switches the engine to Manual. Master bypass, pitch, output routing, and headphone-correction enablement remain orthogonal controls.

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
          ├─ 31-band user EQ
          ├─ physical + psychoacoustic bass
          ├─ adaptive Fidelity / Clarity
          ├─ center-preserving externalization
          ├─ click-free HRTF profile transitions
          ├─ dynamics / Night Mode
          ├─ ±5 semitone spectral pitch shift
          └─ true-peak limiter
          │
          ▼
 physical Windows output
 headphones / speakers / USB / Bluetooth / HDMI
```

The Electron controller stays outside the realtime audio path. Control snapshots are validated and published to the native host; expensive profile fitting, filesystem/network work, allocation-heavy work, and calibration logic stay off the per-sample path.

## What is implemented

### Audio engine

- [x] Native realtime DSP engine
- [x] System-wide `PulseFX Output` virtual playback endpoint architecture
- [x] Event-driven WASAPI relay with clock-drift correction and recovery
- [x] Stereo, 5.1, and 7.1 source handling
- [x] Multichannel-to-binaural rendering
- [x] 31-band EQ and preset surface
- [x] Adaptive Signature policy with bounded finite outputs
- [x] Signature → normal processor-control compiler (one audio chain, not a hidden second engine)
- [x] Physical bass enhancement for capable transducers
- [x] Separate psychoacoustic virtual-bass engine for limited transducers
- [x] Smoothed virtual-bass control transitions
- [x] Adaptive clarity / de-masking instead of a fixed presence boost
- [x] Fidelity enhancement
- [x] Center-preserving perceptual externalization
- [x] Low-frequency spatial anchoring and anti-overwide guard
- [x] Damped binaural early reflections
- [x] HRTF/binaural surround
- [x] Personalized spatial-profile transformation hooks
- [x] Three-bank click-free HRTF profile crossfades
- [x] Rapid profile-update coalescing without coefficient loading in `processStereo()`
- [x] ±5 semitone pitch shifting
- [x] Lookahead true-peak limiter
- [x] Non-finite input/parameter defense at protocol, processor, FIR, and DSP boundaries

### Device adaptation

- [x] Pinned AutoEq headphone-correction workflow with thousands of searchable models
- [x] Device-response analysis for correction demand, LF capability, and treble-risk policy inputs
- [x] Conservative behavior for unknown headphones
- [x] Stronger virtual-bass assistance only when measured LF correction evidence supports it
- [x] Reduced unnecessary coloration for more capable devices
- [x] Real Windows endpoint-volume input for relative low-volume compensation
- [x] Separate measured-response knowledge from genuinely personalized HRTF knowledge

### Product / Windows integration

- [x] Signature / Manual mode migration and persistence
- [x] Visible Signature / Manual desktop control
- [x] Manual sound controls intentionally take ownership from Signature
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
- [x] x64 + ARM64 virtual-audio-driver builds
- [x] SetupAPI root-device creation and safe uninstall flow
- [x] Fail-closed driver installation and device-health verification
- [x] x64 + ARM64 NSIS installer packaging

### Automated quality gates

- [x] Windows and Linux native regression suites
- [x] Adaptive Signature edge-cube / bounds / non-finite tests
- [x] Virtual-bass silence, transparency, spectral, centered-image, and transition tests
- [x] Adaptive-clarity masked-vs-bright/transient tests
- [x] Externalization bass-anchor, mono-center, anti-phase, reflection, and finite-output tests
- [x] Live HRTF profile-swap discontinuity tests
- [x] Host protocol hostile-input tests
- [x] Windows host-process smoke test
- [x] Full Playwright primary-interaction suite
- [x] Fresh-install Signature + Flat migration test
- [x] Legacy-settings → Manual preservation test
- [x] Manual-control → Manual-mode persistence test
- [x] Explicit Signature / Manual selector interaction tests
- [x] Objective probe/capture characterization and A/B analysis tooling
- [x] Architecture-matched packaged-host/helper/driver verification

## What is still required before calling it a validated release

These are **external proof gates**, not excuses to weaken the implementation or Windows security.

### 1. Production driver signing 🔐

The CI virtual-audio-driver artifacts are unsigned. A normal Secure-Boot-enabled Windows installation is expected to reject an untrusted kernel driver.

- [ ] Obtain the required trusted Windows driver-signing credentials/process
- [ ] Sign `.sys` / `.cat` through the proper production path
- [ ] Verify with kernel-policy signature verification
- [ ] Confirm installation succeeds with normal Windows security enabled

PulseFX must **not** depend on test-signing mode, disabled signature enforcement, disabled Secure Boot, or similar security downgrades.

### 2. Physical Windows validation 🖥️

Run the trusted installer on normal physical Windows hardware and complete [`docs/WINDOWS_VALIDATION.md`](docs/WINDOWS_VALIDATION.md).

At minimum:

- [ ] clean install / upgrade / uninstall / reinstall
- [ ] `PulseFX Output` creation and removal
- [ ] existing-audio activation experience
- [ ] built-in speakers / headphone jack
- [ ] USB headphones / DAC
- [ ] Bluetooth
- [ ] HDMI / monitor output
- [ ] live device switching and unplug/replug
- [ ] Windows default-device changes
- [ ] real application session volume/mute
- [ ] stereo / 5.1 / 7.1 sources
- [ ] sleep/resume and reboot/relaunch
- [ ] native-host kill/recovery
- [ ] long playback sessions for drift/dropouts
- [ ] unacceptable underrun/overrun investigation until clean

### 3. Real listening and competitor/reference proof 🎧

PulseFX must **not** be called “better than Boom 3D,” “better than FxSound,” or “the best sound enhancer ever built” merely because the architecture is ambitious or tests are green.

Use identical lossless material and controlled captures:

```text
same source
   ├──► reference / competitor capture
   └──► PulseFX capture
             │
             ├─ latency alignment
             ├─ loudness matching
             ├─ true-peak/headroom comparison
             ├─ spectrum / dynamics / stereo metrics
             └─ residual / behavior profiling
                         │
                         ▼
                   tune and repeat
                         │
                         ▼
               randomized blind listening
```

Required evidence:

- [ ] Boom 3D default “instant wow” capture
- [ ] FxSound default/reference capture where useful
- [ ] individual effect and strength captures
- [ ] 31-band EQ/preset characterization
- [ ] bass and low-volume behavior
- [ ] clarity/transient behavior
- [ ] stereo/externalization/HRTF behavior
- [ ] limiter/headroom behavior
- [ ] multichannel material
- [ ] multiple headphone quality levels
- [ ] volume-matched randomized blind listening
- [ ] repeated tuning until material weaknesses are understood

The black-box tools live under `tools/`; acceptance criteria and reference strategy are documented under `docs/`.

## Engineering principles

- Audio quality before feature count.
- “Instant wow” must not be a disguised loudness increase.
- One tested DSP chain for Signature and Manual.
- No UI/network/filesystem/calibration work in the realtime callback.
- Fixed/bounded realtime storage for critical DSP paths.
- Bounded, validated, finite-only control messages.
- Smooth state/profile transitions instead of audible hard resets.
- Preserve center image, bass anchoring, transients, and headroom while creating spaciousness.
- Device-adaptive processing rather than one overcooked universal preset.
- Unknown hardware gets conservative behavior; measured evidence earns stronger adaptation.
- True-peak protection remains the final safety stage, not a permanent tone shaper.
- Reproducible dependency/data revisions.
- Loudness-matched A/B and blind listening before superiority claims.
- No weakening Windows security to make installation look successful.

## Release rule

Automated CI is necessary but not sufficient for a system-wide audio product. PulseFX becomes a validated release only after the **exact release-candidate commit** passes the complete CI/package matrix **and** the trusted signing, physical Windows, and controlled listening/reference gates above are completed.
