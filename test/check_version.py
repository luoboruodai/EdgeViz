#!/usr/bin/env python3
"""Check source/metadata/packages agree and both Mac architectures exist."""
import hashlib
from pathlib import Path
import plistlib
import re
import struct
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = '0.1.1'
PIPL = 0x00008E01
source = (ROOT / 'src/EdgeVizOutline.cpp').read_text()
assert '#define MAJOR_VERSION      0' in source
assert '#define MINOR_VERSION      1' in source
assert '#define BUG_VERSION        1' in source
assert 'com.edgeviz.outline' in source
assert 'const bool mainT = true' not in source
assert '(IsAerenderProc() || IsHostMainThread())' in source
pipl = (ROOT / 'pipl/EdgeVizOutlinePiPL.r').read_text()
assert 'CodeMacARM64' in pipl and 'CodeMacIntel64' in pipl and '36353' in pipl
winpipl = (ROOT / 'pipl/gen_win_pipl.py').read_text()
assert 'VERSION = 0x00008E01' in winpipl
plist = plistlib.loads((ROOT / 'Info.plist').read_bytes())
assert plist['CFBundleShortVersionString'] == VERSION
assert plist['CFBundleName'] == plist['CFBundleExecutable'] == 'EdgeViz'
art = ROOT / 'artifacts'
mac = art / 'macos-universal/EdgeViz.plugin'
win = art / 'windows-x64/EdgeViz.aex'
mac_bin = mac / 'Contents/MacOS/EdgeViz'
assert mac_bin.is_file() and win.is_file()
assert plistlib.loads((mac / 'Contents/Info.plist').read_bytes())['CFBundleShortVersionString'] == VERSION
# The Mach-O FAT header is big-endian: arm64 0x0100000c, x86_64 0x01000007.
raw = mac_bin.read_bytes()
assert raw[:4] == b'\xca\xfe\xba\xbe', 'expected FAT Mach-O'
fat_count = struct.unpack_from('>I', raw, 4)[0]
archs = {struct.unpack_from('>I', raw, 8 + i * 20)[0] for i in range(fat_count)}
assert archs == {0x0100000c, 0x01000007}, f'wrong Mac archs: {archs}'
assert subprocess.run(['codesign','--verify','--deep','--strict',str(mac)], capture_output=True).returncode == 0
# Check resource versions in binary rather than trusting comments.
derez = subprocess.run(['DeRez', '-useDF', str(mac / 'Contents/Resources/EdgeViz.rsrc')],
                       capture_output=True, check=True).stdout.decode('latin-1')
hex_lines = re.findall(r'\$\"([0-9A-Fa-f\s]+)\"', derez)
assert hex_lines, 'missing macOS PiPL resource dump'
mac_pipl = bytes.fromhex(' '.join(hex_lines))
assert struct.unpack_from('>II', mac_pipl) == (0, 13), 'expected 13 universal PiPL properties'
props = {}
pos = 8
for _ in range(13):
    assert mac_pipl[pos:pos + 4] == b'8BIM'
    key = mac_pipl[pos + 4:pos + 8].decode('ascii')
    prop_id, length = struct.unpack_from('>II', mac_pipl, pos + 8)
    assert prop_id == 0 and key not in props
    props[key] = mac_pipl[pos + 16:pos + 16 + length]
    pos += 16 + ((length + 3) & ~3)
assert pos == len(mac_pipl)
assert props['ma64'][1:] == props['mi64'][1:] == b'EffectMain'
assert props['eVER'] == struct.pack('>I', PIPL)
assert props['eSVR'] == struct.pack('>HH', 13, 2)
assert props['eGLO'] == struct.pack('>I', 0x04000446)
assert props['eGL2'] == struct.pack('>I', 10)
assert props['eMNA'] == bytes([len('com.edgeviz.outline')]) + b'com.edgeviz.outline'
packages = [
    (art / 'macos-universal/EdgeViz-v0.1.1-macOS-universal.zip',
     'EdgeViz.plugin/Contents/MacOS/EdgeViz', raw),
    (art / 'windows-x64/EdgeViz-v0.1.1-Windows-x64.zip', 'EdgeViz.aex', win.read_bytes()),
]
expected_sums = []
for path, member, binary in packages:
    with zipfile.ZipFile(path) as z:
        assert z.testzip() is None and member in z.namelist()
        assert not any('.DS_Store' in x or '__MACOSX' in x for x in z.namelist())
        assert z.read(member) == binary
    expected_sums.append(f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n')
assert (ROOT / 'SHA256SUMS.txt').read_text() == ''.join(expected_sums)
print('PASS: v0.1.1 source/PiPL, universal Mac signature, Windows binary, packages and checksums')
