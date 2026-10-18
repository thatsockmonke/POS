from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class VisualTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='pos-visual-')
        cls.runner=Path(cls.temp.name)/'visual'
        sources=['tests/visual_runner.c']+['components/pos_core/'+n for n in ('pos_core.c','pos_render.c','pos_background.c','pos_visual.c')]
        subprocess.run(['gcc','-std=c11','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all','-I',str(ROOT/'components/pos_core/include'),*map(lambda p:str(ROOT/p),sources),'-lm','-o',str(cls.runner)],check=True)
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def check(self,kind):
        p=subprocess.run([str(self.runner),kind],capture_output=True,text=True)
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
    def test_single_microphone_meter_uses_theme_and_grows_without_covering_controls(self):self.check('mic')
    def test_all_stripe_counts_and_white_selection_flash(self):self.check('stripes')
    def test_counter_button_squishes_and_number_jumps(self):self.check('counter')
    def test_fade_dims_every_channel_and_reaches_black_including_battery(self):self.check('fade')
    def test_spring_overshoots_and_settles_exactly(self):self.check('spring')

    def test_stationary_focus_continuous_spring_momentum_and_cached_neighbors(self):self.check('ribbon')

    def test_music_progress_is_visible_and_does_not_cover_volume(self):self.check('music')

    def test_native_icon_asset_is_reproducible_unique_and_bounded(self):
        import importlib.util
        spec=importlib.util.spec_from_file_location('pos_icons',ROOT/'tools/build_icons.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        asset=module.artwork()
        self.assertEqual(asset,(ROOT/'sd-payload/System/Assets/apps.icons').read_bytes())
        self.assertEqual(len(asset),2180);self.assertEqual(asset[:4],b'POI1')
        self.assertEqual(len(set(asset[4+i*128:4+(i+1)*128] for i in range(17))),17)
