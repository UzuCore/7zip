"""Generate explicit MSI components for every staged runtime file and translation."""
import hashlib, sys
from pathlib import Path
from xml.etree import ElementTree as ET
ns = "http://wixtoolset.org/schemas/v4/wxs"
ET.register_namespace("", ns)
def node(parent, tag, **attrs):
    return ET.SubElement(parent, "{" + ns + "}" + tag, attrs)
payload, output = map(Path, sys.argv[1:])
wix = ET.Element("{" + ns + "}Wix")
fragment = node(wix, "Fragment")
dirroot = node(fragment, "DirectoryRef", Id="INSTALLFOLDER")
group = node(fragment, "ComponentGroup", Id="PayloadComponents")
dirs = {".": dirroot}
for path in sorted(payload.rglob("*")):
    if not path.is_file() or (path.name.startswith("7zip-windows-") or path.name == "7-zip32.dll"):
        continue
    rel = path.relative_to(payload)
    for parent in reversed(rel.parents):
        key = parent.as_posix()
        if key not in dirs:
            ident = "D_" + hashlib.sha256(key.encode()).hexdigest()[:16]
            dirs[key] = node(dirs[parent.parent.as_posix()], "Directory", Id=ident, Name=parent.name)
    ident = hashlib.sha256(rel.as_posix().encode()).hexdigest()[:16]
    component = node(dirs[rel.parent.as_posix()], "Component", Id="C_"+ident, Guid="*")
    file_id = "SevenZipFileManager" if rel.as_posix() == "7zFM.exe" else "F_"+ident
    node(component, "File", Id=file_id, Source=str(path.resolve()), KeyPath="yes")
    node(group, "ComponentRef", Id="C_"+ident)
ET.indent(wix)
ET.ElementTree(wix).write(output, encoding="utf-8", xml_declaration=True)
