#!/usr/bin/env python3
"""Build per-GPU-format KTX2 texture packs from the deployed DDS tree.

Why: the renderer used to ship DXT/S3TC only, so a GPU without
WEBGL_compressed_texture_s3tc — Safari, iOS, most Android GPUs — got the fallback
checkerboard instead of a planet. `TextureFormatSupport.cpp` picks a pack at startup
from the probed extensions (BC7 > BC3 > ASTC > ETC2); this script produces those packs.

Layout: each pack is a subdirectory *next to* the DDS files, so the C++ path rewrite in
TextureFormats::VariantPath is a pure string operation and the DDS tree stays intact:

    resource/textures_low/Ceres_Diffuse_Low.dds
    resource/textures_low/astc/Ceres_Diffuse_Low.ktx2
    resource/textures_low/bc3/Ceres_Diffuse_Low.ktx2

Important: the output must NOT be supercompressed and must NOT be Basis Universal.
The runtime reader is a container parser only (src/3rdparty/ktx2_reader.cpp) — no
transcoder and no zstd decompressor are linked into the Wasm module, which is what
keeps the size delta at zero. `toktx` is therefore invoked with an explicit
`--encode <format>` and without `--zcmp` / `--encode basis-lz`.

Requirements:
  toktx from Khronos KTX-Software (https://github.com/KhronosGroup/KTX-Software),
  and ImageMagick `convert` (or `magick`) to get the DDS into a PNG toktx can read.
  Neither is a runtime dependency; this is an offline asset step.

Typical use:
  python3 scripts/convert_textures_ktx2.py --formats astc,etc2,bc3
  # then deploy the new subdirectories and build the web bundle with
  #   VITE_TEXTURE_PACKS=astc,etc2,bc3 npm run build
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

DEFAULT_SOURCE_DIRS = [
    "resource/textures_low",
    "resource/textures_mid",
    "resource/textures",
]

# toktx --encode arguments per pack name. Quality is deliberately conservative: these
# are the same source pixels the DDS tier already committed to, so the encoder should
# not be the thing that loses detail.
ENCODERS = {
    "astc": ["--encode", "astc", "--astc_blk_d", "6x6", "--astc_quality", "100"],
    "etc2": ["--encode", "uastc"],  # see NOTE below
    "bc3": ["--encode", "uastc"],
    "bc7": ["--encode", "uastc"],
}

# NOTE: toktx only writes ASTC blocks natively. For the BC and ETC2 packs the practical
# pipeline is a two-step one (uastc -> transcode offline with `ktx transcode`), which
# this script drives via KTX_TRANSCODE_TARGETS below so the *shipped* file is still a
# plain, un-supercompressed block format.
KTX_TRANSCODE_TARGETS = {
    "etc2": "etc2-rgba",
    "bc3": "bc3",
    "bc7": "bc7",
}


def find_tool(names: list[str]) -> str | None:
    for name in names:
        found = shutil.which(name)
        if found:
            return found
    return None


def dds_to_png(convert_tool: str, source: Path, destination: Path) -> bool:
    command = [convert_tool]
    if Path(convert_tool).name == "magick":
        command.append("convert")
    command += [str(source), str(destination)]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  ! decode failed for {source}: {result.stderr.strip()}", file=sys.stderr)
        return False
    return True


def encode_pack(toktx: str, ktx_tool: str | None, pack: str, png: Path, destination: Path) -> bool:
    destination.parent.mkdir(parents=True, exist_ok=True)
    command = [toktx, "--t2", "--genmipmap"] + ENCODERS[pack] + [str(destination), str(png)]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  ! toktx failed for {destination}: {result.stderr.strip()}", file=sys.stderr)
        return False

    target = KTX_TRANSCODE_TARGETS.get(pack)
    if target is None:
        return True
    if ktx_tool is None:
        print(f"  ! pack '{pack}' needs the `ktx` CLI to transcode; skipping", file=sys.stderr)
        destination.unlink(missing_ok=True)
        return False

    transcoded = destination.with_suffix(".transcoded.ktx2")
    result = subprocess.run(
        [ktx_tool, "transcode", "--target", target, str(destination), str(transcoded)],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(f"  ! ktx transcode failed for {destination}: {result.stderr.strip()}", file=sys.stderr)
        destination.unlink(missing_ok=True)
        transcoded.unlink(missing_ok=True)
        return False
    transcoded.replace(destination)
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--formats", default="astc,etc2,bc3",
                        help=f"comma-separated pack names ({', '.join(sorted(ENCODERS))})")
    parser.add_argument("--source-dirs", default=",".join(DEFAULT_SOURCE_DIRS),
                        help="comma-separated DDS directories to convert, relative to the repo root")
    parser.add_argument("--toktx", help="path to toktx (default: search PATH)")
    parser.add_argument("--force", action="store_true", help="re-encode files that already exist")
    parser.add_argument("--dry-run", action="store_true", help="list what would be written and exit")
    args = parser.parse_args()

    packs = [pack.strip() for pack in args.formats.split(",") if pack.strip()]
    unknown = [pack for pack in packs if pack not in ENCODERS]
    if unknown:
        print(f"Unknown pack(s): {', '.join(unknown)}", file=sys.stderr)
        return 2

    toktx = args.toktx or find_tool(["toktx"])
    ktx_tool = find_tool(["ktx"])
    convert_tool = find_tool(["magick", "convert"])
    if not args.dry_run and (toktx is None or convert_tool is None):
        print("toktx and ImageMagick are required; see the module docstring.", file=sys.stderr)
        return 2

    written = 0
    failed = 0
    for relative_dir in (d.strip() for d in args.source_dirs.split(",") if d.strip()):
        source_dir = REPO_ROOT / relative_dir
        if not source_dir.is_dir():
            print(f"skip {relative_dir} (not present)")
            continue

        # Non-recursive on purpose: the cube-map subdirectory (Main_SkyBox) stays on DDS
        # because the runtime KTX2 reader is 2D-only.
        for dds in sorted(source_dir.glob("*.dds")):
            for pack in packs:
                destination = source_dir / pack / f"{dds.stem}.ktx2"
                if destination.exists() and not args.force:
                    continue
                if args.dry_run:
                    print(f"would write {destination.relative_to(REPO_ROOT)}")
                    written += 1
                    continue

                with tempfile.TemporaryDirectory() as scratch:
                    png = Path(scratch) / f"{dds.stem}.png"
                    if not dds_to_png(convert_tool, dds, png):
                        failed += 1
                        continue
                    if encode_pack(toktx, ktx_tool, pack, png, destination):
                        print(f"wrote {destination.relative_to(REPO_ROOT)}")
                        written += 1
                    else:
                        failed += 1

    print(f"\n{written} file(s) {'planned' if args.dry_run else 'written'}, {failed} failed")
    if not args.dry_run and written:
        print("Next: deploy the new subdirectories, then build with "
              f"VITE_TEXTURE_PACKS={','.join(packs)}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
