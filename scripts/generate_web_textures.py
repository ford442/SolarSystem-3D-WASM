#!/usr/bin/env python3
"""Generate WebGL-safe DDS textures for the WASM deployment.

Full-resolution planet textures (e.g. 16384x8192) exceed GL_MAX_TEXTURE_SIZE on
many browsers and were causing black orbs after LOD reload. This script builds
a deployable tree of BC3/DXT5 DDS files capped at 8192 px with a full mip
chain, plus optional textures_low placeholders for staged loading.

Requirements:
  Microsoft texconv (DirectXTex). Run scripts/install_texconv.sh to download
  Texconv.exe (Wine is used automatically on Linux), or pass --texconv.

Typical workflow:
  # 1) Point --input-dir at your full-res library (local checkout or mount)
  python3 scripts/generate_web_textures.py \\
      --input-dir /path/to/full-res/resource/textures \\
      --output-dir resource/textures_web \\
      --low-res-dir resource/textures_low

  # 2) Upload output-dir contents to the server:
  #    test.1ink.us/solar-system/resource/textures/
  #    (singular "resource", not "resources")

  # 3) Optionally copy/replace textures_low on the server too.

The game fetches runtime assets from:
  {origin}/solar-system/resource/textures/<name>.dds
"""

from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Optional


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INPUT = PROJECT_ROOT / "resource" / "textures"
DEFAULT_OUTPUT = PROJECT_ROOT / "resource" / "textures_web"
DEFAULT_LOW_RES = PROJECT_ROOT / "resource" / "textures_low"

# Runtime high-res paths referenced by the C++ LOD system and staged loading.
GAME_TEXTURE_MANIFEST = (
    "Earth_Day_Diffuse.dds",
    "Earth_Normal.dds",
    "Earth_Specular.dds",
    "Earth_Clouds_Diffuse.dds",
    "Earth_Clouds_Normal.dds",
    "Earth_Night_Diffuse.dds",
    "Mercury_Diffuse.dds",
    "Mercury_Normal.dds",
    "Mercury_Specular.dds",
    "Venus_Diffuse.dds",
    "Venus_Normal.dds",
    "Mars_Diffuse.dds",
    "Mars_Normal.dds",
    "Jupiter_Diffuse.dds",
    "Jupiter_Normal.dds",
    "Saturn_Diffuse.dds",
    "Saturn_Normal.dds",
    "Saturn_Rings.dds",
    "Uranus_Diffuse.dds",
    "Uranus_Normal.dds",
    "Uranus_Clouds_Diffuse.dds",
    "Uranus_Clouds_Normal.dds",
    "Uranus_Rings.dds",
    "Neptune_Diffuse.dds",
    "Neptune_Normal.dds",
    "Neptune_Clouds_Diffuse.dds",
    "Neptune_Clouds_Normal.dds",
    "Pluto_Diffuse.dds",
    "Pluto_Normal.dds",
    "Pluto_Specular.dds",
    "Moon_Diffuse.dds",
    "Moon_Normal.dds",
    "Phobos_Diffuse.dds",
    "Phobos_Normal.dds",
    "Deimos_Diffuse.dds",
    "Deimos_Normal.dds",
    "Io_Diffuse.dds",
    "Io_Normal.dds",
    "Europa_Diffuse.dds",
    "Europa_Normal.dds",
    "Ganymede_Diffuse.dds",
    "Ganymede_Normal.dds",
    "Callisto_Diffuse.dds",
    "Callisto_Normal.dds",
    "Mimas_Diffuse.dds",
    "Mimas_Normal.dds",
    "Enceladus_Diffuse.dds",
    "Enceladus_Normal.dds",
    "Tethys_Diffuse.dds",
    "Tethys_Normal.dds",
    "Dione_Diffuse.dds",
    "Dione_Normal.dds",
    "Rhea_Diffuse.dds",
    "Rhea_Normal.dds",
    "Titan_Diffuse.dds",
    "Titan_Normal.dds",
    "Iapetus_Diffuse.dds",
    "Iapetus_Normal.dds",
    "Miranda_Diffuse.dds",
    "Miranda_Normal.dds",
    "Ariel_Diffuse.dds",
    "Ariel_Normal.dds",
    "Umbriel_Diffuse.dds",
    "Umbriel_Normal.dds",
    "Titania_Diffuse.dds",
    "Titania_Normal.dds",
    "Oberon_Diffuse.dds",
    "Oberon_Normal.dds",
    "Triton_Diffuse.dds",
    "Triton_Normal.dds",
    "Charon_Diffuse.dds",
    "Charon_Normal.dds",
    "Charon_Specular.dds",
)


@dataclass(frozen=True)
class DdsInfo:
    width: int
    height: int

    @property
    def max_dimension(self) -> int:
        return max(self.width, self.height)


@dataclass
class JobResult:
    source: Path
    output: Path
    action: str
    source_size: DdsInfo
    output_size: Optional[DdsInfo] = None
    error: Optional[str] = None


def read_dds_info(path: Path) -> DdsInfo:
    with path.open("rb") as handle:
        header = handle.read(128)
    if len(header) < 20 or header[:4] != b"DDS ":
        raise ValueError(f"Not a DDS file: {path}")
    height, width = struct.unpack_from("<II", header, 12)
    if width <= 0 or height <= 0:
        raise ValueError(f"Invalid DDS dimensions in {path}: {width}x{height}")
    return DdsInfo(width=width, height=height)


def find_texconv(explicit: Optional[str]) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit))
    env_path = os.environ.get("TEXCONV")
    if env_path:
        candidates.append(Path(env_path))
    candidates.extend(
        [
            PROJECT_ROOT / "scripts" / "bin" / "texconv",
            PROJECT_ROOT / "external" / "texconv" / "texconv",
            Path("/usr/local/bin/texconv"),
        ]
    )
    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate.resolve()

    for name in ("texconv",):
        found = shutil.which(name)
        if found:
            return Path(found).resolve()

    raise FileNotFoundError(
        "texconv not found. Run scripts/install_texconv.sh, add texconv to PATH,\n"
        "or pass --texconv /path/to/texconv\n"
        "Release builds: https://github.com/microsoft/DirectXTex/releases"
    )


def iter_input_files(input_dir: Path, manifest_only: bool) -> Iterable[Path]:
    if manifest_only:
        for name in GAME_TEXTURE_MANIFEST:
            candidate = input_dir / name
            if candidate.is_file():
                yield candidate
            else:
                print(f"[skip] manifest entry missing: {candidate}", file=sys.stderr)
        return

    for path in sorted(input_dir.rglob("*.dds")):
        if path.is_file():
            yield path


def low_res_name(relative_path: Path) -> Path:
    return relative_path.with_name(f"{relative_path.stem}_Low{relative_path.suffix}")


def should_skip(source: Path, output: Path, force: bool) -> bool:
    if force or not output.is_file():
        return False
    return output.stat().st_mtime >= source.stat().st_mtime


def run_texconv(
    texconv: Path,
    source: Path,
    output_dir: Path,
    max_size: int,
    resize: bool,
    dry_run: bool,
) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    cmd = [str(texconv), "-y"]
    if resize:
        cmd.extend(["-maxsize", str(max_size)])
    cmd.extend(
        [
            "-f",
            "BC3_UNORM",
            "-m",
            "0",
            "-if",
            "DDS",
            "-of",
            "DDS",
            "-o",
            str(output_dir),
            str(source),
        ]
    )

    if dry_run:
        print("[dry-run]", " ".join(cmd))
        return output_dir / source.name

    completed = subprocess.run(cmd, capture_output=True, text=True)
    if completed.returncode != 0:
        stderr = completed.stderr.strip() or completed.stdout.strip()
        raise RuntimeError(stderr or f"texconv failed with exit code {completed.returncode}")

    produced = output_dir / source.name
    if not produced.is_file():
        raise RuntimeError(f"texconv did not produce {produced}")
    return produced


def process_file(
    source: Path,
    input_dir: Path,
    output_dir: Path,
    low_res_dir: Optional[Path],
    low_res_max_size: int,
    max_size: int,
    texconv: Path,
    force: bool,
    dry_run: bool,
    build_low_res: bool,
) -> list[JobResult]:
    results: list[JobResult] = []
    relative = source.relative_to(input_dir)
    target = output_dir / relative
    target.parent.mkdir(parents=True, exist_ok=True)

    try:
        info = read_dds_info(source)
    except ValueError as exc:
        return [
            JobResult(
                source=source,
                output=target,
                action="error",
                source_size=DdsInfo(0, 0),
                error=str(exc),
            )
        ]

    resize = info.max_dimension > max_size
    if should_skip(source, target, force):
        results.append(
            JobResult(
                source=source,
                output=target,
                action="skip",
                source_size=info,
                output_size=read_dds_info(target) if target.is_file() else None,
            )
        )
    else:
        try:
            produced = run_texconv(
                texconv=texconv,
                source=source,
                output_dir=target.parent,
                max_size=max_size,
                resize=resize,
                dry_run=dry_run,
            )
            out_info = read_dds_info(produced) if produced.is_file() and not dry_run else None
            results.append(
                JobResult(
                    source=source,
                    output=produced,
                    action="resize" if resize else "remipmap",
                    source_size=info,
                    output_size=out_info,
                )
            )
        except RuntimeError as exc:
            results.append(
                JobResult(
                    source=source,
                    output=target,
                    action="error",
                    source_size=info,
                    error=str(exc),
                )
            )
            return results

    if build_low_res and low_res_dir is not None:
        low_target = low_res_dir / low_res_name(relative)
        low_target.parent.mkdir(parents=True, exist_ok=True)
        low_source = target if target.is_file() else source
        try:
            low_info = read_dds_info(low_source)
        except ValueError as exc:
            results.append(
                JobResult(
                    source=low_source,
                    output=low_target,
                    action="error",
                    source_size=DdsInfo(0, 0),
                    error=str(exc),
                )
            )
            return results

        low_resize = low_info.max_dimension > low_res_max_size
        if should_skip(low_source, low_target, force):
            results.append(
                JobResult(
                    source=low_source,
                    output=low_target,
                    action="skip-low",
                    source_size=low_info,
                    output_size=read_dds_info(low_target) if low_target.is_file() else None,
                )
            )
            return results

        try:
            produced_low = run_texconv(
                texconv=texconv,
                source=low_source,
                output_dir=low_target.parent,
                max_size=low_res_max_size,
                resize=low_resize,
                dry_run=dry_run,
            )
            final_low = low_target
            if produced_low != low_target and not dry_run:
                produced_low.rename(low_target)
            out_low = read_dds_info(final_low) if final_low.is_file() and not dry_run else None
            results.append(
                JobResult(
                    source=low_source,
                    output=final_low,
                    action="low-resize" if low_resize else "low-remipmap",
                    source_size=low_info,
                    output_size=out_low,
                )
            )
        except RuntimeError as exc:
            results.append(
                JobResult(
                    source=low_source,
                    output=low_target,
                    action="error",
                    source_size=low_info,
                    error=str(exc),
                )
            )

    return results


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate WebGL-safe DDS textures (BC3/DXT5, capped size, full mip chain)."
    )
    parser.add_argument(
        "--input-dir",
        type=Path,
        default=DEFAULT_INPUT,
        help=f"Source texture tree (default: {DEFAULT_INPUT})",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT,
        help=f"Web deployment output tree (default: {DEFAULT_OUTPUT})",
    )
    parser.add_argument(
        "--low-res-dir",
        type=Path,
        default=DEFAULT_LOW_RES,
        help=f"Optional low-res output tree (default: {DEFAULT_LOW_RES})",
    )
    parser.add_argument(
        "--no-low-res",
        action="store_true",
        help="Skip generating textures_low variants",
    )
    parser.add_argument(
        "--max-size",
        type=int,
        default=8192,
        help="Maximum width/height for web high-res textures (default: 8192)",
    )
    parser.add_argument(
        "--low-res-max-size",
        type=int,
        default=512,
        help="Maximum width/height for textures_low (default: 512)",
    )
    parser.add_argument(
        "--texconv",
        type=str,
        default=None,
        help="Path to texconv binary (otherwise searched on PATH)",
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=max(1, os.cpu_count() or 1),
        help="Parallel texconv workers (default: CPU count)",
    )
    parser.add_argument(
        "--manifest-only",
        action="store_true",
        help="Only process the DDS files referenced by the game runtime",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Rebuild even when outputs are newer than inputs",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print actions without running texconv",
    )
    return parser.parse_args()


def print_summary(results: list[JobResult]) -> int:
    counts: dict[str, int] = {}
    errors = 0
    for result in results:
        counts[result.action] = counts.get(result.action, 0) + 1
        if result.action == "error":
            errors += 1
            print(f"[error] {result.source}: {result.error}", file=sys.stderr)
            continue

        if result.output_size is None:
            print(f"[{result.action}] {result.source.name}")
            continue

        print(
            f"[{result.action}] {result.source.name}: "
            f"{result.source_size.width}x{result.source_size.height} -> "
            f"{result.output_size.width}x{result.output_size.height} "
            f"({result.output})"
        )

    print("\nSummary:")
    for action, count in sorted(counts.items()):
        print(f"  {action}: {count}")
    if errors:
        print(f"\nCompleted with {errors} error(s).", file=sys.stderr)
        return 1
    print("\nUpload the output tree to: https://<host>/solar-system/resource/textures/")
    return 0


def main() -> int:
    args = parse_args()
    input_dir = args.input_dir.resolve()
    output_dir = args.output_dir.resolve()
    low_res_dir = None if args.no_low_res else args.low_res_dir.resolve()

    if not input_dir.is_dir():
        print(f"Input directory not found: {input_dir}", file=sys.stderr)
        print(
            "Point --input-dir at your full-resolution texture library.\n"
            "Example: --input-dir /mnt/textures/resource/textures",
            file=sys.stderr,
        )
        return 1

    try:
        texconv = find_texconv(args.texconv)
    except FileNotFoundError as exc:
        print(exc, file=sys.stderr)
        return 1

    sources = list(iter_input_files(input_dir, args.manifest_only))
    if not sources:
        print(f"No .dds files found under {input_dir}", file=sys.stderr)
        return 1

    print(f"texconv: {texconv}")
    print(f"input:   {input_dir}")
    print(f"output:  {output_dir}")
    if low_res_dir is not None:
        print(f"low-res: {low_res_dir} (max {args.low_res_max_size}px)")
    print(f"files:   {len(sources)}")
    print(f"max size: {args.max_size}px")
    if args.dry_run:
        print("mode: dry-run")

    results: list[JobResult] = []
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = [
            pool.submit(
                process_file,
                source,
                input_dir,
                output_dir,
                low_res_dir,
                args.low_res_max_size,
                args.max_size,
                texconv,
                args.force,
                args.dry_run,
                low_res_dir is not None,
            )
            for source in sources
        ]
        for future in as_completed(futures):
            results.extend(future.result())

    results.sort(key=lambda item: str(item.source))
    return print_summary(results)


if __name__ == "__main__":
    raise SystemExit(main())
