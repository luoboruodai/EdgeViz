#!/usr/bin/env python3
"""Generate the raw PiPL payload for a Windows x64 After Effects plug-in.

The Mac build wraps the same property list in a classic resource fork.  A
Windows .aex stores the payload as a custom PE resource of type PiPL, so this
script writes only the big-endian property-list bytes.
"""
from pathlib import Path
import struct
import sys

VERSION = 0x00189601  # PF_VERSION(3,1,2,RELEASE,1)
OUT_FLAGS = 0x04000446  # PIX_INDEPENDENT | USE_OUTPUT_EXTENT | WIDE_TIME_INPUT | NON_PARAM_VARY | SEND_UPDATE_PARAMS_UI
OUT_FLAGS2 = 0x0000000A  # I_USE_3D_CAMERA | PARAM_GROUP_START_COLLAPSED_FLAG


def pstring(value: str) -> bytes:
    raw = value.encode("ascii")
    if len(raw) >= 256:
        raise ValueError("PiPL pstring is limited to 255 bytes")
    return bytes([len(raw)]) + raw


def cstring(value: str) -> bytes:
    return value.encode("ascii") + b"\0"


def prop(key: str, data: bytes) -> bytes:
    # PiPL property payloads are long-aligned by AE_General.r. The length field
    # remains the unpadded payload length.
    raw = b"8BIM" + key.encode("ascii") + struct.pack(">ii", 0, len(data)) + data
    return raw + b"\0" * ((-len(data)) & 3)


def u32(value: int) -> bytes:
    return struct.pack(">I", value)


def i16(value: int) -> bytes:
    return struct.pack(">h", value)


def build() -> bytes:
    properties = [
        ("kind", b"eFKT"),
        ("name", pstring("EdgeViz")),
        ("catg", pstring("PlugIn EdgeViz")),
        ("8664", cstring("EffectMain")),
        ("ePVR", u32(0x00020000)),
        ("eSVR", u32(0x000D0002)),
        ("eVER", u32(VERSION)),
        ("eINF", i16(0)),
        ("eGLO", u32(OUT_FLAGS)),
        ("eGL2", u32(OUT_FLAGS2)),
        ("eMNA", pstring("com.edgeviz.outline")),
        ("aeFL", u32(0)),
    ]
    payload = struct.pack(">II", 0, len(properties))
    for key, data in properties:
        payload += prop(key, data)
    return payload


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {Path(sys.argv[0]).name} OUTPUT")
    out = Path(sys.argv[1])
    out.parent.mkdir(parents=True, exist_ok=True)
    data = build()
    out.write_bytes(data)
    print(f"wrote {out} ({len(data)} bytes, eVER=0x{VERSION:08x})")


if __name__ == "__main__":
    main()
