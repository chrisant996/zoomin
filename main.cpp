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

static HINSTANCE g_hinst = 0;

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
    DpiScaler m_dpi;
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
        {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            SaveDC(ps.hdc);

// TODO: paint zoomed content.

            RestoreDC(ps.hdc, -1);
            EndPaint(hwnd, &ps);
        }
        break;

// TODO: mouse ldown/move/lup to position zoom region.

    case WM_COMMAND:
        {
            const WORD id = GET_WM_COMMAND_ID(wParam, lParam);
            const HWND hwndCtrl = GET_WM_COMMAND_HWND(wParam, lParam);
            const WORD code = GET_WM_COMMAND_CMD(wParam, lParam);
            switch (id)
            {
            case IDM_EDIT_COPY:
// TODO: copy zoomed content to clipboard.
                break;
            case IDM_EDIT_REFRESH:
// TODO: refresh zoomed content.
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

            case IDM_MOVE_UP:
            case IDM_MOVE_DOWN:
            case IDM_MOVE_LEFT:
            case IDM_MOVE_RIGHT:
// TODO: move zoom region.
                break;

            default:
                goto LDefault;
            }
        }
        break;

    case WM_DPICHANGED:
        s_zoomin.m_dpi.OnDpiChanged(DpiScaler(wParam));
        InvalidateRect(hwnd, nullptr, false);
        break;

    case WM_CREATE:
        SendMessage(hwnd, WM_SETICON, true, LPARAM(LoadImage(g_hinst, MAKEINTRESOURCE(IDI_MAIN), IMAGE_ICON, 0, 0, 0)));
        SendMessage(hwnd, WM_SETICON, false, LPARAM(LoadImage(g_hinst, MAKEINTRESOURCE(IDI_MAIN), IMAGE_ICON, 16, 16, 0)));
        s_zoomin.m_sizeTracker.OnCreate(hwnd);
        goto LDefault;

    case WM_WINDOWPOSCHANGED:
        s_zoomin.m_sizeTracker.OnSize();
        goto LDefault;

    case WM_DESTROY:
        s_zoomin.m_sizeTracker.OnDestroy();
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

static HWND CreateMainWindow()
{
    WNDCLASS wc = {};
    wc.lpszClassName = c_wndclass_name;
    wc.lpszMenuName = MAKEINTRESOURCE(IDR_MENU);
    wc.hIcon = LoadIcon(g_hinst, MAKEINTRESOURCE(IDI_MAIN));
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = HBRUSH(COLOR_WINDOW + 1);
    wc.hInstance = g_hinst;
    wc.lpfnWndProc = Zoomin::WndProc;
    RegisterClass(&wc);

    const DWORD c_style = WS_OVERLAPPEDWINDOW;
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

    //CoInitialize(0);
    InitCommonControls();

    HWND hwnd = CreateMainWindow();

    if (hwnd)
    {
        ShowWindow(hwnd, nCmdShow);

        while (true)
        {
            if (!GetMessage(&msg, nullptr, 0, 0))
                    break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        MSG tmp;
        do {} while(PeekMessage(&tmp, 0, WM_QUIT, WM_QUIT, PM_REMOVE));
    }

    return int(msg.wParam);
}
