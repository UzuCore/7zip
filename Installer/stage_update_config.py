"""Stage the updater and its immutable build identity with the Windows payload."""
import argparse
import json
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("destination", type=Path)
p.add_argument("--arch", choices=["x64", "arm64"], required=True)
p.add_argument("--build-id", type=int, required=True)
a = p.parse_args()
root = Path(__file__).resolve().parent
identity = json.loads((root / "ModernShell/self-signing.json").read_text(encoding="utf-8-sig"))
# Windows PowerShell 5.1 needs a BOM to read Korean source text correctly.
for name in ("Check-Updates.ps1", "Update-Verification.ps1"):
    script = (root / name).read_text(encoding="utf-8-sig")
    (a.destination / name).write_text(script, encoding="utf-8-sig")
(a.destination / "UpdateConfig.json").write_text(json.dumps({
    "schema": 1, "buildId": str(a.build_id), "arch": a.arch,
    "publisherThumbprint": identity["thumbprint"],
}, indent=2) + "\n", encoding="utf-8")
