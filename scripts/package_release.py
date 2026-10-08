#!/usr/bin/env python3
"""Build deterministic v0.1.1 ZIP assets from already validated binaries.

Run from any directory AFTER build.sh and build_win.sh. Never includes SDK files.
"""
import hashlib
import os
from pathlib import Path
import shutil
import stat
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = '0.1.1'
ART = ROOT / 'artifacts'
MAC = ART / 'macos-universal'
WIN = ART / 'windows-x64'
MAC_BUNDLE = MAC / 'EdgeViz.plugin'
WIN_BINARY = WIN / 'EdgeViz.aex'
MAC_ZIP = MAC / f'EdgeViz-v{VERSION}-macOS-universal.zip'
WIN_ZIP = WIN / f'EdgeViz-v{VERSION}-Windows-x64.zip'


def archive(zip_path, entries):
    zip_path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for src, dst in sorted(entries, key=lambda p: p[1]):
            info = zipfile.ZipInfo(dst, (2026, 10, 8, 0, 0, 0))
            info.create_system = 3
            info.compress_type = zipfile.ZIP_DEFLATED
            mode = 0o755 if os.access(src, os.X_OK) else 0o644
            info.external_attr = (stat.S_IFREG | mode) << 16
            z.writestr(info, src.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


if __name__ == '__main__':
    assert (ROOT / 'build/EdgeViz.plugin/Contents/MacOS/EdgeViz').is_file()
    assert (ROOT / 'build-win/EdgeViz.aex').is_file()
    MAC.mkdir(parents=True, exist_ok=True)
    WIN.mkdir(parents=True, exist_ok=True)
    # The v0.1.0 arm64-only bundle has a different directory; it must not
    # remain alongside the corrected universal package on the active branch.
    legacy_mac = ART / 'macos-arm64'
    if legacy_mac.exists(): shutil.rmtree(legacy_mac)
    for old in WIN.glob('EdgeViz-v0.1.0-*'): old.unlink()
    if MAC_BUNDLE.exists(): shutil.rmtree(MAC_BUNDLE)
    shutil.copytree(ROOT / 'build/EdgeViz.plugin', MAC_BUNDLE, copy_function=shutil.copy2)
    shutil.copy2(ROOT / 'build-win/EdgeViz.aex', WIN_BINARY)
    mac_readme = MAC / 'INSTALL-macOS.txt'
    mac_readme.write_text('''EdgeViz v0.1.1 / macOS Intel + Apple Silicon

Quit After Effects. Remove older EdgeViz and EdgeViz Outline copies from
~/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/
Copy the EdgeViz.plugin folder into that directory, then restart After Effects.
This bundle is ad-hoc signed, not notarized.
''')
    win_readme = WIN / 'INSTALL-Windows.txt'
    win_readme.write_text('''EdgeViz v0.1.1 / Windows x64

Quit After Effects. Remove older EdgeViz.aex and EdgeViz Outline.aex copies
from C:\\Program Files\\Adobe\\Common\\Plug-ins\\7.0\\MediaCore\\
Copy EdgeViz.aex into that folder, then restart After Effects.
If AE still crashes while scanning plug-ins, remove EdgeViz.aex and send
AE version, Windows version, and the crash dump to the maintainer.
Windows AE 23-26 runtime loading/rendering requires testing on each host.
''')
    archive(MAC_ZIP, [(p, 'EdgeViz.plugin/' + p.relative_to(MAC_BUNDLE).as_posix())
                      for p in MAC_BUNDLE.rglob('*') if p.is_file()] +
                      [(mac_readme, mac_readme.name)])
    archive(WIN_ZIP, [(WIN_BINARY, WIN_BINARY.name), (win_readme, win_readme.name)])
    sums = ''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n'
                   for p in (MAC_ZIP, WIN_ZIP))
    (ROOT / 'SHA256SUMS.txt').write_text(sums)
    print(MAC_ZIP, MAC_ZIP.stat().st_size)
    print(WIN_ZIP, WIN_ZIP.stat().st_size)
    print(sums, end='')
