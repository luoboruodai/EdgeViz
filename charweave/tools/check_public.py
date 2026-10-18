#!/usr/bin/env python3
"""Check explicitly selected source, Git blobs, and release ZIPs before upload."""
import argparse
import re
import subprocess
import zipfile
from pathlib import Path, PurePosixPath

RULES = {
    'private-home-path': re.compile(rb'/(?:Users|home)/[A-Za-z0-9_.-]+/'),
    'windows-home-path': re.compile(rb'[A-Za-z]:\\Users\\[^\\\s]+\\'),
    'github-token': re.compile(rb'(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,})'),
    'openai-style-key': re.compile(rb'sk-(?:proj-)?[A-Za-z0-9_-]{32,}'),
    'private-key': re.compile(rb'-----BEGIN (?:[A-Z]+ )?PRIVATE KEY-----'),
    'credential-url': re.compile(rb'https?://[^\s/:]+:[^\s/@]+@'),
    'literal-secret': re.compile(rb'(?:password|api_key|access_token)\s*[:=]\s*[\"\x27][^\s\"\x27]{12,}', re.I),
}
DENIED = {'PROJECT.md', '.DS_Store', 'auth.json', 'AGENTS.md', 'AE_General.r'}

def inspect(name, data):
    path = PurePosixPath(name)
    issues = []
    if (path.name in DENIED or path.name.startswith('.env') or
            path.name.startswith('._') or '__MACOSX' in path.parts or
            path.suffix.lower() in {'.aep', '.aepx', '.evtx', '.dmp', '.db', '.sqlite', '.log'}):
        issues.append('private/generated-member')
    if path.is_absolute() or '..' in path.parts:
        issues.append('unsafe-member-path')
    for label, pattern in RULES.items():
        if pattern.search(data):
            issues.append(label)
    return issues

def self_test():
    cases = [
        ('plain.txt', b'ghp_' + b'A' * 40),
        ('plain.txt', b'/' + b'Users/sample/private.txt'),
        ('plain.txt', b'-----BEGIN ' + b'PRIVATE KEY-----'),
        ('plain.txt', b'https://' + b'person:password@example.test'),
        ('PROJECT.md', b'private handoff'),
        ('../escape.txt', b'not permitted'),
        ('plain.txt', b'api_' + b'key="' + b'X' * 20 + b'"'),
    ]
    assert all(inspect(name, data) for name, data in cases), 'scanner rejection fixture failed'
    assert not inspect('src/visible.cpp', b'int main() { return 0; }'), 'scanner clean fixture failed'
    print('PASS scanner: 7 synthetic violations rejected; clean source accepted')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--git-range', help='scan every outgoing blob in this range plus staged changes')
    parser.add_argument('--git-only', action='store_true', help='check only changed Git blobs, not unrelated existing product files')
    parser.add_argument('paths', nargs='+', type=Path)
    args = parser.parse_args()
    if args.self_test:
        self_test()
    failures, checked = [], 0
    def check(label, name, data):
        nonlocal checked
        checked += 1
        for rule in inspect(name, data):
            failures.append((label, name, rule))
    if args.git_only:
        assert args.git_range, '--git-only requires an explicit outgoing range'
    for path in ([] if args.git_only else args.paths):
        if path.is_dir():
            for file in sorted(path.rglob('*')):
                if '.git' in file.relative_to(path).parts:
                    continue
                if file.is_symlink():
                    failures.append((str(path), file.relative_to(path).as_posix(), 'unexpected-symlink'))
                elif file.is_file():
                    check(str(path), file.relative_to(path).as_posix(), file.read_bytes())
        elif zipfile.is_zipfile(path):
            with zipfile.ZipFile(path) as archive:
                assert archive.testzip() is None, 'ZIP CRC failure'
                for entry in archive.infolist():
                    if not entry.is_dir():
                        check(str(path), entry.filename, archive.read(entry))
        else:
            check(str(path), path.name, path.read_bytes())
    if args.git_range:
        assert len(args.paths) == 1 and args.paths[0].is_dir(), 'Git scan requires one repository path'
        repo = args.paths[0]
        def git(*cmd):
            return subprocess.check_output(['git', '-C', str(repo), *cmd])
        changed = set(git('diff', '--cached', '--name-only', '-z', '--diff-filter=ACMRT').split(b'\0'))
        for entry in git('ls-files', '--stage', '-z').split(b'\0'):
            if entry:
                metadata, raw_name = entry.split(b'\t', 1)
                if raw_name in changed:
                    sha = metadata.split()[1].decode('ascii')
                    check('git-index', raw_name.decode('utf-8'), git('cat-file', 'blob', sha))
        for entry in git('rev-list', '--objects', args.git_range).splitlines():
            sha, _, raw_name = entry.partition(b' ')
            if raw_name and git('cat-file', '-t', sha.decode()).strip() == b'blob':
                check('git-history', raw_name.decode('utf-8'), git('cat-file', 'blob', sha.decode()))
    if failures:
        for label, name, rule in failures:
            print(f'FAIL {label}: {name}: {rule}')  # Never print matched values.
        raise SystemExit(f'{len(failures)} public-content findings across {checked} checked entries')
    print(f'PASS public-content scan: {checked} source/archive/Git entries; no policy findings')

if __name__ == '__main__':
    main()
