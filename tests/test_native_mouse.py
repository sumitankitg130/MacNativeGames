"""Direct mouse camera: real ARM64 veneer, synthetic camera layouts and guards."""
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(platform.system() == 'Darwin' and platform.machine() == 'arm64'
                     and shutil.which('xcrun'), 'needs Apple silicon and Xcode command line tools')
class NativeMouseTests(unittest.TestCase):
    def test_relative_input_command(self):
        game = ROOT / 'games/totk'
        with tempfile.TemporaryDirectory() as tmp:
            probe = str(Path(tmp) / 'probe')
            sources = ['scripts/input_file_probe.c', 'runtime/input.c', 'runtime/hid_feedback.c',
                       'runtime/motion_fusion.c', 'runtime/autoload.c']
            build = subprocess.run(['xcrun', 'clang', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                                    *[str(game / s) for s in sources], '-o', probe], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            env = {k: v for k, v in os.environ.items() if k != 'TOTK_INPUT_FILE'}
            result = subprocess.run([probe], env=env, capture_output=True, text=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertTrue(json.loads(result.stdout)['appended_in_place'])

    def test_native_camera(self):
        with tempfile.TemporaryDirectory() as tmp:
            probe = str(Path(tmp) / 'probe')
            build = subprocess.run(['xcrun', 'clang', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                                    str(ROOT / 'games/totk/tests/native_mouse/probe.c'), '-o', probe],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            env = {k: v for k, v in os.environ.items()
                   if k not in ('TOTK_NATIVE_MOUSE_CAMERA', 'TOTK_EXPORT_IOS')}
            for mode in (None, 'normal', 'short-tail', 'legacy', 'ios-export', 'bad-site', 'occupied'):
                with self.subTest(mode=mode):
                    result = subprocess.run([probe, *([mode] if mode else [])], env=env,
                                            capture_output=True, text=True, timeout=30)
                    if mode in ('bad-site', 'occupied'):
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn('instruction mismatch' if mode == 'bad-site' else 'space occupied', result.stderr)
                    else:
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        self.assertEqual(json.loads(result.stdout)['status'], 'pass')
