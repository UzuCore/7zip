"""Check staged contents, binary architecture and archive round trips before publication."""
import argparse, hashlib, struct, subprocess, tempfile
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument("platform", choices=["windows","linux","macos"])
p.add_argument("arch", choices=["x64","arm64"])
p.add_argument("directory", type=Path)
p.add_argument("--skip-run", action="store_true")
a=p.parse_args(); root=a.directory.resolve()
required=["License.txt","copying.txt","unRarLicense.txt","UnEGG-LICENSE.txt","History.txt","readme.txt","UPSTREAM-RESOURCES.txt"]
programs=["7zz.exe"] if a.platform=="windows" else ["7zz"]
if a.platform=="windows":
    required += ["7z.exe","7zFM.exe","7zG.exe","7z.dll","7-zip.dll","7z.sfx","7zCon.sfx","7-zip.chm","Lang/ko.txt"]
    programs += ["7z.exe"]
    if a.arch=="x64": required += ["7-zip32.dll"]
else:
    required += ["MANUAL/start.htm", "MANUAL/style.css"]
    if a.platform=="linux": programs += ["7zzs"]
for name in required+programs:
    assert (root/name).is_file(), "Missing runtime file: "+name
if a.platform=="windows":
    for name in ["7zz.exe","7z.exe","7zFM.exe","7zG.exe","7z.dll","7-zip.dll"] + (["7-zip32.dll"] if a.arch=="x64" else []):
        data=(root/name).read_bytes(); pe=struct.unpack_from("<I",data,0x3c)[0]
        expected=0x14c if name=="7-zip32.dll" else 0x8664 if a.arch=="x64" else 0xaa64
        assert data[pe:pe+4]==b"PE\0\0" and struct.unpack_from("<H",data,pe+4)[0]==expected, name+" architecture mismatch"
elif a.platform=="linux":
    for name in programs:
        data=(root/name).read_bytes()
        assert data[:4]==b"\x7fELF" and struct.unpack_from("<H",data,18)[0]==(62 if a.arch=="x64" else 183)
    elf=subprocess.check_output(["readelf","-l",str(root/"7zzs")],text=True)
    assert "INTERP" not in elf, "7zzs must be statically linked"
else:
    subprocess.run(["lipo","-verify_arch","x86_64" if a.arch=="x64" else "arm64",str(root/"7zz")],check=True)
if not a.skip_run:
    for name in programs:
        with tempfile.TemporaryDirectory() as work:
            work=Path(work); source=work/"input"; source.mkdir()
            data="Unicode filename and archive content 검증\n".encode()*100
            (source/"한글-test.txt").write_bytes(data)
            for fmt in ["7z","zip","alz","egg"]:
                archive=work/("test."+fmt); out=work/("out-"+fmt)
                subprocess.run([str(root/name),"a","-t"+fmt,str(archive),"한글-test.txt"],cwd=source,check=True,stdout=subprocess.DEVNULL)
                subprocess.run([str(root/name),"t",str(archive)],check=True,stdout=subprocess.DEVNULL)
                subprocess.run([str(root/name),"x","-y","-o"+str(out),str(archive)],check=True,stdout=subprocess.DEVNULL)
                assert (out/"한글-test.txt").read_bytes()==data, name+": "+fmt+" roundtrip mismatch"
print("Distribution contents, architecture and applicable archive checks passed:",a.platform,a.arch)
