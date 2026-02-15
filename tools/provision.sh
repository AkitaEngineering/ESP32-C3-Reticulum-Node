#!/usr/bin/env bash
# Simple provisioning script for ESP32 Reticulum Gateway
# Usage: ./provision.sh <device-ip> <port> <token> <config-json>

set -euo pipefail

if [ "$#" -ne 4 ]; then
  echo "Usage: $0 <device-ip> <port> <token> <config-json>"
  exit 2
fi

DEVICE="$1"
PORT="$2"
TOKEN="$3"
CONFIGFILE="$4"

if [ ! -f "$CONFIGFILE" ]; then
  echo "Config file not found: $CONFIGFILE" >&2
  exit 3
fi

curl -s -X POST \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer ${TOKEN}" \
  --data-binary @"${CONFIGFILE}" \
  "http://${DEVICE}:${PORT}/api/v1/config" | jq .

echo "Provisioning request sent to ${DEVICE}:${PORT}" 
