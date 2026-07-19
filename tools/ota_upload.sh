#!/usr/bin/env bash
# Upload a signed firmware binary to the device OTA endpoint.
# Usage: ./ota_upload.sh <device-ip> <port> <token> <version> <firmware.bin> <signature.hex>

set -euo pipefail

if [ "$#" -ne 6 ]; then
  echo "Usage: $0 <device-ip> <port> <token> <version> <firmware.bin> <signature.hex>"
  exit 2
fi

DEVICE="$1"
PORT="$2"
TOKEN="$3"
VERSION="$4"
FIRMWARE="$5"
SIGFILE="$6"

if [ ! -f "$FIRMWARE" ]; then
  echo "Firmware file not found: $FIRMWARE" >&2
  exit 3
fi
if [ ! -f "$SIGFILE" ]; then
  echo "Signature file not found: $SIGFILE" >&2
  exit 4
fi

SIG=$(cat "$SIGFILE" | tr -d '\n' | tr -d '\r')

curl -v --http1.1 -X POST \
  -H "Authorization: Bearer ${TOKEN}" \
  -H "X-Signature-Ed25519: ${SIG}" \
  -H "X-Firmware-Version: ${VERSION}" \
  --data-binary @"${FIRMWARE}" \
  "http://${DEVICE}:${PORT}/api/v1/ota"

echo "OTA upload attempted to ${DEVICE}:${PORT}"
