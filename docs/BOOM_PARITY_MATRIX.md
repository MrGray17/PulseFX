# PulseFX ↔ Boom 3D Windows parity matrix

Reference target: Boom 3D desktop 2.3.0 (June 18, 2026), Windows.

This document deliberately separates **feature availability** from **verified equivalence**. PulseFX must not claim exact Boom 3D parity merely because a similarly named control exists. Proprietary Boom DSP curves/algorithms must be matched from repeatable black-box captures and volume-matched listening.

Legend:

- ✅ implemented in PulseFX
- 🧪 automated validation exists
- 🖥️ physical Windows validation required
- 🎧 Boom reference capture / listening comparison required
- 🚧 implementation still incomplete
- 🔐 external production credential/service required
- N/A not part of the current Boom 3D Windows surface

| Surface | Implementation | Automated | Remaining release proof |
|---|---|---|---|
| System-wide virtual playback endpoint | ✅ | 🧪 x64/ARM64 WDK build matrix | 🖥️ install, default-device, reboot, sleep, hot-plug |
| Physical output selection / anti-feedback | ✅ | 🧪 native tests | 🖥️ USB/Bluetooth/HDMI matrix |
| 31-band graphic EQ | ✅ | 🧪 DSP response tests | 🎧 match Boom flat/preset curves |
| Flat preset | ✅ | 🧪 renderer/DSP path | 🎧 capture/null against Boom |
| Pop preset | ✅ | 🧪 renderer/DSP path | 🎧 exact curve capture |
| Loud preset | ✅ | 🧪 renderer/DSP path | 🎧 exact curve capture |
| Classical preset | ✅ | 🧪 renderer/DSP path | 🎧 exact curve capture |
| Party preset | ✅ | 🧪 renderer/DSP path | 🎧 exact curve capture |
| Reggae preset | ✅ | 🧪 renderer/DSP path | 🎧 exact curve capture |
| Movie preset | ✅ | 🧪 renderer/DSP path | 🎧 exact curve capture |
| Hip-hop preset | ✅ | 🧪 renderer/DSP path | 🎧 exact curve capture |
| Jazz preset | ✅ | 🧪 renderer/DSP path | 🎧 exact curve capture |
| Deep preset | ✅ | 🧪 renderer/DSP path | 🎧 exact curve capture |
| Dubstep preset | ✅ | 🧪 renderer/DSP path | 🎧 exact curve capture |
| Trap preset | ✅ | 🧪 renderer/DSP path | 🎧 exact curve capture |
| Headphone EQ engine | ✅ typed PK/LSC/HSC + preamp | 🧪 parser + DSP stability | 🎧 correction-result validation |
| Searchable headphone catalog | ✅ pinned AutoEq recommended catalog (6,033 models at pinned revision) | 🧪 catalog/profile parser bounds | 🎧 not Boom's proprietary database; model-by-model equivalence cannot be assumed |
| Fidelity | ✅ | 🧪 finite/output/detail tests | 🎧 black-box tuning against Boom |
| Spatial Stereo | ✅ | 🧪 mono-centre / stability tests | 🎧 black-box width/phase matching |
| Ambience | ✅ | 🧪 reflection/cross-channel tests | 🎧 impulse/decay matching |
| Night Mode | ✅ | 🧪 compatibility + DSP safety | 🎧 threshold/ratio/envelope matching |
| 3D Surround / binaural virtualization | ✅ | 🧪 HRTF/cross-ear tests | 🎧 measured transfer-function matching |
| 5.1 input virtualization | ✅ | 🧪 channel localization tests | 🖥️ real 5.1 application/game/movie sources + 🎧 Boom capture |
| 7.1 input virtualization | ✅ PulseFX extension | 🧪 channel localization tests | 🖥️ source/device matrix |
| Spatial/effect compatibility restrictions | ✅ | 🧪 explicit compatibility tests | 🎧 confirm current Boom behavior on every combination |
| Pitch ±5 semitones | ✅ Signalsmith spectral shifter | 🧪 limits, latency, pitch-frequency test | 🎧 transient/formant/artifact A/B against Boom |
| App Volume Controller | ✅ Windows audio sessions | 🧪 host/session logic | 🖥️ Chrome/Spotify/game/call multi-session validation |
| Persistent settings | ✅ | 🧪 desktop build + bounded native protocol | 🖥️ restart/update/uninstall behavior |
| System tray Quick Controls | ✅ | 🧪 desktop syntax/build | 🖥️ tray lifecycle and fresh-window action delivery |
| Assignable global hotkeys | ✅ | 🧪 desktop syntax/build | 🖥️ OS-reserved/conflicting accelerator behavior |
| Local audio player | ✅ | 🧪 desktop build/CSP | 🖥️ codec matrix |
| Named playlists | ✅ persisted | 🧪 desktop build | 🖥️ large-library/restart behavior |
| Internet radio playback | ✅ Radio Browser-backed | 🧪 URL/station sanitization | 🖥️ live stream codec/drop/reconnect matrix |
| Radio search | ✅ | 🧪 sanitizer/service + renderer | 🖥️ international search behavior |
| Radio Local / Country / Popular browsing | ✅ OS-locale Local + ISO Country + top-voted Popular | 🧪 API-path validation + renderer interaction test | 🖥️ live network/country behavior |
| Register as Windows audio-file handler | ✅ installer associations + cold/warm single-instance file handoff | 🧪 parser + cold hydration/autoplay browser regression + NSIS build gate | 🖥️ Explorer/default-app handoff on clean Windows install |
| Set as Windows default player UX | ✅ opens the official Windows Default Apps settings surface for user choice | 🧪 desktop bridge + renderer interaction test | 🖥️ choose PulseFX in Windows Settings and verify associations |
| x64 native app + driver installer | ✅ pipeline defined | 🧪 CI package job | 🔐 production driver trust + 🖥️ clean-machine install |
| ARM64 native app + driver installer | ✅ pipeline defined | 🧪 CI package job | 🔐 production driver trust + 🖥️ ARM64 hardware install |
| Virtual-driver install from NSIS | ✅ SetupAPI devnode creation + PnPUtil binding + bounded health verification | 🧪 helper/native tests + installer compilation | 🔐 release-signed driver package required; setup intentionally aborts if Windows rejects it |
| Kernel-policy release signature verification | ✅ `verify-release-signature.ps1` | — | 🔐 Microsoft/production signing process must supply trusted package |
| True-peak output protection | ✅ | 🧪 inter-sample peak tests | 🎧 compare Boom headroom/limiter character |
| Audio host crash recovery/watchdog | ✅ | 🧪 native/desktop build | 🖥️ forced-kill and long-run recovery |
| Clock-drift correction / relay telemetry | ✅ | 🧪 controller/native tests | 🖥️ 60+ minute drift/dropout test |
| Reference A/B analyzer | ✅ alignment, RMS matching, spectral/dynamics/stereo/null metrics | 🧪 Python test suite | 🎧 collect real Boom captures for every reference configuration |
| Mac-only system-wide Volume Booster | N/A | — | Current target is Windows Boom parity |
| Mac remote-control surface | N/A | — | Current target is Windows Boom parity |

## Definition of “100% identical”

PulseFX may only use that phrase for a tested configuration when all of the following are true:

1. The same source is captured losslessly through Boom 3D and PulseFX.
2. Captures are time-aligned and loudness/RMS matched before judging quality.
3. The PulseFX analyzer reports sufficiently small spectral, dynamics, stereo, and residual/null differences for an agreed tolerance.
4. Volume-matched blind listening cannot reliably distinguish the target configuration in the validation set.
5. The corresponding system/device lifecycle tests pass without dropouts, dead routing, unsafe peaks, or recovery failures.
6. The Windows release driver passes kernel-policy signature verification and installs on a normal Secure-Boot-enabled machine without weakening OS security.

Until those gates pass, the correct claim is **feature parity implemented, release proof pending**, not exact proprietary-DSP equivalence.
