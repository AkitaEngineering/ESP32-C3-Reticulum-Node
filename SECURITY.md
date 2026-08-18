# Security Policy

**Updated:** 2026-08-18

If you discover a security issue, please follow responsible disclosure:
- Do not open a public issue. Instead contact the maintainers privately (open a private GitHub security advisory).
- Include steps to reproduce, affected versions, and suggested mitigations.

Security recommendations for deployers:
- Never expose the device HTTP API directly to the Internet or an untrusted LAN. Production deployment must place it on an encrypted, access-controlled management network or behind a TLS VPN/gateway. The firmware does not terminate TLS.
- Production manufacturing must use a security-enabled bootloader/firmware build and verify Secure Boot and Flash Encryption eFuse state before shipment. The current pinned Arduino SDK build does not enable either feature, so its artifact is limited to controlled pilots until that build-system migration is complete.
- Verify OTA images with signatures before installing (Ed25519 verification is now supported).

## Signed OTA (Ed25519)
- Enable `OTA_ENABLED` at build time to allow OTA uploads.
- Configure the deployer's Ed25519 `public_key` in `/config.json` under the `api` section (hex-encoded, 32 bytes).
- Sign `SHA-512("RNS-OTA-V1\\0" || version || "\\0" || firmware.bin)` with the deployer's Ed25519 private key. `tools/sign_firmware.sh` implements this contract.
- Upload firmware with both `X-Signature-Ed25519` and `X-Firmware-Version`; same-version and downgrade uploads are rejected.
- The node hashes the upload while streaming it to SPIFFS and verifies the signature before writing the image; invalid signatures are rejected.

Recommendations:
- Keep the private signing key offline and secure.
- Keep the API on an encrypted management network; bearer authentication does not provide transport encryption by itself.
