#!/usr/bin/env python3
"""Characterize Boom/PulseFX captures produced from boom_probe.py stimuli.

The output is a machine-readable behavioral profile. It measures only observable
I/O behavior: latency, frequency/phase response, stereo matrix, dynamics curve,
ambience decay, and 5.1/7.1 virtual-speaker localization.
"""
from __future__ import annotations

import argparse
import cmath
import json
import math
from pathlib import Path
from typing import Any

import audio_lab
import boom_probe


def _db(value: float) -> float:
    return 20.0 * math.log10(max(abs(value), 1e-12))


def _phase_degrees(value: complex) -> float:
    return math.degrees(cmath.phase(value))


def _wrap_degrees(value: float) -> float:
    return (value + 180.0) % 360.0 - 180.0


def _complex_tone(samples: list[float], rate: int, frequency: float) -> complex:
    """Return the complex DFT coefficient at one exact frequency."""
    if not samples or frequency <= 0.0 or frequency >= rate * 0.5:
        return 0j
    step = -2.0j * math.pi * frequency / rate
    oscillator = cmath.exp(step)
    phasor = 1.0 + 0.0j
    total = 0.0 + 0.0j
    for sample in samples:
        total += sample * phasor
        phasor *= oscillator
    return total / len(samples)


def _slice(audio: audio_lab.Audio, start_s: float, end_s: float) -> audio_lab.Audio:
    start = max(0, round(start_s * audio.sample_rate))
    end = min(len(audio.frames), round(end_s * audio.sample_rate))
    if end <= start:
        return audio_lab.Audio(audio.sample_rate, audio.channels, [])
    return audio_lab.Audio(audio.sample_rate, audio.channels, audio.frames[start:end])


def _channel(audio: audio_lab.Audio, index: int) -> list[float]:
    if index >= audio.channels:
        return []
    return [frame[index] for frame in audio.frames]


def _peak_index(samples: list[float]) -> tuple[int, float]:
    if not samples:
        return 0, 0.0
    index = max(range(len(samples)), key=lambda i: abs(samples[i]))
    return index, abs(samples[index])


def _rms(samples: list[float]) -> float:
    return audio_lab.rms(samples)


def characterize_stereo_impulses(capture: audio_lab.Audio, metadata: dict[str, Any]) -> dict[str, Any]:
    if capture.channels != 2:
        raise ValueError('stereo_impulses capture must be stereo')
    events: dict[str, Any] = {}
    for event in metadata['events']:
        event_time = float(event['time_s'])
        window = _slice(capture, max(0.0, event_time - 0.05), event_time + 0.35)
        left = _channel(window, 0)
        right = _channel(window, 1)
        left_index, left_peak = _peak_index(left)
        right_index, right_peak = _peak_index(right)
        window_start = max(0.0, event_time - 0.05)
        left_time = window_start + left_index / capture.sample_rate
        right_time = window_start + right_index / capture.sample_rate
        events[event['name']] = {
            'left_peak_dbfs': _db(left_peak),
            'right_peak_dbfs': _db(right_peak),
            'left_delay_ms': 1000.0 * (left_time - event_time),
            'right_delay_ms': 1000.0 * (right_time - event_time),
            'right_minus_left_peak_db': _db(right_peak / max(left_peak, 1e-12)),
            'interaural_peak_delay_ms': 1000.0 * (right_time - left_time),
        }
    return {'events': events}


def characterize_eq(source: audio_lab.Audio, capture: audio_lab.Audio, metadata: dict[str, Any]) -> dict[str, Any]:
    source, capture, lag = audio_lab.align_and_trim(source, capture, max_seconds=1.0)
    guard_s = 1.0
    end_s = min(len(source.frames), len(capture.frames)) / source.sample_rate - guard_s
    if end_s <= guard_s:
        raise ValueError('eq_multitone capture is too short')
    source_mono = audio_lab.mono(_slice(source, guard_s, end_s))
    capture_mono = audio_lab.mono(_slice(capture, guard_s, end_s))
    points: dict[str, Any] = {}
    for frequency in metadata['frequencies_hz']:
        f = float(frequency)
        src = _complex_tone(source_mono, source.sample_rate, f)
        out = _complex_tone(capture_mono, capture.sample_rate, f)
        transfer = out / src if abs(src) > 1e-12 else 0j
        points[f'{f:g}'] = {
            'gain_db': _db(abs(transfer)),
            'phase_deg': _wrap_degrees(_phase_degrees(transfer)),
        }
    return {
        'latency_frames': lag,
        'latency_ms': 1000.0 * lag / source.sample_rate,
        'points': points,
    }


def characterize_dynamics(source: audio_lab.Audio, capture: audio_lab.Audio, metadata: dict[str, Any]) -> dict[str, Any]:
    lag = audio_lab.estimate_lag_frames(source, capture, max_seconds=1.0)
    lag_s = lag / source.sample_rate
    guard = float(metadata.get('analysis_guard_s', 0.2))
    points = []
    for segment in metadata['segments']:
        start = float(segment['start_s']) + guard
        end = float(segment['end_s']) - guard
        source_slice = _slice(source, start, end)
        capture_slice = _slice(capture, start + lag_s, end + lag_s)
        source_level = _rms(audio_lab.flatten(source_slice))
        output_level = _rms(audio_lab.flatten(capture_slice))
        points.append({
            'nominal_input_dbfs': float(segment['level_dbfs']),
            'measured_input_dbfs': _db(source_level),
            'output_dbfs': _db(output_level),
            'gain_db': _db(output_level / max(source_level, 1e-12)),
        })

    slopes = []
    for previous, current in zip(points, points[1:]):
        input_delta = current['measured_input_dbfs'] - previous['measured_input_dbfs']
        output_delta = current['output_dbfs'] - previous['output_dbfs']
        slopes.append({
            'from_dbfs': previous['nominal_input_dbfs'],
            'to_dbfs': current['nominal_input_dbfs'],
            'output_per_input_db': output_delta / max(input_delta, 1e-9),
        })
    return {'latency_ms': 1000.0 * lag_s, 'points': points, 'local_slopes': slopes}


def characterize_ambience(source: audio_lab.Audio, capture: audio_lab.Audio, metadata: dict[str, Any]) -> dict[str, Any]:
    lag = audio_lab.estimate_lag_frames(source, capture, max_seconds=1.0)
    lag_s = lag / source.sample_rate
    tail_start = float(metadata['burst_end_s']) + lag_s
    windows_ms = ((0, 50), (50, 100), (100, 200), (200, 500), (500, 1000), (1000, 2000), (2000, 3000))
    windows = []
    for start_ms, end_ms in windows_ms:
        segment = _slice(capture, tail_start + start_ms / 1000.0, tail_start + end_ms / 1000.0)
        windows.append({
            'start_ms': start_ms,
            'end_ms': end_ms,
            'rms_dbfs': _db(_rms(audio_lab.flatten(segment))),
            'left_rms_dbfs': _db(_rms(_channel(segment, 0))),
            'right_rms_dbfs': _db(_rms(_channel(segment, 1))),
        })
    return {'latency_ms': 1000.0 * lag_s, 'tail_windows': windows}


def characterize_surround(capture: audio_lab.Audio, metadata: dict[str, Any]) -> dict[str, Any]:
    if capture.channels != 2:
        raise ValueError('surround marker capture must be stereo')
    results = []
    for event in metadata['events']:
        event_time = float(event['time_s'])
        # Marker spacing is 550 ms. A 350 ms search window allows large plugin
        # latency while keeping neighboring channel markers isolated.
        window = _slice(capture, event_time, event_time + 0.35)
        left = _channel(window, 0)
        right = _channel(window, 1)
        left_index, left_peak = _peak_index(left)
        right_index, right_peak = _peak_index(right)
        left_rms = _rms(left)
        right_rms = _rms(right)
        results.append({
            'channel': event['channel'],
            'channel_index': int(event['channel_index']),
            'left_rms_dbfs': _db(left_rms),
            'right_rms_dbfs': _db(right_rms),
            'right_minus_left_rms_db': _db(right_rms / max(left_rms, 1e-12)),
            'left_peak_delay_ms': 1000.0 * left_index / capture.sample_rate,
            'right_peak_delay_ms': 1000.0 * right_index / capture.sample_rate,
            'interaural_peak_delay_ms': 1000.0 * (right_index - left_index) / capture.sample_rate,
        })
    return {'channels': results}


def characterize(probe_dir: Path, capture_dir: Path) -> dict[str, Any]:
    manifest = json.loads((probe_dir / 'manifest.json').read_text(encoding='utf-8'))
    required = manifest['files']
    result: dict[str, Any] = {'schema': 1, 'sample_rate': manifest['sample_rate'], 'measurements': {}}

    def capture(name: str) -> audio_lab.Audio:
        path = capture_dir / name
        if not path.exists():
            raise FileNotFoundError(f'missing capture: {path}')
        return audio_lab.read_wav(path)

    stereo_meta = required['stereo_impulses.wav']
    result['measurements']['stereo_impulses'] = characterize_stereo_impulses(capture('stereo_impulses.wav'), stereo_meta)

    eq_source = audio_lab.read_wav(probe_dir / 'eq_multitone.wav')
    result['measurements']['eq_multitone'] = characterize_eq(eq_source, capture('eq_multitone.wav'), required['eq_multitone.wav'])

    dynamics_source = audio_lab.read_wav(probe_dir / 'dynamics_staircase.wav')
    result['measurements']['dynamics'] = characterize_dynamics(dynamics_source, capture('dynamics_staircase.wav'), required['dynamics_staircase.wav'])

    ambience_source = audio_lab.read_wav(probe_dir / 'ambience_burst.wav')
    result['measurements']['ambience'] = characterize_ambience(ambience_source, capture('ambience_burst.wav'), required['ambience_burst.wav'])

    result['measurements']['surround_5_1'] = characterize_surround(capture('surround_5_1.wav'), required['surround_5_1.wav'])
    result['measurements']['surround_7_1'] = characterize_surround(capture('surround_7_1.wav'), required['surround_7_1.wav'])
    return result


def _rmse(values: list[float]) -> float:
    return math.sqrt(sum(value * value for value in values) / max(1, len(values)))


def compare_profiles(reference: dict[str, Any], candidate: dict[str, Any]) -> dict[str, Any]:
    ref_m = reference['measurements']
    cand_m = candidate['measurements']

    eq_gain = []
    eq_phase = []
    for frequency, ref_point in ref_m['eq_multitone']['points'].items():
        cand_point = cand_m['eq_multitone']['points'][frequency]
        eq_gain.append(cand_point['gain_db'] - ref_point['gain_db'])
        eq_phase.append(_wrap_degrees(cand_point['phase_deg'] - ref_point['phase_deg']))

    dynamics = []
    for ref_point, cand_point in zip(ref_m['dynamics']['points'], cand_m['dynamics']['points']):
        dynamics.append(cand_point['gain_db'] - ref_point['gain_db'])

    ambience = []
    for ref_window, cand_window in zip(ref_m['ambience']['tail_windows'], cand_m['ambience']['tail_windows']):
        ambience.append(cand_window['rms_dbfs'] - ref_window['rms_dbfs'])

    spatial_pan = []
    spatial_delay = []
    for key in ('surround_5_1', 'surround_7_1'):
        for ref_channel, cand_channel in zip(ref_m[key]['channels'], cand_m[key]['channels']):
            spatial_pan.append(cand_channel['right_minus_left_rms_db'] - ref_channel['right_minus_left_rms_db'])
            spatial_delay.append(cand_channel['interaural_peak_delay_ms'] - ref_channel['interaural_peak_delay_ms'])

    impulse = []
    for name, ref_event in ref_m['stereo_impulses']['events'].items():
        cand_event = cand_m['stereo_impulses']['events'][name]
        impulse.append(cand_event['right_minus_left_peak_db'] - ref_event['right_minus_left_peak_db'])
        impulse.append(cand_event['interaural_peak_delay_ms'] - ref_event['interaural_peak_delay_ms'])

    return {
        'schema': 1,
        'eq_gain_rmse_db': _rmse(eq_gain),
        'eq_phase_rmse_deg': _rmse(eq_phase),
        'dynamics_gain_rmse_db': _rmse(dynamics),
        'ambience_tail_rmse_db': _rmse(ambience),
        'surround_pan_rmse_db': _rmse(spatial_pan),
        'surround_interaural_delay_rmse_ms': _rmse(spatial_delay),
        'stereo_impulse_mixed_rmse': _rmse(impulse),
        'details': {
            'eq_gain_delta_db': eq_gain,
            'eq_phase_delta_deg': eq_phase,
            'dynamics_gain_delta_db': dynamics,
            'ambience_tail_delta_db': ambience,
            'surround_pan_delta_db': spatial_pan,
            'surround_delay_delta_ms': spatial_delay,
        },
    }


def _print_profile_summary(profile: dict[str, Any]) -> None:
    measurements = profile['measurements']
    print(f"EQ latency: {measurements['eq_multitone']['latency_ms']:+.2f} ms")
    print('Dynamics gain curve:')
    for point in measurements['dynamics']['points']:
        print(f"  {point['nominal_input_dbfs']:>6.1f} dBFS -> gain {point['gain_db']:+6.2f} dB")
    print('5.1 pan (R-L RMS):')
    for point in measurements['surround_5_1']['channels']:
        print(f"  {point['channel']:>3s}: {point['right_minus_left_rms_db']:+7.2f} dB, ITD {point['interaural_peak_delay_ms']:+6.3f} ms")


def _print_comparison(report: dict[str, Any]) -> None:
    print('Behavioral delta summary (lower is closer)')
    print(f"  EQ gain RMSE:              {report['eq_gain_rmse_db']:.3f} dB")
    print(f"  EQ phase RMSE:             {report['eq_phase_rmse_deg']:.3f} deg")
    print(f"  Dynamics gain RMSE:        {report['dynamics_gain_rmse_db']:.3f} dB")
    print(f"  Ambience-tail RMSE:        {report['ambience_tail_rmse_db']:.3f} dB")
    print(f"  Surround pan RMSE:         {report['surround_pan_rmse_db']:.3f} dB")
    print(f"  Surround ITD RMSE:         {report['surround_interaural_delay_rmse_ms']:.4f} ms")


def main() -> None:
    parser = argparse.ArgumentParser(description='Characterize and compare Boom/PulseFX black-box captures')
    sub = parser.add_subparsers(dest='command', required=True)

    characterize_parser = sub.add_parser('characterize')
    characterize_parser.add_argument('probe_dir', type=Path)
    characterize_parser.add_argument('capture_dir', type=Path)
    characterize_parser.add_argument('--output', type=Path)

    compare_parser = sub.add_parser('compare')
    compare_parser.add_argument('reference_profile', type=Path)
    compare_parser.add_argument('candidate_profile', type=Path)
    compare_parser.add_argument('--output', type=Path)

    args = parser.parse_args()
    if args.command == 'characterize':
        profile = characterize(args.probe_dir, args.capture_dir)
        if args.output:
            args.output.write_text(json.dumps(profile, indent=2, sort_keys=True), encoding='utf-8')
        else:
            _print_profile_summary(profile)
    else:
        reference = json.loads(args.reference_profile.read_text(encoding='utf-8'))
        candidate = json.loads(args.candidate_profile.read_text(encoding='utf-8'))
        report = compare_profiles(reference, candidate)
        if args.output:
            args.output.write_text(json.dumps(report, indent=2, sort_keys=True), encoding='utf-8')
        else:
            _print_comparison(report)


if __name__ == '__main__':
    main()
