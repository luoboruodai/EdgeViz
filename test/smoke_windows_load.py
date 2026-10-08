#!/usr/bin/env python3
"""Run ON WINDOWS to check OS loader, PiPL lookup and effect export.

AE 23-26 rendering tests still require their respective installed hosts.
"""
import ctypes
import os
import sys
from pathlib import Path

if os.name != 'nt':
    raise SystemExit('Run on Windows x64, not on a cross-compilation host')
path = Path(sys.argv[1] if len(sys.argv) > 1 else 'artifacts/windows-x64/EdgeViz.aex').resolve()
kernel = ctypes.WinDLL('kernel32', use_last_error=True)
kernel.LoadLibraryExW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, ctypes.c_uint32]
kernel.LoadLibraryExW.restype = ctypes.c_void_p
kernel.FindResourceW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_wchar_p]
kernel.FindResourceW.restype = ctypes.c_void_p
kernel.GetProcAddress.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
kernel.GetProcAddress.restype = ctypes.c_void_p
kernel.FreeLibrary.argtypes = [ctypes.c_void_p]
kernel.FreeLibrary.restype = ctypes.c_int
h = kernel.LoadLibraryExW(str(path), None, 0)
assert h, f'LoadLibraryExW failed: WinError {ctypes.get_last_error()}'
try:
    # MAKEINTRESOURCE(16000), with the PiPL name as used by AE.
    rid = ctypes.cast(ctypes.c_void_p(16000), ctypes.c_wchar_p)
    r = kernel.FindResourceW(h, rid, 'PiPL')
    assert r, f'FindResourceW PiPL #16000 failed: WinError {ctypes.get_last_error()}'
    fn = kernel.GetProcAddress(h, b'EffectMain')
    assert fn, f'GetProcAddress EffectMain failed: WinError {ctypes.get_last_error()}'
finally:
    kernel.FreeLibrary(h)
print(f'PASS: Windows loaded {path.name}; PiPL #16000 and EffectMain found')
