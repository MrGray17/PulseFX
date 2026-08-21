# PulseFX audio quality target

Boom 3D is a product benchmark, not an implementation source. PulseFX must reach the same class of perceived polish with original DSP.

## Non-negotiable checks

- Bit-transparent explicit bypass.
- Flat processing path is transparent after declared limiter latency.
- No NaN/Inf propagation.
- No sample peaks above the configured ceiling in the supported stereo path.
- No allocations, file I/O, mutex acquisition, or logging in the realtime processing callback.
- Parameter changes are smoothed to avoid zipper noise.
- Mono content stays centered when spatial widening is enabled.
- Tests run at 44.1, 48, and 96 kHz at minimum.

## A/B method

Do not compare enhancers at unequal loudness. Capture the same source through the reference product and PulseFX, then run:

```powershell
python tools/audio_lab.py boom.wav pulsefx.wav
```

The lab RMS-matches the candidate before reporting peak, crest factor, stereo correlation, DC offset, and spectral-probe differences.

Listening tests should use hidden labels and repeated trials. A setting is not accepted because it is simply louder or wider.

## Still required for the final quality bar

- 4x or higher oversampled true-peak limiting validated against inter-sample peaks.
- HRTF convolution from SOFA data for binaural surround.
- Room/ambience early-reflection model with bounded decay.
- Measurement-derived headphone profile ingestion and profile database licensing review.
- Multichannel 5.1/7.1 routing and downmix validation.
- Windows APO/driver integration, device switching, sleep/resume recovery, and fault-safe bypass.
