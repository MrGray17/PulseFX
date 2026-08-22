# PulseFX — Beat Both Roadmap

PulseFX should not win by copying Boom 3D or FxSound feature-for-feature. It should combine their strongest ideas with capabilities neither product makes central, and it should prove quality with repeatable measurements.

## North-star definition

PulseFX is "better" only when it wins on measurable user outcomes:

1. **Instant transformation:** music that is already playing becomes clearer, more separated, more spacious and more immersive immediately when PulseFX is enabled — with no setup ceremony or obvious loudness trick.
2. **Immersion:** convincing, stable headphone spatialization with less coloration and better localization.
3. **Enhancement:** bass/detail/dynamics that sound fuller without turning into clipping, harshness, or pumping.
4. **Personalization:** tuning follows the actual headphone model, listener preference, and application.
5. **Latency:** enhancement stays usable for games, calls, and interactive audio.
6. **Reliability:** no silent routing failures, feedback loops, stale device state, or fragile restart behavior.
7. **Transparency:** users can inspect what processing is active and PulseFX can publish objective benchmark results.

## Competitive position

### Boom 3D strength to beat

- immersive/spatial presentation
- polished headphone-focused experience
- multichannel virtualization
- broad headphone profile surface
- immediate "turn it on and hear the difference" experience on already-playing audio

### FxSound strength to beat

- strong loudness/clarity/bass enhancement
- mature Windows audio-routing experience
- simple preset-driven workflow
- practical everyday sound improvement

### PulseFX differentiators

- **Instant Signature sound**: a carefully tuned default profile that improves already-playing audio with one power toggle
- **Personalized Spatial Calibration** rather than one generic HRTF response
- **Per-app Scenes**: different processing for a game, browser, music player, voice call, movie player, etc.
- **Volume-aware enhancement**: preserve perceived bass/detail at low listening levels without overprocessing at high levels
- **Transparent benchmark mode**: built-in telemetry and reproducible Boom/FxSound/PulseFX comparisons
- **Open headphone correction provenance** with pinned revisions
- **Fail-visible routing**: the app says when Windows is bypassing processing
- **Low-latency mode** for interactive/game audio

---

# Phase 0 — Instant Wow / Signature Experience

This is the most important product test. A user should not have to understand EQ, HRTFs, presets, dynamics, or headphone profiles before PulseFX sounds impressive.

## Required first-run behavior

1. User starts music in Spotify, YouTube, a browser, game, or local player **before** opening PulseFX.
2. PulseFX attaches to the system-audio route without requiring the source application to restart.
3. Pressing the master power button transitions from transparent/bypass to the default **PulseFX Signature** processing chain.
4. The same music should immediately feel clearer, more separated and more spatially externalized.
5. There must be no click, burst, silence gap, device jump, or obvious gain jump during the transition.
6. The user can toggle PulseFX off/on repeatedly for an honest instant A/B.

## Signature profile principles

The default profile should be deliberately conservative and broadly useful rather than an exaggerated demo preset:

- headphone correction when a known model is selected
- gentle detail/fidelity enhancement
- restrained clarity enhancement
- controlled bass enhancement with headroom budgeting
- spatial widening that protects low-frequency mono compatibility
- binaural/3D contribution strong enough to create externalization without hollowing the center image
- minimal early-reflection ambience for externalization, not audible reverb
- true-peak protection at the end of the chain
- no default pitch modification
- no heavy night-mode compression

The exact values are **measurement/tuning outputs**, not constants to guess. Boom, FxSound and PulseFX reference captures should determine the final Signature tuning.

## Activation engineering

- [ ] verify an already-playing source continues through endpoint activation without restarting the source process
- [ ] dedicated master wet/dry transition rather than hard DSP insertion/removal
- [ ] click-free transition ramp for enable/disable and default-profile changes
- [ ] preserve active physical output when PulseFX becomes the Windows default endpoint
- [ ] preload/prepare all default filter state before wet transition begins
- [ ] never allocate, load profiles, access disk/network, or rebuild unrelated filters in the realtime callback
- [ ] expose activation/routing failure immediately instead of showing a false enabled state

## Acceptance tests

These are engineering targets until validated on real Windows hardware:

- [ ] already-playing Spotify/browser/local audio continues without source restart
- [ ] no detectable discontinuity/click in an enable → disable → enable capture
- [ ] activation does not introduce an unbounded silence gap
- [ ] repeated master-toggle stress test does not increment persistent underrun/overrun counters
- [ ] loudness-matched A/B: default PulseFX Signature stays within **±0.5 dB** RMS/short-window level of the chosen reference comparison so preference is not driven by simple gain
- [ ] no true-peak overshoot beyond the configured limiter ceiling during activation
- [ ] blind A/B listeners prefer Signature over clean bypass on clarity/space without a statistically obvious "the louder one wins" bias
- [ ] the Signature profile works acceptably on both unknown/generic outputs and known corrected headphone models

## Default-product rule

Advanced controls remain available, but **the power button must be enough**. If PulseFX only sounds impressive after manually adjusting ten controls, the default product has failed.

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

- [ ] with music already playing, PulseFX can be enabled without source restart, audible discontinuity, or routing confusion
- [ ] blind listeners prefer the loudness-matched PulseFX Signature profile to clean bypass for clarity/space on the reference music set
- [ ] blind listeners prefer PulseFX or cannot reliably distinguish it from the best competitor spatial rendering at matched loudness
- [ ] PulseFX enhancement preserves equal or lower distortion/headroom violations on the reference suite
- [ ] personalized spatial calibration improves localization/externalization for test listeners versus PulseFX default
- [ ] per-app Scenes work without audible switching artifacts
- [ ] game/interactive low-latency mode meets the chosen latency target on real Windows hardware
- [ ] clean-machine routing/recovery/hot-plug tests pass
- [ ] signed installer works under normal Secure Boot configuration

Until those are measured, "better than Boom 3D and FxSound" remains the engineering target—not a marketing claim.
