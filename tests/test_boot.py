"""Actual boot controller/renderer with deterministic 100 Hz queue/timer stubs."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BootTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='pos-boot-')
        cls.runner = Path(cls.temp.name) / 'boot'
        command = ['gcc', '-std=c11', '-D_POSIX_C_SOURCE=200809L',
                   '-Wall', '-Wextra', '-Werror',
                   '-ffunction-sections', '-fdata-sections', '-fsanitize=undefined',
                   '-fno-sanitize-recover=all']
        if os.environ.get('POS_TEST_ASAN'):
            command += ['-fsanitize=address', '-fno-omit-frame-pointer', '-no-pie']
        for path in ('tests/boot_stubs', 'tests/stubs', 'components/pos_device',
                     'components/pos_device/include', 'components/pos_core/include'):
            command += ['-I', str(ROOT / path)]
        for path in ('tests/boot_runner.c', 'components/pos_device/pos_boot.c',
                     'components/pos_core/pos_core.c', 'components/pos_core/pos_render.c'):
            command.append(str(ROOT / path))
        subprocess.run(command + ['-Wl,--gc-sections', '-o', str(cls.runner)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_boot(self, mode):
        result = subprocess.run([str(self.runner), mode], capture_output=True,
                                text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_complete_intro_has_half_the_previous_nominal_duration(self):
        self.run_boot('complete')

    def test_any_press_skips_during_slides_fade_and_final_logo(self):
        self.run_boot('skip')

    def test_release_events_do_not_skip_or_extend_the_intro(self):
        self.run_boot('release')

    def test_skip_input_does_not_activate_the_next_screen_or_latch_home(self):
        self.run_boot('consume')

    def test_slow_drawing_skips_overdue_frames_and_still_presents_the_logo(self):
        self.run_boot('slow')

    def test_missing_or_failed_display_exits_cleanly(self):
        self.run_boot('display')

    def test_initial_scheduling_delay_keeps_the_black_frame_and_backlight(self):
        self.run_boot('first-frame')
