#!/usr/bin/env python3
from __future__ import annotations

import math
import random
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import audio_lab  # noqa: E402
from test_boom_characterize import BoomCharacterizeTests  # noqa: E402,F401
from test_boom_probe import BoomProbeTests  # noqa: E402,F401


class AudioLabTests(unittest.TestCase):
    def make_stereo(self, frames: list[float], rate: int = 48000) -> audio_lab.Audio:
        return audio_lab.Audio(rate, 2, [(value, value * 0.7) for value in frames])

    def test_alignment_finds_candidate_delay(self) -> None:
        rng = random.Random(1234)
        source = [rng.uniform(-0.7, 0.7) if i % 17 == 0 else rng.uniform(-0.06, 0.06) for i in range(24000)]
        delay = 1440
        reference = self.make_stereo(source)
        candidate = self.make_stereo([0.0] * delay + source)
        _, _, lag = audio_lab.align_and_trim(reference, candidate)
        self.assertLessEqual(abs(lag - delay), 480)

    def test_loudness_match_removes_simple_gain_difference_but_reports_bias(self) -> None:
        source = [0.2 * math.sin(2 * math.pi * 997 * i / 48000) for i in range(16000)]
        reference = self.make_stereo(source)
        candidate = audio_lab.gain(reference, 0.5)
        report = audio_lab.compare(reference, candidate, max_alignment_seconds=0.0)
        self.assertAlmostEqual(report['candidate_match_gain_db'], 20 * math.log10(2.0), places=3)
        self.assertLess(report['spectral_delta_rms_db'], 0.001)
        self.assertLess(report['residual_relative_db'], -100.0)
        self.assertIn('loudness_bias', report['integrity']['violations'])

    def test_exact_copy_passes_integrity_gate(self) -> None:
        rate = 48000
        frames = []
        for i in range(rate):
            value = 0.18 * math.sin(2 * math.pi * 440 * i / rate)
            side = 0.035 * math.sin(2 * math.pi * 977 * i / rate)
            frames.append((value + side, value - side))
        audio = audio_lab.Audio(rate, 2, frames)
        report = audio_lab.compare(audio, audio, max_alignment_seconds=0.0)
        self.assertTrue(report['integrity']['passed'])
        self.assertGreater(report['mid_waveform_correlation'], 0.999999)
        self.assertGreater(report['side_waveform_correlation'], 0.999999)

    def test_stereo_width_metric_detects_added_side_energy(self) -> None:
        rate = 48000
        narrow_frames = []
        wide_frames = []
        for i in range(12000):
            mid = 0.2 * math.sin(2 * math.pi * 440 * i / rate)
            side = 0.08 * math.sin(2 * math.pi * 733 * i / rate)
            narrow_frames.append((mid, mid))
            wide_frames.append((mid + side, mid - side))
        narrow = audio_lab.metrics(audio_lab.Audio(rate, 2, narrow_frames))
        wide = audio_lab.metrics(audio_lab.Audio(rate, 2, wide_frames))
        self.assertGreater(wide['side_to_mid_db'], narrow['side_to_mid_db'] + 40.0)
        self.assertLess(wide['correlation'], narrow['correlation'])

    def test_channel_imbalance_trips_integrity_gate(self) -> None:
        rate = 48000
        reference_frames = []
        candidate_frames = []
        for i in range(24000):
            value = 0.22 * math.sin(2 * math.pi * 523 * i / rate)
            reference_frames.append((value, value))
            candidate_frames.append((value * 1.25, value * 0.75))
        report = audio_lab.compare(
            audio_lab.Audio(rate, 2, reference_frames),
            audio_lab.Audio(rate, 2, candidate_frames),
            max_alignment_seconds=0.0,
        )
        self.assertGreater(abs(report['delta']['channel_balance_db']), 3.0)
        self.assertIn('channel_balance', report['integrity']['violations'])

    def test_transient_flattening_is_detected(self) -> None:
        rate = 48000
        reference_frames = []
        candidate_frames = []
        for i in range(rate):
            body = 0.035 * math.sin(2 * math.pi * 330 * i / rate)
            impulse = 0.85 if i % 480 == 0 else 0.0
            ref = body + impulse
            cand = body + (0.16 if impulse else 0.0)
            reference_frames.append((ref, ref))
            candidate_frames.append((cand, cand))
        report = audio_lab.compare(
            audio_lab.Audio(rate, 2, reference_frames),
            audio_lab.Audio(rate, 2, candidate_frames),
            max_alignment_seconds=0.0,
        )
        self.assertLess(report['delta']['transient_crest_p95_db'], -3.0)
        self.assertIn('transient_crest_loss', report['integrity']['violations'])

    def test_short_term_range_detects_overcompression(self) -> None:
        rate = 48000
        reference_frames = []
        candidate_frames = []
        for i in range(rate * 2):
            loud = i >= rate
            reference_amp = 0.42 if loud else 0.035
            candidate_amp = 0.24 if loud else 0.09
            ref = reference_amp * math.sin(2 * math.pi * 440 * i / rate)
            cand = candidate_amp * math.sin(2 * math.pi * 440 * i / rate)
            reference_frames.append((ref, ref))
            candidate_frames.append((cand, cand))
        report = audio_lab.compare(
            audio_lab.Audio(rate, 2, reference_frames),
            audio_lab.Audio(rate, 2, candidate_frames),
            max_alignment_seconds=0.0,
        )
        self.assertLess(
            report['candidate']['short_term_range_db'],
            report['reference']['short_term_range_db'] - 8.0,
        )

    def test_compare_rejects_mismatched_format(self) -> None:
        a = audio_lab.Audio(48000, 2, [(0.0, 0.0)] * 100)
        b = audio_lab.Audio(44100, 2, [(0.0, 0.0)] * 100)
        with self.assertRaises(ValueError):
            audio_lab.compare(a, b)


if __name__ == '__main__':
    unittest.main()
