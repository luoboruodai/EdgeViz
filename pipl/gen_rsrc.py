#!/usr/bin/env python3
"""Generate an AE .rsrc (resource fork) containing a 'PiPL' resource for
EdgeViz. No Rez needed — the format is written out directly.

PiPL data layout (all big-endian):
  uint32 propertyCount
  per property:
    char  vendorID[4]   ('8BIM')
    char  key[4]
    int32 propertyID    (sequential, 0-based)
    uint32 dataLength
    byte  data[dataLength], padded to an even byte count with zeros
"""
import struct
import sys

def pstr(s):
    b = s.encode("ascii")
    assert len(b) < 256
    return bytes([len(b)]) + b

def prop(pid, key, data):
    out = b"8BIM" + key.encode("ascii") + struct.pack(">iI", pid, len(data)) + data
    if len(data) % 2:
        out += b"\x00"
    return out

def u32(v):
    return struct.pack(">I", v)

def u16(v):
    return struct.pack(">h", v)

PROPS = [
    ("kind", b"eFKT"),                          # 0  AEEffect
    ("name", pstr("EdgeViz")),          # 1
    ("catg", pstr("PlugIn EdgeViz")),           # 2  effects-menu category
    ("ma64", pstr("EffectMain")),               # 3  Mac ARM64 entry point
    ("ePVR", u32(0x00020000)),                  # 4  PiPL version 2.0
    ("eSVR", u32(0x000D0002)),                  # 5  required host spec version (13.28)
    ("eVER", u32(0x00008601)),                  # 6  EdgeViz v0.1.0 (PF_VERSION 0x00008601)
    ("eINF", u16(0)),                           # 7  info flags
    ("eGLO", u32(0x04000446)),                   # 8  PIX_INDEPENDENT | USE_OUTPUT_EXTENT | WIDE_TIME_INPUT | NON_PARAM_VARY | SEND_UPDATE_PARAMS_UI
    ("eGL2", u32(10)),                              # 9  I_USE_3D_CAMERA | PARAM_GROUP_START_COLLAPSED_FLAG
    ("eMNA", pstr("com.edgeviz.outline")),      # 10 match name
    ("aeFL", u32(0)),                           # 11
]

def main(out_path):
    data = struct.pack(">II", 0, len(PROPS))  # kPIPropertiesVersion(0) + count
    for pid, (key, pdata) in enumerate(PROPS):
        data += prop(pid, key, pdata)

    # resource-fork container
    data_off = 0x100
    map_off = data_off + 4 + len(data)   # 4 = the data-length prefix
    res_data = struct.pack(">I", len(data)) + data
    data_len = len(res_data)

    # minimal resource map with a single 'PiPL' resource (id 16000)
    map_body = b"\x00" * 16                     # copy of header (filled later)
    map_body += struct.pack(">IHHHH", 0, 0, 0, 28, 50)  # nextMap, refNum, attrs, typeOff, nameOff
    type_list = struct.pack(">H", 0)            # typeCount - 1
    type_list += b"PiPL"
    type_list += struct.pack(">HH", 0, 10)      # resCount-1, refListOffset
    type_list += struct.pack(">hH", 16000, 0xFFFF)  # id, nameOffset(-1)
    type_list += b"\x00" + b"\x00\x00\x00"      # attrs, dataOffset(3 bytes)=0
    type_list += b"\x00\x00\x00\x00"            # reserved
    map_body += type_list
    map_len = len(map_body)
    assert map_len == 50, map_len

    header = struct.pack(">IIII", data_off, map_off, data_len, map_len)
    map_body = header + map_body[16:]

    blob = header + b"\x00" * (data_off - 16) + res_data + map_body
    assert len(blob) == map_off + map_len

    with open(out_path, "wb") as f:
        f.write(blob)
    print(f"wrote {out_path} ({len(blob)} bytes, pipl {len(data)} bytes)")

if __name__ == "__main__":
    main(sys.argv[1])
