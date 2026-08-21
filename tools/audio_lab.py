#!/usr/bin/env python3
"""PulseFX reference-matching audio lab.

Compares two mono/stereo PCM WAV captures after automatic time alignment and
RMS loudness matching. It deliberately uses only Python's standard library so
it can run on a clean Windows installation and in CI.

The tool is meant for controlled A/B captures of the *same source*. It reports
objective deltas rather than claiming that one processing chain sounds better.
"""
from __future__ import annotations

import argparse
import json
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
        if rate <= 0:
            raise ValueError(f"{path}: invalid sample rate")
        raw = wav.readframes(wav.getnframes())
    samples = _decode_pcm(raw, width)
    frames = [tuple(samples[i:i+channels]) for i in range(0, len(samples), channels)]
    return Audio(rate, channels, frames)


def mono(audio: Audio) -> list[float]:
    if audio.channels == 1:
        return [f[0] for f in audio.frames]
    return [0.5 * (f[0] + f[1]) for f in audio.frames]


def flatten(audio: Audio) -> list[float]:
    return [sample for frame in audio.frames for sample in frame]


def rms(samples: Iterable[float]) -> float:
    data = list(samples)
    return math.sqrt(sum(x*x for x in data) / max(1, len(data)))


def db(value: float) -> float:
    return 20.0 * math.log10(max(abs(value), 1e-12))


def gain(audio: Audio, factor: float) -> Audio:
    return Audio(audio.sample_rate, audio.channels, [tuple(x * factor for x in frame) for frame in audio.frames])


def correlation(audio: Audio) -> float:
    if audio.channels != 2 or not audio.frames:
        return 1.0
    left = [f[0] for f in audio.frames]
    right = [f[1] for f in audio.frames]
    ml = sum(left) / len(left)
    mr = sum(right) / len(right)
    numerator = sum((l-ml)*(r-mr) for l, r in zip(left, right))
    dl = math.sqrt(sum((l-ml)**2 for l in left))
    dr = math.sqrt(sum((r-mr)**2 for r in right))
    return numerator / max(dl*dr, 1e-12)


def mid_side(audio: Audio) -> tuple[float, float]:
    if audio.channels != 2 or not audio.frames:
        return rms(mono(audio)), 0.0
    mid = [0.5 * (f[0] + f[1]) for f in audio.frames]
    side = [0.5 * (f[0] - f[1]) for f in audio.frames]
    return rms(mid), rms(side)


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


def _envelope(samples: list[float], block: int) -> list[float]:
    result: list[float] = []
    for start in range(0, len(samples), block):
        chunk = samples[start:start+block]
        if chunk:
            result.append(sum(abs(value) for value in chunk) / len(chunk))
    if result:
        mean = sum(result) / len(result)
        result = [value - mean for value in result]
    return result


def estimate_lag_frames(reference: Audio, candidate: Audio, max_seconds: float = 0.75) -> int:
    """Return candidate delay relative to reference in frames.

    Positive means candidate begins later and must be trimmed at its front.
    A low-rate amplitude envelope keeps this dependency-free and robust to
    nonlinear EQ/dynamics differences between enhancement chains.
    """
    if reference.sample_rate != candidate.sample_rate:
        raise ValueError("captures must have the same sample rate before alignment")
    target_envelope_rate = 500
    block = max(1, reference.sample_rate // target_envelope_rate)
    a = _envelope(mono(reference), block)
    b = _envelope(mono(candidate), block)
    length = min(len(a), len(b))
    if length < 8:
        return 0
    a = a[:length]
    b = b[:length]
    max_lag = min(int(max_seconds * reference.sample_rate / block), max(0, length // 3))

    best_lag = 0
    best_score = -2.0
    for lag in range(-max_lag, max_lag + 1):
        if lag >= 0:
            left = a[:length-lag]
            right = b[lag:length]
        else:
            shift = -lag
            left = a[shift:length]
            right = b[:length-shift]
        if len(left) < 8:
            continue
        numerator = sum(x*y for x, y in zip(left, right))
        denom = math.sqrt(sum(x*x for x in left) * sum(y*y for y in right))
        score = numerator / max(denom, 1e-12)
        if score > best_score:
            best_score = score
            best_lag = lag
    return best_lag * block


def align_and_trim(reference: Audio, candidate: Audio, max_seconds: float = 0.75) -> tuple[Audio, Audio, int]:
    if reference.sample_rate != candidate.sample_rate:
        raise ValueError("captures must have the same sample rate")
    if reference.channels != candidate.channels:
        raise ValueError("captures must have the same channel count")
    lag = estimate_lag_frames(reference, candidate, max_seconds)
    ref_start = max(0, -lag)
    cand_start = max(0, lag)
    count = min(len(reference.frames) - ref_start, len(candidate.frames) - cand_start)
    if count <= 0:
        raise ValueError("captures do not overlap after alignment")
    return (
        Audio(reference.sample_rate, reference.channels, reference.frames[ref_start:ref_start+count]),
        Audio(candidate.sample_rate, candidate.channels, candidate.frames[cand_start:cand_start+count]),
        lag,
    )


def metrics(audio: Audio) -> dict[str, float]:
    samples = flatten(audio)
    mono_samples = mono(audio)
    level = rms(samples)
    peak = max((abs(x) for x in samples), default=0.0)
    dc = sum(samples) / max(1, len(samples))
    mid, side = mid_side(audio)
    result = {
        "rms_dbfs": db(level),
        "peak_dbfs": db(peak),
        "crest_db": db(peak / max(level, 1e-12)),
        "dc": dc,
        "correlation": correlation(audio),
        "mid_rms_dbfs": db(mid),
        "side_rms_dbfs": db(side),
        "side_to_mid_db": db(side / max(mid, 1e-12)),
    }
    for freq in PROBES:
        result[f"probe_{freq:g}"] = db(goertzel(mono_samples, audio.sample_rate, freq))
    return result


def compare(reference: Audio, candidate: Audio, max_alignment_seconds: float = 0.75) -> dict[str, object]:
    reference, candidate, lag = align_and_trim(reference, candidate, max_alignment_seconds)
    ref_samples = flatten(reference)
    cand_samples = flatten(candidate)
    ref_rms = rms(ref_samples)
    cand_rms = rms(cand_samples)
    match_gain = ref_rms / max(cand_rms, 1e-12)
    candidate = gain(candidate, match_gain)

    ref = metrics(reference)
    cand = metrics(candidate)
    matched_candidate_samples = flatten(candidate)
    residual = [b-a for a, b in zip(ref_samples, matched_candidate_samples)]
    residual_rms = rms(residual)
    residual_relative_db = db(residual_rms / max(ref_rms, 1e-12))

    spectral_deltas = {
        f"{frequency:g}": cand[f"probe_{frequency:g}"] - ref[f"probe_{frequency:g}"]
        for frequency in PROBES
    }
    spectral_delta_rms = math.sqrt(sum(value*value for value in spectral_deltas.values()) / len(spectral_deltas))

    return {
        "alignment_lag_frames": lag,
        "alignment_lag_ms": 1000.0 * lag / reference.sample_rate,
        "candidate_match_gain_db": db(match_gain),
        "reference": ref,
        "candidate": cand,
        "delta": {key: cand[key] - ref[key] for key in ref if not key.startswith("probe_")},
        "spectral_delta_db": spectral_deltas,
        "spectral_delta_rms_db": spectral_delta_rms,
        "residual_relative_db": residual_relative_db,
        "compared_frames": len(reference.frames),
        "sample_rate": reference.sample_rate,
        "channels": reference.channels,
    }


def print_report(report: dict[str, object]) -> None:
    print(f"Alignment: {report['alignment_lag_frames']:+d} frames ({report['alignment_lag_ms']:+.2f} ms)")
    print(f"RMS-match gain applied to candidate: {report['candidate_match_gain_db']:+.2f} dB")
    ref = report["reference"]
    cand = report["candidate"]
    print("\nmetric                 reference     PulseFX      delta")
    print("-" * 61)
    keys = ["rms_dbfs", "peak_dbfs", "crest_db", "correlation", "side_to_mid_db", "dc"]
    for key in keys:
        print(f"{key:22s} {ref[key]:10.4f} {cand[key]:10.4f} {cand[key]-ref[key]:+10.4f}")
    print("\nSpectral probes after alignment + loudness match")
    for frequency, delta in report["spectral_delta_db"].items():
        print(f"{float(frequency):7.1f} Hz: {delta:+7.2f} dB")
    print(f"\nSpectral delta RMS: {report['spectral_delta_rms_db']:.3f} dB")
    print(f"Residual/null RMS relative to reference: {report['residual_relative_db']:.2f} dB")


def main() -> None:
    parser = argparse.ArgumentParser(description="Aligned, loudness-matched PulseFX A/B WAV comparison")
    parser.add_argument("reference", type=Path, help="reference capture, e.g. Boom 3D")
    parser.add_argument("candidate", type=Path, help="PulseFX capture")
    parser.add_argument("--max-alignment-ms", type=float, default=750.0, help="maximum absolute capture offset to search")
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    args = parser.parse_args()

    report = compare(
        read_wav(args.reference),
        read_wav(args.candidate),
        max_alignment_seconds=max(0.0, args.max_alignment_ms / 1000.0),
    )
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print_report(report)


if __name__ == "__main__":
    main()
