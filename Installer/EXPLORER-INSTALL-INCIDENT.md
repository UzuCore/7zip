# Explorer taskbar loss during installation (2026-10-29)

## Local evidence

Installed package: 7zip+alpha (x64), version 26.5.33.

- 13:22:42: MsiInstaller 1040 begins the installation transaction.
- 13:22:55: Restart Manager 10010 says Explorer and nexusfile cannot be restarted:
  application SID does not match conductor SID.
- 13:22:59: Restart Manager 10002 starts shutting down nexusfile.
- 13:23:29: Restart Manager 10006 says Windows Explorer could not be shut down.
- 13:23:34: MsiInstaller 11707/1033 reports successful installation.
- 13:24:06 and 13:24:53: Application Hang 1002 reports Explorer hangs.

Read-only inspection of the cached MSI found no MSIRESTARTMANAGERCONTROL property.
The package therefore used Windows Installer's default Restart Manager behavior.
The taskbar recovery also observed a residual Explorer process alongside hung new
shell processes; restarting all Explorer processes in the current session restored
an answering taskbar. The precise internal wait was not established by a dump.

## Change

Set MSIRESTARTMANAGERCONTROL=DisableShutdown in WindowsProduct.wxs. This retains
files-in-use detection and the classic FilesInUse flow, without Restart Manager
closing shell-extension hosts automatically. The same MSI is embedded into the
EXE installer, so the setting covers both delivery formats and both architectures.
A compiled-MSI check runs in CI before the EXE is packaged.

Microsoft reference:
https://learn.microsoft.com/en-us/windows/win32/msi/msirestartmanagercontrol

## Validation and limits

- The installed old MSI fails the policy validator as expected.
- New x64 and ARM64 MSI packages build with WiX 4.0.5 and pass the native Windows
  Installer property-table check.
- These local review packages use existing runtime payloads; they are packaging
  policy checks, not new published UI releases.
- No product was installed, removed, repaired or upgraded on the user's machine
  during verification, and no machine-wide installer policy was changed.
- An isolated Windows upgrade test is still needed, especially the first upgrade
  from the old MSI: RemoveExistingProducts also runs the previous package's removal
  logic. Verify first install, old-version upgrade, repair and uninstall with an
  Explorer/Nexusfile host holding the shell DLL; collect a verbose MSI log and
  monitor taskbar responsiveness. The compiled-property check alone does not prove
  every legacy upgrade path is covered.