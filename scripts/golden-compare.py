#!/usr/bin/env python3
"""Compare two directories of golden PPMs (scripts/golden-native.sh) pixel for pixel.

  scripts/golden-compare.py <dir A> <dir B> [--diff <dir>]

Prints one line per image: identical, or how many pixels differ and by how much at most.
--diff writes an amplified |A - B| PNG per differing image (needs Pillow). Exits 1 if any
image differs or is missing on one side.
"""
import argparse
import os
import sys


def read_ppm(path):
    # golden-native.sh writes exactly "P6\n<w> <h>\n255\n" then the pixels; splitting the whole
    # file on whitespace would eat pixel bytes that happen to be whitespace.
    with open(path, "rb") as f:
        magic = f.readline().strip()
        width, height = (int(v) for v in f.readline().split())
        maximum = f.readline().strip()
        pixels = f.read()
    if magic != b"P6" or maximum != b"255" or len(pixels) != width * height * 3:
        raise ValueError(f"{path}: not an 8-bit binary PPM as golden-native.sh writes it")
    return width, height, pixels


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("a")
    parser.add_argument("b")
    parser.add_argument("--diff")
    args = parser.parse_args()

    names = sorted(set(n for d in (args.a, args.b) for n in os.listdir(d) if n.endswith(".ppm")))
    failed = False
    for name in names:
        pa, pb = os.path.join(args.a, name), os.path.join(args.b, name)
        if not (os.path.exists(pa) and os.path.exists(pb)):
            print(f"MISSING   {name}")
            failed = True
            continue
        wa, ha, da = read_ppm(pa)
        wb, hb, db = read_ppm(pb)
        if (wa, ha) != (wb, hb):
            print(f"SIZE      {name}: {wa}x{ha} vs {wb}x{hb}")
            failed = True
            continue
        if da == db:
            print(f"identical {name}")
            continue
        failed = True
        differing = 0
        worst = 0
        delta = bytearray(len(da))
        for i in range(0, len(da), 3):
            d = max(abs(da[i] - db[i]), abs(da[i + 1] - db[i + 1]), abs(da[i + 2] - db[i + 2]))
            if d:
                differing += 1
                worst = max(worst, d)
                delta[i] = delta[i + 1] = delta[i + 2] = min(255, d * 16)
        print(f"DIFFERS   {name}: {differing} px ({100.0 * differing / (wa * ha):.3f}%), max channel delta {worst}")
        if args.diff:
            from PIL import Image  # only needed for the visual diff

            os.makedirs(args.diff, exist_ok=True)
            Image.frombytes("RGB", (wa, ha), bytes(delta)).save(os.path.join(args.diff, name[:-4] + ".png"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
