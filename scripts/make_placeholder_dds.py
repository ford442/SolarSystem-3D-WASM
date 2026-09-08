#!/usr/bin/env python3
"""Write a 4x4 uncompressed A8R8G8B8 .dds placeholder.

The real textures live on FTP (see README); resource/textures_low/ ships tiny
solid-colour stand-ins so a checkout renders and the staged loader has something
to fetch. This produces byte-identical headers to the existing placeholders.

Usage:
  python3 scripts/make_placeholder_dds.py resource/textures_low/Ceres_Diffuse_Low.dds 8c8177
  python3 scripts/make_placeholder_dds.py resource/textures_low/Ceres_Normal_Low.dds normal
"""
import struct
import sys

FLAT_NORMAL = "8080ff"  # RGB(128,128,255) — flat tangent-space normal


def header(width: int, height: int) -> bytes:
    hdr = struct.pack(
        "<4s7I44s2I4s5I5I",
        b"DDS ",
        124,            # header size
        0x0000100F,     # CAPS | HEIGHT | WIDTH | PITCH | PIXELFORMAT
        height, width,
        width * 4,      # pitch
        1, 1,           # depth, mipmap count
        b"\0" * 44,     # reserved
        32, 0x41,       # pixel format size, RGB | ALPHAPIXELS
        b"\0" * 4,      # fourCC (none — uncompressed)
        32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000,  # bpp + ARGB masks
        0x1000, 0, 0, 0, 0,  # caps (TEXTURE), caps2-4, reserved
    )
    assert len(hdr) == 128, len(hdr)
    return hdr


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    path, colour = sys.argv[1], sys.argv[2]
    if colour == "normal":
        colour = FLAT_NORMAL
    rgb = colour.lstrip("#")
    r, g, b = (int(rgb[i:i + 2], 16) for i in (0, 2, 4))
    pixel = bytes((b, g, r, 0xFF))  # A8R8G8B8 is little-endian BGRA on disk
    with open(path, "wb") as out:
        out.write(header(4, 4) + pixel * 16)
    print(f"wrote {path} ({colour})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
