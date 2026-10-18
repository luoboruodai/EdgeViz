#!/usr/bin/env python3
"""Verify source/PiPL/guide identity without requiring Adobe headers."""
import plistlib
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
for source in ['CharWeave-mac/src/CharWeave.mm', 'CharWeave-windows/src/CharWeaveWin.cpp']:
    text = (ROOT / source).read_text(encoding='utf-8')
    assert 'PF_VERSION(0, 4, 4, PF_Stage_BETA, 2)' in text, source
    assert 'PARAM_COUNT = 40' in text and 'P_TURBULENCE_SCALE = 33' in text, source
    assert '0x288' in text and '0x31574354u' in text, source
r = (ROOT / 'CharWeave-mac/pipl/CharWeavePiPL.r').read_text(encoding='utf-8')
assert '0x00022402' in r and '0x02000004' in r
info = plistlib.loads((ROOT / 'CharWeave-mac/Info.plist').read_bytes())
assert info['CFBundleVersion'] == '40402' and info['CFBundleShortVersionString'] == '0.4.4'
pipl = (ROOT / 'CharWeave-windows/pipl/pipl.bin').read_bytes()
i, props = 10, {}
for _ in range(struct.unpack_from('<I', pipl, 6)[0]):
    vendor, key, ident, size = struct.unpack_from('<4s4sII', pipl, i)
    props[key] = pipl[i+16:i+16+size]
    i += 16 + ((size+3)//4)*4
assert i == len(pipl)
assert struct.unpack('<I', props[b'REVe'])[0] == 0x22402
assert struct.unpack('<I', props[b'OLGe'])[0] == 0x02000004
# Markdown relative links must resolve from the actual source tree.
for doc in ROOT.rglob('*.md'):
    if '.git' in doc.relative_to(ROOT).parts:
        continue
    for target in re.findall(r'\]\(([^)]+)\)', doc.read_text(encoding='utf-8')):
        if '://' not in target and not target.startswith('#'):
            assert (doc.parent / target.split('#')[0]).exists(), (doc, target)
print('PASS version 0.4.4 beta 2 / PF & PiPL 0x00022402 / bundle 40402 / IDs & TCW1 / guide links')
