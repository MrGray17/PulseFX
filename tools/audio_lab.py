#!/usr/bin/env python3
"""PulseFX reference-matching audio lab.

Compares two mono/stereo PCM WAV captures after automatic time alignment and
RMS level matching. The analyzer deliberately uses only Python's standard
library so it can run on a clean Windows installation and in CI.

The tool is meant for controlled A/B captures of the *same source*. It reports
objective deltas rather than claiming that one processing chain sounds better.
In addition to spectral/stereo matching, it checks clipping, channel balance,
short-term dynamics, and transient crest preservation so a processor cannot
look "close" while flattening the music.
"""
from __future__ import annotations

import argparse
import json
import math
import struct
import sys
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


@dataclass(frozen=True)
class IntegrityThresholds:
    max_loudness_bias_db: float = 1.0
    max_added_clip_fraction: float = 1.0e-4
    max_channel_balance_delta_db: float = 0.75
    max_transient_crest_loss_db: float = 2.5
    max_dc_abs: float = 0.01


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


def percentile(values: list[float], fraction: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    position = min(1.0, max(0.0, fraction)) * (len(ordered) - 1)
    lower = int(math.floor(position))
    upper = int(math.ceil(position))
    if lower == upper:
        return ordered[lower]
    blend = position - lower
    return ordered[lower] * (1.0 - blend) + ordered[upper] * blend


def gain(audio: Audio, factor: float) -> Audio:
    return Audio(audio.sample_rate, audio.channels, [tuple(x * factor for x in frame) for frame in audio.frames])


def _pearson(a: list[float], b: list[float]) -> float:
    count = min(len(a), len(b))
    if count == 0:
        return 1.0
    a = a[:count]
    b = b[:count]
    ma = sum(a) / count
    mb = sum(b) / count
    numerator = sum((x-ma)*(y-mb) for x, y in zip(a, b))
    da = math.sqrt(sum((x-ma)**2 for x in a))
    dbv = math.sqrt(sum((y-mb)**2 for y in b))
    return numerator / max(da*dbv, 1e-12)


def correlation(audio: Audio) -> float:
    if audio.channels != 2 or not audio.frames:
        return 1.0
    return _pearson([f[0] for f in audio.frames], [f[1] for f in audio.frames])


def mid_side(audio: Audio) -> tuple[float, float]:
    if audio.channels != 2 or not audio.frames:
        return rms(mono(audio)), 0.0
    mid = [0.5 * (f[0] + f[1]) for f in audio.frames]
    side = [0.5 * (f[0] - f[1]) for f in audio.frames]
    return rms(mid), rms(side)


def channel_balance_db(audio: Audio) -> float:
    if audio.channels != 2 or not audio.frames:
        return 0.0
    left = rms(f[0] for f in audio.frames)
    right = rms(f[1] for f in audio.frames)
    return db(left / max(right, 1e-12))


def _window_levels(samples: list[float], window: int, hop: int) -> list[float]:
    if not samples:
        return []
    window = max(1, window)
    hop = max(1, hop)
    result: list[float] = []
    for start in range(0, max(1, len(samples) - window + 1), hop):
        chunk = samples[start:start+window]
        if chunk:
            result.append(db(rms(chunk)))
    if not result:
        result.append(db(rms(samples)))
    return result


def _window_crest(samples: list[float], window: int, hop: int) -> list[float]:
    if not samples:
        return []
    window = max(1, window)
    hop = max(1, hop)
    result: list[float] = []
    for start in range(0, max(1, len(samples) - window + 1), hop):
        chunk = samples[start:start+window]
        if not chunk:
            continue
        level = rms(chunk)
        peak = max(abs(value) for value in chunk)
        if level > 1e-9:
            result.append(db(peak / level))
    return result


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

    short_levels = _window_levels(
        mono_samples,
        max(1, int(audio.sample_rate * 0.400)),
        max(1, int(audio.sample_rate * 0.100)),
    )
    transient_crest = _window_crest(
        mono_samples,
        max(1, int(audio.sample_rate * 0.010)),
        max(1, int(audio.sample_rate * 0.005)),
    )
    clip_fraction = sum(1 for x in samples if abs(x) >= 0.999) / max(1, len(samples))

    result = {
        "rms_dbfs": db(level),
        "peak_dbfs": db(peak),
        "crest_db": db(peak / max(level, 1e-12)),
        "dc": dc,
        "clip_fraction": clip_fraction,
        "correlation": correlation(audio),
        "channel_balance_db": channel_balance_db(audio),
        "mid_rms_dbfs": db(mid),
        "side_rms_dbfs": db(side),
        "side_to_mid_db": db(side / max(mid, 1e-12)),
        "short_term_p10_dbfs": percentile(short_levels, 0.10),
        "short_term_p50_dbfs": percentile(short_levels, 0.50),
        "short_term_p95_dbfs": percentile(short_levels, 0.95),
        "short_term_range_db": percentile(short_levels, 0.95) - percentile(short_levels, 0.10),
        "transient_crest_p50_db": percentile(transient_crest, 0.50),
        "transient_crest_p95_db": percentile(transient_crest, 0.95),
    }
    for freq in PROBES:
        result[f"probe_{freq:g}"] = db(goertzel(mono_samples, audio.sample_rate, freq))
    return result


def _stereo_waveform_correlations(reference: Audio, candidate: Audio) -> dict[str, float]:
    ref_mid = mono(reference)
    cand_mid = mono(candidate)
    result = {"mid_waveform_correlation": _pearson(ref_mid, cand_mid)}
    if reference.channels != 2:
        result["side_waveform_correlation"] = 1.0
        return result
    ref_side = [0.5 * (f[0] - f[1]) for f in reference.frames]
    cand_side = [0.5 * (f[0] - f[1]) for f in candidate.frames]
    if rms(ref_side) < 1e-9 and rms(cand_side) < 1e-9:
        result["side_waveform_correlation"] = 1.0
    elif rms(ref_side) < 1e-9 or rms(cand_side) < 1e-9:
        result["side_waveform_correlation"] = 0.0
    else:
        result["side_waveform_correlation"] = _pearson(ref_side, cand_side)
    return result


def evaluate_integrity(report: dict[str, object], thresholds: IntegrityThresholds | None = None) -> dict[str, object]:
    thresholds = thresholds or IntegrityThresholds()
    reference = report["reference"]
    candidate = report["candidate"]
    delta = report["delta"]
    violations: list[str] = []

    if abs(float(report["candidate_match_gain_db"])) > thresholds.max_loudness_bias_db:
        violations.append("loudness_bias")
    if float(candidate["clip_fraction"]) - float(reference["clip_fraction"]) > thresholds.max_added_clip_fraction:
        violations.append("added_clipping")
    if abs(float(delta["channel_balance_db"])) > thresholds.max_channel_balance_delta_db:
        violations.append("channel_balance")
    if float(delta["transient_crest_p95_db"]) < -thresholds.max_transient_crest_loss_db:
        violations.append("transient_crest_loss")
    if abs(float(candidate["dc"])) > thresholds.max_dc_abs:
        violations.append("dc_offset")

    return {"passed": not violations, "violations": violations}


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

    report: dict[str, object] = {
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
    report.update(_stereo_waveform_correlations(reference, candidate))
    report["integrity"] = evaluate_integrity(report)
    return report


def print_report(report: dict[str, object]) -> None:
    print(f"Alignment: {report['alignment_lag_frames']:+d} frames ({report['alignment_lag_ms']:+.2f} ms)")
    print(f"RMS-match gain applied to candidate: {report['candidate_match_gain_db']:+.2f} dB")
    ref = report["reference"]
    cand = report["candidate"]
    print("\nmetric                       reference     PulseFX      delta")
    print("-" * 67)
    keys = [
        "rms_dbfs", "peak_dbfs", "crest_db", "transient_crest_p95_db",
        "short_term_range_db", "correlation", "side_to_mid_db",
        "channel_balance_db", "clip_fraction", "dc",
    ]
    for key in keys:
        print(f"{key:28s} {ref[key]:10.4f} {cand[key]:10.4f} {cand[key]-ref[key]:+10.4f}")
    print(f"\nAligned mid waveform correlation:  {report['mid_waveform_correlation']:.6f}")
    print(f"Aligned side waveform correlation: {report['side_waveform_correlation']:.6f}")
    print("\nSpectral probes after alignment + RMS match")
    for frequency, delta in report["spectral_delta_db"].items():
        print(f"{float(frequency):7.1f} Hz: {delta:+7.2f} dB")
    print(f"\nSpectral delta RMS: {report['spectral_delta_rms_db']:.3f} dB")
    print(f"Residual/null RMS relative to reference: {report['residual_relative_db']:.2f} dB")
    integrity = report["integrity"]
    print(f"Integrity gate: {'PASS' if integrity['passed'] else 'FAIL'}")
    if integrity["violations"]:
        print("Violations: " + ", ".join(integrity["violations"]))


def main() -> None:
    parser = argparse.ArgumentParser(description="Aligned, level-matched PulseFX A/B WAV comparison")
    parser.add_argument("reference", type=Path, help="reference capture, e.g. Boom 3D")
    parser.add_argument("candidate", type=Path, help="PulseFX capture")
    parser.add_argument("--max-alignment-ms", type=float, default=750.0, help="maximum absolute capture offset to search")
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    parser.add_argument("--fail-on-integrity", action="store_true", help="exit non-zero on clipping, balance, transient, DC, or loudness-bias violations")
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
    if args.fail_on_integrity and not report["integrity"]["passed"]:
        sys.exit(2)


if __name__ == "__main__":
    main()
