# 7zip+α distribution baseline

The displayed application name is **7zip+α**. The default Windows directory is
`C:\Program Files\7z alpha`. Legacy registry keys and UpgradeCode remain stable
so upgrades can detect and remove older packages from the previous directory.

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

The setup EXE and portable .7z contain the same runtime files. The EXE embeds an
internal MSI which owns installation, upgrades, rollback, shortcuts and uninstall
(the upstream Uninstall.exe is intentionally replaced by MSI's uninstall entry).
The setup EXE embeds the MSI. Explorer context/drag-drop handlers and COM classes
are registered declaratively, including the x86 COM view on x64.

Standalone MSI files are no longer published as release assets or Windows build
artifacts. Internal MSI and WiX debug files are created in RUNNER_TEMP only. The
release has eight platform packages plus `update-windows.json` (nine assets).
Updating an existing daily release also removes its two former standalone MSIs.

Major upgrades ignore installer language, so English-registered products are
detected when the Korean UI transform is active. Multiple older product records
are removed inside the installation transaction before new files are installed;
a failed upgrade rolls their removal back instead of leaving no installation.

MSIX shell versions use `major.minor.run-number.attempt` in CI, including reruns.
Local previews must use the latest existing CI run number and its matching run
ID, not a guessed future run number. `shell_package_version.py` assigns local
revisions 32768 through 65535 using a persistent local state file. Rebuilding a
local preview increments that revision; the next CI run has a larger third
component and supersedes it. Two different package contents must never share an
MSIX identity and version: Windows rejects that situation with 0x80073CFB, which
causes MSI error 1722/1603 during shell registration.

The fork uses its own shell CLSID ending in 0001, handler names, install directory
and preference registry key (Software\7-Zip UzuCore) to coexist with upstream.
It does not silently take over archive default apps; the file manager's options
can configure associations as with upstream. On Windows 11, the classic shell
extension appears under **Show more options**. The file manager automatically
selects the system language using shipped language files; Korean is Lang/ko.txt.
The setup EXE selects Korean for a Korean Windows display language and English
otherwise. A single MSI contains the Korean UI as the embedded `ko-KR` transform.
Direct MSI installation defaults to English; use `msiexec /i package.msi
TRANSFORMS=:ko-KR` to select Korean explicitly.
Explorer may need a restart or sign-out to refresh a newly installed extension.

The Windows file manager checks GitHub Releases once per day at startup. Help
contains **Check for updates** and a persistent **Automatically check for updates**
toggle (disabled by default). A new build, including another build on the same
date, prompts before downloading and running the installer. Administrator
permission and the installer consent screens remain interactive. Portable builds
also prompt before installing; their files are not overwritten in place.

After the download and its signature are verified, the updater requests a normal
close of File Manager windows from the same installation and current session.
If an archive job is active, a close is refused, or a window does not exit in
15 seconds, setup is deferred. The updater never force-kills archive jobs or
Windows Explorer. Existing explicit automatic-check preferences are preserved.

Update install logs are kept in `%LOCALAPPDATA%\UzuCore\7zipAlpha\InstallerLogs`.
The setup EXE also creates `%TEMP%\7zip-alpha-setup.log` when the caller did not
request an explicit MSI log. Shell registration keeps its separate transcript
under `%ProgramData%\UzuCore\7zipAlpha\InstallerLogs`.

`UpdateConfig.json` records the build ID, architecture and pinned signing
certificate. The release workflow publishes `update-windows.json` with both
installer hashes and the CI attempt. New attempts of the same CI run can also
be offered as updates. The updater checks HTTPS release locations, SHA-256 and the
pinned Authenticode signer before launching a download. No certificate trust
store changes are made by the update checker. Signing-key rotation requires an
explicitly distributed installer with the new pinned identity.

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

### Windows 11 first-tier context menu

`7-zip.dll` now exposes a separate modern Explorer-command CLSID ending in
`0002`; the classic `IContextMenu` CLSID ending in `0001` remains available.
`ModernShell/AppxManifest.xml.in` supplies sparse package identity and the
`windows.comServer` / `windows.fileExplorerContextMenus` declarations for files
and directories. Native DLL architecture follows the x64/ARM64 build matrix.
The root command defers filesystem/menu construction from Explorer's fast
`GetState` call, retains the shell selection, and exposes the existing archive
commands through `IEnumExplorerCommand`.

Windows CI builds an architecture-specific `ShellIntegration/7zip-alpha-shell.msix`
before producing portable and installer payloads. It checks the packaged
manifest/assets and native DLL architecture; a matching Python architecture
also exercises COM state deferral and file/folder/mixed-selection enumeration.
The EXE continues to embed the same MSI.

Windows releases now use a persistent self-signing identity when the repository
variable `WINDOWS_SIGNING_MODE` is `self-signed`. CI signs native EXE/DLL files,
the sparse MSIX, MSI, and the final setup EXE with SHA-256 and a timestamp. It
uses the encrypted `WINDOWS_SELF_SIGN_PFX` and `WINDOWS_SELF_SIGN_PASSWORD`
Actions secrets. Public certificate metadata is pinned in
`ModernShell/self-signing.json`; no private keys belong in source or artifacts.
The signing identity is reused across builds, not regenerated per run.

The installer displays a Korean administrator-privilege notice and a separate
certificate-trust checkbox, unchecked by default. Only explicit consent permits
adding the pinned end-entity code-signing certificate to
`LocalMachine\TrustedPeople` and registering the Windows 11 menu. The dialog
explains that other code signed by this certificate is trusted too. Declining
still installs the application and classic context menu. Quiet installations
skip certificate import/menu registration unless the administrator explicitly
passes `SELF_SIGNED_MENU_CONSENT=1`. No Developer Mode or end-user Root-store
change is used. Initial installer SmartScreen warnings are still possible.

The helper checks the public certificate fingerprint, package signer, manifest
publisher, code-signing EKU, non-CA constraint and validity. MSIX deployment
verifies the package signature and contents using machine certificate trust.
Generic Authenticode validation may continue to report an untrusted root when
only TrustedPeople is used; the installer does not widen trust to Root to
satisfy that separate policy. Uninstall/failed fresh-install rollback removes
only certificates recorded as added by this installer, preserving preexisting
trust. Failed registration cleans up newly added trust. A per-user portable
registration with a self-signed certificate also needs administrator privileges
for machine trust; CurrentUser TrustedPeople alone failed deployment locally.

`Initialize-SelfSigning.ps1` performs the one-time maintainer setup using an
authenticated `gh`: it creates/reuses the private key in Windows CurrentUser My,
exports the password-protected PFX only in memory to Actions secrets, and sets
the signing-mode variable. It writes only the public CER and metadata to source.
Retain/backup the Windows personal-store private key; never rotate automatically
when it is missing. `.pfx`, `.p12` and `.key` files are ignored by Git and rejected
from the staged identity package. CI uses temporary CurrentUser trust (including
Root only on the ephemeral hosted runner for full Authenticode checks) and
removes its signing-store entries in an `always()` cleanup step. The shipped
registration helper never imports into Root.

With neither self-signing nor SignPath configured, installers retain classic
integration and skip automatic sparse-package registration.

To switch to SignPath later, change `WINDOWS_SIGNING_MODE` away from `self-signed`.
After approval, configure `SIGNPATH_API_TOKEN` as a repository secret and these
repository variables:

- `SIGNPATH_SHELL_ENABLED`: `true` to require production signing on the default branch.
- `SIGNPATH_SHELL_PUBLISHER`: the exact certificate Subject (MSIX Identity Publisher).
- `SIGNPATH_ORGANIZATION_ID`, `SIGNPATH_PROJECT_SLUG`, `SIGNPATH_SIGNING_POLICY_SLUG`.
- `SIGNPATH_SHELL_ARTIFACT_CONFIGURATION_SLUG`: a ZIP artifact configuration that
  signs the root `7zip-alpha-shell.msix` member. This is separate from future
  Authenticode signing of the EXE/DLL/MSI files.

The SignPath action downloads the signed package before packaging. CI fails if
the signature is not trusted. Only then does WiX enable per-machine staging and
provisioning after `InstallFiles`, and package removal before uninstall removes
files. Upgrade removal preserves the identity package; failed fresh installs
schedule identity cleanup on rollback. The helper uses no force-shutdown flags
or Explorer restart. Per-machine provisioning and upgrade/rollback still require
validation on an isolated Windows VM with the actual trusted certificate;
local review builds test MSI construction and sequence metadata, not a complete
MSI upgrade of the existing installation.

For portable users with a CA-signed package, run
`ShellIntegration/Register-ModernShell.ps1 -InstallDirectory <portable-directory>`
and use `-Unregister` before moving/removing that directory. Windows 10 skips the
modern menu registration. The existing classic integration remains independent.

For local development on a PC where Developer Mode is already enabled, build
with `Installer/build_modern_shell.py --arch x64 --version 26.5.34.0 --preview
--output <runtime>/ShellIntegrationPreview`, then run that directory's
`Register-ModernShell.ps1 -InstallDirectory <runtime> -Development`.
The separate `.Preview` identity keeps production registration independent; use
`-Unregister` on the preview helper before testing the production identity.
No self-signed certificate or security-setting changes are needed for this test.

Local Windows 11 x64 review (2026-10-29): registered the preview identity, opened
the actual first-tier Explorer menu, verified `7zip+α`, created and integrity-
checked a ZIP through the menu, and extracted selected ZIP/ALZ/EGG archives into
separate per-archive folders with content verification. Directory selections
show compression commands. Explorer and its taskbar stayed running. ARM64
runtime behavior and trusted per-machine deployment were not exercised locally.

References: [Microsoft Explorer integration](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/integrate-packaged-app-with-file-explorer),
[identity package deployment](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/grant-identity-to-nonpackaged-apps),
[SignPath GitHub integration](https://docs.signpath.io/trusted-build-systems/github).

Self-signing review (2026-10-29): x64 and ARM64 MSI consent configurations built;
compiled MSI checks verified the unchecked consent default, pre-execution dialog,
elevated registration gate and removal ordering. Signed x64 native binaries,
MSIX, MSI and setup EXE were produced locally. An actual elevated test imported
the pinned certificate into LocalMachine TrustedPeople, staged/provisioned the
signed package for all users, and removed the package and installer-owned
certificate afterward. The test confirmed no LocalMachine Root entry was added.
The interactive review installer was canceled before InstallInitialize; it did
not replace the existing installation. Complete upgrade/rollback behavior through
Windows Installer and ARM64 execution still require separate runtime review.

### MSI 1722 registration failure fixed

The first self-signed installation attempt failed in `RegisterModernShell`
(error 1722 / overall 1603). The helper had passed an elevated interactive-user
test, but deferred MSI actions execute as SYSTEM. A SYSTEM task reproduced the
failure before helper execution: that account's default Restricted PowerShell
policy blocked `-File`. No Appx deployment event was emitted in that attempt.

All three MSI helper launchers now specify `-ExecutionPolicy RemoteSigned` for
that child process only. No machine/user execution-policy setting is changed;
Group Policy remains authoritative. The MSI-installed local script can execute
under SYSTEM while downloaded unsigned scripts remain subject to RemoteSigned.
Compiled MSI validation requires this flag on registration, removal and rollback.
The helper accepts a diagnostic transcript path; installer calls retain logs in
`%ProgramData%\UzuCore\7zipAlpha\InstallerLogs`.

An actual SYSTEM scheduled-task test then executed registration with consent
and removal using the same command-line policy as MSI. Both returned exit 0,
including machine certificate trust and all-user package provisioning/cleanup.
This covers the account difference that the earlier elevated-user test missed.

Follow-up full installation review: the rebuilt x64 MSI was installed interactively
with certificate consent on 2026-10-29. Windows Installer logged successful
`RegisterModernShell` execution and overall return code 0 (version 26.5.34).
The installed product version, current-user production identity registration and
machine TrustedPeople certificate were verified; Explorer remained responsive.
The obsolete development Preview identity was removed to avoid duplicate menus.
This supersedes the earlier limitation about not exercising the complete x64
upgrade. ARM64 runtime and failed-upgrade rollback remain unexercised locally.
