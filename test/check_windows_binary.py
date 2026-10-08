#!/usr/bin/env python3
"""Validate the complete PE/resource contract, not just strings in a DLL.

This is a format check, not a substitute for loading/rendering in AE on Windows.
"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'pipl'))
from gen_win_pipl import build as expected_pipl, VERSION  # noqa: E402

path = Path(sys.argv[1]) if len(sys.argv) == 2 else ROOT / 'artifacts/windows-x64/EdgeViz.aex'
buf = path.read_bytes()


def u16(off): return struct.unpack_from('<H', buf, off)[0]
def u32(off): return struct.unpack_from('<I', buf, off)[0]
def cstr(off): return buf[off:buf.index(b'\x00', off)].decode('ascii')

assert buf[:2] == b'MZ', 'missing DOS header'
pe = u32(0x3c)
assert buf[pe:pe + 4] == b'PE\0\0', 'missing PE signature'
coff = pe + 4
machine, section_count, _, _, _, optional_size, flags = struct.unpack_from('<HHIIIHH', buf, coff)
assert machine == 0x8664 and flags & 0x2000, 'not a Windows x64 DLL'
opt = coff + 20
assert u16(opt) == 0x20b, 'not PE32+'
assert u16(opt + 68) == 2, 'not a Windows GUI subsystem'
assert u32(opt + 108) >= 3, 'not enough PE data directories'
sections = []
for i in range(section_count):
    off = opt + optional_size + i * 40
    name = buf[off:off + 8].split(b'\0')[0]
    vsize, rva, rawsize, rawptr = struct.unpack_from('<IIII', buf, off + 8)
    sections.append((name, rva, max(vsize, rawsize), rawptr))


def at(rva):
    for _, base, size, pos in sections:
        if base <= rva < base + size:
            return pos + rva - base
    raise AssertionError(f'RVA outside PE sections: 0x{rva:x}')


def directory(i):
    return struct.unpack_from('<II', buf, opt + 112 + 8 * i)

export_rva, export_len = directory(0)
assert export_rva and export_len
export = at(export_rva)
_, _, _, _, dll_name_rva, ordinal_base, num_funcs, num_names, _, names_rva, _ = struct.unpack_from('<IIHHIIIIIII', buf, export)
exports = {cstr(at(u32(at(names_rva) + 4 * i))) for i in range(num_names)}
assert exports == {'EffectMain'}, f'unexpected exports: {exports}'

imports_rva, _ = directory(1)
imports = set()
for i in range(128):
    off = at(imports_rva) + i * 20
    ilt, _, _, name_rva, iat = struct.unpack_from('<IIIII', buf, off)
    if not any((ilt, name_rva, iat)):
        break
    imports.add(cstr(at(name_rva)).lower())
else:
    raise AssertionError('unterminated PE import directory')
assert imports, 'missing Windows CRT imports'
assert all(x == 'kernel32.dll' or x.startswith('api-ms-win-crt-') or
           x in {'ucrtbase.dll', 'msvcrt.dll'} for x in imports), f'unexpected DLL dependencies: {imports}'

resource_rva, _ = directory(2)
assert resource_rva, 'missing resource directory'
base = at(resource_rva)


def entries(rel):
    d = base + rel
    named, numbered = struct.unpack_from('<HH', buf, d + 12)
    for i in range(named + numbered):
        id_or_name, child = struct.unpack_from('<II', buf, d + 16 + 8 * i)
        if id_or_name & 0x80000000:
            nameoff = base + (id_or_name & 0x7fffffff)
            n = u16(nameoff)
            key = buf[nameoff + 2:nameoff + 2 + 2 * n].decode('utf-16le')
        else:
            key = id_or_name
        yield key, child

resources = {}
for typ, child in entries(0):
    assert child & 0x80000000
    for rid, id_child in entries(child & 0x7fffffff):
        assert id_child & 0x80000000
        for lang, leaf in entries(id_child & 0x7fffffff):
            assert not leaf & 0x80000000
            raw_rva, raw_len = struct.unpack_from('<II', buf, base + leaf)
            resources[(typ, rid, lang)] = buf[at(raw_rva):at(raw_rva) + raw_len]

pipl_matches = [(k, v) for k, v in resources.items() if k[0] == 'PiPL' and k[1] == 16000]
assert len(pipl_matches) == 1, f'exactly one PiPL resource ID 16000 expected: {list(resources)}'
assert pipl_matches[0][1] == expected_pipl(), 'embedded PiPL differs from source generator'
version_resources = [v for k, v in resources.items() if k[0] == 16]
assert len(version_resources) == 1, 'expected one VERSIONINFO resource'
ver = version_resources[0]
fixed = ver.find(struct.pack('<I', 0xFEEF04BD))
assert fixed >= 0, 'missing VS_FIXEDFILEINFO'
assert struct.unpack_from('<6I', ver, fixed) == (
    0xFEEF04BD, 0x00010000, 0x00000001, 0x00010001,
    0x00000001, 0x00010001), 'wrong Win file/product version'
assert '0.1.1'.encode('utf-16le') in ver, 'wrong Windows version string'

# Fully walk the big-endian PiPL: validates lengths, four-byte alignment,
# x64 entry key, flags and version instead of merely searching for byte strings.
pipl = pipl_matches[0][1]
version, count = struct.unpack_from('>II', pipl)
assert version == 0 and count == 12
pos, props = 8, {}
for _ in range(count):
    assert pipl[pos:pos + 4] == b'8BIM', 'malformed PiPL vendor'
    key = pipl[pos + 4:pos + 8].decode('ascii')
    property_id, length = struct.unpack_from('>II', pipl, pos + 8)
    assert property_id == 0 and key not in props and length < 1024
    props[key] = pipl[pos + 16:pos + 16 + length]
    pos += 16 + (length + 3 & ~3)
assert pos == len(pipl), 'PiPL property alignment/length mismatch'
assert props['kind'] == b'eFKT'
assert props['8664'] == b'EffectMain\0'
assert props['eMNA'] == bytes([len('com.edgeviz.outline')]) + b'com.edgeviz.outline'
assert props['eVER'] == struct.pack('>I', VERSION)
assert props['eSVR'] == struct.pack('>HH', 13, 2)
assert props['eGLO'] == struct.pack('>I', 0x04000446)
assert props['eGL2'] == struct.pack('>I', 10)
print(f'PASS: {path.name}: x64 PE DLL, EffectMain, PiPL #16000, v0.1.1, flags, VERSIONINFO, imports')
