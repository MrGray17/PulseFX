# PulseFX — Universal Expansive Engine

## Product requirement

PulseFX should make *any* normal pair of headphones — cheap or expensive — feel substantially more spacious, separated and externalized within the physical limits of the transducer.

The goal is not generic stereo widening. The goal is convincing **externalization**: the listener should perceive a wider and deeper soundstage with stable center imaging, clean vocals, believable placement, and minimal coloration.

A $15 headphone cannot be made physically identical to a low-distortion reference headphone by DSP alone. PulseFX therefore adapts the processing strategy instead of applying the same aggressive preset to every device.

## North-star test

1. Music is already playing through the headphones.
2. PulseFX is enabled without interrupting playback.
3. Within the transition window, the listener should hear:
   - clearer separation,
   - more distance between sources,
   - a wider and deeper image,
   - stable centered vocals,
   - fuller low end without mud,
   - less 'inside the skull' presentation.
4. Loudness-matched A/B must still favor PulseFX. A volume increase alone does not count.
5. Toggling must be click-free and fast enough for immediate comparison.

## Adaptive processing tiers

### Tier 1 — Unknown headphone

When the model is unknown, PulseFX uses conservative universal processing:

- bounded spectral tilt correction,
- mild HRTF/binaural externalization,
- mono-compatible low-frequency anchoring,
- restrained early reflections,
- true-peak headroom protection,
- no large narrow-band boosts.

This must improve spatial impression without assuming anything about the headphone's frequency response or distortion capability.

### Tier 2 — Known headphone model

When an AutoEq/reliable headphone profile is available:

1. apply model-specific correction with preamp headroom;
2. derive safe enhancement headroom from the correction profile;
3. apply the spatial renderer after correction;
4. tune bass strategy based on low-frequency extension/correction demand;
5. avoid boosting already-problematic resonances.

### Tier 3 — Personalized headphone + listener

When the user has completed Spatial Calibration:

- personalize ITD scale,
- personalize contralateral/head-shadow strength,
- personalize front/back spectral cue strength,
- personalize early-reflection amount,
- personalize source-distance/wet balance,
- store calibration per headphone model.

The realtime callback does not run the calibration algorithm. Calibration produces a bounded profile outside the audio thread and installs precomputed FIR/filter parameters atomically.

## Cheap-headphone strategy

Cheap headphones commonly need more help but have less headroom and higher distortion. PulseFX must therefore use *smarter* processing, not simply more boost.

Priorities:

- frequency-response correction where measurements exist,
- de-harshing / resonance control before clarity enhancement,
- strict correction preamp/headroom budgeting,
- psychoacoustic virtual bass when true sub-bass boost would waste excursion/headroom,
- center-image protection so widening does not hollow vocals,
- dynamic cap on spatial/bass intensity when the limiter is continuously active,
- conservative high-frequency enhancement to avoid exposing driver grain.

## Expensive-headphone strategy

Good headphones may already have strong extension, low distortion and useful spatial cues. PulseFX should preserve those strengths.

Priorities:

- lighter model correction,
- no unnecessary bass synthesis,
- more transparent HRTF/early-reflection rendering,
- preserve transients and microdynamics,
- less aggressive stereo decorrelation,
- lower wet mix when the headphone already externalizes well.

'Premium' must never mean 'more effects'. It means less corrective intervention and higher spatial transparency.

## Spatial design rules

### 1. Externalization over width

Do not optimize for maximum L/R decorrelation. Over-widening can sound phasey, hollow or unstable.

The renderer should preserve:

- a solid phantom center,
- front/back plausibility,
- low-frequency mono stability,
- interaural timing cues,
- appropriate spectral head-shadow cues,
- coherent early reflections.

### 2. Low frequencies stay anchored

Very low frequencies should remain largely centered/mono-compatible. Spatial width should increase gradually with frequency rather than applying the same widening to bass and upper bands.

### 3. Early reflections are part of the spatial model

Subtle short reflections can improve distance/externalization. They should be:

- low in level,
- bounded in decay,
- spectrally damped,
- disabled/reduced in low-latency mode,
- never allowed to smear transients.

### 4. Personalized HRTF profiles

PulseFX already represents stereo HRTF rendering as four FIR paths:

- left → left,
- left → right,
- right → left,
- right → right.

Personalization should transform/precompute these profiles outside the realtime callback rather than adding expensive logic inside the callback.

## Bass modes

PulseFX should expose internally distinct bass strategies even if the normal UI keeps them automatic.

### Physical Bass

For headphones with adequate low-frequency capability:

- low shelf / harmonic-safe enhancement,
- headroom budget before limiter,
- content-aware but bounded strength,
- no continuous limiter dependence.

### Virtual Bass

For transducers that cannot reproduce deep fundamentals cleanly:

- synthesize restrained upper harmonics from low-frequency content,
- preserve the original low-frequency signal,
- avoid excessive harmonic density,
- gate/limit synthesis to avoid audible buzz,
- automatically reduce as true LF capability improves.

## Quality protections

The Universal Expansive Engine must always enforce:

- finite-sample sanitization,
- true-peak ceiling,
- bounded EQ/profile gains,
- preamp/headroom calculation,
- click-free control transitions,
- no filter/profile allocation from the realtime callback,
- mono-compatibility checks,
- center-image stability checks,
- bypass transparency.

## Automatic adaptation inputs

Useful inputs available without invasive sensing:

- selected headphone profile and correction curve,
- endpoint volume,
- sample rate / channel layout,
- current limiter activity,
- active app scene,
- user calibration profile,
- processing mode (Signature / Low Latency / Movie / Voice etc.).

Do **not** pretend to know acoustic SPL without calibration.

## Signature default

The default Signature mode should be deliberately conservative enough to work on unknown headphones yet deliver an obvious A/B improvement.

Initial design intent:

- headphone correction: automatic when a known model is selected,
- spatial/HRTF: moderate,
- stereo widening: restrained and frequency-aware,
- ambience: subtle early reflections,
- fidelity/clarity: moderate,
- bass: adaptive physical/virtual strategy,
- dynamics: low-to-moderate,
- limiter: always protecting output,
- loudness bias: controlled so matched-A/B improvement remains convincing.

The user should not need to understand any of those stages to get the best first-run experience.

## Acceptance criteria

Do not claim universal expansiveness until these are measured:

- [ ] unknown inexpensive headphone: blind A/B preference for Signature over bypass at matched loudness;
- [ ] measured inexpensive headphone: profile-aware mode improves preference over universal mode;
- [ ] premium/reference headphone: Signature improves externalization without statistically significant degradation in tonal preference;
- [ ] center-vocal localization remains stable under spatial processing;
- [ ] mono low-frequency content remains centered;
- [ ] no persistent true-peak overshoot;
- [ ] no audible switching artifacts;
- [ ] no persistent underrun/overrun accumulation during long-run testing;
- [ ] personalization improves localization/externalization for a majority of test listeners versus the generic profile;
- [ ] at matched loudness, PulseFX is preferred to or competitive with Boom 3D and FxSound on the same headphone/reference material.

## Rule

PulseFX must adapt to the headphone.

The user should never have to buy expensive headphones just to experience the core PulseFX spatial effect — and expensive headphones should never be made worse by an enhancer that assumes more processing is always better.
