"""Exercise the actual device capture task with bounded, timed host primitives."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class AudioCaptureTests(unittest.TestCase):
    def test_capture_survives_ui_sd_stalls_and_cleans_up_on_errors(self):
        with tempfile.TemporaryDirectory(prefix='pos-audio-capture-') as folder:
            runner = Path(folder) / 'capture'
            command = ['gcc', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall',
                       '-Wextra', '-Werror', '-fsanitize=undefined',
                       '-fno-sanitize-recover=all', '-pthread']
            for path in ('tests/audio_stubs', 'components/pos_device/include',
                         'components/pos_core/include', 'components/pos_install/include'):
                command += ['-I', str(ROOT / path)]
            command += [str(ROOT / 'tests/audio_capture_runner.c'),
                        str(ROOT / 'components/pos_device/pos_audio_device.c'),
                        '-o', str(runner)]
            subprocess.run(command, check=True, capture_output=True, text=True)
            result = subprocess.run([str(runner)], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
