#!/usr/bin/env python3
"""Guard the v0.1.0 source/PiPL/metadata/package naming contract."""
from pathlib import Path
import plistlib
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = '0.1.0'
PIPL = 0x00008601
source = (ROOT / 'src/EdgeVizOutline.cpp').read_text()
assert '#define MAJOR_VERSION      0' in source
assert '#define MINOR_VERSION      1' in source
assert '#define BUG_VERSION        0' in source
assert 'com.edgeviz.outline' in source
pipl = (ROOT / 'pipl/EdgeVizOutlinePiPL.r').read_text()
assert f'{PIPL}' in pipl and VERSION.replace('.', ',') in pipl
winpipl = (ROOT / 'pipl/gen_win_pipl.py').read_text()
assert 'VERSION = 0x00008601' in winpipl
plist = plistlib.loads((ROOT / 'Info.plist').read_bytes())
assert plist['CFBundleShortVersionString'] == VERSION
assert plist['CFBundleName'] == plist['CFBundleExecutable'] == 'EdgeViz'
art = ROOT / 'artifacts'
mac = art / 'macos-arm64/EdgeViz.plugin'
win = art / 'windows-x64/EdgeViz.aex'
assert (mac / 'Contents/MacOS/EdgeViz').is_file() and win.is_file()
assert plistlib.loads((mac / 'Contents/Info.plist').read_bytes())['CFBundleShortVersionString'] == VERSION
for resource in [mac / 'Contents/Resources/EdgeViz.rsrc', win]:
    raw = resource.read_bytes()
    marker = raw.index(b'eVER')
    assert struct.pack('>I', PIPL) in raw[marker:marker + 32], resource
assert b'8664' in win.read_bytes() and b'EffectMain' in win.read_bytes()
for path, member in [
    (art / 'macos-arm64/EdgeViz-v0.1.0-macOS-arm64.zip', 'EdgeViz.plugin/Contents/MacOS/EdgeViz'),
    (art / 'windows-x64/EdgeViz-v0.1.0-Windows-x64.zip', 'EdgeViz.aex'),
]:
    with zipfile.ZipFile(path) as z:
        assert member in z.namelist() and not any('.DS_Store' in x for x in z.namelist())
print('PASS: v0.1.0 source, PiPL, metadata, bare binaries and both packages agree')
