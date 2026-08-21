#!/usr/bin/env python3
"""PulseFX A/B capture lab.

Compares two PCM WAV captures after RMS loudness matching. This is deliberately
small and dependency-free so it can run on a clean Windows Python install.
It reports the things most likely to fool an enhancer comparison: level, peak,
crest factor, DC offset, stereo correlation, and octave-ish spectral probes.
"""
from __future__ import annotations

import argparse
import math
import struct
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

PROBES = (31.5, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0)

@dataclass
class Audio:
    sample_rate: int
    channels: int
    frames: list[tuple[float, ...]]


def _decode_pcm(raw: bytes, width: int) -> list[float]:
    if width == 1:
        return [(b - 128) / 128.0 for b in raw]
    if width == 2:
        values = struct.unpack(f"<{len(raw)//2}h", raw)
        return [v / 32768.0 for v in values]
    if width == 3:
        out: list[float] = []
        for i in range(0, len(raw), 3):
            value = int.from_bytes(raw[i:i+3], "little", signed=False)
            if value & 0x800000:
                value -= 1 << 24
            out.append(value / 8388608.0)
        return out
    if width == 4:
        values = struct.unpack(f"<{len(raw)//4}i", raw)
        return [v / 2147483648.0 for v in values]
    raise ValueError(f"unsupported PCM sample width: {width * 8} bit")


def read_wav(path: Path) -> Audio:
    with wave.open(str(path), "rb") as wav:
        channels = wav.getnchannels()
        if channels not in (1, 2):
            raise ValueError(f"{path}: only mono/stereo PCM WAV is supported")
        width = wav.getsampwidth()
        rate = wav.getframerate()
        raw = wav.readframes(wav.getnframes())
    samples = _decode_pcm(raw, width)
    frames = [tuple(samples[i:i+channels]) for i in range(0, len(samples), channels)]
    return Audio(rate, channels, frames)


def mono(audio: Audio) -> list[float]:
    if audio.channels == 1:
        return [f[0] for f in audio.frames]
    return [0.5 * (f[0] + f[1]) for f in audio.frames]


def rms(samples: Iterable[float]) -> float:
    data = list(samples)
    return math.sqrt(sum(x*x for x in data) / max(1, len(data)))


def db(value: float) -> float:
    return 20.0 * math.log10(max(value, 1e-12))


def gain(audio: Audio, factor: float) -> Audio:
    return Audio(audio.sample_rate, audio.channels, [tuple(x * factor for x in f) for f in audio.frames])


def correlation(audio: Audio) -> float:
    if audio.channels != 2 or not audio.frames:
        return 1.0
    left = [f[0] for f in audio.frames]
    right = [f[1] for f in audio.frames]
    ml = sum(left)/len(left)
    mr = sum(right)/len(right)
    numerator = sum((l-ml)*(r-mr) for l, r in zip(left, right))
    dl = math.sqrt(sum((l-ml)**2 for l in left))
    dr = math.sqrt(sum((r-mr)**2 for r in right))
    return numerator / max(dl*dr, 1e-12)


def goertzel(samples: list[float], rate: int, frequency: float) -> float:
    if frequency >= rate * 0.49 or not samples:
        return 0.0
    omega = 2.0 * math.pi * frequency / rate
    coeff = 2.0 * math.cos(omega)
    s1 = s2 = 0.0
    for x in samples:
        s0 = x + coeff*s1 - s2
        s2, s1 = s1, s0
    power = s1*s1 + s2*s2 - coeff*s1*s2
    return math.sqrt(max(power, 0.0)) / len(samples)


def metrics(audio: Audio) -> dict[str, float]:
    samples = [x for frame in audio.frames for x in frame]
    mono_samples = mono(audio)
    level = rms(samples)
    peak = max((abs(x) for x in samples), default=0.0)
    dc = sum(samples) / max(1, len(samples))
    result = {
        "rms_dbfs": db(level),
        "peak_dbfs": db(peak),
        "crest_db": db(peak / max(level, 1e-12)),
        "dc": dc,
        "correlation": correlation(audio),
    }
    for freq in PROBES:
        result[f"probe_{freq:g}"] = db(goertzel(mono_samples, audio.sample_rate, freq))
    return result


def trim_to_match(a: Audio, b: Audio) -> tuple[Audio, Audio]:
    if a.sample_rate != b.sample_rate:
        raise ValueError("captures must have the same sample rate")
    if a.channels != b.channels:
        raise ValueError("captures must have the same channel count")
    count = min(len(a.frames), len(b.frames))
    return Audio(a.sample_rate, a.channels, a.frames[:count]), Audio(b.sample_rate, b.channels, b.frames[:count])


def main() -> None:
    parser = argparse.ArgumentParser(description="Loudness-matched PulseFX A/B WAV comparison")
    parser.add_argument("reference", type=Path, help="reference capture, e.g. Boom 3D")
    parser.add_argument("candidate", type=Path, help="PulseFX capture")
    args = parser.parse_args()

    reference, candidate = trim_to_match(read_wav(args.reference), read_wav(args.candidate))
    ref_rms = rms([x for f in reference.frames for x in f])
    cand_rms = rms([x for f in candidate.frames for x in f])
    match_gain = ref_rms / max(cand_rms, 1e-12)
    candidate = gain(candidate, match_gain)

    ref = metrics(reference)
    cand = metrics(candidate)
    print(f"RMS-match gain applied to candidate: {db(match_gain):+.2f} dB")
    print("\nmetric                 reference     PulseFX      delta")
    print("-" * 58)
    keys = ["rms_dbfs", "peak_dbfs", "crest_db", "correlation", "dc"]
    for key in keys:
        print(f"{key:22s} {ref[key]:10.4f} {cand[key]:10.4f} {cand[key]-ref[key]:+10.4f}")
    print("\nSpectral probes after loudness match (dB, candidate - reference)")
    for freq in PROBES:
        key = f"probe_{freq:g}"
        print(f"{freq:7.1f} Hz: {cand[key]-ref[key]:+7.2f} dB")


if __name__ == "__main__":
    main()
