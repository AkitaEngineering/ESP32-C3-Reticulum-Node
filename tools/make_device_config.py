#!/usr/bin/env python3
"""Generate per-device production config JSON files.

This is intentionally small and dependency-free so it can run on a
manufacturing laptop or CI release job without extra Python packages.
"""

from __future__ import annotations

import argparse
import csv
import json
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
    parser.add_argument("--rns-app-name", default="esp32.node", help="Reticulum application name")
    parser.add_argument("--api-public-key", default="", help="OTA Ed25519 public key, hex encoded")
    parser.add_argument("--template", default="data/config.example.json", help="Template config JSON")
    parser.add_argument("--out-dir", default="provisioning/devices", help="Output directory")
    parser.add_argument("--manifest", default="provisioning/device_manifest.csv", help="CSV manifest path")
    args = parser.parse_args()

    root = Path.cwd()
    template_path = Path(args.template)
    if not template_path.is_absolute():
        template_path = root / template_path

    config = load_template(template_path)
    config["node_name"] = args.node_name or args.device_id
    config["rns_app_name"] = args.rns_app_name

    wifi = ensure_object(config, "wifi")
    wifi["ssid"] = args.wifi_ssid
    wifi["password"] = args.wifi_password

    api = ensure_object(config, "api")
    api["auth_enabled"] = True
    api["token"] = secrets.token_urlsafe(32)
    api["public_key"] = args.api_public_key

    out_dir = Path(args.out_dir)
    if not out_dir.is_absolute():
        out_dir = root / out_dir
    out_dir.mkdir(parents=True, exist_ok=True)

    config_path = out_dir / f"{args.device_id}.config.json"
    with config_path.open("w", encoding="utf-8") as f:
        json.dump(config, f, indent=2)
        f.write("\n")

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

    print(config_path)
    print(manifest_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
