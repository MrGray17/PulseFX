# FxSound reference notes

FxSound is a useful public engineering reference for PulseFX because it solves the same broad Windows problem: expose a virtual playback device, process system audio in user space, and forward the result to a selected real playback device.

PulseFX must **not copy FxSound source code**. The FxSound application and driver are published under AGPL-3.0 (with additional Microsoft sample licensing in the driver repository). PulseFX uses FxSound only as an architectural and black-box behavioral reference unless the project deliberately adopts a compatible licensing strategy in the future.

## Public FxSound architecture

FxSound's own repository describes three application components:

1. a JUCE GUI application;
2. an `AudioPassthru` module that interacts with Windows audio devices;
3. a `DfxDsp` processing module.

Its separate driver repository contains a Windows virtual audio driver derived from a Microsoft Virtual Audio Device Driver sample.

This strongly validates the high-level PulseFX architecture:

```text
Windows applications
       ↓
virtual render endpoint
       ↓
user-mode passthrough / processing engine
       ↓
selected physical playback endpoint
```

PulseFX reached the same topology independently through current Windows/WASAPI/driver research.

## Useful behavioral comparison points

FxSound's public DSP API exposes effects in the same broad class as the products PulseFX is studying:

- Fidelity
- Ambience
- Surround
- Dynamic Boost
- Bass
- graphic EQ
- normalization
- volume leveling
- master gain
- presets
- spectrum analysis

Its DSP sources also reference binaural and surround synthesis modules.

These are useful clues about what a mature audio enhancer considers important, but they are **not** a specification for PulseFX and must not be mechanically translated from FxSound source.

## How PulseFX should use FxSound

Use three independent references:

```text
Boom 3D     = primary target behavior / perceived sound
FxSound     = open-source mature architecture + secondary black-box benchmark
PulseFX     = independent implementation
```

The deterministic probes in `tools/boom_probe.py` are intentionally product-neutral. Run the exact same probe WAVs through all three processors and store captures separately:

```text
reference/
  probes/
  captures/
    boom/<scenario>/
    fxsound/<scenario>/
    pulsefx/<scenario>/
```

Then characterize each capture with `tools/boom_characterize.py` and compare behavioral profiles.

FxSound is especially useful for validating whether PulseFX's general processing choices are reasonable when Boom behavior is difficult to isolate. It should **never override Boom measurements** when the goal is Boom parity.

## Clean-reference rules

When studying FxSound:

- Record architecture-level facts and observable behavior.
- Prefer black-box measurements produced by PulseFX's own probes.
- Do not paste or port FxSound implementation code into PulseFX.
- Do not reproduce FxSound presets or coefficient tables verbatim.
- Do not reuse names/assets/trademarks as PulseFX branding.
- If an algorithm is a general DSP technique, implement it independently from primary technical literature or our own derivation rather than translating FxSound source line-by-line.
- Keep any future third-party code integration separately reviewed for licensing before it enters the PulseFX engine.

## Repositories reviewed

- FxSound application/DSP: `fxsound2/fxsound-app`
- FxSound virtual audio driver: `fxsound2/fxsound-driver`

The repositories were actively maintained when reviewed in August 2026, including Windows x86/x64 and ARM64 distribution support on FxSound's official download surface.
