// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include <windows.h>
#include <windowsx.h>
#include <assert.h>

#include "dpi.h"

#ifndef ILC_COLORMASK
#define ILC_COLORMASK   0x00FE
#endif

#ifndef BORDERX_PEN
#define BORDERX_PEN     32
#endif

WORD __GetHdcDpi(HDC hdc)
{
    const WORD dxLogPixels = static_cast<WORD>(GetDeviceCaps(hdc, LOGPIXELSX));
#ifdef DEBUG
    const WORD dyLogPixels = static_cast<WORD>(GetDeviceCaps(hdc, LOGPIXELSY));
    assert(dxLogPixels == dyLogPixels);
#endif
    return dxLogPixels;
}

class User32
{
public:
                            User32();

    WORD                    GetDpiForSystem();
    WORD                    GetDpiForWindow(HWND hwnd);
    int                     GetSystemMetricsForDpi(int nIndex, UINT dpi);
    bool                    SystemParametersInfoForDpi(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni, UINT dpi);
    bool                    IsValidDpiAwarenessContext(DPI_AWARENESS_CONTEXT context);
    bool                    AreDpiAwarenessContextsEqual(DPI_AWARENESS_CONTEXT contextA, DPI_AWARENESS_CONTEXT contextB);
    DPI_AWARENESS_CONTEXT   SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT context);
    DPI_AWARENESS_CONTEXT   GetWindowDpiAwarenessContext(HWND hwnd);
    bool                    EnableNonClientDpiScaling(HWND hwnd);
    bool                    EnablePerMonitorMenuScaling();

private:
    bool                    Initialize();

private:
    DWORD                   m_dwErr = 0;
    HMODULE                 m_hLib = 0;
    bool                    m_fInitialized = false;
    union
    {
        FARPROC	proc[10];
        struct
        {
            UINT (WINAPI* GetDpiForSystem)();
            UINT (WINAPI* GetDpiForWindow)(HWND hwnd);
            int (WINAPI* GetSystemMetricsForDpi)(int nIndex, UINT dpi);
            BOOL (WINAPI* SystemParametersInfoForDpi)(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni, UINT dpi);
            BOOL (WINAPI* IsValidDpiAwarenessContext)(DPI_AWARENESS_CONTEXT context);
            BOOL (WINAPI* AreDpiAwarenessContextsEqual)(DPI_AWARENESS_CONTEXT contextA, DPI_AWARENESS_CONTEXT contextB);
            DPI_AWARENESS_CONTEXT (WINAPI* SetThreadDpiAwarenessContext)(DPI_AWARENESS_CONTEXT context);
            DPI_AWARENESS_CONTEXT (WINAPI* GetWindowDpiAwarenessContext)(HWND hwnd);
            BOOL (WINAPI* EnableNonClientDpiScaling)(HWND hwnd);
            BOOL (WINAPI* EnablePerMonitorMenuScaling)();
        };
    } m_user32;
};

User32 g_user32;

User32::User32()
{
    ZeroMemory(&m_user32, sizeof(m_user32));

    Initialize();

    static_assert(_countof(m_user32.proc) == sizeof(m_user32) / sizeof(FARPROC), "mismatched FARPROC struct");
}

bool User32::Initialize()
{
    if (!m_fInitialized)
    {
        m_fInitialized = true;
        m_hLib = LoadLibrary(TEXT("user32.dll"));
        if (!m_hLib)
        {
            m_dwErr = GetLastError();
        }
        else
        {
            size_t cProcs = 0;

            if (!(m_user32.proc[cProcs++] = GetProcAddress(m_hLib, "GetDpiForSystem")))
                m_dwErr = GetLastError();
            if (!(m_user32.proc[cProcs++] = GetProcAddress(m_hLib, "GetDpiForWindow")))
                m_dwErr = GetLastError();
            if (!(m_user32.proc[cProcs++] = GetProcAddress(m_hLib, "GetSystemMetricsForDpi")))
                m_dwErr = GetLastError();
            if (!(m_user32.proc[cProcs++] = GetProcAddress(m_hLib, "SystemParametersInfoForDpi")))
                m_dwErr = GetLastError();
            if (!(m_user32.proc[cProcs++] = GetProcAddress(m_hLib, "IsValidDpiAwarenessContext")))
                m_dwErr = GetLastError();
            if (!(m_user32.proc[cProcs++] = GetProcAddress(m_hLib, "AreDpiAwarenessContextsEqual")))
                m_dwErr = GetLastError();
            if (!(m_user32.proc[cProcs++] = GetProcAddress(m_hLib, "SetThreadDpiAwarenessContext")))
                m_dwErr = GetLastError();
            if (!(m_user32.proc[cProcs++] = GetProcAddress(m_hLib, "GetWindowDpiAwarenessContext")))
                m_dwErr = GetLastError();
            if (!(m_user32.proc[cProcs++] = GetProcAddress(m_hLib, "EnableNonClientDpiScaling")))
                m_dwErr = GetLastError();
            m_user32.proc[cProcs++] = GetProcAddress(m_hLib, "EnablePerMonitorMenuScaling"); // Optional: not an error if it's missing.
            assert(_countof(m_user32.proc) == cProcs);
        }
    }

    return !m_dwErr;
}

WORD User32::GetDpiForSystem()
{
    if (m_user32.GetDpiForSystem)
        return m_user32.GetDpiForSystem();

    const HDC hdc = GetDC(0);
    const WORD dpi = __GetHdcDpi(hdc);
    ReleaseDC(0, hdc);
    return dpi;
}

WORD User32::GetDpiForWindow(HWND hwnd)
{
    if (m_user32.GetDpiForWindow)
        return m_user32.GetDpiForWindow(hwnd);

    const HDC hdc = GetDC(hwnd);
    const WORD dpi = __GetHdcDpi(hdc);
    ReleaseDC(hwnd, hdc);
    return dpi;
}

int User32::GetSystemMetricsForDpi(int nIndex, UINT dpi)
{
    if (m_user32.GetSystemMetricsForDpi)
    {
        // Scale these ourselves because the OS doesn't seem to return them scaled.  ?!
        if (nIndex == SM_CXFOCUSBORDER || nIndex == SM_CYFOCUSBORDER)
            return HIDPIMulDiv(GetSystemMetrics(nIndex), dpi, 96);

        return m_user32.GetSystemMetricsForDpi(nIndex, dpi);
    }

    return GetSystemMetrics(nIndex);
}

bool User32::SystemParametersInfoForDpi(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni, UINT _dpi)
{
    DpiScaler dpi(static_cast<WORD>(_dpi));
    DpiScaler dpiSystem(__GetDpiForSystem());

    switch (uiAction)
    {
    case SPI_GETICONTITLELOGFONT:
        if (SystemParametersInfo(uiAction, uiParam, pvParam, fWinIni))
        {
            const LPLOGFONT plf = LPLOGFONT(pvParam);
            plf->lfHeight = dpi.ScaleFrom(plf->lfHeight, dpiSystem);
            return true;
        }
        break;

    case SPI_GETICONMETRICS:
        if (SystemParametersInfo(uiAction, uiParam, pvParam, fWinIni))
        {
            const LPICONMETRICS pim = LPICONMETRICS(pvParam);
            pim->lfFont.lfHeight = dpi.ScaleFrom(pim->lfFont.lfHeight, dpiSystem);
            return true;
        }
        break;

    case SPI_GETNONCLIENTMETRICS:
        if (SystemParametersInfo(uiAction, uiParam, pvParam, fWinIni))
        {
            const LPNONCLIENTMETRICS pncm = LPNONCLIENTMETRICS(pvParam);
            pncm->lfCaptionFont.lfHeight = dpi.ScaleFrom(pncm->lfCaptionFont.lfHeight, dpiSystem);
            pncm->lfMenuFont.lfHeight = dpi.ScaleFrom(pncm->lfMenuFont.lfHeight, dpiSystem);
            pncm->lfMessageFont.lfHeight = dpi.ScaleFrom(pncm->lfMessageFont.lfHeight, dpiSystem);
            pncm->lfSmCaptionFont.lfHeight = dpi.ScaleFrom(pncm->lfSmCaptionFont.lfHeight, dpiSystem);
            pncm->lfStatusFont.lfHeight = dpi.ScaleFrom(pncm->lfStatusFont.lfHeight, dpiSystem);
            return true;
        }
        break;
    }

    return false;
}

bool User32::IsValidDpiAwarenessContext(DPI_AWARENESS_CONTEXT context)
{
    if (m_user32.IsValidDpiAwarenessContext)
        return m_user32.IsValidDpiAwarenessContext(context);

    return false;
}

bool User32::AreDpiAwarenessContextsEqual(DPI_AWARENESS_CONTEXT contextA, DPI_AWARENESS_CONTEXT contextB)
{
    if (m_user32.AreDpiAwarenessContextsEqual)
        return m_user32.AreDpiAwarenessContextsEqual(contextA, contextB);

    return (contextA == contextB);
}

DPI_AWARENESS_CONTEXT User32::SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT context)
{
    if (m_user32.SetThreadDpiAwarenessContext)
        return m_user32.SetThreadDpiAwarenessContext(context);

    return DPI_AWARENESS_CONTEXT_UNAWARE;
}

DPI_AWARENESS_CONTEXT User32::GetWindowDpiAwarenessContext(HWND hwnd)
{
    if (m_user32.GetWindowDpiAwarenessContext)
        return m_user32.GetWindowDpiAwarenessContext(hwnd);

    return DPI_AWARENESS_CONTEXT_UNAWARE;
}

bool User32::EnableNonClientDpiScaling(HWND hwnd)
{
    if (m_user32.EnableNonClientDpiScaling)
        return m_user32.EnableNonClientDpiScaling(hwnd);

    return true;
}

bool User32::EnablePerMonitorMenuScaling()
{
    if (m_user32.EnablePerMonitorMenuScaling)
        return m_user32.EnablePerMonitorMenuScaling();

    return false;
}

WORD __GetDpiForSystem()
{
    return WORD(g_user32.GetDpiForSystem());
}

WORD __GetDpiForWindow(HWND hwnd)
{
    return WORD(g_user32.GetDpiForWindow(hwnd));
}

int __GetSystemMetricsForDpi(int nIndex, UINT dpi)
{
    return g_user32.GetSystemMetricsForDpi(nIndex, dpi);
}

bool __SystemParametersInfoForDpi(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni, UINT dpi)
{
    return g_user32.SystemParametersInfoForDpi(uiAction, uiParam, pvParam, fWinIni, dpi);
}

bool __IsValidDpiAwarenessContext(DPI_AWARENESS_CONTEXT context)
{
    return g_user32.IsValidDpiAwarenessContext(context);
}

bool __AreDpiAwarenessContextsEqual(DPI_AWARENESS_CONTEXT contextA, DPI_AWARENESS_CONTEXT contextB)
{
    return g_user32.AreDpiAwarenessContextsEqual(contextA, contextB);
}

DPI_AWARENESS_CONTEXT __SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT context)
{
    return g_user32.SetThreadDpiAwarenessContext(context);
}

DPI_AWARENESS_CONTEXT __GetWindowDpiAwarenessContext(HWND hwnd)
{
    return g_user32.GetWindowDpiAwarenessContext(hwnd);
}

bool __EnableNonClientDpiScaling(HWND hwnd)
{
    return g_user32.EnableNonClientDpiScaling(hwnd);
}

bool __EnablePerMonitorMenuScaling()
{
    return g_user32.EnablePerMonitorMenuScaling();
}

bool __IsHwndPerMonitorAware(HWND hwnd)
{
    const DPI_AWARENESS_CONTEXT context = __GetWindowDpiAwarenessContext(hwnd);

    return (__AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE) ||
            __AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
}

ThreadDpiAwarenessContext::ThreadDpiAwarenessContext(const bool fUsePerMonitorAwareness)
{
    DPI_AWARENESS_CONTEXT const context = fUsePerMonitorAwareness ? DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE : DPI_AWARENESS_CONTEXT_SYSTEM_AWARE;

    m_fRestore = true;
    m_context = __SetThreadDpiAwarenessContext(context);
}

ThreadDpiAwarenessContext::ThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT context)
{
    if (context == DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 && !__IsValidDpiAwarenessContext(context))
        context = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE;

    m_fRestore = true;
    m_context = __SetThreadDpiAwarenessContext(context);
}

void ThreadDpiAwarenessContext::Restore()
{
    if (m_fRestore)
    {
        __SetThreadDpiAwarenessContext(m_context);
        m_fRestore = false;
    }
}

// HIDPISIGN and HIDPIABS ensure correct rounding for negative numbers passed
// into HIDPIMulDiv as x (round -1.5 to -1, 2.5 to round to 2, etc).  This is
// done by taking the absolute value of x and multiplying the result by the
// sign of x.  Y and z should never be negative, as y is the dpi of the
// device, and z is always 96 (100%).
inline int HIDPISIGN(int x)
{
    return (x < 0) ? -1 : 1;
}
inline int HIDPIABS(int x)
{
    return (x < 0) ? -x : x;
}

int HIDPIMulDiv(int x, int y, int z)
{
    assert(y);
    assert(z);
    // >>1 rounds up at 0.5, >>2 rounds up at 0.75, >>3 rounds up at 0.875
    //return (((HIDPIABS(x) * y) + (z >> 1)) / z) * HIDPISIGN(x);
    //return (((HIDPIABS(x) * y) + (z >> 2)) / z) * HIDPISIGN(x);
    return (((HIDPIABS(x) * y) + (z >> 3)) / z) * HIDPISIGN(x);
}

DpiScaler::DpiScaler()
{
    m_logPixels = 96;
}

DpiScaler::DpiScaler(WORD dpi)
{
    assert(dpi);
    m_logPixels = dpi;
}

DpiScaler::DpiScaler(WPARAM wParam)
{
    assert(wParam);
    assert(LOWORD(wParam));
    m_logPixels = LOWORD(wParam);
}

DpiScaler::DpiScaler(const DpiScaler& dpi)
{
    m_logPixels = dpi.m_logPixels;
}

DpiScaler::DpiScaler(DpiScaler&& dpi)
{
    m_logPixels = dpi.m_logPixels;
}

bool DpiScaler::IsDpiEqual(UINT dpi) const
{
    assert(dpi);
    return dpi == m_logPixels;
}

bool DpiScaler::IsDpiEqual(const DpiScaler& dpi) const
{
    return (dpi.m_logPixels == m_logPixels);
}

DpiScaler& DpiScaler::operator=(WORD dpi)
{
    assert(dpi);
    m_logPixels = dpi;
    return *this;
}

DpiScaler& DpiScaler::operator=(const DpiScaler& dpi)
{
    m_logPixels = dpi.m_logPixels;
    return *this;
}

DpiScaler& DpiScaler::operator=(DpiScaler&& dpi)
{
    m_logPixels = dpi.m_logPixels;
    return *this;
}

void DpiScaler::OnDpiChanged(const DpiScaler& dpi)
{
    m_logPixels = dpi.m_logPixels;
}

int DpiScaler::Scale(int n) const
{
    return HIDPIMulDiv(n, m_logPixels, 96);
}

float DpiScaler::ScaleF(float n) const
{
    return n * float(m_logPixels) / 96.0f;
}

int DpiScaler::ScaleTo(int n, DWORD dpi) const
{
    assert(dpi);
    return HIDPIMulDiv(n, dpi, m_logPixels);
}

int DpiScaler::ScaleTo(int n, const DpiScaler& dpi) const
{
    return HIDPIMulDiv(n, dpi.m_logPixels, m_logPixels);
}

int DpiScaler::ScaleFrom(int n, DWORD dpi) const
{
    assert(dpi);
    return HIDPIMulDiv(n, m_logPixels, dpi);
}

int DpiScaler::ScaleFrom(int n, const DpiScaler& dpi ) const
{
    return HIDPIMulDiv(n, m_logPixels, dpi.m_logPixels);
}

int DpiScaler::PointSizeToHeight(int nPointSize) const
{
    assert(nPointSize >= 1);
    return -MulDiv(nPointSize, m_logPixels, 72);
}

int DpiScaler::PointSizeToHeight(float pointSize) const
{
    assert(pointSize >= 1);
    return -MulDiv(int(pointSize * 10), m_logPixels, 720);
}

int DpiScaler::GetSystemMetrics(int nIndex) const
{
    return __GetSystemMetricsForDpi(nIndex, m_logPixels);
}

bool DpiScaler::SystemParametersInfo(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni) const
{
    return __SystemParametersInfoForDpi(uiAction, uiParam, pvParam, fWinIni, m_logPixels);
}

WPARAM DpiScaler::MakeWParam() const
{
    return MAKELONG(m_logPixels, m_logPixels);
}

int __MessageBox(__in_opt HWND hWnd, __in_opt LPCTSTR lpText, __in_opt LPCTSTR lpCaption, __in UINT uType)
{
    ThreadDpiAwarenessContext dpiContext(DPI_AWARENESS_CONTEXT_SYSTEM_AWARE);
    return MessageBox(hWnd, lpText, lpCaption, uType);
}

