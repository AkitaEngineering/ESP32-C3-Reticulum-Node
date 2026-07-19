#!/usr/bin/env python3
"""Generate per-device production config JSON files.

This is intentionally small and dependency-free so it can run on a
manufacturing laptop or CI release job without extra Python packages.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import secrets
from datetime import datetime, timezone
from pathlib import Path


def load_template(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as f:
        data = json.load(f)
    if not isinstance(data, dict):
        raise ValueError("config template must be a JSON object")
    return data


def ensure_object(parent: dict, key: str) -> dict:
    value = parent.get(key)
    if not isinstance(value, dict):
        value = {}
        parent[key] = value
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate a per-device config.json")
    parser.add_argument("--device-id", required=True, help="Stable device ID or serial number")
    parser.add_argument("--node-name", help="Friendly node name; defaults to device ID")
    parser.add_argument("--wifi-ssid", default="", help="WiFi SSID for STA mode")
    parser.add_argument("--wifi-password", default="", help="WiFi password for STA mode")
    parser.add_argument("--allow-espnow-only", action="store_true", help="Allow an empty WiFi SSID for offline USB/ESP-NOW units")
    parser.add_argument("--rns-app-name", default="esp32.node", help="Reticulum application name")
    parser.add_argument("--api-public-key", required=True, help="OTA Ed25519 public key, 64 hex characters")
    parser.add_argument("--template", default="data/config.example.json", help="Template config JSON")
    parser.add_argument("--out-dir", default="provisioning/devices", help="Output directory")
    parser.add_argument("--manifest", default="provisioning/device_manifest.csv", help="CSV manifest path")
    args = parser.parse_args()

    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}", args.device_id):
        parser.error("--device-id must be 1-64 safe filename characters")
    node_name = args.node_name or args.device_id
    if not 1 <= len(node_name) <= 47:
        parser.error("--node-name must be 1-47 characters")
    if not 1 <= len(args.rns_app_name) <= 63:
        parser.error("--rns-app-name must be 1-63 characters")
    if len(args.wifi_ssid.encode("utf-8")) > 32:
        parser.error("--wifi-ssid must be at most 32 UTF-8 bytes")
    if not args.wifi_ssid and not args.allow_espnow_only:
        parser.error("--wifi-ssid is required for managed production units (or pass --allow-espnow-only)")
    password_bytes = args.wifi_password.encode("utf-8")
    valid_passphrase = (
        not password_bytes
        or (8 <= len(password_bytes) <= 63 and all(0x20 <= byte <= 0x7E for byte in password_bytes))
        or (len(password_bytes) == 64 and re.fullmatch(rb"[0-9A-Fa-f]{64}", password_bytes))
    )
    if not valid_passphrase:
        parser.error("--wifi-password must be empty, an 8-63 character printable WPA passphrase, or a 64-digit hex PSK")
    try:
        public_key_bytes = bytes.fromhex(args.api_public_key)
    except ValueError:
        parser.error("--api-public-key must be hexadecimal")
    if len(public_key_bytes) != 32:
        parser.error("--api-public-key must encode exactly 32 bytes")
    if not any(public_key_bytes):
        parser.error("--api-public-key must not be all zero")

    # Provisioning output contains WiFi credentials and API bearer secrets.
    # Never create it with group/world-readable permissions.
    os.umask(0o077)

    root = Path.cwd()
    template_path = Path(args.template)
    if not template_path.is_absolute():
        template_path = root / template_path

    config = load_template(template_path)
    config["node_name"] = node_name
    config["rns_app_name"] = args.rns_app_name

    wifi = ensure_object(config, "wifi")
    wifi["ssid"] = args.wifi_ssid
    wifi["password"] = args.wifi_password

    api = ensure_object(config, "api")
    api["auth_enabled"] = True
    api["token"] = secrets.token_urlsafe(32)
    api["public_key"] = args.api_public_key.lower()
    api["public_key_id"] = hashlib.sha256(public_key_bytes).hexdigest()[:16]

    out_dir = Path(args.out_dir)
    if not out_dir.is_absolute():
        out_dir = root / out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    config_path = out_dir / f"{args.device_id}.config.json"
    if config_path.exists():
        raise FileExistsError(f"refusing to overwrite existing device config: {config_path}")
    with config_path.open("x", encoding="utf-8") as f:
        json.dump(config, f, indent=2)
        f.write("\n")
    config_path.chmod(0o600)

    manifest_path = Path(args.manifest)
    if not manifest_path.is_absolute():
        manifest_path = root / manifest_path
    manifest_path.parent.mkdir(parents=True, exist_ok=True)

    write_header = not manifest_path.exists()
    with manifest_path.open("a", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=[
                "created_at",
                "device_id",
                "node_name",
                "rns_app_name",
                "config_path",
                "api_token",
                "ota_public_key",
            ],
        )
        if write_header:
            writer.writeheader()
        writer.writerow(
            {
                "created_at": datetime.now(timezone.utc).isoformat(),
                "device_id": args.device_id,
                "node_name": config["node_name"],
                "rns_app_name": config["rns_app_name"],
                "config_path": str(config_path),
                "api_token": api["token"],
                "ota_public_key": api["public_key"],
            }
        )
    manifest_path.chmod(0o600)

    print(config_path)
    print(manifest_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
