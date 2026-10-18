#!/usr/bin/env python3
"""Package approved builds using a fixed public file map; never rebuild or install."""
import argparse
import hashlib
import json
import os
import plistlib
import re
import shutil
import tempfile
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION = '0.4.4-b2'

def sha(data):
    return hashlib.sha256(data).hexdigest()

def copy_file(source, dest):
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, dest)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mac-bundle', required=True, type=Path)
    parser.add_argument('--windows-aex', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    assert args.mac_bundle.is_dir() and args.windows_aex.is_file()
    info = plistlib.loads((args.mac_bundle / 'Contents/Info.plist').read_bytes())
    assert info['CFBundleVersion'] == '40402' and info['CFBundleShortVersionString'] == '0.4.4'
    assert b'CharWeave Native 0.4.4 beta 2' in args.windows_aex.read_bytes()
    args.output.mkdir(parents=True, exist_ok=True)
    archives = []
    with tempfile.TemporaryDirectory(prefix='charweave-package-', dir=args.output) as temporary:
        tmp = Path(temporary)
        for platform in ['macOS', 'Windows-x64']:
            name = f'CharWeave-{VERSION}-{platform}'
            folder = tmp / name
            folder.mkdir()
            for guide in ['INSTALL.zh-CN.md', 'INSTALL.en.md']:
                copy_file(ROOT / 'docs' / guide, folder / 'docs' / guide)
            copy_file(ROOT / 'THIRD_PARTY_NOTICES.md', folder / 'THIRD_PARTY_NOTICES.md')
            for notice in ['COPYING3', 'COPYING.RUNTIME', 'MinGW-w64-COPYING.txt']:
                copy_file(ROOT / 'licenses' / notice, folder / 'licenses' / notice)
            (folder / 'README.md').write_text(
                '# CharWeave Native 0.4.4 beta 2\n\n'
                '[中文安装与使用](docs/INSTALL.zh-CN.md) · [English installation and usage](docs/INSTALL.en.md)\n\n'
                'This is an installable pre-release package, not the automatic GitHub source archive. '
                'Check SHA256SUMS.txt and manifest.json before installation.\n\n'
                'macOS: universal/ad-hoc signed, local AE 26.5x89 sample only. '
                'Windows: x64 candidate; cross-build/model tests only, real AE acceptance pending.\n\n'
                '原生 Create/Manage 保持灰显，请使用独立 Styles Manager；不要同时安装新旧副本。'
                '升级先退出 AE/aerender 并保留旧版备份。安装指南可离线阅读。\n', encoding='utf-8')
            if platform == 'macOS':
                # Bundle files are the already approved signed payload, copied verbatim.
                shutil.copytree(args.mac_bundle, folder / 'build/CharWeave.plugin', symlinks=True)
                copy_file(ROOT / 'CharWeave-mac/Install.command', folder / 'Install.command')
                script = ROOT / 'CharWeave-mac/scripts/CharWeave Styles Manager.jsx'
                copy_file(script, folder / 'scripts/CharWeave Styles Manager.jsx')
                binary = 'build/CharWeave.plugin/Contents/MacOS/CharWeave'
                arch = ['arm64', 'x86_64']
            else:
                copy_file(args.windows_aex, folder / 'CharWeave.aex')
                for installer in ['Install.cmd', 'Install.ps1', 'Load-Smoke.cmd', 'Load-Smoke.ps1',
                                  'Collect-Diagnostics.cmd', 'Collect-Diagnostics.ps1']:
                    copy_file(ROOT / 'CharWeave-windows/install' / installer, folder / installer)
                installer = folder / 'Install.ps1'
                text = installer.read_text(encoding='utf-8')
                text, count = re.subn(r"\$expected = '[0-9A-Fa-f]{64}'",
                                     "$expected = '" + sha(args.windows_aex.read_bytes()).upper() + "'", text)
                assert count == 1
                installer.write_text(text, encoding='utf-8')
                copy_file(ROOT / 'CharWeave-windows/scripts/CharWeave Styles Manager.jsx',
                          folder / 'scripts/CharWeave Styles Manager.jsx')
                binary, arch = 'CharWeave.aex', ['x86_64']
            manifest = {
                'product': 'CharWeave Native', 'version': '0.4.4', 'stage': 'beta', 'build': 2,
                'pfVersion': '0x00022402', 'outFlags': '0x02000004',
                'platform': platform, 'architectures': arch, 'binary': binary,
                'binarySHA256': sha((folder / binary).read_bytes()),
                'hostVerification': 'Apple Silicon AE 26.5x89 registration and frame export' if platform == 'macOS'
                                    else 'Not performed; cross-build and GDI behavior model only',
                'sourceVersion': VERSION,
            }
            (folder / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
            for guide in folder.rglob('*.md'):
                for target in re.findall(r'\]\(([^)]+)\)', guide.read_text(encoding='utf-8')):
                    if '://' not in target and not target.startswith('#'):
                        assert (guide.parent / target.split('#')[0]).exists(), (guide.name, target)
            records = []
            for file in sorted(folder.rglob('*')):
                if file.is_file():
                    assert not file.is_symlink(), 'Unexpected symbolic link in payload'
                    records.append(sha(file.read_bytes()) + '  ' + file.relative_to(folder).as_posix())
            (folder / 'SHA256SUMS.txt').write_text('\n'.join(records) + '\n', encoding='utf-8')
            candidate = tmp / (name + '.zip')
            with zipfile.ZipFile(candidate, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
                for file in sorted(folder.rglob('*')):
                    if file.is_file():
                        archive.write(file, file.relative_to(tmp))
            with zipfile.ZipFile(candidate) as archive:
                assert archive.testzip() is None
                assert set(archive.namelist()) == {
                    str(file.relative_to(tmp)) for file in folder.rglob('*') if file.is_file()}
                for file in folder.rglob('*'):
                    if file.is_file():
                        assert archive.read(str(file.relative_to(tmp))) == file.read_bytes()
            sys.dont_write_bytecode = True
            from check_public import inspect
            with zipfile.ZipFile(candidate) as archive:
                for member in archive.infolist():
                    assert not inspect(member.filename, archive.read(member)), member.filename
            destination = args.output / candidate.name
            if destination.exists():
                raise SystemExit(f'Refusing to replace an existing release asset: {destination.name}')
            os.replace(candidate, destination)
            archives.append(destination)
            print(f'PASS {destination.name}: CRC + explicit member map + byte comparison + public policy')
    checksums = args.output / 'SHA256SUMS.txt'
    assert not checksums.exists(), 'Do not replace an existing checksum file'
    checksums.write_text(''.join(sha(path.read_bytes()) + '  ' + path.name + '\n' for path in archives), encoding='utf-8')

if __name__ == '__main__':
    main()
