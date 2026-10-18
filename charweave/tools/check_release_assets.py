#!/usr/bin/env python3
"""Verify the frozen CharWeave install assets locally or from its exact prerelease."""
import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path, PurePosixPath

sys.dont_write_bytecode = True
from check_public import inspect

ROOT = Path(__file__).resolve().parents[1]

def sha(data):
    return hashlib.sha256(data).hexdigest()

def verify(directory):
    index = json.loads((ROOT / 'release-assets.json').read_text(encoding='utf-8'))
    for source in index['approvedRuntimeInputs']:
        assert sha((ROOT / source['path']).read_bytes()) == source['sha256'], source['path']
    expected = {item['name']: item['sha256'] for item in index['assets']}
    checksum_text = (directory / index['checksumsAsset']).read_text(encoding='utf-8')
    checksums = {}
    for line in checksum_text.splitlines():
        value, member = line.split('  ', 1)
        assert re.fullmatch('[0-9a-f]{64}', value) and member not in checksums
        checksums[member] = value
    for name, digest in expected.items():
        data = (directory / name).read_bytes()
        assert sha(data) == digest == checksums[name], name
        with zipfile.ZipFile(directory / name) as archive:
            assert archive.testzip() is None, name
            names = archive.namelist()
            assert len(names) == len(set(names)), 'Duplicate ZIP members'
            assert all(not inspect(member, archive.read(member)) for member in names), 'Public policy mismatch'
            prefix = name[:-4] + '/'
            assert all(member.startswith(prefix) for member in names), 'Unexpected package root'
            files = {member[len(prefix):]: archive.read(member) for member in names}
            manifest = json.loads(files['manifest.json'])
            assert manifest['version'] == '0.4.4' and manifest['build'] == 2
            assert manifest['pfVersion'] == '0x00022402' and manifest['outFlags'] == '0x02000004'
            binary = files[manifest['binary']]
            assert sha(binary) == manifest['binarySHA256'] == index['approvedBinaries'][manifest['platform']]
            listed = {}
            for line in files['SHA256SUMS.txt'].decode('utf-8').splitlines():
                digest_value, member = line.split('  ', 1)
                assert member not in listed and member in files
                assert sha(files[member]) == digest_value
                listed[member] = digest_value
            assert set(files) == set(listed) | {'SHA256SUMS.txt'}, 'Unlisted package members'
            for member, contents in files.items():
                if member.endswith('.md'):
                    for target in re.findall(r'\]\(([^)]+)\)', contents.decode('utf-8')):
                        if '://' not in target and not target.startswith('#'):
                            path = str(PurePosixPath(member).parent / target.split('#')[0])
                            normalized = []
                            for part in PurePosixPath(path).parts:
                                if part == '..':
                                    assert normalized, 'Guide link escapes package'
                                    normalized.pop()
                                elif part != '.':
                                    normalized.append(part)
                            assert '/'.join(normalized) in files, (member, target)
            script = 'scripts/CharWeave Styles Manager.jsx'
            assert files[script] == (ROOT / 'CharWeave-mac' / script).read_bytes()
            if manifest['platform'] == 'Windows-x64':
                pe = struct.unpack_from('<I', binary, 0x3c)[0]
                assert binary[pe:pe+4] == b'PE\0\0'
                assert struct.unpack_from('<H', binary, pe+4)[0] == 0x8664
                assert b'GdiFlush\0' in binary and b'GetGlyphIndicesW\0' in binary
                assert (ROOT / 'CharWeave-windows/pipl/pipl.bin').read_bytes() in binary
                installer = files['Install.ps1'].decode('utf-8')
                assert re.search(r"\$expected = '([A-F0-9]{64})'", installer).group(1).lower() == sha(binary)
                assert files['Install.cmd'] == (ROOT / 'CharWeave-windows/install/Install.cmd').read_bytes()
            else:
                import plistlib
                info = plistlib.loads(files['build/CharWeave.plugin/Contents/Info.plist'])
                assert info['CFBundleVersion'] == '40402'
                assert files['Install.command'] == (ROOT / 'CharWeave-mac/Install.command').read_bytes()
                assert struct.unpack_from('>I', binary)[0] in (0xcafebabe, 0xcafebabf), 'Expected universal Mach-O'
        print(f'PASS release archive {name}: frozen digest + complete member map + guides + runtime + installer')
    print(f"PASS approved runtime source mapping for {index['tag']}; 2 install archives checked (not skipped)")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--directory', type=Path, help='an already downloaded or locally frozen asset directory')
    group.add_argument('--download', action='store_true', help='download this exact tag from the declared repository')
    args = parser.parse_args()
    if args.directory:
        verify(args.directory)
    else:
        index = json.loads((ROOT / 'release-assets.json').read_text(encoding='utf-8'))
        with tempfile.TemporaryDirectory(prefix='charweave-release-readback-') as path:
            command = ['gh', 'release', 'download', index['tag'], '--repo', index['repository'], '--dir', path]
            for name in [item['name'] for item in index['assets']] + [index['checksumsAsset']]:
                command += ['--pattern', name]
            subprocess.run(command, check=True)
            verify(Path(path))

if __name__ == '__main__':
    main()
