"""Publish the identities and SHA-256 hashes of both signed Windows installers."""
import argparse
import hashlib
import json
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("--artifacts", type=Path, required=True)
p.add_argument("--release", type=Path, required=True)
p.add_argument("--tag", required=True)
a = p.parse_args()
manifest = {"schema": 1, "releaseTag": a.tag, "windows": {}}
for arch in ("x64", "arm64"):
    config = json.loads((a.artifacts / f"7zip-windows-{arch}/UpdateConfig.json").read_text())
    if "buildId" in manifest and manifest["buildId"] != config["buildId"]:
        raise SystemExit("Windows builds have different build identities")
    manifest["buildId"] = config["buildId"]
    name = f"7zip-windows-{arch}.exe"
    manifest["windows"][arch] = {
        "name": name, "sha256": hashlib.sha256((a.release / name).read_bytes()).hexdigest(),
        "publisherThumbprint": config["publisherThumbprint"],
    }
(a.release / "update-windows.json").write_text(json.dumps(manifest, indent=2) + "\n")
