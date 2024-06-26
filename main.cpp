// Copyright (c) 2024 Christopher Antos
// License: http://opensource.org/licenses/MIT

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stdlib.h>
#include <assert.h>
#include <algorithm>

#include "dpi.h"
#include "reticle.h"
#include "version.h"
#include "res.h"

static const WCHAR c_reg_root[] = TEXT("Software\\chrisant996\\Zoomin");
static const WCHAR c_wndclass_name[] = TEXT("ZoominMainWindow");
static const WCHAR* const c_gridline_spacing_name[] =
{
    TEXT("SpacingMinorGridlines"),
    TEXT("SpacingMajorGridlines"),
};
static const WCHAR* const c_show_gridlines_name[] =
{
    TEXT("ShowMinorGridlines"),
    TEXT("ShowMajorGridlines"),
};
static const BYTE c_default_gridlines_spacing[] =
{
    1,
    8,
};

constexpr INT c_min_zoom = 1;
constexpr INT c_max_zoom = 32;
constexpr LONG c_def_width = 480;
constexpr LONG c_def_height = 320;
constexpr UINT c_refresh_timer_id = 1;

static HINSTANCE g_hinst = 0;
static HACCEL g_haccel = 0;

//------------------------------------------------------------------------------
// Registry.

LONG ReadRegLong(const WCHAR* name, LONG default_value)
{
    HKEY hkey;
    LONG ret = default_value;

    if (ERROR_SUCCESS == RegOpenKey(HKEY_CURRENT_USER, c_reg_root, &hkey))
    {
        DWORD type;
        LONG value;
        DWORD cb = sizeof(value);
        if (ERROR_SUCCESS == RegQueryValueEx(hkey, name, 0, &type, reinterpret_cast<BYTE*>(&value), &cb) &&
            type == REG_DWORD &&
            cb == sizeof(value))
        {
            ret = value;
        }
        RegCloseKey(hkey);
    }

    return ret;
}

void WriteRegLong(const WCHAR* name, LONG value)
{
    HKEY hkey;

    if (ERROR_SUCCESS == RegCreateKey(HKEY_CURRENT_USER, c_reg_root, &hkey))
    {
        RegSetValueEx(hkey, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));
        RegCloseKey(hkey);
    }
}

//------------------------------------------------------------------------------
// SizeTracker.

class SizeTracker
{
public:
    void OnCreate(HWND hwnd);
    void OnSize();
    void OnDpiChanged(const DpiScaler& dpi);
    void OnDestroy();

private:
    HWND m_hwnd;
    DpiScaler m_dpi;
    RECT m_rcRestore;
    bool m_maximized = false;
    bool m_resized = false;
};

void SizeTracker::OnCreate(HWND hwnd)
{
    assert(!m_hwnd);

    constexpr DWORD c_flags = SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER;

    m_hwnd = hwnd;
    m_dpi = __GetDpiForWindow(hwnd);
    m_resized = false;

    RECT rcOriginal;
    GetWindowRect(hwnd, &rcOriginal);

    MONITORINFO info = { sizeof(info) };
    {
        POINT ptMonitor;
        ptMonitor.x = ReadRegLong(TEXT("MonitorX"), CW_USEDEFAULT);
        ptMonitor.y = ReadRegLong(TEXT("MonitorY"), CW_USEDEFAULT);

        HMONITOR hmon;
        const bool use_hwnd = (ptMonitor.x == CW_USEDEFAULT || ptMonitor.y == CW_USEDEFAULT);
        if (use_hwnd)
            hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
        else
            hmon = MonitorFromPoint(ptMonitor, MONITOR_DEFAULTTOPRIMARY);
        GetMonitorInfo(hmon, &info);

        if (!use_hwnd)
        {
            RECT rc;
            rc.left = info.rcWork.left + (info.rcWork.right - info.rcWork.left) / 4;
            rc.right = info.rcWork.right - (info.rcWork.right - info.rcWork.left) / 4;
            rc.top = info.rcWork.top + (info.rcWork.bottom - info.rcWork.top) / 4;
            rc.bottom = info.rcWork.bottom - (info.rcWork.bottom - info.rcWork.top) / 4;
            SetWindowPos(hwnd, NULL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, c_flags);

            // m_dpi should get updated by WM_DPICHANGED inside SetWindowPos
            // when appropriate.
            assert(m_dpi.IsDpiEqual(__GetDpiForWindow(hwnd)));
        }
    }

    const LONG xx = ReadRegLong(TEXT("WindowLeftRatio"), CW_USEDEFAULT);
    const LONG yy = ReadRegLong(TEXT("WindowTopRatio"), CW_USEDEFAULT);
    LONG cx96 = ReadRegLong(TEXT("WindowWidth"), CW_USEDEFAULT);
    LONG cy96 = ReadRegLong(TEXT("WindowHeight"), CW_USEDEFAULT);
    const bool maximized = !!ReadRegLong(TEXT("Maximized"), false);

    RECT rcWindow;
    GetWindowRect(hwnd, &rcWindow);

    RECT rc;
    if (xx == CW_USEDEFAULT || yy == CW_USEDEFAULT)
    {
        rc.left = rcWindow.left;
        rc.top = rcWindow.top;
    }
    else
    {
        rc.left = info.rcWork.left + ((xx >= 0) ? xx * (info.rcWork.right - info.rcWork.left) / 50000 : 0);
        rc.top = info.rcWork.top + ((yy >= 0) ? yy * (info.rcWork.bottom - info.rcWork.top) / 50000 : 0);
    }
    if (cx96 == CW_USEDEFAULT || cy96 == CW_USEDEFAULT)
    {
        cx96 = c_def_width;
        cy96 = c_def_height;
    }
    rc.right = rc.left + m_dpi.Scale(cx96);
    rc.bottom = rc.top + m_dpi.Scale(cy96);

    if (rc.right > info.rcWork.right)
        OffsetRect(&rc, info.rcWork.right - rc.right, 0);
    if (rc.bottom > info.rcWork.bottom)
        OffsetRect(&rc, 0, info.rcWork.bottom - rc.bottom);
    if (rc.left < info.rcWork.left)
        OffsetRect(&rc, info.rcWork.left - rc.left, 0);
    if (rc.top < info.rcWork.top)
        OffsetRect(&rc, 0, info.rcWork.top - rc.top);
    rc.right = std::min<LONG>(rc.right, info.rcWork.right);
    rc.bottom = std::min<LONG>(rc.bottom, info.rcWork.bottom);
    SetWindowPos(hwnd, 0, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, c_flags);

    GetWindowRect(hwnd, &m_rcRestore);

    ShowWindow(hwnd, m_maximized ? SW_MAXIMIZE : SW_NORMAL);
}

void SizeTracker::OnSize()
{
    if (!m_hwnd || IsIconic(m_hwnd))
        return;

    bool const maximized = !!IsMaximized(m_hwnd);
    DpiScaler dpi(__GetDpiForWindow(m_hwnd));

    RECT rc;
    GetWindowRect(m_hwnd, &rc);

    if (!maximized &&
        (memcmp(&m_rcRestore, &rc, sizeof(m_rcRestore)) || !dpi.IsDpiEqual(m_dpi)))
    {
        m_resized = true;
        m_rcRestore = rc;
        m_dpi = dpi;
    }

    if (maximized != m_maximized)
    {
        m_resized = true;
        m_maximized = maximized;
    }
}

void SizeTracker::OnDpiChanged(const DpiScaler& dpi)
{
    m_dpi.OnDpiChanged(dpi);
}

void SizeTracker::OnDestroy()
{
    MONITORINFO info = { sizeof(info) };
    HMONITOR hmon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfo(hmon, &info);

    const LONG cxWork = (info.rcWork.right - info.rcWork.left);
    const LONG cyWork = (info.rcWork.bottom - info.rcWork.top);

    WriteRegLong(TEXT("MonitorX"), (info.rcMonitor.left + info.rcMonitor.right) / 2);
    WriteRegLong(TEXT("MonitorY"), (info.rcMonitor.top + info.rcMonitor.bottom) / 2);
    WriteRegLong(TEXT("WindowLeftRatio"), (cxWork > 0) ? (m_rcRestore.left - info.rcWork.left) * 50000 / cxWork : 0);
    WriteRegLong(TEXT("WindowTopRatio"), (cyWork > 0) ? (m_rcRestore.top - info.rcWork.top) * 50000 / cyWork : 0);
    WriteRegLong(TEXT("WindowWidth"), m_dpi.ScaleTo(m_rcRestore.right - m_rcRestore.left, 96));
    WriteRegLong(TEXT("WindowHeight"), m_dpi.ScaleTo(m_rcRestore.bottom - m_rcRestore.top, 96));
    WriteRegLong(TEXT("Maximized"), m_maximized);

    m_resized = false;
}

//------------------------------------------------------------------------------
// CreatePhysicalPalette.
//
// So palette-managed display devices can be rendered.

static HPALETTE CreatePhysicalPalette()
{
    constexpr UINT c_num = 256;

    PLOGPALETTE ppal = (PLOGPALETTE)LocalAlloc(LPTR, sizeof(*ppal) + sizeof(PALETTEENTRY) * c_num);
    if (!ppal)
        return NULL;

    ppal->palVersion = 0x300; // Would be PALVERSION, but that's no longer present in Windows SDKs.
    ppal->palNumEntries = c_num;

    for (UINT ii = 0; ii < c_num; ++ii)
    {
        *((DWORD*)&ppal->palPalEntry[ii]) = ii;
        ppal->palPalEntry[ii].peFlags = (BYTE)PC_EXPLICIT;
    }

    HPALETTE hpal = CreatePalette(ppal);
    LocalFree(ppal);

    return hpal;
}

//------------------------------------------------------------------------------
// Main window.

class Zoomin
{
public:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    // Main functions.
    void OnCreate(HWND hwnd);
    void OnDestroy();
    void OnPaint();
    void OnTimer(WPARAM wParam);
    void OnButtonDown(LPARAM lParam);
    void OnMouseMove(LPARAM lParam);
    void OnCancelMode();
    void OnVScroll(WPARAM wParam);
    void OnKeyDown(WPARAM wParam, LPARAM lParam);
    void OnInitMenuPopup(HMENU hmenu);
    bool OnCommand(WORD id, WORD code, HWND hwndCtrl);
    LRESULT OnNotify(WPARAM wParam, LPARAM lParam);
    void OnSize();
    void OnDpiChanged(const DpiScaler& dpi);

    // Internal helpers.
    void Init();
    void UpdateTitle();
    void SetZoomPoint(LPARAM lParam);
    void SetZoomPoint(POINT pt);
    void SetZoomFactor(INT factor);
    void SetRefresh(bool refresh);
    void SetInterval(UINT interval);
    void SetReticleOpacity(UINT opacity);
    void CalcZoomArea();
    bool GetZoomArea(RECT& rc, POINT* ptCenter=nullptr);
    void PaintZoomRect(HDC hdc=NULL);
    void CopyZoomContent();
    void RelayEvent(UINT msg, WPARAM wParam, LPARAM lParam);

    static INT_PTR CALLBACK OptionsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static INT_PTR CALLBACK AboutDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    HWND m_hwnd = NULL;
    HWND m_tooltips = NULL;
    HPALETTE m_hpal = NULL;
    DpiScaler m_dpi;
    bool m_show_gridlines[2] = {};
    INT m_gridline_spacing[2] = {};
    POINT m_pt = {};
    SIZE m_area;
    INT m_factor = 0;
    RECT m_rcMonitor;
    bool m_captured = false;
    bool m_refresh = false;
    INT m_interval = 0;
    COLORREF m_crGridlines = RGB(0, 0, 0);
    COLORREF m_crReticle = RGB(255, 0, 0);
    COLORREF m_crReticleBorder = RGB(255, 255, 255);
    INT m_reticleOpacity = 75;
    std::unique_ptr<ZoomReticle> m_reticle;
    SizeTracker m_sizeTracker;
};

static Zoomin s_zoomin;

LRESULT CALLBACK Zoomin::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return true;
    case WM_PAINT:
        s_zoomin.OnPaint();
        break;
    case WM_TIMER:
        s_zoomin.OnTimer(wParam);
        break;

    case WM_LBUTTONDOWN:
        s_zoomin.OnButtonDown(lParam);
        break;
    case WM_MOUSEMOVE:
        s_zoomin.RelayEvent(msg, wParam, lParam);
        s_zoomin.OnMouseMove(lParam);
        break;
    case WM_NCMOUSEMOVE:
        s_zoomin.RelayEvent(msg, wParam, lParam);
        goto LDefault;
    case WM_LBUTTONUP:
    case WM_CANCELMODE:
        s_zoomin.OnCancelMode();
        break;

    case WM_VSCROLL:
        s_zoomin.OnVScroll(wParam);
        break;
    case WM_KEYDOWN:
        s_zoomin.OnKeyDown(wParam, lParam);
        break;

    case WM_NOTIFY:
        return s_zoomin.OnNotify(wParam, lParam);

    case WM_INITMENUPOPUP:
        s_zoomin.OnInitMenuPopup(HMENU(wParam));
        break;
    case WM_COMMAND:
        {
            const WORD id = GET_WM_COMMAND_ID(wParam, lParam);
            const HWND hwndCtrl = GET_WM_COMMAND_HWND(wParam, lParam);
            const WORD code = GET_WM_COMMAND_CMD(wParam, lParam);
            if (!s_zoomin.OnCommand(id, code, hwndCtrl))
                goto LDefault;
        }
        break;

    case WM_WINDOWPOSCHANGED:
        s_zoomin.OnSize();
        goto LDefault;
    case WM_DPICHANGED:
        {
            const RECT& rc = *LPCRECT(lParam);
            const DWORD c_flags = SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOOWNERZORDER|SWP_DRAWFRAME;
            s_zoomin.OnDpiChanged(DpiScaler(wParam));
            SetWindowPos(hwnd, NULL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, c_flags);
        }
        break;

    case WM_CREATE:
        s_zoomin.OnCreate(hwnd);
        goto LDefault;
    case WM_DESTROY:
        s_zoomin.OnDestroy();
        break;
    case WM_NCDESTROY:
        PostQuitMessage(0);
        break;

    default:
LDefault:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    return 0;
}

void Zoomin::OnCreate(HWND hwnd)
{
    m_hwnd = hwnd;
    m_dpi = __GetDpiForWindow(m_hwnd);
    Init();
    SendMessage(hwnd, WM_SETICON, true, LPARAM(LoadImage(g_hinst, MAKEINTRESOURCE(IDI_MAIN), IMAGE_ICON, 0, 0, 0)));
    SendMessage(hwnd, WM_SETICON, false, LPARAM(LoadImage(g_hinst, MAKEINTRESOURCE(IDI_MAIN), IMAGE_ICON, 16, 16, 0)));
    m_sizeTracker.OnCreate(hwnd);
}

void Zoomin::OnDestroy()
{
    m_sizeTracker.OnDestroy();

    WriteRegLong(TEXT("PointX"), m_pt.x);
    WriteRegLong(TEXT("PointY"), m_pt.y);
    WriteRegLong(TEXT("ZoomFactor"), m_factor);
    WriteRegLong(TEXT("RefreshEnabled"), m_refresh);
    WriteRegLong(TEXT("RefreshInterval"), m_interval);

    WriteRegLong(TEXT("GridlinesColor"), m_crGridlines);
    WriteRegLong(TEXT("ReticleColor"), m_crReticle);
    WriteRegLong(TEXT("ReticleOutlineColor"), m_crReticleBorder);
    WriteRegLong(TEXT("ReticleOpacity"), clamp<INT>(m_reticleOpacity, 10, 100));

    for (size_t ii = _countof(m_show_gridlines); ii--;)
    {
        WriteRegLong(c_show_gridlines_name[ii], m_show_gridlines[ii]);
        WriteRegLong(c_gridline_spacing_name[ii], m_gridline_spacing[ii]);
    }

    if (m_tooltips)
    {
        DestroyWindow(m_tooltips);
        m_tooltips = NULL;
    }

    if (m_hpal)
    {
        DeleteObject(m_hpal);
        m_hpal = NULL;
    }
}

void Zoomin::OnPaint()
{
    PAINTSTRUCT ps;
    BeginPaint(m_hwnd, &ps);
    SaveDC(ps.hdc);

    PaintZoomRect(ps.hdc);

    RestoreDC(ps.hdc, -1);
    EndPaint(m_hwnd, &ps);
}

void Zoomin::OnTimer(WPARAM wParam)
{
    if (wParam == c_refresh_timer_id)
    {
        const HCURSOR hcur = SetCursor(LoadCursor(NULL, IDC_WAIT));
        PaintZoomRect();
        SetCursor(hcur);
    }
}

void Zoomin::OnButtonDown(LPARAM lParam)
{
    POINT pt;
    RECT rcClient;
    pt.x = SHORT(LOWORD(lParam));
    pt.y = SHORT(HIWORD(lParam));
    GetClientRect(m_hwnd, &rcClient);
    if (!PtInRect(&rcClient, pt))
        return;

    RECT rc;
    if (!GetZoomArea(rc))
        return;

    ZoomReticleSettings settings;
    settings.m_mainColor = m_crReticle;
    settings.m_borderColor = m_crReticleBorder;
    settings.m_opacity = m_reticleOpacity;

    m_reticle = CreateZoomReticle(g_hinst, rc.right - rc.left, rc.bottom - rc.top, settings);
    if (!m_reticle)
        return;
    m_reticle->InitReticle();

    if (m_tooltips)
    {
        DestroyWindow(m_tooltips);
        m_tooltips = nullptr;
    }

    SetCapture(m_hwnd);
    m_captured = true;

    SetZoomPoint(lParam);
}

void Zoomin::OnMouseMove(LPARAM lParam)
{
    if (!m_captured)
        return;

    SetZoomPoint(lParam);
}

void Zoomin::OnCancelMode()
{
    if (!m_captured)
        return;

    m_reticle = nullptr;

    ReleaseCapture();
    m_captured = false;
}

void Zoomin::OnVScroll(WPARAM wParam)
{
    INT factor = m_factor;

    switch (LOWORD(wParam))
    {
    case SB_LINEUP:
        --factor;
        break;
    case SB_LINEDOWN:
        ++factor;
        break;
    case SB_PAGEUP:
        factor -= 2;
        break;
    case SB_PAGEDOWN:
        factor += 2;
        break;
    case SB_THUMBPOSITION:
    case SB_THUMBTRACK:
        factor = HIWORD(wParam);
        break;
    }

    SetZoomFactor(factor);
}

void Zoomin::OnKeyDown(WPARAM wParam, LPARAM lParam)
{
    switch (wParam)
    {
    case VK_UP:
    case VK_DOWN:
    case VK_LEFT:
    case VK_RIGHT:
        if (m_pt.x != MAXINT && m_pt.y != MAXINT)
        {
            const bool shift = (GetKeyState(VK_SHIFT) < 0);
            const bool ctrl = (GetKeyState(VK_CONTROL) < 0);

            POINT pt = m_pt;
            switch (wParam)
            {
            case VK_UP:
                if (ctrl) pt.y = m_rcMonitor.top;
                else pt.y -= shift ? 8 : 1;
                break;
            case VK_DOWN:
                if (ctrl) pt.y = m_rcMonitor.bottom - 1;
                else pt.y += shift ? 8 : 1;
                break;
            case VK_LEFT:
                if (ctrl) pt.x = m_rcMonitor.left;
                else pt.x -= shift ? 8 : 1;
                break;
            case VK_RIGHT:
                if (ctrl) pt.x = m_rcMonitor.right - 1;
                else pt.x += shift ? 8 : 1;
                break;
            default:
                return;
            }

            SetZoomPoint(pt);
        }
        break;
    }
}

void Zoomin::OnInitMenuPopup(HMENU hmenu)
{
    CheckMenuItem(hmenu, IDM_OPTIONS_GRIDLINES, m_show_gridlines[0] ? MF_CHECKED : MF_UNCHECKED);
}

bool Zoomin::OnCommand(WORD id, WORD code, HWND hwndCtrl)
{
    switch (id)
    {
    case IDM_EDIT_COPY:
        CopyZoomContent();
        break;
    case IDM_EDIT_REFRESH:
        PaintZoomRect();
        break;
    case IDM_OPTIONS_GRIDLINES:
        m_show_gridlines[0] = !m_show_gridlines[0];
        PaintZoomRect();
        break;
    case IDM_OPTIONS_OPTIONS:
        if (DialogBox(g_hinst, MAKEINTRESOURCE(IDD_OPTIONS), m_hwnd, OptionsDlgProc))
            PaintZoomRect();
        break;
    case IDM_HELP_ABOUT:
        DialogBox(g_hinst, MAKEINTRESOURCE(IDD_ABOUT), m_hwnd, AboutDlgProc);
        break;
    case IDM_REFRESH_ONOFF:
        SetRefresh(!m_refresh);
        break;

    case IDM_ZOOM_OUT:
        SetZoomFactor(m_factor - 1);
        break;
    case IDM_ZOOM_IN:
        SetZoomFactor(m_factor + 1);
        break;

    case IDM_FLASH_BORDER:
        if (!m_reticle)
        {
            RECT rc;
            POINT pt;
            if (!GetZoomArea(rc, &pt))
                break;

            ZoomReticleSettings settings;
            settings.m_mainColor = m_crReticle;
            settings.m_borderColor = m_crReticleBorder;
            settings.m_opacity = m_reticleOpacity;

            std::unique_ptr<ZoomReticle> reticle = CreateZoomReticle(g_hinst, rc.right - rc.left, rc.bottom - rc.top, settings);
            if (reticle)
            {
                reticle->InitReticle();
                reticle->UpdateReticlePosition(pt);
                reticle->Flash();
            }
        }
        break;

    default:
        return false;
    }

    return true;
}

LRESULT Zoomin::OnNotify(WPARAM wParam, LPARAM lParam)
{
    NMHDR* const pnm = reinterpret_cast<NMHDR*>(lParam);

    switch (pnm->code)
    {
    case TTN_SHOW:
        {
            TOOLINFO ti = { sizeof(ti) };
            ti.uFlags = TTF_TRACK|TTF_ABSOLUTE;
            ti.hwnd = m_hwnd;
            ti.uId = 1;
            const DWORD size = LOWORD(SendMessage(m_tooltips, TTM_GETBUBBLESIZE, 0, LPARAM(&ti)));

            RECT rcWindow;
            RECT rcTooltip;
            GetWindowRect(m_hwnd, &rcWindow);
            GetClientRect(m_hwnd, &rcTooltip);
            MapWindowPoints(m_hwnd, NULL, LPPOINT(&rcTooltip), 2);
            rcWindow.top = rcTooltip.top;

            SendMessage(m_tooltips, TTM_ADJUSTRECT, false, (LPARAM)&rcTooltip);
            rcTooltip.right = rcTooltip.left + LOWORD(size);
            OffsetRect(&rcTooltip, -rcTooltip.left, -rcTooltip.top);
            OffsetRect(&rcTooltip, (rcWindow.left + rcWindow.right) / 2 - (rcTooltip.right - rcTooltip.left) / 2, rcWindow.top + m_dpi.Scale(8));

            SetWindowPos(m_tooltips, NULL, rcTooltip.left, rcTooltip.top, 0, 0, SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
            SetWindowLong(m_hwnd, DWLP_MSGRESULT, true);
        }
        return true;
    }

    return 0;
}

void Zoomin::OnSize()
{
    m_sizeTracker.OnSize();
    CalcZoomArea();
}

void Zoomin::OnDpiChanged(const DpiScaler& dpi)
{
    m_dpi.OnDpiChanged(dpi);
    m_sizeTracker.OnDpiChanged(dpi);
    CalcZoomArea();
    InvalidateRect(m_hwnd, nullptr, false);
}

void Zoomin::Init()
{
    POINT pt;
    pt.x = ReadRegLong(TEXT("PointX"), MAXINT);
    pt.y = ReadRegLong(TEXT("PointY"), MAXINT);
    SetZoomPoint(pt);

    SetZoomFactor(ReadRegLong(TEXT("ZoomFactor"), 4));

    SetInterval(ReadRegLong(TEXT("RefreshInterval"), 20));
    SetRefresh(!!ReadRegLong(TEXT("RefreshEnabled"), false));

    m_crGridlines = ReadRegLong(L"GridlinesColor", RGB(0, 0, 0));
    m_crReticle = ReadRegLong(L"ReticleColor", RGB(255, 0, 0));
    m_crReticleBorder = ReadRegLong(L"ReticleOutlineColor", RGB(255, 255, 255));
    SetReticleOpacity(ReadRegLong(L"ReticleOpacity", 75));

    for (size_t ii = _countof(m_show_gridlines); ii--;)
    {
        m_show_gridlines[ii] = !!ReadRegLong(c_show_gridlines_name[ii], false);
        m_gridline_spacing[ii] = ReadRegLong(c_gridline_spacing_name[ii], c_default_gridlines_spacing[ii]);
    }

    m_hpal = CreatePhysicalPalette();

    m_tooltips = CreateWindow(TOOLTIPS_CLASS, L"", WS_POPUP,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            CW_USEDEFAULT, CW_USEDEFAULT,
                            m_hwnd, NULL, g_hinst, NULL);
    if (m_tooltips)
    {
        TOOLINFO ti = { sizeof(ti) };
        ti.uFlags = /*TTF_SUBCLASS|*/TTF_TRANSPARENT;
        ti.hwnd = m_hwnd;
        ti.uId = 1;
        ti.rect.right = MAXINT;
        ti.rect.bottom = MAXINT;
        ti.lpszText = L"Click and drag to select zoomin area.";
        SendMessage(m_tooltips, TTM_ADDTOOL, 0, LPARAM(&ti));
    }
}

void Zoomin::UpdateTitle()
{
    WCHAR title[64];
    wsprintfW(title, TEXT("Zoomin \u00b7 %ux"), m_factor);
    SetWindowText(m_hwnd, title);
}

void Zoomin::SetZoomPoint(LPARAM lParam)
{
    POINT pt;
    pt.x = SHORT(LOWORD(lParam));
    pt.y = SHORT(HIWORD(lParam));

    ClientToScreen(m_hwnd, &pt);

    SetZoomPoint(pt);
}

void Zoomin::SetZoomPoint(POINT pt)
{
    if (pt.x == MAXINT || pt.y == MAXINT)
        return;

    MONITORINFO mi = { sizeof(mi) };
    HMONITOR hmon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfo(hmon, &mi))
    {
        SetRectEmpty(&m_rcMonitor);
        return;
    }

    m_rcMonitor = mi.rcMonitor;

    m_pt.x = clamp(pt.x, m_rcMonitor.left, m_rcMonitor.right - 1);
    m_pt.y = clamp(pt.y, m_rcMonitor.top, m_rcMonitor.bottom - 1);

    if (m_reticle)
    {
        RECT rc;
        if (!GetZoomArea(rc, &pt))
        {
            assert(false); // This should be impossible.
            return;
        }

        m_reticle->UpdateReticlePosition(pt);
        m_reticle->Invoke([](){ s_zoomin.PaintZoomRect(); });
    }
    else
    {
        PaintZoomRect();
    }
}

void Zoomin::SetZoomFactor(INT factor)
{
    factor = clamp(factor, c_min_zoom, c_max_zoom);

    if (factor == m_factor)
        return;

    m_factor = factor;

    CalcZoomArea();

    SCROLLINFO si = { sizeof(si) };
    si.fMask = SIF_ALL|SIF_DISABLENOSCROLL;
    si.nMin = c_min_zoom;
    si.nMax = c_max_zoom;
    si.nPage = 1;
    si.nPos = m_factor;
    SetScrollInfo(m_hwnd, SB_VERT, &si, true);

    UpdateTitle();

    InvalidateRect(m_hwnd, nullptr, false);
}

void Zoomin::SetRefresh(bool refresh)
{
    if (refresh == m_refresh)
        return;

    m_refresh = refresh;

    if (refresh)
        SetInterval(m_interval);
    else
        KillTimer(m_hwnd, c_refresh_timer_id);

    MENUITEMINFO mii = { sizeof(mii) };
    mii.fMask = MIIM_STRING;
    mii.dwTypeData = refresh ? TEXT("Turn &Refresh Off!") : TEXT("Turn &Refresh On!");
    SetMenuItemInfo(GetMenu(m_hwnd), IDM_REFRESH_ONOFF, false, &mii);

    DrawMenuBar(m_hwnd);
}

void Zoomin::SetInterval(UINT interval)
{
    m_interval = interval;
    if (m_refresh)
        SetTimer(m_hwnd, c_refresh_timer_id, std::max<INT>(m_interval, 1) * 100, nullptr);
}

void Zoomin::SetReticleOpacity(UINT opacity)
{
    m_reticleOpacity = clamp<INT>(opacity, 10, 100);
}

void Zoomin::CalcZoomArea()
{
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const INT factor = std::max<INT>(0, m_dpi.Scale(m_factor));
    m_area.cx = ((rc.right - rc.left) + factor - 1) / factor;
    m_area.cy = ((rc.bottom - rc.top) + factor - 1) / factor;
    UpdateTitle();
}

bool Zoomin::GetZoomArea(RECT& rc, POINT* pt)
{
    if (m_pt.x == MAXINT || m_pt.y == MAXINT)
        return false;

    const LONG xx = clamp(m_pt.x, m_rcMonitor.left + m_area.cx / 2, m_rcMonitor.right - (m_area.cx - m_area.cx / 2));
    const LONG yy = clamp(m_pt.y, m_rcMonitor.top + m_area.cy / 2, m_rcMonitor.bottom - (m_area.cy - m_area.cy / 2));

    rc.left = xx - m_area.cx / 2;
    rc.top = yy - m_area.cy / 2;
    rc.right = rc.left + m_area.cx;
    rc.bottom = rc.top + m_area.cy;

    // GetZoomArea adjusts the rect to be fully on a single monitor.
    // Update the point so the reticle position matches the zoom area.
    if (pt)
    {
        pt->x = rc.left + (rc.right - rc.left) / 2;
        pt->y = rc.top + (rc.bottom - rc.top) / 2;
    }

    return (rc.right > rc.left && rc.bottom > rc.top);
}

void Zoomin::PaintZoomRect(HDC hdc)
{
    RECT rc;
    if (!GetZoomArea(rc))
        return;

    assert(rc.right > rc.left);
    assert(rc.bottom > rc.top);

    RECT rcClient;
    GetClientRect(m_hwnd, &rcClient);

    const HDC hdcTo = hdc ? hdc : GetDC(m_hwnd);
    const HDC hdcFrom = GetDC(NULL);
    const int bltmode = SetStretchBltMode(hdcTo, COLORONCOLOR);

    HPALETTE hpal;
    if (m_hpal)
    {
        hpal = SelectPalette(hdcTo, m_hpal, false);
        RealizePalette(hdcTo);
    }

    const INT factor = std::max<INT>(1, m_dpi.Scale(m_factor));

    StretchBlt(hdcTo, 0, 0, factor * m_area.cx, factor * m_area.cy,
               hdcFrom, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SRCCOPY);

    static_assert(_countof(m_show_gridlines) == _countof(m_gridline_spacing), "array size mismatch");
    for (size_t ii = 0; ii < _countof(m_show_gridlines); ++ii)
    {
        const int thick = !ii ? 0 : (m_show_gridlines[0] ? 2 : 0);
        if (m_show_gridlines[ii] && factor > (thick ? 2 : 1))
        {
            const HPEN hpenLine = CreatePen(PS_SOLID, thick, m_crGridlines);
            const HPEN hpenOld = SelectPen(hdcTo, hpenLine);
            for (LONG xx = rcClient.left; xx <= rcClient.right; xx += factor * m_gridline_spacing[ii])
            {
                MoveToEx(hdcTo, xx, rcClient.top, nullptr);
                LineTo(hdcTo, xx, rcClient.bottom);
            }
            for (LONG yy = rcClient.top; yy <= rcClient.bottom; yy += factor * m_gridline_spacing[ii])
            {
                MoveToEx(hdcTo, rcClient.left, yy, nullptr);
                LineTo(hdcTo, rcClient.right, yy);
            }
            SelectPen(hdcTo, hpenOld);
            DeleteObject(hpenLine);
        }
    }

    if (m_hpal)
    {
        SelectPalette(hdcTo, hpal, false);
    }

    SetStretchBltMode(hdcTo, bltmode);
    ReleaseDC(NULL, hdcFrom);
    if (!hdc)
        ReleaseDC(m_hwnd, hdcTo);
}

void Zoomin::CopyZoomContent()
{
    RECT rc;
    GetClientRect(m_hwnd, &rc);

    HDC hdcFrom = GetDC(m_hwnd);
    HDC hdcTo = hdcFrom ? CreateCompatibleDC(hdcFrom) : NULL;
    HBITMAP hbmp = hdcFrom ? CreateCompatibleBitmap(hdcFrom, rc.right - rc.left, rc.bottom - rc.top) : NULL;
    if (hdcFrom && hdcTo && hbmp && OpenClipboard(m_hwnd))
    {
        EmptyClipboard();

        const DWORD width = MulDiv(rc.right - rc.left, 254, GetDeviceCaps(hdcFrom, LOGPIXELSX));
        const DWORD height = MulDiv(rc.bottom - rc.top, 254, GetDeviceCaps(hdcFrom, LOGPIXELSY));
        SetBitmapDimensionEx(hbmp, width, height, nullptr);

        HBITMAP hbmpOld = SelectBitmap(hdcTo, hbmp);
        BitBlt(hdcTo, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
               hdcFrom, rc.left, rc.top, SRCCOPY);
        SelectBitmap(hdcTo, hbmpOld);

        SetClipboardData(CF_BITMAP, hbmp);
        hbmp = NULL;

        CloseClipboard();
    }
    else
    {
        MessageBeep(0xffffffff);
    }

    if (hbmp)
        DeleteObject(hbmp);
    if (hdcTo)
        DeleteDC(hdcTo);
    if (hdcFrom)
        ReleaseDC(m_hwnd, hdcFrom);
}

void Zoomin::RelayEvent(UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (m_tooltips)
    {
        MSG relay = {};
        relay.hwnd = m_hwnd;
        relay.message = msg;
        relay.wParam = wParam;
        relay.lParam = lParam;
        SendMessage(m_tooltips, TTM_RELAYEVENT, 0, LPARAM(&relay));
    }
}

static void CenterDialog(HWND hwnd)
{
    RECT rc;
    RECT rcParent;

    GetWindowRect(hwnd, &rc);
    GetWindowRect(GetParent(hwnd), &rcParent);

    const LONG xx = (rcParent.right + rcParent.left) / 2 - (rc.right - rc.left) / 2;
    const LONG yy = (rcParent.bottom + rcParent.top) / 2 - (rc.bottom - rc.top) / 2;

    MoveWindow(hwnd, xx, yy, rc.right - rc.left, rc.bottom - rc.top, false);
}

INT_PTR CALLBACK Zoomin::OptionsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static COLORREF s_crGridlines;
    static COLORREF s_crReticle;
    static COLORREF s_crReticleBorder;

    switch (msg)
    {
    case WM_INITDIALOG:
        CheckDlgButton(hwnd, IDC_ENABLE_REFRESH, s_zoomin.m_refresh ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_ENABLE_MINORLINES, s_zoomin.m_show_gridlines[0] ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_ENABLE_MAJORLINES, s_zoomin.m_show_gridlines[1] ? BST_CHECKED : BST_UNCHECKED);
        SendDlgItemMessage(hwnd, IDC_REFRESH_INTERVAL, EM_LIMITTEXT, 3, 0);
        SendDlgItemMessage(hwnd, IDC_MINOR_RESOLUTION, EM_LIMITTEXT, 4, 0);
        SendDlgItemMessage(hwnd, IDC_MAJOR_RESOLUTION, EM_LIMITTEXT, 4, 0);
        SetDlgItemInt(hwnd, IDC_REFRESH_INTERVAL, s_zoomin.m_interval, false);
        SetDlgItemInt(hwnd, IDC_MINOR_RESOLUTION, s_zoomin.m_gridline_spacing[0], false);
        SetDlgItemInt(hwnd, IDC_MAJOR_RESOLUTION, s_zoomin.m_gridline_spacing[1], false);
        s_crGridlines = s_zoomin.m_crGridlines;
        s_crReticle = s_zoomin.m_crReticle;
        s_crReticleBorder = s_zoomin.m_crReticleBorder;
        SetDlgItemInt(hwnd, IDC_RETICLE_OPACITY, s_zoomin.m_reticleOpacity, false);
        CenterDialog(hwnd);
        return true;

    case WM_DRAWITEM:
        {
            COLORREF cr;
            const DRAWITEMSTRUCT* p = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            switch (wParam)
            {
            case IDC_GRIDLINES_SAMPLE:      cr = s_crGridlines; break;
            case IDC_RETICLE_SAMPLE:        cr = s_crReticle; break;
            case IDC_OUTLINE_SAMPLE:        cr = s_crReticleBorder; break;
            default:                        cr = RGB(255, 0, 255); break;
            }

            RECT rc = p->rcItem;
            HBRUSH hbr = CreateSolidBrush(cr);
            const HPEN old_pen = SelectPen(p->hDC, GetStockPen(BLACK_PEN));
            Rectangle(p->hDC, rc.left, rc.top, rc.right, rc.bottom);
            InflateRect(&rc, -1, -1);
            FillRect(p->hDC, &rc, hbr);
            SelectPen(p->hDC, old_pen);
            DeleteObject(hbr);
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_GRIDLINES_COLOR:
        case IDC_RETICLE_COLOR:
        case IDC_OUTLINE_COLOR:
            {
                static bool s_init_colors = true;
                static COLORREF s_custom_colors[16];
                if (s_init_colors)
                {
                    s_init_colors = false;
                    memset(&s_custom_colors, 255, sizeof(s_custom_colors));
                }

                CHOOSECOLOR cc = { sizeof(cc) };
                cc.hwndOwner = hwnd;
                cc.lpCustColors = s_custom_colors;
                cc.Flags = CC_RGBINIT|CC_SOLIDCOLOR|CC_FULLOPEN;
                switch (LOWORD(wParam))
                {
                case IDC_GRIDLINES_COLOR:   cc.rgbResult = s_crGridlines; break;
                case IDC_RETICLE_COLOR:     cc.rgbResult = s_crReticle; break;
                case IDC_OUTLINE_COLOR:     cc.rgbResult = s_crReticleBorder; break;
                }
                if (ChooseColor(&cc))
                {
                    switch (LOWORD(wParam))
                    {
                    case IDC_GRIDLINES_COLOR:
                        s_crGridlines = cc.rgbResult;
                        InvalidateRect(GetDlgItem(hwnd, IDC_GRIDLINES_SAMPLE), nullptr, true);
                        break;
                    case IDC_RETICLE_COLOR:
                        s_crReticle = cc.rgbResult;
                        InvalidateRect(GetDlgItem(hwnd, IDC_RETICLE_SAMPLE), nullptr, true);
                        break;
                    case IDC_OUTLINE_COLOR:
                        s_crReticleBorder = cc.rgbResult;
                        InvalidateRect(GetDlgItem(hwnd, IDC_OUTLINE_SAMPLE), nullptr, true);
                        break;
                    }
                }
            }
            break;

        case IDOK:
            s_zoomin.SetInterval(GetDlgItemInt(hwnd, IDC_REFRESH_INTERVAL, nullptr, false));
            s_zoomin.m_gridline_spacing[0] = GetDlgItemInt(hwnd, IDC_MINOR_RESOLUTION, nullptr, false);
            s_zoomin.m_gridline_spacing[1] = GetDlgItemInt(hwnd, IDC_MAJOR_RESOLUTION, nullptr, false);
            s_zoomin.SetRefresh(!!IsDlgButtonChecked(hwnd, IDC_ENABLE_REFRESH));
            s_zoomin.m_show_gridlines[0] = !!IsDlgButtonChecked(hwnd, IDC_ENABLE_MINORLINES);
            s_zoomin.m_show_gridlines[1] = !!IsDlgButtonChecked(hwnd, IDC_ENABLE_MAJORLINES);
            if (s_zoomin.m_crGridlines != s_crGridlines)
            {
                s_zoomin.m_crGridlines = s_crGridlines;
                InvalidateRect(s_zoomin.m_hwnd, nullptr, true);
            }
            s_zoomin.m_crReticle = s_crReticle;
            s_zoomin.m_crReticleBorder = s_crReticleBorder;
            s_zoomin.SetReticleOpacity(GetDlgItemInt(hwnd, IDC_RETICLE_OPACITY, nullptr, false));
            EndDialog(hwnd, true);
            break;

        case IDCANCEL:
            EndDialog(hwnd, false);
            break;
        }
        break;
    }

    return false;
}

INT_PTR CALLBACK Zoomin::AboutDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        {
            char version[64];
            wsprintfA(version, "Zoomin v%u.%u", VERSION_MAJOR, VERSION_MINOR);
            SetDlgItemTextA(hwnd, IDC_VERSION, version);
            SetDlgItemTextA(hwnd, IDC_COPYRIGHT, COPYRIGHT_STR);
            SetFocus(GetDlgItem(hwnd, IDOK));
            CenterDialog(hwnd);
        }
        return false;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
        case IDCANCEL:
            EndDialog(hwnd, true);
            break;

        case IDC_REPO:
            AllowSetForegroundWindow(ASFW_ANY);
            ShellExecuteA(0, nullptr, "https://github.com/chrisant996/zoomin", 0, 0, SW_NORMAL);
            break;
        }
        break;
    }

    return false;
}

static HWND CreateMainWindow()
{
    WNDCLASS wc = {};
    wc.lpszClassName = c_wndclass_name;
    wc.lpszMenuName = MAKEINTRESOURCE(IDR_MENU);
    wc.style = CS_HREDRAW|CS_VREDRAW;
    wc.hIcon = LoadIcon(g_hinst, MAKEINTRESOURCE(IDI_MAIN));
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = HBRUSH(COLOR_WINDOW + 1);
    wc.hInstance = g_hinst;
    wc.lpfnWndProc = Zoomin::WndProc;
    RegisterClass(&wc);

    const DWORD c_style = WS_OVERLAPPEDWINDOW|WS_VSCROLL;
    return CreateWindow(c_wndclass_name, TEXT("Zoomin"), c_style,
                        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                        NULL, NULL, g_hinst, NULL);
}

//------------------------------------------------------------------------------
// WinMain.

int PASCAL WinMain(HINSTANCE hinstCurrent, HINSTANCE /*hinstPrevious*/, LPSTR /*lpszCmdLine*/, int nCmdShow)
{
    MSG msg = {};
    g_hinst = hinstCurrent;
    g_haccel = LoadAccelerators(g_hinst, MAKEINTRESOURCE(IDR_ACCEL));

    HWND hwnd = CreateMainWindow();
    if (hwnd)
    {
        ShowWindow(hwnd, nCmdShow);

        while (GetMessage(&msg, nullptr, 0, 0))
        {
            if (!g_haccel || !TranslateAccelerator(hwnd, g_haccel, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }

    return int(msg.wParam);
}
