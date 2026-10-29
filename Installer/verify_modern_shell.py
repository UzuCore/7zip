"""Validate sparse shell metadata and native Explorer command enumeration.

The COM smoke test runs only when Python matches the target architecture.
It never registers a package, edits Explorer settings, or invokes user actions.
"""
import argparse, ctypes, struct, sys, sysconfig, tempfile, uuid, zipfile
import hashlib, json
from pathlib import Path
from xml.etree import ElementTree as ET

p = argparse.ArgumentParser()
p.add_argument("--arch", choices=("x64", "arm64"), required=True)
p.add_argument("--directory", type=Path, required=True)
p.add_argument("--dll", type=Path, required=True)
a = p.parse_args()
ns = {"f": "http://schemas.microsoft.com/appx/manifest/foundation/windows10",
      "com": "http://schemas.microsoft.com/appx/manifest/com/windows10",
      "d5": "http://schemas.microsoft.com/appx/manifest/desktop/windows10/5"}
manifest = (a.directory / "AppxManifest.xml").read_bytes()
m = ET.fromstring(manifest)
assert m.find("f:Identity", ns).attrib["ProcessorArchitecture"] == a.arch
clsid = "23170F69-40C1-278A-1000-000100020002"
server = m.find(".//com:Class", ns)
assert server.attrib == {"Id": clsid, "Path": "7-zip.dll", "ThreadingModel": "STA"}
assert {x.attrib["Type"] for x in m.findall(".//d5:ItemType", ns)} == {"*", "Directory"}
assert all(x.attrib["Clsid"] == clsid for x in m.findall(".//d5:Verb", ns))
assert not any(x.suffix.lower() in (".pfx", ".p12", ".key") for x in a.directory.rglob("*")), "Private signing keys must never be packaged"
if (a.directory / "self-signing.json").is_file():
    config = json.loads((a.directory / "self-signing.json").read_text(encoding="utf-8"))
    certificate = (a.directory / "7zip-alpha-signing.cer").read_bytes()
    assert hashlib.sha1(certificate).hexdigest().upper() == config["thumbprint"]
    assert m.find("f:Identity", ns).attrib["Publisher"] == config["subject"]
with zipfile.ZipFile(a.directory / "7zip-alpha-shell.msix") as z:
    assert ET.tostring(ET.fromstring(z.read("AppxManifest.xml"))) == ET.tostring(m)
    for size in (44, 50, 150):
        assert z.read("Assets/Logo%d.png" % size).startswith(b"\x89PNG")
data = a.dll.read_bytes()
pe = struct.unpack_from("<I", data, 0x3c)[0]
assert struct.unpack_from("<H", data, pe + 4)[0] == {"x64": 0x8664, "arm64": 0xaa64}[a.arch]
print("Modern shell manifest, packaged assets and DLL architecture passed", flush=True)
if sys.platform != "win32" or sysconfig.get_platform().lower() != {"x64": "win-amd64", "arm64": "win-arm64"}[a.arch]:
    print("COM execution skipped: Python and target architectures differ")
    sys.exit(0)

HRESULT = ctypes.c_long
PTR = ctypes.c_void_p
UINT = ctypes.c_uint32
tracked = []
def guid(value):
    return ctypes.create_string_buffer(uuid.UUID(value).bytes_le)
def call(obj, slot, types, *args):
    vt = ctypes.cast(obj, ctypes.POINTER(ctypes.POINTER(PTR))).contents
    return ctypes.WINFUNCTYPE(HRESULT, PTR, *types)(vt[slot])(obj, *args)
def keep(obj):
    tracked.append(obj)
    return obj
def title(obj, items=None):
    text = PTR()
    assert call(obj, 3, [PTR, ctypes.POINTER(PTR)], items, ctypes.byref(text)) == 0
    try:
        return ctypes.wstring_at(text)
    finally:
        ctypes.windll.ole32.CoTaskMemFree(text)
ctypes.windll.ole32.CoInitialize(None)
dll = ctypes.WinDLL(str(a.dll.resolve()))
f = dll.DllGetClassObject
f.argtypes = [PTR, PTR, ctypes.POINTER(PTR)]
f.restype = HRESULT
factory = PTR()
assert f(guid(clsid), guid("00000001-0000-0000-c000-000000000046"), ctypes.byref(factory)) == 0
keep(factory)
def root():
    obj = PTR()
    assert call(factory, 3, [PTR, PTR, ctypes.POINTER(PTR)], None,
                guid("a08ce4d0-fa25-44ab-b57c-c7b1c323e0b9"), ctypes.byref(obj)) == 0
    return keep(obj)
def selection(paths):
    pidls = (PTR * len(paths))()
    shell = ctypes.windll.shell32
    for i, path in enumerate(paths):
        assert shell.SHParseDisplayName(ctypes.c_wchar_p(str(path)), None, ctypes.byref(pidls, i * ctypes.sizeof(PTR)), 0, None) == 0
    result = PTR()
    try:
        assert shell.SHCreateShellItemArrayFromIDLists(len(paths), pidls, ctypes.byref(result)) == 0
    finally:
        for pidl in pidls:
            ctypes.windll.ole32.CoTaskMemFree(PTR(pidl))
    return keep(result)
def commands(obj, items):
    assert title(obj, items) == "7zip+\u03b1"
    state = UINT()
    assert call(obj, 7, [PTR, ctypes.c_int, ctypes.POINTER(UINT)], items, 0, ctypes.byref(state)) == -2147483638  # E_PENDING
    assert call(obj, 7, [PTR, ctypes.c_int, ctypes.POINTER(UINT)], items, 1, ctypes.byref(state)) == 0
    assert state.value == 0
    enum = PTR()
    assert call(obj, 10, [ctypes.POINTER(PTR)], ctypes.byref(enum)) == 0
    keep(enum)
    clone = PTR()
    assert call(enum, 6, [ctypes.POINTER(PTR)], ctypes.byref(clone)) == 0
    keep(clone)
    assert call(clone, 4, [UINT], 9999) == 1
    rows = []
    while True:
        child = PTR(); fetched = UINT()
        hr = call(enum, 3, [UINT, ctypes.POINTER(PTR), ctypes.POINTER(UINT)], 1, ctypes.byref(child), ctypes.byref(fetched))
        if hr == 1:
            assert fetched.value == 0
            break
        assert hr == 0 and fetched.value == 1
        keep(child)
        rows.append(title(child, items))
    assert rows
    return rows
try:
    with tempfile.TemporaryDirectory(prefix="7zip-modern-com-") as work:
        work = Path(work)
        folder = work / "folder"; folder.mkdir()
        source = work / "sample.txt"; source.write_text("shell smoke test")
        archive = work / "sample.zip"
        with zipfile.ZipFile(archive, "w") as z:
            z.write(source, source.name)
        for paths in ([source], [folder], [archive], [source, folder]):
            rows = commands(root(), selection(paths))
            assert any(".zip" in row for row in rows)
            print("COM selection passed:", [x.name for x in paths], len(rows), "commands", flush=True)
        # Both CLSIDs must remain available to the classic and modern hosts.
        classic = PTR()
        assert f(guid("23170F69-40C1-278A-1000-000100020001"), guid("00000001-0000-0000-c000-000000000046"), ctypes.byref(classic)) == 0
        keep(classic)
finally:
    for obj in reversed(tracked):
        call(obj, 2, [])
    ctypes.windll.ole32.CoUninitialize()
print("Modern COM state deferral, selection handling, clone and enumeration passed")
