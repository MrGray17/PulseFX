# PulseFX Windows release validation

Automated CI is necessary but not sufficient for a system-wide audio product. A release is not considered Boom-class or production-ready until the following tests are run on the target Windows PC with the actual `PulseFX Output` virtual endpoint loaded.

## 1. Virtual endpoint / routing

- `PulseFX Output` appears exactly once under Windows playback devices.
- It can be selected as the Windows default playback endpoint.
- PulseFX never selects `PulseFX Output` as its own downstream sink.
- The UI lists only real physical sinks in the output selector.
- Starting PulseFX while the virtual endpoint is absent reports a visible error and never claims processing is live.
- Starting PulseFX with no physical sink available fails open with a visible error rather than hanging.

## 2. Audio continuity

Use continuous music plus a transient-heavy test file.

- 30 minutes wired headphones/speakers: zero audible clicks/dropouts.
- 60 minutes continuous playback: no growing latency and no sustained ring-buffer drift.
- Relay telemetry remains stable; investigate every underrun/overrun counter increment.
- Master bypass is transparent apart from the declared fixed pipeline latency.
- Rapid UI automation (EQ/effects/preamp) does not restart the audio client or produce zipper clicks.

## 3. Device lifecycle

Repeat while audio is playing:

- unplug/replug 3.5 mm / USB output;
- disconnect/reconnect Bluetooth output;
- change the selected physical output in PulseFX;
- change Windows default endpoint while PulseFX is the default virtual endpoint;
- sleep for at least 60 seconds and resume;
- restart Windows;
- quit/relaunch PulseFX repeatedly;
- crash/kill `pulsefx_audio_host.exe` and confirm Electron restarts it without entering a feedback loop.

Expected result: audio recovers automatically where Windows exposes a valid sink. Any unrecoverable state must be visible in the UI instead of silently producing no sound.

## 4. Format / source matrix

Validate at minimum:

- browser video;
- Spotify/streaming music;
- local PCM/WAV playback;
- game audio;
- voice/video-call playback;
- 44.1 kHz source into the 48 kHz virtual endpoint;
- quiet source, near-full-scale source, and transient-heavy source.

No clipping, NaNs, DC jumps, channel swaps or sustained level pumping.

## 5. Loudness and output safety

- Capture bypass and processed output digitally.
- Confirm the 4x detector/limiter keeps reconstructed peaks within the configured safety ceiling.
- Confirm no preset can exceed the limiter ceiling under full-scale torture input.
- Loudness-match comparisons before judging quality. A louder candidate is not automatically better.

## 6. Boom 3D reference matching

For every reference preset/effect configuration being targeted:

1. Play the exact same source file through Boom 3D and capture its output losslessly.
2. Play the source through PulseFX and capture its output losslessly.
3. Run:

   `python tools/audio_lab.py boom.wav pulsefx.wav --json`

4. Compare alignment, RMS-match gain, spectral delta RMS, crest/dynamics, stereo correlation, side-to-mid balance and residual/null error.
5. Tune PulseFX only from repeatable captures, then repeat the test.
6. Perform volume-matched blind listening after the objective deltas are small.

Do **not** claim exact Boom equivalence from screenshots, slider positions or casual listening. Boom's proprietary DSP implementation is not public; equivalence must be demonstrated empirically.

## 7. Acceptance log

Record for every candidate release:

- Windows version/build;
- physical audio device and driver version;
- PulseFX commit SHA;
- virtual-driver package revision;
- sample rate;
- test duration;
- underrun/overrun counts;
- sleep/hot-plug results;
- Boom reference configuration and A/B analyzer output.

A release fails if any reproducible dropout, dead-routing state, unbounded clock drift, unsafe output peak, or silent recovery failure remains.
