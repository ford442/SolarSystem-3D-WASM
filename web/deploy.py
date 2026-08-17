#!/usr/bin/env python3
"""Deploy the web bundle and/or the independently hosted runtime asset tree."""

from __future__ import annotations

import argparse
import getpass
import hashlib
import json
import os
import posixpath
from pathlib import Path, PurePosixPath
from typing import Iterable

import paramiko


WEB_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = WEB_DIR.parent
DEFAULT_APP_SOURCE = WEB_DIR / "dist"
DEFAULT_ASSET_SOURCE = PROJECT_ROOT / "resource"
DEFAULT_MANIFEST = DEFAULT_ASSET_SOURCE / "asset-manifest.json"
ASSET_DIRECTORIES = ("textures", "textures_low", "textures_mid", "sounds")


def env(name: str, default: str | None = None) -> str | None:
    return os.environ.get(f"SOLAR_{name}", default)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Upload the Vite bundle and runtime assets as independent deploy targets."
    )
    parser.add_argument("target", choices=("app", "assets", "all"), help="tree to upload")
    parser.add_argument("--host", default=env("DEPLOY_HOST"))
    parser.add_argument("--port", type=int, default=int(env("DEPLOY_PORT", "22")))
    parser.add_argument("--user", default=env("DEPLOY_USER"))
    parser.add_argument("--password", default=env("DEPLOY_PASSWORD"))
    parser.add_argument("--key-file", default=env("DEPLOY_KEY_FILE"))
    parser.add_argument("--app-source", type=Path, default=DEFAULT_APP_SOURCE)
    parser.add_argument("--asset-source", type=Path, default=DEFAULT_ASSET_SOURCE)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--app-remote", default=env("DEPLOY_APP_REMOTE"))
    parser.add_argument("--asset-remote", default=env("DEPLOY_ASSET_REMOTE"))
    parser.add_argument("--dry-run", action="store_true", help="validate and print without connecting")
    parser.add_argument(
        "--update-manifest",
        action="store_true",
        help="Fill sha256 digests for existing local asset-manifest files before upload",
    )
    args = parser.parse_args()

    if not args.host and not args.dry_run:
        parser.error("--host or SOLAR_DEPLOY_HOST is required")
    if not args.user and not args.dry_run:
        parser.error("--user or SOLAR_DEPLOY_USER is required")
    if args.target in ("app", "all") and not args.app_remote:
        parser.error("--app-remote or SOLAR_DEPLOY_APP_REMOTE is required")
    if args.target in ("assets", "all") and not args.asset_remote:
        parser.error("--asset-remote or SOLAR_DEPLOY_ASSET_REMOTE is required")
    return args


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fill_manifest_checksums(manifest: dict, asset_root: Path) -> int:
    """Set sha256 for every manifest entry whose file exists on disk. Returns update count."""
    updated = 0
    for asset in manifest.get("files", []):
        relative = PurePosixPath(asset["path"])
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError(f"Unsafe manifest path: {relative}")
        local_path = asset_root.joinpath(*relative.parts)
        if not local_path.is_file():
            continue
        digest = sha256(local_path)
        if asset.get("sha256") != digest:
            asset["sha256"] = digest
            updated += 1
    return updated


def load_and_validate_manifest(
    manifest_path: Path, asset_root: Path, *, update_checksums: bool = False
) -> dict:
    with manifest_path.open(encoding="utf-8") as source:
        manifest = json.load(source)

    if update_checksums:
        updated = fill_manifest_checksums(manifest, asset_root)
        with manifest_path.open("w", encoding="utf-8") as sink:
            json.dump(manifest, sink, indent=2)
            sink.write("\n")
        print(f"Updated {updated} sha256 entries in {manifest_path}")

    missing: list[str] = []
    for asset in manifest.get("files", []):
        relative = PurePosixPath(asset["path"])
        if relative.is_absolute() or ".." in relative.parts:
            raise ValueError(f"Unsafe manifest path: {relative}")

        local_path = asset_root.joinpath(*relative.parts)
        if not local_path.is_file():
            if asset.get("optional", False):
                print(f"Skipping optional missing asset: {relative}")
                continue
            missing.append(str(relative))
            continue

        expected = asset.get("sha256")
        if expected and sha256(local_path) != expected.lower():
            raise ValueError(f"SHA256 mismatch for {relative}")

    if missing:
        formatted = "\n  ".join(missing)
        raise FileNotFoundError(f"Required manifest assets are missing:\n  {formatted}")
    return manifest


def iter_files(root: Path) -> Iterable[Path]:
    return (path for path in sorted(root.rglob("*")) if path.is_file())


def ensure_remote_directory(sftp: paramiko.SFTPClient, remote_path: str) -> None:
    current = "/" if remote_path.startswith("/") else ""
    for part in PurePosixPath(remote_path).parts:
        if part == "/":
            continue
        current = posixpath.join(current, part)
        try:
            sftp.stat(current)
        except IOError:
            sftp.mkdir(current)


def upload_file(
    sftp: paramiko.SFTPClient | None, local_path: Path, remote_path: str, dry_run: bool
) -> None:
    print(f"{('[dry-run] ' if dry_run else '')}{local_path} -> {remote_path}")
    if dry_run:
        return
    assert sftp is not None
    ensure_remote_directory(sftp, posixpath.dirname(remote_path))
    sftp.put(str(local_path), remote_path)


def upload_tree(
    sftp: paramiko.SFTPClient | None,
    local_root: Path,
    remote_root: str,
    dry_run: bool,
) -> None:
    if not local_root.is_dir():
        raise FileNotFoundError(f"Local directory does not exist: {local_root}")
    for local_path in iter_files(local_root):
        relative = local_path.relative_to(local_root).as_posix()
        upload_file(sftp, local_path, posixpath.join(remote_root, relative), dry_run)


def upload_assets(
    sftp: paramiko.SFTPClient | None,
    asset_root: Path,
    manifest_path: Path,
    remote_root: str,
    dry_run: bool,
    *,
    update_manifest: bool = False,
) -> None:
    load_and_validate_manifest(
        manifest_path, asset_root, update_checksums=update_manifest
    )
    for directory in ASSET_DIRECTORIES:
        local_directory = asset_root / directory
        if not local_directory.is_dir():
            if directory == "sounds":
                print(f"Skipping optional asset directory: {local_directory}")
                continue
            raise FileNotFoundError(f"Required asset directory does not exist: {local_directory}")
        upload_tree(sftp, local_directory, posixpath.join(remote_root, directory), dry_run)
    upload_file(sftp, manifest_path, posixpath.join(remote_root, manifest_path.name), dry_run)


def connect(args: argparse.Namespace) -> paramiko.SSHClient:
    client = paramiko.SSHClient()
    client.load_system_host_keys()
    client.set_missing_host_key_policy(paramiko.RejectPolicy())

    password = args.password
    if not args.key_file and not password:
        password = getpass.getpass(f"Password for {args.user}@{args.host}: ")

    client.connect(
        hostname=args.host,
        port=args.port,
        username=args.user,
        password=password,
        key_filename=args.key_file,
    )
    return client


def main() -> None:
    args = parse_args()
    client: paramiko.SSHClient | None = None
    sftp: paramiko.SFTPClient | None = None
    try:
        if not args.dry_run:
            client = connect(args)
            sftp = client.open_sftp()

        if args.target in ("app", "all"):
            upload_tree(sftp, args.app_source.resolve(), args.app_remote, args.dry_run)
        if args.target in ("assets", "all"):
            upload_assets(
                sftp,
                args.asset_source.resolve(),
                args.manifest.resolve(),
                args.asset_remote,
                args.dry_run,
                update_manifest=args.update_manifest,
            )
        print("Deployment complete.")
    finally:
        if sftp is not None:
            sftp.close()
        if client is not None:
            client.close()


if __name__ == "__main__":
    main()
