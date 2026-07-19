import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "package_release.py"
VERSION = "0.3.1"
OTA_DOMAIN = b"RNS-OTA-V1\0"
ED25519_SPKI_PREFIX = bytes.fromhex("302a300506032b6570032100")


class ReleasePackageTests(unittest.TestCase):
    def test_verifies_signature_and_uses_raw_public_key_id(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            private_key = temp / "ota-private.pem"
            public_key = temp / "ota-public.pem"
            firmware = temp / "firmware.bin"
            digest_file = temp / "digest.bin"
            signature_binary = temp / "signature.bin"
            signature_hex = temp / "signature.hex"
            output = temp / "release"

            subprocess.run(
                ["openssl", "genpkey", "-algorithm", "ED25519", "-out", str(private_key)],
                check=True,
                capture_output=True,
            )
            subprocess.run(
                ["openssl", "pkey", "-in", str(private_key), "-pubout", "-out", str(public_key)],
                check=True,
                capture_output=True,
            )
            firmware.write_bytes(b"test-image\0version=" + VERSION.encode("ascii") + b"\0payload")
            digest_file.write_bytes(
                hashlib.sha512(
                    OTA_DOMAIN + VERSION.encode("ascii") + b"\0" + firmware.read_bytes()
                ).digest()
            )
            subprocess.run(
                [
                    "openssl", "pkeyutl", "-sign", "-rawin", "-inkey", str(private_key),
                    "-in", str(digest_file), "-out", str(signature_binary),
                ],
                check=True,
                capture_output=True,
            )
            signature_hex.write_text(signature_binary.read_bytes().hex() + "\n", encoding="ascii")

            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--version", VERSION,
                    "--firmware", str(firmware),
                    "--signature", str(signature_hex),
                    "--public-key", str(public_key),
                    "--changelog", str(ROOT / "CHANGELOG.md"),
                    "--output-dir", str(output),
                    "--allow-dirty",
                ],
                cwd=ROOT,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)

            public_der = subprocess.run(
                ["openssl", "pkey", "-pubin", "-in", str(public_key), "-outform", "DER"],
                check=True,
                capture_output=True,
            ).stdout
            self.assertTrue(public_der.startswith(ED25519_SPKI_PREFIX))
            raw_public_key = public_der[len(ED25519_SPKI_PREFIX):]
            manifest = json.loads((output / "release-manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(manifest["public_key_id"], hashlib.sha256(raw_public_key).hexdigest()[:16])
            self.assertEqual(manifest["version"], VERSION)
            self.assertIn("release-manifest.json", (output / "SHA256SUMS").read_text(encoding="ascii"))

    def test_rejects_invalid_semantic_version(self) -> None:
        result = subprocess.run(
            [
                sys.executable, str(SCRIPT), "--version", "v1",
                "--firmware", "missing", "--signature", "missing",
                "--public-key", "missing", "--output-dir", "missing",
            ],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("MAJOR.MINOR.PATCH", result.stderr)


if __name__ == "__main__":
    unittest.main()
