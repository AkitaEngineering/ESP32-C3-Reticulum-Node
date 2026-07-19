#!/usr/bin/env bash
# Sign firmware for the production OTA endpoint.
#
# The device verifies Ed25519(signature,
# SHA-512("RNS-OTA-V1\\0" || version || "\\0" || firmware.bin)).
# The signed version enables replay/downgrade rejection.
#
# Usage:
#   ./tools/sign_firmware.sh <version> <firmware.bin> <ed25519-private-key.pem> <signature.hex>

set -euo pipefail

if [ "$#" -ne 4 ]; then
  echo "Usage: $0 <version> <firmware.bin> <ed25519-private-key.pem> <signature.hex>"
  exit 2
fi

VERSION="$1"
FIRMWARE="$2"
PRIVATE_KEY="$3"
SIGNATURE_HEX="$4"

if ! [[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Version must use MAJOR.MINOR.PATCH format" >&2
  exit 2
fi

if [ ! -f "$FIRMWARE" ]; then
  echo "Firmware file not found: $FIRMWARE" >&2
  exit 3
fi
if [ ! -f "$PRIVATE_KEY" ]; then
  echo "Private key file not found: $PRIVATE_KEY" >&2
  exit 4
fi
if ! command -v openssl >/dev/null 2>&1; then
  echo "openssl is required" >&2
  exit 5
fi
if ! command -v xxd >/dev/null 2>&1; then
  echo "xxd is required" >&2
  exit 6
fi

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

DIGEST_BIN="$TMP_DIR/firmware.sha512.bin"
SIGNATURE_BIN="$TMP_DIR/signature.bin"

{ printf 'RNS-OTA-V1\0%s\0' "$VERSION"; cat "$FIRMWARE"; } | openssl dgst -sha512 -binary > "$DIGEST_BIN"
openssl pkeyutl -sign -rawin -inkey "$PRIVATE_KEY" -in "$DIGEST_BIN" -out "$SIGNATURE_BIN"
xxd -p -c 256 "$SIGNATURE_BIN" > "$SIGNATURE_HEX"

SIG_LEN="$(tr -d '\r\n' < "$SIGNATURE_HEX" | wc -c | tr -d ' ')"
if [ "$SIG_LEN" != "128" ]; then
  echo "Unexpected signature length: $SIG_LEN hex characters" >&2
  exit 7
fi

echo "Wrote OTA signature to $SIGNATURE_HEX"
