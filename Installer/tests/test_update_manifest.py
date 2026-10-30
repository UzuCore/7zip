import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

GENERATOR = Path(__file__).resolve().parents[1] / "generate_update_manifest.py"


class UpdateManifestTests(unittest.TestCase):
    def test_manifest_pins_actual_installer_bytes_and_both_architectures(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for arch in ("x64", "arm64"):
                payload = root / f"7zip-windows-{arch}"
                payload.mkdir()
                (payload / "UpdateConfig.json").write_text(json.dumps({
                    "buildId": "123", "publisherThumbprint": "A" * 40,
                }))
                (root / f"7zip-windows-{arch}.exe").write_bytes(arch.encode())
            command = [sys.executable, str(GENERATOR), "--artifacts", str(root),
                       "--release", str(root), "--tag", "build-2026.10.30"]
            subprocess.run(command, check=True)
            manifest = json.loads((root / "update-windows.json").read_text())
            self.assertEqual(manifest["buildId"], "123")
            for arch in ("x64", "arm64"):
                self.assertEqual(manifest["windows"][arch]["sha256"],
                                 hashlib.sha256(arch.encode()).hexdigest())
            config = root / "7zip-windows-arm64/UpdateConfig.json"
            config.write_text(json.dumps({"buildId": "124", "publisherThumbprint": "A" * 40}))
            result = subprocess.run(command, capture_output=True)
            self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
