#!/usr/bin/env python3
"""Verify and package a signed production firmware release."""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


OTA_DOMAIN = b"RNS-OTA-V1\0"
ED25519_SPKI_PREFIX = bytes.fromhex("302a300506032b6570032100")


def run(*args: str, cwd: Path | None = None) -> bytes:
    return subprocess.run(args, cwd=cwd, check=True, capture_output=True).stdout


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--firmware", required=True, type=Path)
    parser.add_argument("--signature", required=True, type=Path)
    parser.add_argument("--public-key", required=True, type=Path, help="Ed25519 public key in PEM format")
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--changelog", default=Path("CHANGELOG.md"), type=Path)
    parser.add_argument("--allow-dirty", action="store_true", help="Permit packaging an uncommitted tree for local testing")
    args = parser.parse_args()

    if not re.fullmatch(r"\d+\.\d+\.\d+", args.version):
        parser.error("--version must use MAJOR.MINOR.PATCH format")
    for path in (args.firmware, args.signature, args.public_key, args.changelog):
        if not path.is_file():
            parser.error(f"file not found: {path}")
    if args.firmware.stat().st_size == 0:
        parser.error("firmware is empty")
    if args.version.encode("ascii") not in args.firmware.read_bytes():
        parser.error("firmware does not contain the requested embedded version")

    root = Path(__file__).resolve().parents[1]
    dirty = run("git", "status", "--porcelain", cwd=root).decode().strip()
    if dirty and not args.allow_dirty:
        parser.error("refusing to package a dirty worktree")
    commit = run("git", "rev-parse", "HEAD", cwd=root).decode().strip()

    signature_hex = "".join(args.signature.read_text(encoding="ascii").split())
    try:
        signature = bytes.fromhex(signature_hex)
    except ValueError:
        parser.error("signature must be hexadecimal")
    if len(signature) != 64:
        parser.error("signature must encode exactly 64 bytes")

    digest = hashlib.sha512()
    digest.update(OTA_DOMAIN)
    digest.update(args.version.encode("ascii"))
    digest.update(b"\0")
    with args.firmware.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)

    with tempfile.TemporaryDirectory(prefix="rns-release-") as temp_dir:
        temp = Path(temp_dir)
        digest_path = temp / "digest.bin"
        signature_path = temp / "signature.bin"
        digest_path.write_bytes(digest.digest())
        signature_path.write_bytes(signature)
        subprocess.run(
            [
                "openssl", "pkeyutl", "-verify", "-pubin", "-rawin",
                "-inkey", str(args.public_key), "-in", str(digest_path),
                "-sigfile", str(signature_path),
            ],
            check=True,
        )
        public_der = run("openssl", "pkey", "-pubin", "-in", str(args.public_key), "-outform", "DER")
        if not public_der.startswith(ED25519_SPKI_PREFIX) or len(public_der) != len(ED25519_SPKI_PREFIX) + 32:
            parser.error("--public-key must contain an Ed25519 public key")
        public_key_raw = public_der[len(ED25519_SPKI_PREFIX):]

    if args.output_dir.exists() and any(args.output_dir.iterdir()):
        parser.error(f"output directory is not empty: {args.output_dir}")
    args.output_dir.mkdir(parents=True, exist_ok=True)

    firmware_name = f"esp32-c3-prod-managed-{args.version}.bin"
    signature_name = f"esp32-c3-prod-managed-{args.version}.signature.hex"
    firmware_out = args.output_dir / firmware_name
    signature_out = args.output_dir / signature_name
    changelog_out = args.output_dir / "CHANGELOG.md"
    shutil.copy2(args.firmware, firmware_out)
    signature_out.write_text(signature_hex + "\n", encoding="ascii")
    shutil.copy2(args.changelog, changelog_out)

    checksums = {
        firmware_name: sha256(firmware_out),
        signature_name: sha256(signature_out),
        changelog_out.name: sha256(changelog_out),
    }
    # This must match make_device_config.py, which receives the raw 32-byte key.
    key_id = hashlib.sha256(public_key_raw).hexdigest()[:16]
    manifest = {
        "schema": 1,
        "product": "ESP32-RNS-GW-PROD",
        "target": "esp32-c3-prod-managed",
        "version": args.version,
        "git_commit": commit,
        "created_at": datetime.now(timezone.utc).isoformat(),
        "ota_signature_contract": "Ed25519(SHA-512(RNS-OTA-V1\\0 || version || \\0 || firmware))",
        "public_key_id": key_id,
        "artifacts": checksums,
    }
    manifest_path = args.output_dir / "release-manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    checksums[manifest_path.name] = sha256(manifest_path)
    (args.output_dir / "SHA256SUMS").write_text(
        "".join(f"{digest_value}  {name}\n" for name, digest_value in sorted(checksums.items())),
        encoding="ascii",
    )
    print(args.output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
