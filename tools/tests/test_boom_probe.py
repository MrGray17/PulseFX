#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import boom_probe  # noqa: E402


class BoomProbeTests(unittest.TestCase):
    def test_generate_writes_complete_deterministic_probe_set(self) -> None:
        with tempfile.TemporaryDirectory() as first_root, tempfile.TemporaryDirectory() as second_root:
            first = Path(first_root)
            second = Path(second_root)
            manifest_a = boom_probe.generate(first)
            manifest_b = boom_probe.generate(second)

            self.assertEqual(manifest_a, manifest_b)
            self.assertEqual(len(manifest_a['files']), 7)
            self.assertEqual(manifest_a['sample_rate'], 48000)
            self.assertEqual(manifest_a['sample_format'], 'PCM16')

            for filename, metadata in manifest_a['files'].items():
                path_a = first / filename
                path_b = second / filename
                self.assertTrue(path_a.exists())
                self.assertEqual(hashlib.sha256(path_a.read_bytes()).digest(), hashlib.sha256(path_b.read_bytes()).digest())
                with wave.open(str(path_a), 'rb') as wav:
                    self.assertEqual(wav.getframerate(), 48000)
                    self.assertEqual(wav.getsampwidth(), 2)
                    self.assertEqual(wav.getnchannels(), metadata['channels'])
                    self.assertEqual(wav.getnframes(), metadata['frames'])

            persisted = json.loads((first / 'manifest.json').read_text(encoding='utf-8'))
            self.assertEqual(persisted, manifest_a)

    def test_probe_metadata_covers_measurement_matrix(self) -> None:
        with tempfile.TemporaryDirectory() as root:
            manifest = boom_probe.generate(Path(root))
            self.assertEqual(len(manifest['files']['eq_multitone.wav']['frequencies_hz']), 31)
            self.assertEqual(len(manifest['files']['dynamics_staircase.wav']['segments']), len(boom_probe.DYNAMICS_LEVELS_DBFS))
            self.assertEqual(manifest['files']['surround_5_1.wav']['channel_order'], ['FL', 'FR', 'FC', 'LFE', 'SL', 'SR'])
            self.assertEqual(manifest['files']['surround_7_1.wav']['channel_order'], ['FL', 'FR', 'FC', 'LFE', 'BL', 'BR', 'SL', 'SR'])


if __name__ == '__main__':
    unittest.main()
