// Copyright (c) 2024 Christopher Antos
// License: http://opensource.org/licenses/MIT

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <stdlib.h>
#include <assert.h>
#include <algorithm>

#include "dpi.h"
#include "res.h"

static const WCHAR c_reg_root[] = TEXT("Software\\chrisant996\\Zoomin");
static const WCHAR c_wndclass_name[] = TEXT("ZoominMainWindow");

constexpr INT c_min_zoom = 1;
constexpr INT c_max_zoom = 32;

static HINSTANCE g_hinst = 0;
static HACCEL g_haccel = 0;

template <typename T> T clamp(const T value, const T low, const T high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

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

    m_hwnd = hwnd;
    m_dpi = __GetDpiForWindow(hwnd);
    m_resized = false;

    LONG cx = ReadRegLong(TEXT("WindowWidth"), 0);
    LONG cy = ReadRegLong(TEXT("WindowHeight"), 0);
    const bool maximized = !!ReadRegLong(TEXT("Maximized"), false);

    MONITORINFO info = { sizeof(info) };
    HMONITOR hmon = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTOPRIMARY);
    GetMonitorInfo(hmon, &info);

    if (cx > 0)
        cx = m_dpi.Scale(cx);
    if (cy > 0)
        cy = m_dpi.Scale(cy);

    cx = std::min<LONG>(cx, info.rcWork.right - info.rcWork.left);
    cy = std::min<LONG>(cy, info.rcWork.bottom - info.rcWork.top);
    cx = std::max<LONG>(cx, m_dpi.Scale(480));
    cy = std::max<LONG>(cy, m_dpi.Scale(320));

// TODO: restore position also.
#if 0
    const LONG xx = info.rcWork.left + ((info.rcWork.right - info.rcWork.left) - cx) / 2;
    const LONG yy = info.rcWork.top + ((info.rcWork.bottom - info.rcWork.top) - cy) / 2;
#else
    RECT rc;
    GetWindowRect(m_hwnd, &rc);
    const LONG xx = rc.left;
    const LONG yy = rc.top;
#endif

    SetWindowPos(m_hwnd, 0, xx, yy, cx, cy, SWP_NOZORDER);
    GetWindowRect(m_hwnd, &m_rcRestore);

    ShowWindow(m_hwnd, m_maximized ? SW_MAXIMIZE : SW_NORMAL);
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
    const LONG cx = m_dpi.ScaleTo(m_rcRestore.right - m_rcRestore.left, 96);
    const LONG cy = m_dpi.ScaleTo(m_rcRestore.bottom - m_rcRestore.top, 96);

    WriteRegLong(TEXT("WindowWidth"), cx);
    WriteRegLong(TEXT("WindowHeight"), cy);
    WriteRegLong(TEXT("Maximized"), m_maximized);

    m_resized = false;
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
    void OnButtonDown(LPARAM lParam);
    void OnMouseMove(LPARAM lParam);
    void OnCancelMode();
    void OnVScroll(WPARAM wParam);
    void OnKeyDown(WPARAM wParam, LPARAM lParam);
    bool OnCommand(WORD id, WORD code, HWND hwndCtrl);
    void OnSize();
    void OnDpiChanged(const DpiScaler& dpi);

    // Internal helpers.
    void Init();
    void SetZoomPoint(LPARAM lParam);
    void SetZoomPoint(POINT pt);
    void SetZoomFactor(INT factor);
    void CalcZoomArea();
    void GetZoomArea(RECT& rc);
    void InvertReticle();
    void PaintZoomRect(HDC hdc=NULL);

private:
    HWND m_hwnd = NULL;
    DpiScaler m_dpi;
    POINT m_pt;
    SIZE m_area;
    INT m_factor = 0;
    RECT m_rcMonitor;
    bool m_captured = false;
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

    case WM_LBUTTONDOWN:
        s_zoomin.OnButtonDown(lParam);
        break;
    case WM_MOUSEMOVE:
        s_zoomin.OnMouseMove(lParam);
        break;
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
        s_zoomin.OnDpiChanged(DpiScaler(wParam));
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
    WriteRegLong(TEXT("PointX"), m_pt.x);
    WriteRegLong(TEXT("PointY"), m_pt.y);
    WriteRegLong(TEXT("ZoomFactor"), m_factor);
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

void Zoomin::OnButtonDown(LPARAM lParam)
{
    SetCapture(m_hwnd);
    m_captured = true;

    SetZoomPoint(lParam);
    InvertReticle();

    PaintZoomRect();
}

void Zoomin::OnMouseMove(LPARAM lParam)
{
    if (!m_captured)
        return;

    InvertReticle();
    SetZoomPoint(lParam);
    InvertReticle();

    PaintZoomRect();
}

void Zoomin::OnCancelMode()
{
    if (!m_captured)
        return;

    InvertReticle();

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

bool Zoomin::OnCommand(WORD id, WORD code, HWND hwndCtrl)
{
    switch (id)
    {
    case IDM_EDIT_COPY:
// TODO: copy zoomed content to clipboard.
        break;
    case IDM_EDIT_REFRESH:
        PaintZoomRect();
        break;
    case IDM_OPTIONS_GRIDLINES:
// TODO: toggle gridlines.
        break;
    case IDM_OPTIONS_OPTIONS:
// TODO: Options dialog.
        break;
    case IDM_HELP_ABOUT:
// TODO: About dialog with link to repo.
        break;
    case IDM_REFRESH_ONOFF:
// TODO: toggle auto-refresh.
        break;

    case IDM_ZOOM_OUT:
        SetZoomFactor(m_factor - 1);
        break;
    case IDM_ZOOM_IN:
        SetZoomFactor(m_factor + 1);
        break;

    default:
        return false;
    }

    return true;
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
    InvalidateRect(m_hwnd, nullptr, false);
}

void Zoomin::Init()
{
    POINT pt;
    pt.x = ReadRegLong(TEXT("PointX"), MAXINT);
    pt.y = ReadRegLong(TEXT("PointY"), MAXINT);
    SetZoomPoint(pt);

    SetZoomFactor(ReadRegLong(TEXT("ZoomFactor"), 4));
}

void Zoomin::SetZoomPoint(LPARAM lParam)
{
    POINT pt;
    pt.x = SHORT(LOWORD(lParam));
    pt.y = SHORT(HIWORD(lParam));
// TODO: handle DPI awareness.
    ClientToScreen(m_hwnd, &pt);
    SetZoomPoint(pt);
}

void Zoomin::SetZoomPoint(POINT pt)
{
    const bool invalid = (pt.x == MAXINT || pt.y == MAXINT);
    if (invalid)
    {
        RECT rc;
        GetWindowRect(m_hwnd, &rc);
        pt.x = rc.left + (rc.right - rc.left) / 2;
        pt.y = rc.top + (rc.bottom - rc.top) / 2;
    }

    MONITORINFO mi = { sizeof(mi) };
    HMONITOR hmon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfo(hmon, &mi))
    {
        SetRectEmpty(&m_rcMonitor);
        return;
    }

    m_rcMonitor = mi.rcMonitor;

    if (invalid)
    {
        pt.x = m_rcMonitor.left + (m_rcMonitor.right - m_rcMonitor.left) / 2;
        pt.y = m_rcMonitor.top + (m_rcMonitor.bottom - m_rcMonitor.top) / 2;
    }

    m_pt.x = clamp(pt.x, m_rcMonitor.left, m_rcMonitor.right - 1);
    m_pt.y = clamp(pt.y, m_rcMonitor.top, m_rcMonitor.bottom - 1);
    PaintZoomRect();
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

    WCHAR title[64];
    wsprintfW(title, TEXT("Zoomin  ( %ux )"), m_factor);
    SetWindowText(m_hwnd, title);

    InvalidateRect(m_hwnd, nullptr, false);
}

void Zoomin::CalcZoomArea()
{
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    m_area.cx = ((rc.right - rc.left) + m_factor - 1) / m_factor;
    m_area.cy = ((rc.bottom - rc.top) + m_factor - 1) / m_factor;
}

void Zoomin::GetZoomArea(RECT& rc)
{
    const LONG xx = clamp(m_pt.x, m_rcMonitor.left + m_area.cx / 2, m_rcMonitor.right - (m_area.cx - m_area.cx / 2));
    const LONG yy = clamp(m_pt.y, m_rcMonitor.top + m_area.cy / 2, m_rcMonitor.bottom - (m_area.cy - m_area.cy / 2));

    rc.left = xx - m_area.cx / 2;
    rc.top = yy - m_area.cy / 2;
    rc.right = rc.left + m_area.cx;
    rc.bottom = rc.top + m_area.cy;
}

void Zoomin::InvertReticle()
{
    if (m_rcMonitor.right <= m_rcMonitor.left || m_rcMonitor.bottom <= m_rcMonitor.top)
        return;

    RECT rc;
    GetZoomArea(rc);

    const LONG thick = 1;
    InflateRect(&rc, thick, thick);

    HDC hdc = GetDC(NULL);
    SaveDC(hdc);

    PatBlt(hdc, rc.left, rc.top, thick, rc.bottom - rc.top, DSTINVERT);
    PatBlt(hdc, rc.right - thick, rc.top, thick, rc.bottom - rc.top, DSTINVERT);
    PatBlt(hdc, rc.left + thick, rc.top, (rc.right - thick) - (rc.left + thick), thick, DSTINVERT);
    PatBlt(hdc, rc.left + thick, rc.bottom - thick, (rc.right - thick) - (rc.left + thick), thick, DSTINVERT);

    RestoreDC(hdc, -1);
    ReleaseDC(NULL, hdc);
}

void Zoomin::PaintZoomRect(HDC hdc)
{
    RECT rc;
    GetZoomArea(rc);

    if (rc.right <= rc.left || rc.bottom <= rc.top)
        return;

    const HDC hdcTo = hdc ? hdc : GetDC(m_hwnd);
    const HDC hdcFrom = GetDC(NULL);
    const int bltmode = SetStretchBltMode(hdcTo, COLORONCOLOR);

    StretchBlt(hdcTo, 0, 0, m_factor * m_area.cx, m_factor * m_area.cy,
               hdcFrom, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SRCCOPY);

    SetStretchBltMode(hdcTo, bltmode);
    ReleaseDC(NULL, hdcFrom);
    if (!hdc)
        ReleaseDC(m_hwnd, hdcTo);
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

int PASCAL WinMain(
    HINSTANCE hinstCurrent,
    HINSTANCE /*hinstPrevious*/,
    LPSTR /*lpszCmdLine*/,
    int nCmdShow)
{
    MSG msg = { 0 };
    g_hinst = hinstCurrent;
    g_haccel = LoadAccelerators(g_hinst, MAKEINTRESOURCE(IDR_ACCEL));

    //CoInitialize(0);
    InitCommonControls();

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

        MSG tmp;
        do {} while(PeekMessage(&tmp, 0, WM_QUIT, WM_QUIT, PM_REMOVE));
    }

    return int(msg.wParam);
}
