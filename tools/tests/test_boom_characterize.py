#!/usr/bin/env python3
from __future__ import annotations

import math
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import audio_lab  # noqa: E402
import boom_characterize  # noqa: E402
import boom_probe  # noqa: E402


class BoomCharacterizeTests(unittest.TestCase):
    def test_eq_characterization_recovers_simple_gain(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            root_path = Path(root)
            boom_probe.generate(root_path)
            source = audio_lab.read_wav(root_path / 'eq_multitone.wav')
            capture = audio_lab.gain(source, 0.5)
            metadata = boom_probe.generate(root_path)['files']['eq_multitone.wav']
            result = boom_characterize.characterize_eq(source, capture, metadata)
            gains = [point['gain_db'] for point in result['points'].values()]
            self.assertEqual(len(gains), 31)
            for gain_db in gains:
                self.assertAlmostEqual(gain_db, 20.0 * math.log10(0.5), delta=0.08)

    def test_dynamics_characterization_recovers_constant_gain(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            root_path = Path(root)
            manifest = boom_probe.generate(root_path)
            source = audio_lab.read_wav(root_path / 'dynamics_staircase.wav')
            capture = audio_lab.gain(source, 0.25)
            result = boom_characterize.characterize_dynamics(source, capture, manifest['files']['dynamics_staircase.wav'])
            self.assertEqual(len(result['points']), len(boom_probe.DYNAMICS_LEVELS_DBFS))
            for point in result['points']:
                self.assertAlmostEqual(point['gain_db'], 20.0 * math.log10(0.25), delta=0.05)

    def test_stereo_impulse_characterization_detects_crossfeed_and_delay(self) -> None:
        rate = boom_probe.RATE
        frames = [(0.0, 0.0)] * (rate * 3)
        event = round(0.5 * rate)
        frames[event + 48] = (0.8, 0.0)
        frames[event + 72] = (0.7, 0.2)
        capture = audio_lab.Audio(rate, 2, frames)
        metadata = {'events': [{'name': 'left', 'time_s': 0.5}]}
        result = boom_characterize.characterize_stereo_impulses(capture, metadata)['events']['left']
        self.assertAlmostEqual(result['left_delay_ms'], 1.0, delta=0.05)
        self.assertAlmostEqual(result['right_delay_ms'], 1.5, delta=0.05)
        self.assertAlmostEqual(result['interaural_peak_delay_ms'], 0.5, delta=0.05)
        self.assertLess(result['right_minus_left_peak_db'], -10.0)

    def test_identical_profiles_compare_to_zero(self) -> None:
        profile = {
            'measurements': {
                'eq_multitone': {
                    'points': {
                        '1000': {'gain_db': 1.25, 'phase_deg': -17.0},
                        '4000': {'gain_db': -0.5, 'phase_deg': 33.0},
                    },
                },
                'dynamics': {'points': [{'gain_db': 0.0}, {'gain_db': -1.0}]},
                'ambience': {'tail_windows': [{'rms_dbfs': -40.0}, {'rms_dbfs': -55.0}]},
                'surround_5_1': {'channels': [
                    {'right_minus_left_rms_db': -4.0, 'interaural_peak_delay_ms': 0.2},
                ]},
                'surround_7_1': {'channels': [
                    {'right_minus_left_rms_db': 3.0, 'interaural_peak_delay_ms': -0.3},
                ]},
                'stereo_impulses': {'events': {
                    'left': {'right_minus_left_peak_db': -8.0, 'interaural_peak_delay_ms': 0.25},
                    'right': {'right_minus_left_peak_db': 8.0, 'interaural_peak_delay_ms': -0.25},
                    'mono': {'right_minus_left_peak_db': 0.0, 'interaural_peak_delay_ms': 0.0},
                }},
            },
        }
        report = boom_characterize.compare_profiles(profile, profile)
        for key, value in report.items():
            if key.endswith('_rmse_db') or key.endswith('_rmse_deg') or key.endswith('_rmse_ms') or key == 'stereo_impulse_mixed_rmse':
                self.assertAlmostEqual(value, 0.0, places=9)

    def test_full_characterization_pipeline_accepts_synthetic_capture_directory(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            root_path = Path(root)
            probes = root_path / 'probes'
            captures = root_path / 'captures'
            captures.mkdir()
            manifest = boom_probe.generate(probes)

            for filename in ('stereo_impulses.wav', 'eq_multitone.wav', 'dynamics_staircase.wav', 'ambience_burst.wav'):
                shutil.copyfile(probes / filename, captures / filename)

            for filename in ('surround_5_1.wav', 'surround_7_1.wav'):
                metadata = manifest['files'][filename]
                duration = float(metadata['duration_s'])
                frames = [(0.0, 0.0)] * round(duration * boom_probe.RATE)
                for event in metadata['events']:
                    start = round((float(event['time_s']) + 0.012) * boom_probe.RATE)
                    pan = (int(event['channel_index']) % 5 - 2) / 2.0
                    left = 0.4 * (1.0 - 0.35 * pan)
                    right = 0.4 * (1.0 + 0.35 * pan)
                    frames[start] = (left, right)
                boom_probe.write_pcm16(captures / filename, 2, frames)

            profile = boom_characterize.characterize(probes, captures)
            self.assertEqual(len(profile['measurements']['eq_multitone']['points']), 31)
            self.assertEqual(len(profile['measurements']['surround_5_1']['channels']), 6)
            self.assertEqual(len(profile['measurements']['surround_7_1']['channels']), 8)
            zero = boom_characterize.compare_profiles(profile, profile)
            self.assertAlmostEqual(zero['eq_gain_rmse_db'], 0.0, places=9)
            self.assertAlmostEqual(zero['surround_pan_rmse_db'], 0.0, places=9)


if __name__ == '__main__':
    unittest.main()
