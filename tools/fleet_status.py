#!/usr/bin/env python3
"""Poll /api/v1/status for one or more devices.

Inventory CSV columns:
  device_id,base_url,api_token

Example:
  ./tools/fleet_status.py --inventory provisioning/fleet_inventory.csv
  ./tools/fleet_status.py --device RNS-000001,http://192.168.1.42:80,secret-token
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path


@dataclass
class Device:
    device_id: str
    base_url: str
    api_token: str


def parse_device(value: str) -> Device:
    parts = [part.strip() for part in value.split(",", 2)]
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("device must be device_id,base_url,api_token")
    return Device(parts[0], parts[1].rstrip("/"), parts[2])


def load_inventory(path: Path) -> list[Device]:
    devices: list[Device] = []
    with path.open("r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            device_id = (row.get("device_id") or "").strip()
            base_url = (row.get("base_url") or "").strip().rstrip("/")
            api_token = (row.get("api_token") or "").strip()
            if device_id and base_url and api_token:
                devices.append(Device(device_id, base_url, api_token))
    return devices


def fetch_status(device: Device, timeout_s: float) -> dict:
    request = urllib.request.Request(
        f"{device.base_url}/api/v1/status",
        headers={"Authorization": f"Bearer {device.api_token}"},
    )
    started = time.monotonic()
    with urllib.request.urlopen(request, timeout=timeout_s) as response:
        body = response.read()
    elapsed_ms = int((time.monotonic() - started) * 1000)
    payload = json.loads(body.decode("utf-8"))
    return {
        "device_id": device.device_id,
        "ok": True,
        "elapsed_ms": elapsed_ms,
        "status": payload,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Poll fleet status endpoints")
    parser.add_argument("--inventory", help="CSV with device_id,base_url,api_token columns")
    parser.add_argument("--device", action="append", type=parse_device, default=[], help="device_id,base_url,api_token")
    parser.add_argument("--timeout", type=float, default=5.0, help="HTTP timeout seconds")
    parser.add_argument("--pretty", action="store_true", help="Pretty-print a JSON array instead of JSONL")
    args = parser.parse_args()

    devices: list[Device] = list(args.device)
    if args.inventory:
        devices.extend(load_inventory(Path(args.inventory)))

    if not devices:
        parser.error("provide --inventory or at least one --device")

    results = []
    for device in devices:
        try:
            result = fetch_status(device, args.timeout)
        except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, OSError) as exc:
            result = {
                "device_id": device.device_id,
                "ok": False,
                "error": str(exc),
            }
        results.append(result)

    if args.pretty:
        json.dump(results, sys.stdout, indent=2)
        sys.stdout.write("\n")
    else:
        for result in results:
            sys.stdout.write(json.dumps(result, separators=(",", ":")) + "\n")

    return 0 if all(result["ok"] for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
