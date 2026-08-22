#!/usr/bin/env python3
"""Generate deterministic WAV probes for Boom/PulseFX black-box matching.

The probes are designed to characterize observable processing behavior without
requiring access to proprietary implementation details. All files are PCM16 WAV
and a JSON manifest records the exact timing/levels used for later analysis.
"""
from __future__ import annotations

import argparse
import json
import math
import random
import struct
import wave
from pathlib import Path

RATE = 48_000
PCM_PEAK = 32767
EQ_FREQUENCIES = (
    20.0, 25.0, 31.5, 40.0, 50.0, 63.0, 80.0, 100.0, 125.0, 160.0,
    200.0, 250.0, 315.0, 400.0, 500.0, 630.0, 800.0, 1000.0, 1250.0,
    1600.0, 2000.0, 2500.0, 3150.0, 4000.0, 5000.0, 6300.0, 8000.0,
    10000.0, 12500.0, 16000.0, 20000.0,
)
DYNAMICS_LEVELS_DBFS = (-60, -48, -36, -30, -24, -18, -12, -9, -6, -3)


def _clip(value: float) -> float:
    return max(-1.0, min(1.0, value))


def _pcm16(value: float) -> int:
    return int(round(_clip(value) * PCM_PEAK))


def write_pcm16(path: Path, channels: int, frames: list[tuple[float, ...]], rate: int = RATE) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), 'wb') as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        raw = bytearray()
        for frame in frames:
            if len(frame) != channels:
                raise ValueError('frame channel count mismatch')
            raw.extend(struct.pack('<' + 'h' * channels, *(_pcm16(value) for value in frame)))
        wav.writeframes(bytes(raw))


def _silence(seconds: float, channels: int) -> list[tuple[float, ...]]:
    return [tuple(0.0 for _ in range(channels)) for _ in range(round(seconds * RATE))]


def stereo_impulses() -> tuple[list[tuple[float, float]], dict[str, object]]:
    duration = 2.5
    frames = _silence(duration, 2)
    events = [
        ('left', 0.50, (0.85, 0.0)),
        ('right', 1.50, (0.0, 0.85)),
        ('mono', 2.10, (0.65, 0.65)),
    ]
    for name, seconds, values in events:
        index = round(seconds * RATE)
        frames[index] = values
    return frames, {
        'purpose': 'latency, channel crossfeed, HRTF/interaural timing, stereo matrix',
        'events': [{'name': name, 'time_s': seconds} for name, seconds, _ in events],
    }


def eq_multitone(seconds: float = 12.0) -> tuple[list[tuple[float, float]], dict[str, object]]:
    # Randomized deterministic phases reduce the crest factor versus phase-aligned tones.
    rng = random.Random(0xB003D)
    phases = [rng.uniform(0.0, 2.0 * math.pi) for _ in EQ_FREQUENCIES]
    frames: list[tuple[float, float]] = []
    count = round(seconds * RATE)
    tone_gain = 0.72 / math.sqrt(len(EQ_FREQUENCIES))
    fade_frames = round(0.05 * RATE)
    for i in range(count):
        value = sum(tone_gain * math.sin(2.0 * math.pi * f * i / RATE + phase)
                    for f, phase in zip(EQ_FREQUENCIES, phases))
        if i < fade_frames:
            value *= i / max(1, fade_frames)
        if i >= count - fade_frames:
            value *= (count - 1 - i) / max(1, fade_frames)
        value = _clip(value)
        frames.append((value, value))
    return frames, {
        'purpose': '31-band frequency-response and phase behavior at fixed frequencies',
        'frequencies_hz': list(EQ_FREQUENCIES),
        'analysis_window_s': [1.0, seconds - 1.0],
        'deterministic_phase_seed': 0xB003D,
    }


def logarithmic_sweep(seconds: float = 14.0) -> tuple[list[tuple[float, float]], dict[str, object]]:
    start_hz, stop_hz = 18.0, 22_000.0
    frames: list[tuple[float, float]] = []
    count = round(seconds * RATE)
    ratio = stop_hz / start_hz
    log_ratio = math.log(ratio)
    fade_frames = round(0.10 * RATE)
    phase = 0.0
    for i in range(count):
        t = i / RATE
        frequency = start_hz * math.exp(log_ratio * t / seconds)
        phase += 2.0 * math.pi * frequency / RATE
        value = 0.32 * math.sin(phase)
        if i < fade_frames:
            value *= i / max(1, fade_frames)
        if i >= count - fade_frames:
            value *= (count - 1 - i) / max(1, fade_frames)
        frames.append((value, value))
    return frames, {
        'purpose': 'broadband frequency-response/nonlinear artifact inspection',
        'start_hz': start_hz,
        'stop_hz': stop_hz,
        'duration_s': seconds,
    }


def dynamics_staircase(segment_seconds: float = 1.25, gap_seconds: float = 0.30) -> tuple[list[tuple[float, float]], dict[str, object]]:
    frames: list[tuple[float, float]] = []
    segments: list[dict[str, float]] = []
    frames.extend(_silence(0.75, 2))
    cursor = len(frames) / RATE
    for level_dbfs in DYNAMICS_LEVELS_DBFS:
        amplitude = 10.0 ** (level_dbfs / 20.0)
        start = cursor
        count = round(segment_seconds * RATE)
        for i in range(count):
            # A two-tone segment exposes level-dependent processing while being
            # less likely than a pure 1 kHz sine to trigger special-case paths.
            t = i / RATE
            value = amplitude * (0.82 * math.sin(2 * math.pi * 997 * t) +
                                 0.18 * math.sin(2 * math.pi * 3011 * t))
            frames.append((value, value))
        cursor += segment_seconds
        end = cursor
        segments.append({'level_dbfs': level_dbfs, 'start_s': start, 'end_s': end})
        frames.extend(_silence(gap_seconds, 2))
        cursor += gap_seconds
    return frames, {
        'purpose': 'compressor/night-mode/fidelity level-transfer curve',
        'segments': segments,
        'analysis_guard_s': 0.20,
    }


def ambience_burst() -> tuple[list[tuple[float, float]], dict[str, object]]:
    rng = random.Random(0xA11B1E)
    pre_s, burst_s, tail_s = 0.5, 0.08, 3.0
    frames = _silence(pre_s, 2)
    burst_count = round(burst_s * RATE)
    previous = 0.0
    for i in range(burst_count):
        white = rng.uniform(-1.0, 1.0)
        # Very small one-pole coloration keeps the burst broadband without a
        # brutally bright full-scale discontinuity.
        previous = 0.65 * previous + 0.35 * white
        envelope = math.sin(math.pi * i / max(1, burst_count - 1)) ** 2
        value = 0.55 * previous * envelope
        frames.append((value, value))
    frames.extend(_silence(tail_s, 2))
    return frames, {
        'purpose': 'ambience/early-reflection decay and late-tail measurement',
        'burst_start_s': pre_s,
        'burst_end_s': pre_s + burst_s,
        'tail_end_s': pre_s + burst_s + tail_s,
    }


def multichannel_markers(channels: int) -> tuple[list[tuple[float, ...]], dict[str, object]]:
    if channels not in (6, 8):
        raise ValueError('multichannel markers support only 5.1 (6) and 7.1 (8)')
    names = ('FL', 'FR', 'FC', 'LFE', 'SL', 'SR') if channels == 6 else ('FL', 'FR', 'FC', 'LFE', 'BL', 'BR', 'SL', 'SR')
    spacing_s = 0.55
    start_s = 0.50
    duration_s = start_s + spacing_s * channels + 0.60
    frames = _silence(duration_s, channels)
    events = []
    pulse_length = round(0.012 * RATE)
    for channel, name in enumerate(names):
        event_s = start_s + spacing_s * channel
        start = round(event_s * RATE)
        frequency = 500.0 if name == 'LFE' else 1400.0 + channel * 137.0
        for i in range(pulse_length):
            envelope = math.sin(math.pi * i / max(1, pulse_length - 1)) ** 2
            value = 0.55 * envelope * math.sin(2.0 * math.pi * frequency * i / RATE)
            frame = list(frames[start + i])
            frame[channel] = value
            frames[start + i] = tuple(frame)
        events.append({'channel_index': channel, 'channel': name, 'time_s': event_s, 'frequency_hz': frequency})
    return frames, {
        'purpose': '5.1/7.1 virtual-speaker localization and channel-routing matrix',
        'events': events,
        'channel_order': list(names),
    }


def generate(output: Path) -> dict[str, object]:
    output.mkdir(parents=True, exist_ok=True)
    files: dict[str, object] = {}

    generators = [
        ('stereo_impulses.wav', 2, stereo_impulses),
        ('eq_multitone.wav', 2, eq_multitone),
        ('log_sweep.wav', 2, logarithmic_sweep),
        ('dynamics_staircase.wav', 2, dynamics_staircase),
        ('ambience_burst.wav', 2, ambience_burst),
        ('surround_5_1.wav', 6, lambda: multichannel_markers(6)),
        ('surround_7_1.wav', 8, lambda: multichannel_markers(8)),
    ]

    for filename, channels, factory in generators:
        frames, metadata = factory()
        write_pcm16(output / filename, channels, frames)
        files[filename] = {
            'channels': channels,
            'sample_rate': RATE,
            'frames': len(frames),
            'duration_s': len(frames) / RATE,
            **metadata,
        }

    manifest = {
        'schema': 1,
        'sample_rate': RATE,
        'sample_format': 'PCM16',
        'files': files,
        'capture_rule': 'Do not normalize, EQ, resample, trim, or change Windows volume between Boom and PulseFX captures.',
    }
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2, sort_keys=True), encoding='utf-8')
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description='Generate PulseFX/Boom deterministic black-box audio probes')
    parser.add_argument('output', type=Path, nargs='?', default=Path('reference/probes'))
    args = parser.parse_args()
    manifest = generate(args.output)
    print(f"Generated {len(manifest['files'])} deterministic probes in {args.output}")


if __name__ == '__main__':
    main()
