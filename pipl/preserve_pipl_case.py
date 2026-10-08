#!/usr/bin/env python3
"""Keep the Adobe PiPL resource name's exact case in MinGW's COFF object.

GNU windres normalizes custom resource type identifiers to uppercase (PIPL),
although the source .rc spells "PiPL".  Preserve the canonical spelling so
FindResourceW with an exact type string is unambiguous on Windows.
"""
from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit('usage: preserve_pipl_case.py COMPILED_RESOURCE_OBJECT')
path = Path(sys.argv[1])
blob = path.read_bytes()
old = b'\x04\x00' + 'PIPL'.encode('utf-16le')
new = b'\x04\x00' + 'PiPL'.encode('utf-16le')
assert blob.count(old) == 1, 'expected exactly one windres PIPL resource type'
path.write_bytes(blob.replace(old, new, 1))
print('preserved PiPL case in resource object')
