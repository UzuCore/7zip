# Windows theme preview

This work is on the local `ui-theme-preview` branch. It is not published to the
GitHub release. Windows x64 preview binaries are built from the WSL source with
Visual Studio 2022, using a Windows build directory for MSVC compatibility.

## Coverage

- System / Light / Dark mode, with a native high-contrast fallback.
- Main file manager: title, menus, file lists, headers, toolbar, address entry,
  status bar and subtle gray separators / file-list frames.
- Application dialogs: settings/property pages, compression, extraction,
  passwords and progress, through the common dialog procedure.
- Dark buttons, checkboxes/radio buttons, group boxes, tabs, combo boxes,
  editable text, list headers, spin buttons and progress bars.
- Existing worker dialogs follow changes to the app theme through a registered
  notification message. Windows theme preferences are not modified.

`Z7_UI_THEME` enables the dialog implementation only in the Windows MSVC GUI and
File Manager makefiles. Other binaries and the Linux/macOS CLI builds retain
their existing build settings. Native control styles and event handling remain
in place; subclasses provide painting. The upstream makefiles track source files rather than transitive header changes;
use `nmake /A` after editing the theme headers or nested resource scripts.

The preview preference is `HKCU\Software\7-Zip UzuCore\ThemePreview\Mode`:
0 = System, 1 = Light, 2 = Dark.

## Local validation

- MSVC x64 builds of 7zFM.exe, 7zG.exe and the current 7z.dll succeeded.
- Main window: 30 light/dark transitions, system preference, new second panel,
  native menu labels, mnemonic dispatch, dropdowns and measured gray borders.
- All six settings pages opened; a checkbox toggled and restored correctly;
  changes were cancelled, without modifying file associations.
- Compression and extraction dialogs opened/cancelled, then executed successfully.
- A real 64 MiB compression job paused/resumed and completed; ZIP contents matched
  the source bytes. The compression options dialog produced a valid 7z archive.
- An already open compression dialog changed between native Light and custom Dark.
- Rebuilt codec resources and format controls include lowercase alz and egg.
- Window-only captures confirmed light/dark dialog appearance, including settings,
  compression, extraction and progress. GDI counts were stable across transitions.

## Remaining validation

This prototype has not received a multi-monitor / mixed-DPI test pass or native
ARM64 runtime validation. Windows-owned file pickers and standard message boxes
follow Windows behavior; they are not restyled by the application dialog layer.
Installer/release integration remains separate from this local preview.

See [UI-VISUAL-REVIEW.md](UI-VISUAL-REVIEW.md) for the 100% scaling visual audit,
subsequent fixes and GUI archive-operation regression results.

Theme selection is under **Tools > Theme**, directly below **Options**.
The default is **System default** (Mode = 0); an explicit Light/Dark choice is saved.
Menu placement, repeated submenu opening and System-mode persistence across restart
were verified in the local Windows preview.
