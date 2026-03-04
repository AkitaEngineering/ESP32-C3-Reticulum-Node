# Security Policy

If you discover a security issue, please follow responsible disclosure:
- Do not open a public issue. Instead contact the maintainers privately (open a private GitHub security advisory).
- Include steps to reproduce, affected versions, and suggested mitigations.

Security recommendations for deployers:
- Use HTTPS when exposing the Web UI remotely.
- Enable Secure Boot / Flash Encryption on supported hardware when used in untrusted environments.
- Verify OTA images with signatures before installing (Ed25519 verification is now supported).

## Signed OTA (Ed25519)
- Enable `OTA_ENABLED` at build time to allow OTA uploads.
- Configure the deployer's Ed25519 `public_key` in `/config.json` under the `api` section (hex-encoded, 32 bytes).
- Upload firmware via the Web UI or POST /api/v1/ota with header `X-Signature-Ed25519: <hex-signature>`.
- The node will verify the signature before writing the image; invalid signatures are rejected.

Recommendations:
- Keep the private signing key offline and secure.
- Use HTTPS and Web UI authentication to protect OTA endpoints.
- Consider adding Secure Boot / Flash Encryption for additional protection.
