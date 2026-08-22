# PulseFX — Beat Both Roadmap

PulseFX should not win by copying Boom 3D or FxSound feature-for-feature. It should combine their strongest ideas with capabilities neither product makes central, and it should prove quality with repeatable measurements.

## North-star definition

PulseFX is "better" only when it wins on measurable user outcomes:

1. **Immersion:** convincing, stable headphone spatialization with less coloration and better localization.
2. **Enhancement:** bass/detail/dynamics that sound fuller without turning into clipping, harshness, or pumping.
3. **Personalization:** tuning follows the actual headphone model, listener preference, and application.
4. **Latency:** enhancement stays usable for games, calls, and interactive audio.
5. **Reliability:** no silent routing failures, feedback loops, stale device state, or fragile restart behavior.
6. **Transparency:** users can inspect what processing is active and PulseFX can publish objective benchmark results.

## Competitive position

### Boom 3D strength to beat

- immersive/spatial presentation
- polished headphone-focused experience
- multichannel virtualization
- broad headphone profile surface

### FxSound strength to beat

- strong loudness/clarity/bass enhancement
- mature Windows audio-routing experience
- simple preset-driven workflow
- practical everyday sound improvement

### PulseFX differentiators

- **Personalized Spatial Calibration** rather than one generic HRTF response
- **Per-app Scenes**: different processing for a game, browser, music player, voice call, movie player, etc.
- **Volume-aware enhancement**: preserve perceived bass/detail at low listening levels without overprocessing at high levels
- **Transparent benchmark mode**: built-in telemetry and reproducible Boom/FxSound/PulseFX comparisons
- **Open headphone correction provenance** with pinned revisions
- **Fail-visible routing**: the app says when Windows is bypassing processing
- **Low-latency mode** for interactive/game audio

---

# Phase A — Measurement before tuning

Status: in progress in PR #2.

- [x] deterministic reference probes
- [x] 31-frequency EQ characterization
- [x] stereo impulse/crossfeed characterization
- [x] dynamics staircase
- [x] ambience decay probe
- [x] isolated 5.1 / 7.1 channel probes
- [x] machine-readable behavioral profiles
- [x] Boom-vs-PulseFX profile comparison metrics
- [x] FxSound clean-reference documentation
- [ ] run all probes through Boom 3D on physical Windows
- [ ] run all probes through FxSound
- [ ] run all probes through PulseFX
- [ ] store comparison reports and tune by measured deltas

## Acceptance targets

These are goals, not claims until measured:

- bypass residual below -100 dBFS relative to source after declared fixed latency alignment
- no non-finite output under extreme controls
- no true-peak overshoot beyond configured limiter ceiling
- no persistent underrun/overrun accumulation during a 60-minute run
- no audible zippering during continuous control automation
- stable localization under 5.1 / 7.1 test material

---

# Phase B — Personalized Spatial Calibration

Boom-class spatial audio is useful, but generic HRTFs vary strongly between listeners. PulseFX should let each listener tune the renderer without needing laboratory ear measurements.

## User flow

1. Select headphone model.
2. Enter Spatial Calibration.
3. PulseFX plays short localization probes from virtual left/front/right/rear positions.
4. User chooses which rendering sounds most externalized and correctly located.
5. PulseFX estimates a compact listener profile.
6. Store profile per headphone model.

## Parameters to personalize

- ITD scale
- contralateral head-shadow strength
- crossfeed amount
- virtual source distance
- front/back spectral cue amount
- room/early-reflection amount
- surround wet/dry balance

## Engineering requirements

- [ ] calibration profile schema
- [ ] native control parameters with bounds and smoothing
- [ ] deterministic calibration probe generator
- [ ] desktop calibration wizard
- [ ] per-headphone calibration persistence
- [ ] ABX-style comparison between default and personalized profile
- [ ] regression tests ensuring calibration changes cannot destabilize the realtime thread

---

# Phase C — Per-App Scenes

PulseFX should remember how the user wants each application to sound.

Examples:

- game.exe → low-latency spatial + restrained bass
- browser.exe → neutral correction + clarity
- spotify.exe → music preset + headphone correction
- movie player → multichannel virtualization + dynamics
- Discord / Teams → voice-focused clarity, no ambience

## Requirements

- [ ] scene schema with versioning
- [ ] map Windows audio session/process identity to a scene
- [ ] atomic scene transition on active-session changes
- [ ] configurable priority when multiple apps play simultaneously
- [ ] manual override that temporarily beats automation
- [ ] UI showing which scene is currently active and why
- [ ] no scene switching from the realtime audio callback
- [ ] scene transition smoothing / ramping to avoid pops
- [ ] automated session-change tests

---

# Phase D — Volume-Aware Enhancement

A static bass/detail curve is not ideal at every listening level. PulseFX should optionally compensate gently at lower endpoint levels and back off as listening level rises.

## Requirements

- [ ] read endpoint-volume state outside realtime callback
- [ ] bounded equal-loudness-inspired bass/detail compensation curve
- [ ] user strength control
- [ ] strict headroom budgeting before limiter
- [ ] no gain chasing from content RMS alone
- [ ] test monotonicity and maximum boost limits
- [ ] objective low-volume listening comparisons

This is an approximation unless actual SPL is calibrated; the UI must say that clearly.

---

# Phase E — Low-Latency Mode

Provide a mode designed specifically for games/calls/live monitoring.

- [ ] measure end-to-end PulseFX pipeline latency
- [ ] expose current processing latency in UI
- [ ] bypass or simplify block-latency-heavy modules where needed
- [ ] define low-latency spatial preset
- [ ] keep headphone correction / EQ / limiter where latency permits
- [ ] automated latency regression benchmark
- [ ] target stable performance under CPU stress

---

# Phase F — Benchmark / Proof Mode

PulseFX should be unusually honest for consumer audio software.

Expose:

- current sample rate / channel layout
- processing latency
- underrun / overrun counters
- active scene
- active headphone model and correction revision
- true-peak activity
- routing-active status

Release benchmark reports should compare:

- clean bypass
- Boom 3D
- FxSound
- PulseFX default
- PulseFX personalized

using the same reference material and loudness-matched conditions.

---

# Definition of "better than both"

Do not claim this until we can demonstrate all of the following:

- [ ] blind listeners prefer PulseFX or cannot reliably distinguish it from the best competitor spatial rendering at matched loudness
- [ ] PulseFX enhancement preserves equal or lower distortion/headroom violations on the reference suite
- [ ] personalized spatial calibration improves localization/externalization for test listeners versus PulseFX default
- [ ] per-app Scenes work without audible switching artifacts
- [ ] game/interactive low-latency mode meets the chosen latency target on real Windows hardware
- [ ] clean-machine routing/recovery/hot-plug tests pass
- [ ] signed installer works under normal Secure Boot configuration

Until those are measured, "better than Boom 3D and FxSound" remains the engineering target—not a marketing claim.
