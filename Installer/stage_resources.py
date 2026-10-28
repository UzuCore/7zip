"""Stage version-matched upstream translations/help/manuals; never ship upstream executables."""
import argparse, hashlib, json, shutil, subprocess, tarfile, tempfile, urllib.request
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("platform", choices=["windows", "linux", "macos"])
p.add_argument("arch", choices=["x64", "arm64"])
p.add_argument("destination", type=Path)
p.add_argument("--extractor", type=Path)
p.add_argument("--cache", type=Path)
a = p.parse_args()
root = Path(__file__).resolve().parent.parent
manifest = json.loads((root / "Installer/upstream-assets.json").read_text())
header = (root / "C/7zVersion.h").read_text()
import re
version = re.search(r'^#define MY_VERSION_NUMBERS "([^"]+)"', header, re.M).group(1)
if version != manifest["version"]:
    raise SystemExit("Update pinned upstream resources to match C/7zVersion.h")
name = (f"7z2605-{a.arch}.exe" if a.platform == "windows" else
        f"7z2605-linux-{a.arch}.tar.xz" if a.platform == "linux" else "7z2605-mac.tar.xz")
a.destination.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory() as work:
    work = Path(work)
    archive = work / name
    if a.cache:
        shutil.copyfile(a.cache / name, archive)
    else:
        with urllib.request.urlopen(manifest["base_url"] + name) as response:
            archive.write_bytes(response.read())
    if hashlib.sha256(archive.read_bytes()).hexdigest() != manifest["sha256"][name]:
        raise SystemExit("Upstream resource SHA-256 mismatch")
    if a.platform == "windows":
        if not a.extractor:
            raise SystemExit("--extractor is required")
        unpacked = work / "unpacked"
        subprocess.run([str(a.extractor.resolve()), "x", "-y", "-o" + str(unpacked),
                        str(archive), "Lang/*", "7-zip.chm", "History.txt", "readme.txt", "descript.ion"], check=True)
        for item in unpacked.iterdir():
            if item.is_dir():
                shutil.copytree(item, a.destination / item.name, dirs_exist_ok=True)
            else:
                shutil.copyfile(item, a.destination / item.name)
        for language in (a.destination / "Lang").glob("*.txt"):
            lines = language.read_text(encoding="utf-8-sig").splitlines(keepends=True)
            preserved_identifier = False
            for index, line in enumerate(lines):
                # Lang2 entry 0 identifies the upstream file format, not the UI brand.
                if not preserved_identifier and line.strip() == "7-Zip":
                    preserved_identifier = True
                elif not line.lstrip().startswith(";"):
                    lines[index] = line.replace("7-Zip", "7zip+α")
            assert preserved_identifier, "Missing Lang2 product identifier"
            language.write_text("".join(lines), encoding="utf-8-sig")
        assert (a.destination / "Lang/ko.txt").is_file()
        assert (a.destination / "7-zip.chm").is_file()
    else:
        # Explicit allowlist: downloaded binary programs are never extracted.
        with tarfile.open(archive) as tar:
            for member in tar.getmembers():
                parts = Path(member.name).parts
                if member.isfile() and (member.name in ("History.txt", "readme.txt") or
                                        (parts and parts[0] == "MANUAL")):
                    if ".." in parts or Path(member.name).is_absolute():
                        raise SystemExit("Unsafe upstream archive member")
                    target = a.destination / member.name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(tar.extractfile(member).read())
        assert (a.destination / "MANUAL/start.htm").is_file()
    (a.destination / "UPSTREAM-RESOURCES.txt").write_text(
        "Documentation/translations from 7-Zip " + manifest["version"] + " by Igor Pavlov.\n" +
        manifest["base_url"] + name + "\nSHA256: " + manifest["sha256"][name] + "\n" +
        "Executable programs in this package are built from the UzuCore fork.\n")
