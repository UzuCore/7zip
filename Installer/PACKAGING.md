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

### Per-item Explorer commands

The Windows shell menu and its options include **Compress each file/folder to
ZIP** and **Extract each archive to its own folder**, enabled by default for new
settings. Existing custom menu selections can enable them in Options > 7-Zip.
The compression command produces one ZIP per selected item, using the file stem
or full folder name, beside the source items (or in the drag-and-drop target).
Existing output archives are preserved; collisions receive ` (2)`, ` (3)`, etc.
A GUI worker processes the entire selection sequentially, including selections
over 16 items, and stops on cancellation or failure. Extraction uses the existing
archive-name output-folder mode, retaining the normal password, overwrite, and
error dialogs. It does not remove a matching top-level folder inside the archive.

Local Windows x64 validation: built 7zG, 7zFM and the Explorer DLL; invoked both
shell extension commands via COM with temporary files, including 20 selected
items, Unicode/spaces, matching file stems, existing output preservation, nested
folders, and ZIP/7z/ALZ/EGG extraction. These checks did not install the extension
or automate a physical Explorer right-click. The installed Bandizip Korean and
English language files supplied menu semantics; native decompilation was unavailable
because no analysis engine was configured.

### Explorer availability during MSI installation

The MSI sets `MSIRESTARTMANAGERCONTROL=DisableShutdown`. Windows Installer still
checks files in use but uses the classic FilesInUse handling instead of asking
Restart Manager to automatically shut down Explorer or other shell-extension
hosts. If a DLL cannot be replaced while its host is running, close the affected
application manually or complete any restart requested by Windows Installer.
The installer must not kill or restart Explorer through custom actions.

This addresses the 2026-10-29 installation incident with package 26.5.33:
Restart Manager event 10010 reported that Explorer could not be restarted due
to an application/conductor SID mismatch; event 10006 then reported that
Explorer could not be shut down. Installation completed successfully, followed
by Explorer hangs and loss of the taskbar. The cached installed MSI had no
Restart Manager control property, so Windows Installer's automatic behavior
was active. The precise internal Explorer wait causing the residual hang was
not established by a dump analysis.

Windows CI reads the compiled MSI with `verify_windows_installer.ps1` before
embedding that same MSI into the setup EXE, covering both x64 and ARM64 packages.
Reference: [Microsoft MSIRESTARTMANAGERCONTROL documentation](https://learn.microsoft.com/en-us/windows/win32/msi/msirestartmanagercontrol).
