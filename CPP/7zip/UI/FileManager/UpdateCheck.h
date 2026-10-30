// GitHub release update checks run outside the UI process.
#ifndef Z7_UPDATE_CHECK_H
#define Z7_UPDATE_CHECK_H
#include "LangUtils.h"
#define IDM_CHECK_UPDATES 962
#define IDM_AUTO_UPDATES 963
inline bool AutomaticUpdatesEnabled()
{
  HKEY key;
  DWORD enabled = 1, size = sizeof(enabled), type = 0;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\7-Zip UzuCore\\Updates", 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS)
  {
    if (RegQueryValueExW(key, L"Enabled", NULL, &type, (BYTE *)&enabled, &size) != ERROR_SUCCESS || type != REG_DWORD)
      enabled = 1;
    RegCloseKey(key);
  }
  return enabled != 0;
}
inline void ToggleAutomaticUpdates()
{
  const DWORD enabled = AutomaticUpdatesEnabled() ? 0 : 1;
  HKEY key;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\7-Zip UzuCore\\Updates", 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) == ERROR_SUCCESS)
  {
    RegSetValueExW(key, L"Enabled", 0, REG_DWORD, (const BYTE *)&enabled, sizeof(enabled));
    RegCloseKey(key);
  }
}
inline void RunUpdateCheck(HWND owner, bool manual)
{
  if (!manual && !AutomaticUpdatesEnabled())
    return;
  wchar_t module[MAX_PATH], windows[MAX_PATH];
  const DWORD count = GetModuleFileNameW(NULL, module, MAX_PATH);
  if (count == 0 || count >= MAX_PATH || !GetWindowsDirectoryW(windows, MAX_PATH))
    return;
  UString folder(module);
  const int slash = folder.ReverseFind(L'\\');
  if (slash < 0)
    return;
  folder.DeleteFrom((unsigned)slash + 1);
  const UString script = folder + L"Check-Updates.ps1";
  if (GetFileAttributesW(script) == INVALID_FILE_ATTRIBUTES)
    return;
  const UString executable = UString(windows) + L"\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
  UString command = L"\"";
  command += executable;
  command += L"\" -NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -ExecutionPolicy RemoteSigned -File \"";
  command += script;
  command += L"\" -Language ";
  command += g_LangID.IsEqualTo_Ascii_NoCase("ko") ? L"ko" : L"en";
  if (manual)
    command += L" -Manual";
  STARTUPINFOW startup;
  ZeroMemory(&startup, sizeof(startup));
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process;
  if (CreateProcessW(executable, command.Ptr_non_const(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, folder, &startup, &process))
  {
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
  }
  else if (manual)
    MessageBoxW(owner, L"Could not start the update checker.", L"7zip+alpha", MB_OK | MB_ICONERROR);
}
inline void AddUpdateMenu(HMENU menu)
{
  HMENU help = GetSubMenu(menu, GetMenuItemCount(menu) - 1);
  if (!help)
    return;
  DeleteMenu(help, IDM_CHECK_UPDATES, MF_BYCOMMAND);
  DeleteMenu(help, IDM_AUTO_UPDATES, MF_BYCOMMAND);
  const bool ko = g_LangID.IsEqualTo_Ascii_NoCase("ko");
  AppendMenuW(help, MF_STRING, IDM_CHECK_UPDATES, ko ? L"\uc5c5\ub370\uc774\ud2b8 \ud655\uc778..." : L"Check for updates...");
  AppendMenuW(help, MF_STRING | (AutomaticUpdatesEnabled() ? MF_CHECKED : MF_UNCHECKED), IDM_AUTO_UPDATES,
      ko ? L"\uc790\ub3d9\uc73c\ub85c \uc5c5\ub370\uc774\ud2b8 \ud655\uc778" : L"Automatically check for updates");
}
#endif
