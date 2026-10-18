#!/usr/bin/env python3
"""Release-tool failure fixtures, using synthetic non-runnable binary stand-ins."""
import contextlib
import hashlib
import importlib.util
import io
import plistlib
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('package_release', ROOT / 'tools/package_release.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class PackageFixtures(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='charweave-release-fixture-')
        self.base = Path(self.temp.name)
        self.source = self.base / 'source'
        relatives = [
            'docs/INSTALL.zh-CN.md', 'docs/INSTALL.en.md', 'THIRD_PARTY_NOTICES.md',
            'licenses/COPYING3', 'licenses/COPYING.RUNTIME', 'licenses/MinGW-w64-COPYING.txt',
            'CharWeave-mac/Install.command', 'CharWeave-mac/scripts/CharWeave Styles Manager.jsx',
            'CharWeave-windows/scripts/CharWeave Styles Manager.jsx',
        ] + ['CharWeave-windows/install/' + name for name in [
            'Install.cmd', 'Install.ps1', 'Load-Smoke.cmd', 'Load-Smoke.ps1',
            'Collect-Diagnostics.cmd', 'Collect-Diagnostics.ps1']]
        for relative in relatives:
            dest = self.source / relative
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / relative, dest)
        self.bundle = self.base / 'synthetic.plugin'
        (self.bundle / 'Contents/MacOS').mkdir(parents=True)
        (self.bundle / 'Contents/Info.plist').write_bytes(plistlib.dumps({
            'CFBundleVersion': '40402', 'CFBundleShortVersionString': '0.4.4'}))
        (self.bundle / 'Contents/MacOS/CharWeave').write_bytes(b'synthetic, not a runnable binary')
        self.win = self.base / 'synthetic.aex'
        self.win.write_bytes(b'CharWeave Native 0.4.4 beta 2; synthetic, not runnable')
        self.output = self.base / 'output'
        self.root_patch = mock.patch.object(module, 'ROOT', self.source)
        self.root_patch.start()
        sys.path.insert(0, str(ROOT / 'tools'))

    def tearDown(self):
        sys.path.remove(str(ROOT / 'tools'))
        self.root_patch.stop()
        self.temp.cleanup()

    def run_tool(self):
        argv = ['package_release.py', '--mac-bundle', str(self.bundle),
                '--windows-aex', str(self.win), '--output', str(self.output)]
        with mock.patch.object(sys, 'argv', argv), contextlib.redirect_stdout(io.StringIO()):
            module.main()

    def test_clean_explicit_map_and_two_archives(self):
        self.run_tool()
        self.assertEqual(len(list(self.output.glob('*.zip'))), 2)
        self.assertTrue((self.output / 'SHA256SUMS.txt').is_file())

    def test_mismatched_version_is_rejected(self):
        (self.bundle / 'Contents/Info.plist').write_bytes(plistlib.dumps({
            'CFBundleVersion': '40401', 'CFBundleShortVersionString': '0.4.4'}))
        with self.assertRaises(AssertionError):
            self.run_tool()
        self.assertFalse(self.output.exists())

    def test_private_extra_member_is_rejected(self):
        (self.bundle / '.env.local').write_text('synthetic fixture', encoding='utf-8')
        with self.assertRaises(AssertionError):
            self.run_tool()
        self.assertFalse(list(self.output.glob('*.zip')))

    def test_stale_guide_link_is_rejected(self):
        (self.source / 'docs/INSTALL.en.md').write_text('[guide](missing-guide.txt)', encoding='utf-8')
        with self.assertRaises(AssertionError):
            self.run_tool()
        self.assertFalse(list(self.output.glob('*.zip')))

    def test_existing_asset_is_never_overwritten(self):
        self.output.mkdir()
        previous = self.output / 'CharWeave-0.4.4-b2-macOS.zip'
        previous.write_bytes(b'previous known-good asset')
        expected = hashlib.sha256(previous.read_bytes()).hexdigest()
        with self.assertRaises(SystemExit):
            self.run_tool()
        self.assertEqual(hashlib.sha256(previous.read_bytes()).hexdigest(), expected)

if __name__ == '__main__':
    unittest.main(verbosity=2)
