// Windows dialog theming, enabled only for the GUI and File Manager builds.
#ifndef ZIP7_INC_DIALOG_THEME_H
#define ZIP7_INC_DIALOG_THEME_H

#include <commctrl.h>
#include "../../Common/Defs.h"

namespace NWindows { namespace NControl { namespace NDialogTheme {

const UINT kRefresh = WM_APP + 0x572;
const UINT_PTR kSubclass = 0x7B00;
const DWORD_PTR kDark = 0x10000;
enum { kDialog, kButton, kTab, kCombo, kOther, kSpin, kEdit };
inline COLORREF Background() { return RGB(36, 39, 44); }
inline COLORREF Foreground() { return RGB(232, 234, 238); }
inline COLORREF Border() { return RGB(76, 80, 88); }
inline COLORREF Input() { return RGB(26, 28, 32); }

struct CBrushes
{
  HBRUSH Back;
  HBRUSH Edit;
  CBrushes(): Back(CreateSolidBrush(Background())), Edit(CreateSolidBrush(Input())) {}
  ~CBrushes() { DeleteObject(Back); DeleteObject(Edit); }
};
inline CBrushes &Brushes() { static CBrushes brushes; return brushes; }

inline bool ReadDark()
{
  HIGHCONTRASTW hc; ZeroMemory(&hc, sizeof(hc)); hc.cbSize = sizeof(hc);
  if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0)
      && (hc.dwFlags & HCF_HIGHCONTRASTON)) return false;
  HKEY key;
  DWORD mode = 0, bytes = sizeof(mode), type = 0;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\7-Zip UzuCore\\ThemePreview", 0, KEY_READ, &key) == ERROR_SUCCESS)
  {
    if (RegQueryValueExW(key, L"Mode", NULL, &type, (BYTE *)&mode, &bytes) != ERROR_SUCCESS
        || type != REG_DWORD || mode > 2) mode = 0;
    RegCloseKey(key);
  }
  if (mode != 0) return mode == 2;
  DWORD light = 1; bytes = sizeof(light); type = 0;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &key) == ERROR_SUCCESS)
  {
    if (RegQueryValueExW(key, L"AppsUseLightTheme", NULL, &type, (BYTE *)&light, &bytes) != ERROR_SUCCESS
        || type != REG_DWORD) light = 1;
    RegCloseKey(key);
  }
  return light == 0;
}

inline void VisualStyle(HWND wnd, const wchar_t *name)
{
  typedef HRESULT (WINAPI *F)(HWND, LPCWSTR, LPCWSTR);
  static HMODULE lib = LoadLibraryW(L"uxtheme.dll");
  if (lib)
  {
    const F f = (F)(void *)GetProcAddress(lib, "SetWindowTheme");
    if (f) f(wnd, name, *name ? NULL : L"");
  }
}

inline void TitleBar(HWND wnd, bool dark)
{
  typedef HRESULT (WINAPI *F)(HWND, DWORD, LPCVOID, DWORD);
  static HMODULE lib = LoadLibraryW(L"dwmapi.dll");
  if (lib)
  {
    const F f = (F)(void *)GetProcAddress(lib, "DwmSetWindowAttribute");
    if (f)
    {
      const BOOL value = dark;
      if (FAILED(f(wnd, 20, &value, sizeof(value)))) f(wnd, 19, &value, sizeof(value));
    }
  }
}

inline void Solid(HDC dc, const RECT &rc, COLORREF color)
{
  SetDCBrushColor(dc, color);
  FillRect(dc, &rc, (HBRUSH)GetStockObject(DC_BRUSH));
}

inline void Stroke(HDC dc, RECT rc, COLORREF color)
{
  SetDCBrushColor(dc, color);
  FrameRect(dc, &rc, (HBRUSH)GetStockObject(DC_BRUSH));
}

inline void Text(HWND wnd, HDC dc, RECT rc, UINT flags)
{
  const int size = GetWindowTextLengthW(wnd);
  UString text;
  GetWindowTextW(wnd, text.GetBuf((unsigned)size + 1), size + 1);
  text.ReleaseBuf_CalcLen((unsigned)size);
  DrawTextW(dc, text.Ptr(), -1, &rc, flags);
}

inline void FocusLabel(HWND wnd, HDC dc, RECT label)
{
  const int size = GetWindowTextLengthW(wnd);
  UString text;
  GetWindowTextW(wnd, text.GetBuf((unsigned)size + 1), size + 1);
  text.ReleaseBuf_CalcLen((unsigned)size);
  RECT bounds = { 0, 0, 0, 0 };
  DrawTextW(dc, text.Ptr(), -1, &bounds, DT_CALCRECT | DT_SINGLELINE);
  const int height = bounds.bottom;
  label.right = MyMin(label.right, label.left + bounds.right + 3);
  label.top += MyMax(0, (int)(label.bottom - label.top - height) / 2);
  label.bottom = MyMin(label.bottom, label.top + height + 1);
  DrawFocusRect(dc, &label);
}

inline void PaintInputBorder(HWND wnd, HDC printDC)
{
  const LONG_PTR style = GetWindowLongPtr(wnd, GWL_STYLE);
  const LONG_PTR exStyle = GetWindowLongPtr(wnd, GWL_EXSTYLE);
  if (!(style & WS_BORDER) && !(exStyle & WS_EX_CLIENTEDGE)) return;
  RECT window; POINT origin = { 0, 0 };
  if (!GetWindowRect(wnd, &window) || !ClientToScreen(wnd, &origin)) return;
  const int width = window.right - window.left;
  const int height = window.bottom - window.top;
  const int edgeX = origin.x - window.left;
  const int edgeY = origin.y - window.top;
  if (edgeX <= 0 || edgeY <= 0) return;
  HDC dc = printDC ? printDC : GetWindowDC(wnd);
  if (!dc) return;
  const int saved = SaveDC(dc);
  const bool enabled = IsWindowEnabled(wnd) != FALSE;
  const COLORREF background = enabled ? Input() : Background();
  RECT sides[4] = {
    { 0, 0, width, edgeY }, { 0, height - edgeY, width, height },
    { 0, edgeY, edgeX, height - edgeY }, { width - edgeX, edgeY, width, height - edgeY }
  };
  for (unsigned i = 0; i < 4; i++) Solid(dc, sides[i], background);
  RECT frame = { 0, 0, width, height };
  Stroke(dc, frame, !enabled ? RGB(58, 62, 68) : GetFocus() == wnd ? RGB(104, 165, 240) : Border());
  RestoreDC(dc, saved);
  if (!printDC) ReleaseDC(wnd, dc);
}

inline bool PaintButton(HWND wnd, HDC printDC = NULL)
{
  const LONG_PTR style = GetWindowLongPtr(wnd, GWL_STYLE);
  const unsigned kind = (unsigned)(style & BS_TYPEMASK);
  if (kind == BS_OWNERDRAW || (style & (BS_BITMAP | BS_ICON))) return false;
  if (kind > BS_GROUPBOX && kind != BS_AUTORADIOBUTTON) return false;
  PAINTSTRUCT ps; HDC dc = printDC ? printDC : BeginPaint(wnd, &ps);
  const int saved = SaveDC(dc);
  RECT rc; GetClientRect(wnd, &rc);
  Solid(dc, rc, Background());
  HFONT font = (HFONT)SendMessage(wnd, WM_GETFONT, 0, 0);
  if (font) SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);
  const bool enabled = IsWindowEnabled(wnd) != FALSE;
  SetTextColor(dc, enabled ? Foreground() : RGB(137, 141, 150));
  const bool focused = GetFocus() == wnd;
  const bool hovered = enabled && GetPropW(wnd, L"7zip.DialogTheme.Hover") != NULL;
  const COLORREF checkColor = enabled ? RGB(125, 185, 250) : RGB(137, 141, 150);
  const UINT ui = (UINT)SendMessage(wnd, WM_QUERYUISTATE, 0, 0);
  const UINT hidePrefix = (ui & UISF_HIDEACCEL) ? DT_HIDEPREFIX : 0;
  if (kind == BS_GROUPBOX)
  {
    TEXTMETRICW tm; GetTextMetricsW(dc, &tm);
    RECT frame = rc; frame.top += tm.tmHeight / 2;
    Stroke(dc, frame, Border());
    RECT label = { 10, 0, rc.right - 10, rc.bottom };
    const int size = GetWindowTextLengthW(wnd);
    UString text;
    GetWindowTextW(wnd, text.GetBuf((unsigned)size + 1), size + 1);
    text.ReleaseBuf_CalcLen((unsigned)size);
    RECT bounds = label; DrawTextW(dc, text.Ptr(), -1, &bounds, DT_CALCRECT | DT_SINGLELINE);
    bounds.left -= 3; bounds.right += 3;
    Solid(dc, bounds, Background());
    DrawTextW(dc, text.Ptr(), -1, &label, DT_SINGLELINE | hidePrefix);
  }
  else if (kind == BS_PUSHBUTTON || kind == BS_DEFPUSHBUTTON)
  {
    const bool pressed = (SendMessage(wnd, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0;
    Solid(dc, rc, pressed ? RGB(58, 67, 82) : hovered ? RGB(57, 65, 80) : RGB(47, 51, 58));
    Stroke(dc, rc, focused || kind == BS_DEFPUSHBUTTON ? RGB(104, 165, 240) : hovered ? RGB(137, 141, 150) : Border());
    Text(wnd, dc, rc, DT_SINGLELINE | DT_VCENTER | DT_CENTER | hidePrefix);
    if (focused && !(ui & UISF_HIDEFOCUS))
    {
      RECT focus = rc; InflateRect(&focus, -4, -4); DrawFocusRect(dc, &focus);
    }
  }
  else
  {
    TEXTMETRICW tm; GetTextMetricsW(dc, &tm);
    const int size = MyMax(13, (int)tm.tmHeight - 1);
    RECT box = { 0, (rc.bottom - size) / 2, size, (rc.bottom + size) / 2 };
    const bool radio = kind == BS_RADIOBUTTON || kind == BS_AUTORADIOBUTTON;
    const LRESULT checked = SendMessage(wnd, BM_GETCHECK, 0, 0);
    if (radio)
    {
      SelectObject(dc, GetStockObject(DC_PEN)); SetDCPenColor(dc, Border());
      SelectObject(dc, GetStockObject(DC_BRUSH)); SetDCBrushColor(dc, Input());
      Ellipse(dc, box.left, box.top, box.right, box.bottom);
      if (checked == BST_CHECKED)
      {
        SetDCBrushColor(dc, checkColor); SetDCPenColor(dc, checkColor);
        Ellipse(dc, box.left + 4, box.top + 4, box.right - 4, box.bottom - 4);
      }
    }
    else
    {
      Solid(dc, box, Input()); Stroke(dc, box, enabled ? RGB(137, 141, 150) : Border());
      if (checked == BST_CHECKED)
      {
        HPEN pen = CreatePen(PS_SOLID, 2, checkColor);
        HGDIOBJ old = SelectObject(dc, pen);
        MoveToEx(dc, box.left + 3, box.top + size / 2, NULL);
        LineTo(dc, box.left + size / 2 - 1, box.bottom - 4);
        LineTo(dc, box.right - 3, box.top + 3);
        SelectObject(dc, old); DeleteObject(pen);
      }
      else if (checked == BST_INDETERMINATE)
      {
        RECT middle = box; InflateRect(&middle, -3, -5); Solid(dc, middle, RGB(137, 141, 150));
      }
    }
    RECT label = rc; label.left = size + 6;
    Text(wnd, dc, label, DT_SINGLELINE | DT_VCENTER | hidePrefix);
    if (focused && !(ui & UISF_HIDEFOCUS)) FocusLabel(wnd, dc, label);
  }
  RestoreDC(dc, saved); if (!printDC) EndPaint(wnd, &ps);
  return true;
}

inline void PaintTabs(HWND wnd, HDC printDC = NULL)
{
  PAINTSTRUCT ps; HDC dc = printDC ? printDC : BeginPaint(wnd, &ps); const int saved = SaveDC(dc);
  RECT area; GetClientRect(wnd, &area); Solid(dc, area, Background());
  HFONT font = (HFONT)SendMessage(wnd, WM_GETFONT, 0, 0); if (font) SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);
  const int selected = TabCtrl_GetCurSel(wnd);
  const int count = TabCtrl_GetItemCount(wnd);
  for (int i = 0; i < count; i++)
  {
    RECT rc; TabCtrl_GetItemRect(wnd, i, &rc);
    Solid(dc, rc, i == selected ? RGB(57, 65, 80) : Background());
    Stroke(dc, rc, Border());
    wchar_t text[256]; TCITEMW item; ZeroMemory(&item, sizeof(item));
    item.mask = TCIF_TEXT; item.pszText = text; item.cchTextMax = 256;
    SendMessageW(wnd, TCM_GETITEMW, (WPARAM)i, (LPARAM)&item);
    SetTextColor(dc, Foreground());
    DrawTextW(dc, text, -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    if (i == selected && GetFocus() == wnd)
    { InflateRect(&rc, -3, -3); DrawFocusRect(dc, &rc); }
  }
  RestoreDC(dc, saved); if (!printDC) EndPaint(wnd, &ps);
}

inline void PaintSpin(HWND wnd, HDC printDC = NULL)
{
  PAINTSTRUCT ps; HDC dc = printDC ? printDC : BeginPaint(wnd, &ps); const int saved = SaveDC(dc);
  RECT rc; GetClientRect(wnd, &rc);
  Solid(dc, rc, RGB(47, 51, 58)); Stroke(dc, rc, Border());
  const bool horizontal = (GetWindowLongPtr(wnd, GWL_STYLE) & UDS_HORZ) != 0;
  const COLORREF color = IsWindowEnabled(wnd) ? Foreground() : RGB(137, 141, 150);
  SelectObject(dc, GetStockObject(DC_PEN)); SetDCPenColor(dc, color);
  SelectObject(dc, GetStockObject(DC_BRUSH)); SetDCBrushColor(dc, color);
  for (int i = 0; i < 2; i++)
  {
    const int x = horizontal ? rc.right * (1 + 2 * i) / 4 : rc.right / 2;
    const int y = horizontal ? rc.bottom / 2 : rc.bottom * (1 + 2 * i) / 4;
    POINT arrow[3];
    if (horizontal)
    {
      const int direction = i ? 1 : -1;
      arrow[0].x = x - 2 * direction; arrow[0].y = y - 3;
      arrow[1].x = x - 2 * direction; arrow[1].y = y + 3;
      arrow[2].x = x + 2 * direction; arrow[2].y = y;
    }
    else
    {
      const int direction = i ? 1 : -1;
      arrow[0].x = x - 3; arrow[0].y = y - 2 * direction;
      arrow[1].x = x + 3; arrow[1].y = y - 2 * direction;
      arrow[2].x = x; arrow[2].y = y + 2 * direction;
    }
    Polygon(dc, arrow, 3);
  }
  RestoreDC(dc, saved); if (!printDC) EndPaint(wnd, &ps);
}

inline void PaintComboFrame(HWND wnd, HDC printDC = NULL)
{
  COMBOBOXINFO info; ZeroMemory(&info, sizeof(info)); info.cbSize = sizeof(info);
  if (!GetComboBoxInfo(wnd, &info)) return;
  HDC dc = printDC ? printDC : GetDC(wnd); if (!dc) return;
  const int saved = SaveDC(dc);
  const bool enabled = IsWindowEnabled(wnd) != FALSE;
  if (!enabled && (GetWindowLongPtr(wnd, GWL_STYLE) & 3) == CBS_DROPDOWNLIST)
  {
    Solid(dc, info.rcItem, RGB(47, 51, 58));
    HFONT font = (HFONT)SendMessage(wnd, WM_GETFONT, 0, 0);
    if (font) SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(137, 141, 150));
    Text(wnd, dc, info.rcItem, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
  }
  RECT rc; GetClientRect(wnd, &rc);
  Stroke(dc, rc, enabled && (GetFocus() == wnd || GetFocus() == info.hwndItem) ? RGB(104, 165, 240) : Border());
  Solid(dc, info.rcButton, RGB(47, 51, 58));
  const int x = (info.rcButton.left + info.rcButton.right) / 2;
  const int y = (info.rcButton.top + info.rcButton.bottom) / 2;
  POINT arrow[3] = { {x - 4, y - 2}, {x + 4, y - 2}, {x, y + 2} };
  const COLORREF color = IsWindowEnabled(wnd) ? Foreground() : RGB(137, 141, 150);
  SelectObject(dc, GetStockObject(DC_PEN)); SetDCPenColor(dc, color);
  SelectObject(dc, GetStockObject(DC_BRUSH)); SetDCBrushColor(dc, color);
  Polygon(dc, arrow, 3);
  RestoreDC(dc, saved); if (!printDC) ReleaseDC(wnd, dc);
}

inline bool HeaderDraw(LPARAM lp, LRESULT &result)
{
  NMHDR *hdr = (NMHDR *)lp;
  if (hdr->code != NM_CUSTOMDRAW) return false;
  wchar_t name[64]; GetClassNameW(hdr->hwndFrom, name, 64);
  if (lstrcmpiW(name, WC_HEADERW) != 0) return false;
  NMCUSTOMDRAW *cd = (NMCUSTOMDRAW *)lp;
  if (cd->dwDrawStage == CDDS_PREPAINT)
  {
    RECT rc; GetClientRect(hdr->hwndFrom, &rc); Solid(cd->hdc, rc, Background());
    result = CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
    return true;
  }
  if (cd->dwDrawStage == CDDS_POSTPAINT)
  {
    RECT blank; GetClientRect(hdr->hwndFrom, &blank);
    const int count = Header_GetItemCount(hdr->hwndFrom); int right = 0;
    for (int i = 0; i < count; i++)
    {
      RECT rc; if (Header_GetItemRect(hdr->hwndFrom, i, &rc) && rc.right > right) right = rc.right;
    }
    blank.left = right; Solid(cd->hdc, blank, Background());
    result = CDRF_DODEFAULT; return true;
  }
  if (cd->dwDrawStage == CDDS_ITEMPREPAINT)
  {
    const int saved = SaveDC(cd->hdc);
    Solid(cd->hdc, cd->rc, Background());
    wchar_t text[512]; HDITEMW item; ZeroMemory(&item, sizeof(item));
    item.mask = HDI_TEXT | HDI_FORMAT; item.pszText = text; item.cchTextMax = 512;
    SendMessageW(hdr->hwndFrom, HDM_GETITEMW, cd->dwItemSpec, (LPARAM)&item);
    RECT rc = cd->rc; rc.left += 8; rc.right -= 8;
    SetBkMode(cd->hdc, TRANSPARENT); SetTextColor(cd->hdc, Foreground());
    const UINT align = (item.fmt & HDF_RIGHT) ? DT_RIGHT : (item.fmt & HDF_CENTER) ? DT_CENTER : DT_LEFT;
    DrawTextW(cd->hdc, text, -1, &rc, align | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    RestoreDC(cd->hdc, saved); result = CDRF_SKIPDEFAULT; return true;
  }
  return false;
}

inline LRESULT CALLBACK Procedure(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data);
inline void Apply(HWND wnd);

inline BOOL CALLBACK ApplyChild(HWND wnd, LPARAM dark)
{
  wchar_t name[64]; GetClassNameW(wnd, name, 64);
  DWORD_PTR kind = kOther;
  if (lstrcmpiW(name, L"#32770") == 0) kind = kDialog;
  else if (lstrcmpiW(name, L"Button") == 0) kind = kButton;
  else if (lstrcmpiW(name, WC_TABCONTROLW) == 0) kind = kTab;
  else if (lstrcmpiW(name, L"ComboBox") == 0) kind = kCombo;
  else if (lstrcmpiW(name, UPDOWN_CLASSW) == 0) kind = kSpin;
  else if (lstrcmpiW(name, L"Edit") == 0) kind = kEdit;
  if (kind == kButton || kind == kTab || lstrcmpiW(name, PROGRESS_CLASSW) == 0)
    VisualStyle(wnd, dark ? L"" : L"Explorer");
  else if (lstrcmpiW(name, WC_HEADERW) == 0)
    VisualStyle(wnd, dark ? L"" : L"Explorer");
  else if (kind == kCombo || lstrcmpiW(name, L"Edit") == 0)
    VisualStyle(wnd, dark ? L"DarkMode_CFD" : L"Explorer");
  if (lstrcmpiW(name, WC_LISTVIEWW) == 0)
  {
    VisualStyle(wnd, dark ? L"DarkMode_Explorer" : L"Explorer");
    ListView_SetBkColor(wnd, dark ? Input() : GetSysColor(COLOR_WINDOW));
    ListView_SetTextBkColor(wnd, dark ? Input() : GetSysColor(COLOR_WINDOW));
    ListView_SetTextColor(wnd, dark ? Foreground() : GetSysColor(COLOR_WINDOWTEXT));
  }
  if (lstrcmpiW(name, PROGRESS_CLASSW) == 0)
  {
    SendMessage(wnd, PBM_SETBKCOLOR, 0, dark ? Input() : CLR_DEFAULT);
    SendMessage(wnd, PBM_SETBARCOLOR, 0, dark ? RGB(90, 163, 235) : CLR_DEFAULT);
  }
  if (!dark) RemovePropW(wnd, L"7zip.DialogTheme.Hover");
  SetWindowSubclass(wnd, Procedure, kSubclass, kind | (dark ? kDark : 0));
  InvalidateRect(wnd, NULL, TRUE);
  return TRUE;
}

inline void Apply(HWND wnd)
{
  const bool dark = ReadDark();
  ApplyChild(wnd, dark);
  EnumChildWindows(wnd, ApplyChild, dark);
  TitleBar(wnd, dark);
  RedrawWindow(wnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

inline LRESULT CALLBACK Procedure(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data)
{
  const bool dark = (data & kDark) != 0;
  const DWORD_PTR kind = data & ~kDark;
  if (msg == WM_NCDESTROY)
  {
    RemovePropW(wnd, L"7zip.DialogTheme.Hover");
    RemoveWindowSubclass(wnd, Procedure, id);
  }
  if (msg == kRefresh || msg == RegisterWindowMessageW(L"7zip.Alpha.ThemeChanged"))
  {
    Apply(wnd);
    return 0;
  }
  if (kind == kDialog && (msg == WM_SETTINGCHANGE || msg == WM_SYSCOLORCHANGE))
    PostMessage(wnd, kRefresh, 0, 0);
  if (kind == kDialog && msg == WM_PARENTNOTIFY && LOWORD(wp) == WM_CREATE)
    PostMessage(wnd, kRefresh, 0, 0);
  if (dark)
  {
    if (kind == kEdit && (msg == WM_PAINT || msg == WM_NCPAINT
        || (msg == WM_PRINT && (lp & PRF_NONCLIENT))))
    {
      const LRESULT result = DefSubclassProc(wnd, msg, wp, lp);
      PaintInputBorder(wnd, msg == WM_PRINT ? (HDC)wp : NULL);
      return result;
    }
    if (kind == kButton && (msg == WM_MOUSEMOVE || msg == WM_MOUSELEAVE))
    {
      if (msg == WM_MOUSEMOVE && !GetPropW(wnd, L"7zip.DialogTheme.Hover"))
      {
        SetPropW(wnd, L"7zip.DialogTheme.Hover", (HANDLE)(UINT_PTR)1);
        TRACKMOUSEEVENT track; ZeroMemory(&track, sizeof(track));
        track.cbSize = sizeof(track); track.dwFlags = TME_LEAVE; track.hwndTrack = wnd;
        TrackMouseEvent(&track); InvalidateRect(wnd, NULL, TRUE);
      }
      else if (msg == WM_MOUSELEAVE)
      {
        RemovePropW(wnd, L"7zip.DialogTheme.Hover"); InvalidateRect(wnd, NULL, TRUE);
      }
    }
    if (msg == WM_NOTIFY)
    {
      LRESULT result;
      if (HeaderDraw(lp, result)) return result;
    }
    if (msg == WM_ERASEBKGND && (kind == kDialog || kind == kTab || kind == kButton))
    {
      RECT rc; GetClientRect(wnd, &rc); FillRect((HDC)wp, &rc, Brushes().Back); return 1;
    }
    if (msg == WM_CTLCOLORDLG || msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORBTN
        || msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX)
    {
      HDC dc = (HDC)wp;
      const bool input = msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX;
      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, IsWindowEnabled((HWND)lp) ? Foreground() : RGB(137, 141, 150));
      SetBkColor(dc, input ? Input() : Background());
      return (LRESULT)(input ? Brushes().Edit : Brushes().Back);
    }
    if (msg == WM_PAINT || msg == WM_PRINTCLIENT)
    {
      HDC printDC = msg == WM_PRINTCLIENT ? (HDC)wp : NULL;
      if (kind == kButton && PaintButton(wnd, printDC)) return 0;
      if (kind == kTab) { PaintTabs(wnd, printDC); return 0; }
      if (kind == kSpin) { PaintSpin(wnd, printDC); return 0; }
      if (kind == kCombo)
      {
        const LRESULT result = DefSubclassProc(wnd, msg, wp, lp);
        PaintComboFrame(wnd, printDC);
        return result;
      }
    }
    if (kind == kEdit && (msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == WM_ENABLE))
    {
      const LRESULT result = DefSubclassProc(wnd, msg, wp, lp);
      RedrawWindow(wnd, NULL, NULL, RDW_INVALIDATE | RDW_FRAME);
      return result;
    }
    if ((kind == kButton || kind == kSpin) && (msg == BM_SETCHECK || msg == WM_ENABLE || msg == WM_SETFOCUS
        || msg == WM_KILLFOCUS || msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP
        || msg == WM_KEYDOWN || msg == WM_KEYUP))
    {
      const LRESULT result = DefSubclassProc(wnd, msg, wp, lp);
      InvalidateRect(wnd, NULL, TRUE);
      return result;
    }
  }
  return DefSubclassProc(wnd, msg, wp, lp);
}

inline void Attach(HWND wnd, bool page = false)
{
  Apply(wnd);
  if (page)
  {
    HWND parent = GetParent(wnd);
    wchar_t name[64]; GetClassNameW(parent, name, 64);
    if (lstrcmpiW(name, L"#32770") == 0)
    {
      Apply(parent);
      PostMessage(parent, kRefresh, 0, 0);
    }
  }
}

}}}
#endif
