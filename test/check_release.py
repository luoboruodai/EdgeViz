#!/usr/bin/env python3
"""Fail closed on common secrets, private paths, or private AE project files."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
secret = re.compile(r"(?i)(api[_-]?key|access[_-]?token|auth[_-]?token|password|passwd|bearer|private[_-]?key)\s*[:=]|AKIA[0-9A-Z]{16}|gh[pousr]_[A-Za-z0-9_]{20,}|-----BEGIN .*PRIVATE KEY-----")
private_path = re.compile(r"/(?:Users|Volumes)/[^\s\"']+")
for p in ROOT.rglob('*'):
    if not p.is_file() or '.git' in p.parts or p == Path(__file__):
        continue
    if p.suffix.lower() in {'.aep', '.aepx', '.aet'}:
        raise SystemExit(f'private AE project must not be published: {p}')
    if any(part in {'SDK', 'build', '.cache'} for part in p.parts):
        raise SystemExit(f'private/build directory must not be published: {p}')
    if p.name == '.DS_Store' or p.name.startswith('.env'):
        raise SystemExit(f'private OS/environment file must not be published: {p}')
    try:
        text = p.read_text(errors='ignore')
    except UnicodeDecodeError:
        continue
    if secret.search(text):
        raise SystemExit(f'credential-like text found: {p}')
    if private_path.search(text):
        raise SystemExit(f'absolute local path found: {p}')
print('PASS: clean release contains no obvious secrets, private paths, AE projects, SDK, caches, or OS metadata')
