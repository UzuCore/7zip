# 7zip+α distribution baseline

The displayed application name is **7zip+α**. Legacy install paths, registry keys
and UpgradeCode remain stable so previous UzuCore packages can be upgraded.

The baseline is the official 7-Zip 26.05 release (https://www.7-zip.org/download.html).
`upstream-assets.json` pins resource archive URLs and SHA-256 hashes. Update it when
C/7zVersion.h changes. `stage_resources.py` imports only documentation, translations
and help; every executable and DLL is built from this repository's source.

## Windows x64 / ARM64

Runtime: 7z.exe, 7zz.exe (additional standalone CLI), 7zFM.exe, 7zG.exe,
7z.dll, 7-zip.dll, 7z.sfx, 7zCon.sfx, Lang/*.txt, 7-zip.chm, History.txt,
readme.txt, descript.ion and all applicable licenses. x64 also builds the x86
Explorer extension as 7-zip32.dll, matching the upstream x64 distribution.
ARM64 follows the upstream ARM64 layout without that x86 extension.

The MSI and setup EXE install the same payload; the portable .7z has the same
runtime files. MSI owns installation, upgrades, rollback, shortcuts and uninstall
(the upstream Uninstall.exe is intentionally replaced by MSI's uninstall entry).
The setup EXE embeds the MSI. Explorer context/drag-drop handlers and COM classes
are registered declaratively, including the x86 COM view on x64.

The fork uses its own shell CLSID ending in 0001, handler names, install directory
and preference registry key (Software\7-Zip UzuCore) to coexist with upstream.
It does not silently take over archive default apps; the file manager's options
can configure associations as with upstream. On Windows 11, the classic shell
extension appears under **Show more options**. The file manager automatically
selects the system language using shipped language files; Korean is Lang/ko.txt.
MSI installer dialogs remain English, as with the upstream English MSI baseline.
Explorer may need a restart or sign-out to refresh a newly installed extension.

## Linux x64 / ARM64

Like upstream: 7zz, statically linked 7zzs, MANUAL/, History.txt, readme.txt
and licenses in tar.xz. Preserve executable permissions after GitHub artifact
transfer. No desktop GUI or file-manager integration is supplied by upstream.

## macOS x64 / ARM64

Like upstream: 7zz, MANUAL/, History.txt, readme.txt and licenses in tar.xz.
This fork publishes separate architecture archives as requested, while upstream
uses one universal binary archive. No Finder extension, GUI app, DMG or PKG is
provided by the upstream baseline. These builds are unsigned and not notarized.

## Checks

verify_distribution.py checks required files and PE/ELF/Mach-O architecture.
Native builds round-trip Unicode filenames and contents through 7z, zip, alz
and egg; Linux checks the static binary too. macOS ARM64 is cross-built on Intel,
so its architecture is checked without claiming a runtime test on ARM hardware.
WiX builds validate generated explicit components for every runtime resource.
Actual Explorer interactions, UAC installation and uninstall must also be checked
on disposable Windows machines; CI packaging checks do not substitute for these.
