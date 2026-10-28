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

// Preserve native strings and command IDs so menu commands and accessibility
// continue to work when toggling themes or rebuilding dynamic menus.
struct CMenuPaintItem
{
  // MSAAMENUINFO-compatible prefix for screen readers.
  DWORD Signature;
  DWORD TextLength;
  LPWSTR AccessibleText;
  HMENU Menu;
  UINT Position;
  UINT Type;
  ULONG_PTR Data;
  UString Label;
  bool Bar;
  bool Submenu;
};
struct CMenuBackground
{
  HMENU Menu;
  HBRUSH Brush;
};
static CObjectVector<CMenuPaintItem> g_MenuItems;
static CRecordVector<CMenuBackground> g_MenuBackgrounds;
static HFONT g_MenuFont = NULL;

static CMenuPaintItem *FindMenuItem(ULONG_PTR data)
{
  FOR_VECTOR (i, g_MenuItems)
    if ((ULONG_PTR)&g_MenuItems[i] == data)
      return &g_MenuItems[i];
  return NULL;
}

static void RestoreMenus()
{
  FOR_VECTOR (i, g_MenuItems)
  {
    CMenuPaintItem &item = g_MenuItems[i];
    MENUITEMINFOW info; ZeroMemory(&info, sizeof(info)); info.cbSize = sizeof(info);
    info.fMask = MIIM_DATA;
    if (IsMenu(item.Menu) && GetMenuItemInfoW(item.Menu, item.Position, TRUE, &info)
        && info.dwItemData == (ULONG_PTR)&item)
    {
      info.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_STRING;
      info.fType = item.Type; info.dwItemData = item.Data;
      info.dwTypeData = (LPWSTR)item.Label.Ptr();
      SetMenuItemInfoW(item.Menu, item.Position, TRUE, &info);
    }
  }
  FOR_VECTOR (i, g_MenuBackgrounds)
  {
    const CMenuBackground &bg = g_MenuBackgrounds[i];
    MENUINFO info; ZeroMemory(&info, sizeof(info)); info.cbSize = sizeof(info);
    info.fMask = MIM_BACKGROUND;
    if (IsMenu(bg.Menu) && GetMenuInfo(bg.Menu, &info) && info.hbrBack == g_Surface)
    {
      info.hbrBack = bg.Brush;
      SetMenuInfo(bg.Menu, &info);
    }
  }
  g_MenuItems.Clear();
  g_MenuBackgrounds.Clear();
}

static void PaintMenus(HMENU menu, bool bar)
{
  if (!menu || !g_Dark) return;
  MENUINFO bg; ZeroMemory(&bg, sizeof(bg)); bg.cbSize = sizeof(bg); bg.fMask = MIM_BACKGROUND;
  if (GetMenuInfo(menu, &bg))
  {
    CMenuBackground saved; saved.Menu = menu; saved.Brush = bg.hbrBack;
    g_MenuBackgrounds.Add(saved);
    bg.hbrBack = g_Surface;
    SetMenuInfo(menu, &bg);
  }
  const int count = GetMenuItemCount(menu);
  for (int i = 0; i < count; i++)
  {
    MENUITEMINFOW info; ZeroMemory(&info, sizeof(info)); info.cbSize = sizeof(info);
    info.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_STRING | MIIM_SUBMENU;
    if (!GetMenuItemInfoW(menu, (UINT)i, TRUE, &info)) continue;
    const HMENU sub = info.hSubMenu;
    // Leave external owner-draw handlers in control of their own items.
    if (!(info.fType & MFT_OWNERDRAW))
    {
      CMenuPaintItem &item = g_MenuItems.AddNew();
      item.Menu = menu; item.Position = (UINT)i; item.Type = info.fType;
      item.Data = info.dwItemData; item.Bar = bar; item.Submenu = sub != NULL;
      const unsigned len = info.cch;
      info.dwTypeData = item.Label.GetBuf(len + 1); info.cch = len + 1;
      GetMenuItemInfoW(menu, (UINT)i, TRUE, &info);
      item.Label.ReleaseBuf_CalcLen(len);
      item.Signature = 0xAA0DF00D;
      item.TextLength = item.Label.Len(); item.AccessibleText = (LPWSTR)item.Label.Ptr();
      info.fMask = MIIM_FTYPE | MIIM_DATA;
      info.fType = item.Type | MFT_OWNERDRAW; info.dwItemData = (ULONG_PTR)&item;
      SetMenuItemInfoW(menu, (UINT)i, TRUE, &info);
    }
    PaintMenus(sub, false);
  }
}

static HFONT MenuFont()
{
  if (!g_MenuFont)
  {
    NONCLIENTMETRICSW metrics; ZeroMemory(&metrics, sizeof(metrics)); metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
      g_MenuFont = CreateFontIndirectW(&metrics.lfMenuFont);
  }
  return g_MenuFont;
}

static bool MeasureMenu(MEASUREITEMSTRUCT *measure)
{
  if (measure->CtlType != ODT_MENU) return false;
  const CMenuPaintItem *item = FindMenuItem(measure->itemData);
  if (!item) return false;
  if (item->Type & MFT_SEPARATOR)
  {
    measure->itemWidth = 0; measure->itemHeight = 9;
    return true;
  }
  HDC dc = GetDC(NULL);
  const int saved = SaveDC(dc);
  if (MenuFont()) SelectObject(dc, g_MenuFont);
  RECT rc = {0, 0, 0, 0};
  DrawTextW(dc, item->Label.Ptr(), -1, &rc, DT_CALCRECT | DT_SINGLELINE);
  measure->itemWidth = (UINT)(rc.right + (item->Bar ? 18 : 76));
  measure->itemHeight = (UINT)MyMax((int)(rc.bottom + (item->Bar ? 2 : 8)), GetSystemMetrics(SM_CYMENU));
  RestoreDC(dc, saved); ReleaseDC(NULL, dc);
  return true;
}

static bool DrawMenu(DRAWITEMSTRUCT *draw)
{
  if (draw->CtlType != ODT_MENU) return false;
  const CMenuPaintItem *item = FindMenuItem(draw->itemData);
  if (!item) return false;
  HDC dc = draw->hDC;
  const int saved = SaveDC(dc);
  const bool selected = (draw->itemState & (ODS_SELECTED | ODS_HOTLIGHT)) != 0;
  const bool disabled = (draw->itemState & (ODS_DISABLED | ODS_GRAYED)) != 0;
  SetDCBrushColor(dc, selected ? RGB(57, 65, 80) : Surface());
  FillRect(dc, &draw->rcItem, (HBRUSH)GetStockObject(DC_BRUSH));
  if (item->Type & MFT_SEPARATOR)
  {
    RECT rc = draw->rcItem; rc.left += 30; rc.right -= 8;
    rc.top = (rc.top + rc.bottom) / 2; rc.bottom = rc.top + 1;
    SetDCBrushColor(dc, RGB(65, 68, 74));
    FillRect(dc, &rc, (HBRUSH)GetStockObject(DC_BRUSH));
  }
  else
  {
    if (MenuFont()) SelectObject(dc, g_MenuFont);
    const COLORREF color = disabled ? RGB(137, 141, 150) : Text();
    SetTextColor(dc, color); SetBkMode(dc, TRANSPARENT);
    RECT rc = draw->rcItem;
    rc.left += item->Bar ? 9 : 30;
    rc.right -= item->Bar ? 9 : 24;
    UINT flags = DT_SINGLELINE | DT_VCENTER;
    if (draw->itemState & ODS_NOACCEL) flags |= DT_HIDEPREFIX;
    const int tab = item->Label.Find(L'\t');
    if (tab >= 0)
    {
      DrawTextW(dc, item->Label.Ptr(), tab, &rc, flags | DT_LEFT);
      DrawTextW(dc, item->Label.Ptr((unsigned)tab + 1), -1, &rc, flags | DT_RIGHT);
    }
    else
      DrawTextW(dc, item->Label.Ptr(), -1, &rc, flags | (item->Bar ? DT_CENTER : DT_LEFT));
    if (draw->itemState & ODS_CHECKED)
    {
      RECT check = draw->rcItem; check.left += 7; check.right = check.left + 16;
      DrawTextW(dc, (item->Type & MFT_RADIOCHECK) ? L"\x2022" : L"\x2713", 1,
          &check, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }
    if (item->Submenu && !item->Bar)
    {
      RECT arrow = draw->rcItem; arrow.left = arrow.right - 20;
      DrawTextW(dc, L"\x203A", 1, &arrow, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }
  }
  RestoreDC(dc, saved);
  return true;
}

// Owner-drawn menu mnemonics are dispatched by Windows through WM_MENUCHAR.
static LRESULT MenuChar(HMENU menu, wchar_t key)
{
  int first = -1, next = -1, selected = -1;
  unsigned count = 0;
  const int size = GetMenuItemCount(menu);
  for (int i = 0; i < size; i++)
  {
    const UINT state = GetMenuState(menu, (UINT)i, MF_BYPOSITION);
    if (state & MF_HILITE) selected = i;
  }
  for (int i = 0; i < size; i++)
  {
    MENUITEMINFOW info; ZeroMemory(&info, sizeof(info)); info.cbSize = sizeof(info);
    info.fMask = MIIM_DATA | MIIM_STATE;
    if (!GetMenuItemInfoW(menu, (UINT)i, TRUE, &info) || (info.fState & (MFS_DISABLED | MFS_GRAYED))) continue;
    const CMenuPaintItem *item = FindMenuItem(info.dwItemData);
    if (!item) continue;
    for (unsigned j = 0; j + 1 < item->Label.Len(); j++)
    {
      if (item->Label[j] != L'&') continue;
      if (item->Label[j + 1] == L'&') { j++; continue; }
      wchar_t mnemonic[2] = { item->Label[j + 1], 0 };
      wchar_t pressed[2] = { key, 0 };
      if (lstrcmpiW(mnemonic, pressed) == 0)
      {
        if (first < 0) first = i;
        if (next < 0 && i > selected) next = i;
        count++;
      }
      break;
    }
  }
  if (!count) return MAKELRESULT(0, MNC_IGNORE);
  return MAKELRESULT(next >= 0 ? next : first, count > 1 ? MNC_SELECT : MNC_EXECUTE);
}

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
  {
    RemovePropW(wnd, L"7zip.ThemePreview.ToolbarDivider");
    RemoveWindowSubclass(wnd, ChildProc, id);
  }
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
  {
    // The native toolbar draws a bright 3D divider even with custom colors.
    // Preserve its original style so Light mode restores the normal separator.
    LONG_PTR style = GetWindowLongPtr(wnd, GWL_STYLE);
    HANDLE original = GetPropW(wnd, L"7zip.ThemePreview.ToolbarDivider");
    if (!original)
    {
      original = (HANDLE)(UINT_PTR)((style & CCS_NODIVIDER) ? 2 : 1);
      SetPropW(wnd, L"7zip.ThemePreview.ToolbarDivider", original);
    }
    if (g_Dark || (UINT_PTR)original == 2)
      style |= CCS_NODIVIDER;
    else
      style &= ~(LONG_PTR)CCS_NODIVIDER;
    SetWindowLongPtr(wnd, GWL_STYLE, style);
    SetWindowPos(wnd, NULL, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    SetTheme(wnd, g_Dark ? L"" : L"Explorer");
  }
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
}


static void Apply(HWND wnd)
{
  if (g_Applying) return;
  g_Applying = true;
  RestoreMenus();
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
  PaintMenus(GetMenu(wnd), true);
  DrawMenuBar(wnd);
  RedrawWindow(wnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
  g_Applying = false;
}

// DefWindowProc draws a system-colored non-client separator below the menu.
// Repaint only that strip, using the actual menu/client coordinates so it also
// follows menu wrapping, window resizing and the current display scale.
static void PaintMenuSeparator(HWND wnd, HDC printDC)
{
  if (!g_Dark || !g_Surface || !GetMenu(wnd)) return;
  MENUBARINFO bar; ZeroMemory(&bar, sizeof(bar)); bar.cbSize = sizeof(bar);
  RECT window, client;
  POINT origin = { 0, 0 };
  if (!GetMenuBarInfo(wnd, OBJID_MENU, 0, &bar)
      || !GetWindowRect(wnd, &window) || !GetClientRect(wnd, &client)
      || !ClientToScreen(wnd, &origin)) return;
  RECT strip;
  strip.left = origin.x - window.left;
  strip.right = strip.left + client.right;
  strip.top = bar.rcBar.bottom - window.top - 1;
  strip.bottom = origin.y - window.top;
  if (strip.bottom <= strip.top) return;
  HDC dc = printDC ? printDC : GetWindowDC(wnd);
  if (dc)
  {
    FillRect(dc, &strip, g_Surface);
    if (!printDC) ReleaseDC(wnd, dc);
  }
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
  if (msg == WM_MEASUREITEM && MeasureMenu((MEASUREITEMSTRUCT *)lp))
    return TRUE;
  if (msg == WM_DRAWITEM && DrawMenu((DRAWITEMSTRUCT *)lp))
    return TRUE;
  if (g_Dark && msg == WM_MENUCHAR)
    return MenuChar((HMENU)lp, (wchar_t)LOWORD(wp));
  if (msg == WM_INITMENU || msg == WM_INITMENUPOPUP)
  {
    // Dynamic menu code must see the original native strings and item data.
    RestoreMenus();
    const LRESULT result = DefSubclassProc(wnd, msg, wp, lp);
    EnsureMenu(wnd);
    PaintMenus(GetMenu(wnd), true);
    return result;
  }
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
  if (g_Dark && (msg == WM_NCPAINT || msg == WM_NCACTIVATE
      || (msg == WM_PRINT && (lp & PRF_NONCLIENT))))
  {
    const LRESULT result = DefSubclassProc(wnd, msg, wp, lp);
    PaintMenuSeparator(wnd, msg == WM_PRINT ? (HDC)wp : NULL);
    return result;
  }
  if (msg == WM_NCDESTROY)
  {
    RestoreMenus();
    if (g_MenuFont) { DeleteObject(g_MenuFont); g_MenuFont = NULL; }
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
