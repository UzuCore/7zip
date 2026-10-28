// Local Windows file-manager theme prototype. Included by FM.cpp only.
#ifndef ZIP7_INC_THEME_PREVIEW_H
#define ZIP7_INC_THEME_PREVIEW_H

#include <commctrl.h>
#include "LangUtils.h"

namespace NThemePreview
{
static UINT g_Mode = 0; // system, light, dark
static bool g_Dark = false;
static bool g_Applying = false;
static HBRUSH g_Back = NULL;
static HBRUSH g_Surface = NULL;
static HMENU g_Menu = NULL;
static const UINT kFirstCommand = 48000;
static const UINT kRefresh = WM_APP + 0x571;
static COLORREF Back() { return g_Dark ? RGB(26, 28, 32) : GetSysColor(COLOR_WINDOW); }
static COLORREF Surface() { return g_Dark ? RGB(36, 39, 44) : GetSysColor(COLOR_BTNFACE); }
static COLORREF Text() { return g_Dark ? RGB(232, 234, 238) : GetSysColor(COLOR_WINDOWTEXT); }

static void SetTheme(HWND wnd, const wchar_t *name)
{
  typedef HRESULT (WINAPI *F)(HWND, LPCWSTR, LPCWSTR);
  static HMODULE lib = LoadLibraryW(L"uxtheme.dll");
  if (lib)
  {
    const F f = (F)(void *)GetProcAddress(lib, "SetWindowTheme");
    if (f) f(wnd, name, NULL);
  }
}

static bool SystemDark()
{
  DWORD val = 1, bytes = sizeof(val), type = 0;
  HKEY key;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
      0, KEY_READ, &key) == ERROR_SUCCESS)
  {
    if (RegQueryValueExW(key, L"AppsUseLightTheme", NULL, &type,
        (LPBYTE)&val, &bytes) != ERROR_SUCCESS || type != REG_DWORD)
      val = 1;
    RegCloseKey(key);
  }
  return val == 0;
}

static bool IsHighContrast()
{
  HIGHCONTRASTW hc;
  ZeroMemory(&hc, sizeof(hc));
  hc.cbSize = sizeof(hc);
  return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0)
      && (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

static LRESULT CALLBACK ChildProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp,
    UINT_PTR id, DWORD_PTR data)
{
  if (msg == WM_NCDESTROY)
    RemoveWindowSubclass(wnd, ChildProc, id);
  if (g_Dark)
  {
    if (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX || msg == WM_CTLCOLORSTATIC)
    {
      HDC dc = (HDC)wp;
      SetTextColor(dc, Text());
      SetBkColor(dc, Back());
      return (LRESULT)g_Back;
    }
    if (msg == WM_ERASEBKGND && data == 0)
    {
      RECT rc; GetClientRect(wnd, &rc);
      FillRect((HDC)wp, &rc, g_Surface);
      return 1;
    }
    if (msg == WM_NOTIFY)
    {
      const NMHDR *hdr = (const NMHDR *)lp;
      if (hdr->code == NM_CUSTOMDRAW)
      {
        wchar_t name[64]; GetClassNameW(hdr->hwndFrom, name, 64);
        if (lstrcmpiW(name, WC_HEADERW) == 0)
        {
          NMCUSTOMDRAW *cd = (NMCUSTOMDRAW *)lp;
          if (cd->dwDrawStage == CDDS_PREPAINT)
          {
            RECT rc; GetClientRect(hdr->hwndFrom, &rc);
            FillRect(cd->hdc, &rc, g_Surface);
            return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
          }
          if (cd->dwDrawStage == CDDS_POSTPAINT)
          {
            RECT blank; GetClientRect(hdr->hwndFrom, &blank);
            int right = 0;
            const int count = Header_GetItemCount(hdr->hwndFrom);
            for (int i = 0; i < count; i++)
            {
              RECT rc;
              if (Header_GetItemRect(hdr->hwndFrom, i, &rc) && rc.right > right)
                right = rc.right;
            }
            blank.left = right;
            FillRect(cd->hdc, &blank, g_Surface);
            return CDRF_DODEFAULT;
          }
          if (cd->dwDrawStage == CDDS_ITEMPREPAINT)
          {
            FillRect(cd->hdc, &cd->rc, g_Surface);
            wchar_t text[512];
            HDITEMW item; ZeroMemory(&item, sizeof(item));
            item.mask = HDI_TEXT | HDI_FORMAT; item.pszText = text; item.cchTextMax = 512;
            Header_GetItem(hdr->hwndFrom, (int)cd->dwItemSpec, &item);
            RECT rc = cd->rc; rc.left += 10; rc.right -= 10;
            SetBkMode(cd->hdc, TRANSPARENT); SetTextColor(cd->hdc, Text());
            const UINT align = (item.fmt & HDF_RIGHT) ? DT_RIGHT : DT_LEFT;
            DrawTextW(cd->hdc, text, -1, &rc, align | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            // Preserve the visible sort direction in report view.
            if (item.fmt & (HDF_SORTUP | HDF_SORTDOWN))
            {
              RECT arrow = cd->rc; arrow.left = arrow.right - 13;
              DrawTextW(cd->hdc, (item.fmt & HDF_SORTUP) ? L"\x25B4" : L"\x25BE", 1,
                  &arrow, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            }
            return CDRF_SKIPDEFAULT;
          }
        }
        if (lstrcmpiW(name, TOOLBARCLASSNAMEW) == 0)
        {
          NMTBCUSTOMDRAW *cd = (NMTBCUSTOMDRAW *)lp;
          if (cd->nmcd.dwDrawStage == CDDS_PREPAINT)
          {
            RECT rc; GetClientRect(hdr->hwndFrom, &rc);
            FillRect(cd->nmcd.hdc, &rc, g_Surface);
            return CDRF_NOTIFYITEMDRAW;
          }
          if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
          {
            cd->clrText = Text(); cd->clrBtnFace = Surface();
            cd->clrHighlightHotTrack = RGB(57, 65, 80);
            return 0x00800000 /* TBCDRF_USECDCOLORS */ | TBCDRF_HILITEHOTTRACK;
          }
        }
      }
    }
    if (msg == WM_PAINT && data == 2)
    {
      PAINTSTRUCT ps; HDC dc = BeginPaint(wnd, &ps);
      RECT area; GetClientRect(wnd, &area); FillRect(dc, &area, g_Surface);
      HFONT font = (HFONT)SendMessage(wnd, WM_GETFONT, 0, 0);
      HGDIOBJ old = font ? SelectObject(dc, font) : NULL;
      SetTextColor(dc, Text()); SetBkMode(dc, TRANSPARENT);
      const int count = (int)SendMessage(wnd, SB_GETPARTS, 0, 0);
      for (int i = 0; i < count; i++)
      {
        RECT rc;
        if (!SendMessage(wnd, SB_GETRECT, (WPARAM)i, (LPARAM)&rc)) continue;
        const LRESULT len = SendMessage(wnd, SB_GETTEXTLENGTHW, (WPARAM)i, 0);
        if (HIWORD(len) & SBT_OWNERDRAW) continue;
        UString text;
        SendMessageW(wnd, SB_GETTEXTW, (WPARAM)i, (LPARAM)text.GetBuf(LOWORD(len) + 1));
        text.ReleaseBuf_CalcLen(LOWORD(len));
        rc.left += 8; rc.right -= 4;
        DrawTextW(dc, text.Ptr(), -1, &rc, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
      }
      if (old) SelectObject(dc, old);
      EndPaint(wnd, &ps);
      return 0;
    }
  }
  return DefSubclassProc(wnd, msg, wp, lp);
}

static BOOL CALLBACK ApplyChild(HWND wnd, LPARAM)
{
  wchar_t name[64]; GetClassNameW(wnd, name, 64);
  DWORD_PTR kind = 0;
  if (lstrcmpiW(name, WC_LISTVIEWW) == 0)
  {
    kind = 1;
    SetTheme(wnd, g_Dark ? L"DarkMode_Explorer" : L"Explorer");
    ListView_SetBkColor(wnd, Back());
    ListView_SetTextBkColor(wnd, Back());
    ListView_SetTextColor(wnd, Text());
  }
  else if (lstrcmpiW(name, STATUSCLASSNAMEW) == 0)
    kind = 2;
  else if (lstrcmpiW(name, WC_HEADERW) == 0)
  {
    kind = 1;
    SetTheme(wnd, g_Dark ? L"" : L"Explorer");
  }
  else if (lstrcmpiW(name, TOOLBARCLASSNAMEW) == 0)
    SetTheme(wnd, g_Dark ? L"" : L"Explorer");
  else if (lstrcmpiW(name, L"Edit") == 0 || lstrcmpiW(name, L"ComboBox") == 0
      || lstrcmpiW(name, L"ComboBoxEx32") == 0)
  {
    kind = 1;
    SetTheme(wnd, g_Dark ? L"DarkMode_CFD" : L"Explorer");
  }
  SetWindowSubclass(wnd, ChildProc, 0x7A01, kind);
  InvalidateRect(wnd, NULL, TRUE);
  return TRUE;
}

static void EnsureMenu(HWND wnd)
{
  HMENU main = GetMenu(wnd);
  if (!main) return;
  bool found = false;
  for (int i = 0; i < GetMenuItemCount(main); i++)
    if (GetSubMenu(main, i) == g_Menu && g_Menu) found = true;
  if (!found)
  {
    g_Menu = CreatePopupMenu();
    const bool ko = g_LangID.IsEqualTo_Ascii_NoCase("ko");
    AppendMenuW(g_Menu, MF_STRING, kFirstCommand, ko ? L"\uc2dc\uc2a4\ud15c \uc124\uc815 \ub530\ub974\uae30" : L"System default");
    AppendMenuW(g_Menu, MF_STRING, kFirstCommand + 1, ko ? L"\ub77c\uc774\ud2b8" : L"Light");
    AppendMenuW(g_Menu, MF_STRING, kFirstCommand + 2, ko ? L"\ub2e4\ud06c" : L"Dark");
    AppendMenuW(main, MF_POPUP, (UINT_PTR)g_Menu, ko ? L"\ud14c\ub9c8 (\ubbf8\ub9ac\ubcf4\uae30)" : L"Theme (preview)");
  }
  CheckMenuRadioItem(g_Menu, kFirstCommand, kFirstCommand + 2, kFirstCommand + g_Mode, MF_BYCOMMAND);
  DrawMenuBar(wnd);
}

static void Apply(HWND wnd)
{
  if (g_Applying) return;
  g_Applying = true;
  g_Dark = !IsHighContrast() && (g_Mode == 2 || (g_Mode == 0 && SystemDark()));
  // Children redraw after all brushes have been replaced on the UI thread.
  if (g_Back) DeleteObject(g_Back);
  if (g_Surface) DeleteObject(g_Surface);
  g_Back = CreateSolidBrush(Back()); g_Surface = CreateSolidBrush(Surface());
  typedef HRESULT (WINAPI *F)(HWND, DWORD, LPCVOID, DWORD);
  static HMODULE lib = LoadLibraryW(L"dwmapi.dll");
  if (lib)
  {
    const F f = (F)(void *)GetProcAddress(lib, "DwmSetWindowAttribute");
    if (f)
    {
      const BOOL dark = g_Dark;
      if (FAILED(f(wnd, 20, &dark, sizeof(dark))))
        f(wnd, 19, &dark, sizeof(dark));
    }
  }
  EnumChildWindows(wnd, ApplyChild, 0);
  EnsureMenu(wnd);
  RedrawWindow(wnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
  g_Applying = false;
}

static LRESULT CALLBACK MainProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp,
    UINT_PTR id, DWORD_PTR)
{
  if (msg == WM_COMMAND && LOWORD(wp) >= kFirstCommand && LOWORD(wp) <= kFirstCommand + 2)
  {
    g_Mode = LOWORD(wp) - kFirstCommand;
    // This preview has its own preference and does not alter Windows theme settings.
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\7-Zip UzuCore\\ThemePreview", 0,
        NULL, 0, KEY_WRITE, NULL, &key, NULL) == ERROR_SUCCESS)
    {
      const DWORD mode = g_Mode;
      RegSetValueExW(key, L"Mode", 0, REG_DWORD, (const BYTE *)&mode, sizeof(mode));
      RegCloseKey(key);
    }
    Apply(wnd);
    return 0;
  }
  if (g_Dark && msg == WM_NOTIFY && ((NMHDR *)lp)->code == NM_CUSTOMDRAW)
  {
    wchar_t name[64]; GetClassNameW(((NMHDR *)lp)->hwndFrom, name, 64);
    if (lstrcmpiW(name, TOOLBARCLASSNAMEW) == 0)
      return ChildProc(wnd, msg, wp, lp, id, 0);
  }
  if (msg == WM_SETTINGCHANGE || msg == WM_SYSCOLORCHANGE
      || (msg == WM_PARENTNOTIFY && LOWORD(wp) == WM_CREATE))
    PostMessage(wnd, kRefresh, 0, 0);
  if (msg == WM_INITMENU)
    EnsureMenu(wnd);
  if (msg == kRefresh)
  {
    Apply(wnd);
    return 0;
  }
  if (msg == WM_ERASEBKGND && g_Dark)
  {
    RECT rc; GetClientRect(wnd, &rc);
    FillRect((HDC)wp, &rc, g_Surface);
    return 1;
  }
  if (msg == WM_NCDESTROY)
  {
    RemoveWindowSubclass(wnd, MainProc, id);
    if (g_Back) { DeleteObject(g_Back); g_Back = NULL; }
    if (g_Surface) { DeleteObject(g_Surface); g_Surface = NULL; }
  }
  return DefSubclassProc(wnd, msg, wp, lp);
}

static void Attach(HWND wnd)
{
  HKEY key;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\7-Zip UzuCore\\ThemePreview", 0, KEY_READ, &key) == ERROR_SUCCESS)
  {
    DWORD mode = 0, size = sizeof(mode), type = 0;
    if (RegQueryValueExW(key, L"Mode", NULL, &type, (BYTE *)&mode, &size) == ERROR_SUCCESS
        && type == REG_DWORD && mode <= 2)
      g_Mode = (UINT)mode;
    RegCloseKey(key);
  }
  SetWindowSubclass(wnd, MainProc, 0x7A00, 0);
  Apply(wnd);
}
}
#endif
