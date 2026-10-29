"""Stage a sparse identity package referencing the installed shell DLL."""
import argparse, shutil, xml.etree.ElementTree as ET
from pathlib import Path
from xml.sax.saxutils import escape

p = argparse.ArgumentParser()
p.add_argument("--arch", choices=("x64", "arm64"), required=True)
p.add_argument("--version", required=True)
p.add_argument("--output", type=Path, required=True)
p.add_argument("--publisher", default="CN=UzuCore")
p.add_argument("--preview", action="store_true")
p.add_argument("--self-signed", action="store_true")
a = p.parse_args()
parts = a.version.split(".")
if len(parts) != 4 or any(not s.isdecimal() or int(s) > 65535 for s in parts):
    p.error("Version must contain four integers in the range 0..65535")
root = Path(__file__).resolve().parent
source = root / "ModernShell"
a.output.mkdir(parents=True, exist_ok=True)
if a.self_signed:
    import json
    config = json.loads((source / "self-signing.json").read_text(encoding="utf-8"))
    a.publisher = config["subject"]
    for filename in ("self-signing.json", "7zip-alpha-signing.cer"):
        shutil.copy2(source / filename, a.output / filename)
name = "UzuCore.7zipAlpha.ContextMenu" + (".Preview" if a.preview else "")
text = (source / "AppxManifest.xml.in").read_text(encoding="utf-8")
for token, value in {"NAME": name, "PUBLISHER": a.publisher, "VERSION": a.version, "ARCH": a.arch}.items():
    text = text.replace("@" + token + "@", escape(value, {'"': "&quot;"}))
ET.fromstring(text)
(a.output / "AppxManifest.xml").write_bytes(text.encode("utf-8"))
shutil.copytree(source / "Assets", a.output / "Assets", dirs_exist_ok=True)
shutil.copy2(source / "Register-ModernShell.ps1", a.output / "Register-ModernShell.ps1")
print("Staged modern shell identity:", name, a.arch, a.version)
