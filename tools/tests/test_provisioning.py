import json
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "tools" / "make_device_config.py"
PUBLIC_KEY = "ab" * 32


class ProvisioningToolTests(unittest.TestCase):
    def run_tool(self, temp: Path, *extra: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                "--device-id",
                "RNS-000001",
                "--api-public-key",
                PUBLIC_KEY,
                "--wifi-ssid",
                "field-net",
                "--template",
                str(ROOT / "data" / "config.example.json"),
                "--out-dir",
                str(temp / "devices"),
                "--manifest",
                str(temp / "manifest.csv"),
                *extra,
            ],
            text=True,
            capture_output=True,
            check=False,
        )

    def test_generates_valid_secret_files_with_restrictive_permissions(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            result = self.run_tool(temp, "--wifi-password", "test-password")
            self.assertEqual(result.returncode, 0, result.stderr)
            config_path = temp / "devices" / "RNS-000001.config.json"
            manifest_path = temp / "manifest.csv"
            config = json.loads(config_path.read_text(encoding="utf-8"))
            self.assertGreaterEqual(len(config["api"]["token"]), 32)
            self.assertEqual(config["api"]["public_key"], PUBLIC_KEY)
            self.assertEqual(config["api"]["public_key_id"], hashlib.sha256(bytes.fromhex(PUBLIC_KEY)).hexdigest()[:16])
            self.assertEqual(config["wifi"]["ssid"], "field-net")
            self.assertEqual(config_path.stat().st_mode & 0o777, 0o600)
            self.assertEqual(manifest_path.stat().st_mode & 0o777, 0o600)

    def test_refuses_to_overwrite_a_device_identity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            self.assertEqual(self.run_tool(temp).returncode, 0)
            self.assertNotEqual(self.run_tool(temp).returncode, 0)

    def test_rejects_path_traversal_device_id(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--device-id",
                    "../../escape",
                    "--api-public-key",
                    PUBLIC_KEY,
                    "--out-dir",
                    directory,
                ],
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertNotEqual(result.returncode, 0)

    def test_rejects_invalid_wifi_password_and_zero_key(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            self.assertNotEqual(self.run_tool(temp, "--wifi-password", "short").returncode, 0)
            result = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--device-id",
                    "RNS-000002",
                    "--api-public-key",
                    "00" * 32,
                    "--wifi-ssid",
                    "field-net",
                    "--out-dir",
                    str(temp / "zero-key"),
                ],
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
