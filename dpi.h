// Copyright (c) 2023 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#ifndef WM_DPICHANGED
#define WM_DPICHANGED           0x02E0
#endif

#define WMU_DPICHANGED          (WM_USER + 9997)    // Specialized internal use.
#define WMU_REFRESHDPI          (WM_USER + 9998)    // Specialized internal use.

#ifndef DPI_AWARENESS_CONTEXT_UNAWARE
DECLARE_HANDLE(DPI_AWARENESS_CONTEXT);
#define DPI_AWARENESS_CONTEXT_UNAWARE               (DPI_AWARENESS_CONTEXT(-1))
#define DPI_AWARENESS_CONTEXT_SYSTEM_AWARE          (DPI_AWARENESS_CONTEXT(-2))
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE     (DPI_AWARENESS_CONTEXT(-3))
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2  (DPI_AWARENESS_CONTEXT(-4))
#endif // !DPI_AWARENESS_CONTEXT_UNAWARE

int HIDPIMulDiv(int x, int y, int z);

WORD __GetHdcDpi(HDC hdc);
WORD __GetDpiForSystem();
WORD __GetDpiForWindow(HWND hwnd);
WORD __GetDpiForMonitor(HMONITOR hmon);
bool __IsHwndPerMonitorAware(HWND hwnd);

int __GetSystemMetricsForDpi(int nIndex, UINT dpi);
bool __SystemParametersInfoForDpi(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni, UINT dpi);

bool __EnableNonClientDpiScaling(HWND hwnd);
bool __EnablePerMonitorMenuScaling();

int __MessageBox(__in_opt HWND hWnd, __in_opt LPCTSTR lpText, __in_opt LPCTSTR lpCaption, __in UINT uType);

class ThreadDpiAwarenessContext
{
public:
    ThreadDpiAwarenessContext(bool fUsePerMonitorAwareness);
    ThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT context);
    ~ThreadDpiAwarenessContext() { Restore(); }

    void                Restore();

private:
    DPI_AWARENESS_CONTEXT m_context;
    bool                m_fRestore;
};

class DpiScaler
{
public:
                DpiScaler();
    explicit    DpiScaler(WORD dpi);
    explicit    DpiScaler(WPARAM wParam);
    explicit    DpiScaler(const DpiScaler& dpi);
    explicit    DpiScaler(DpiScaler&& dpi);

    bool        IsDpiEqual(UINT dpi) const;
    bool        IsDpiEqual(const DpiScaler& dpi) const;
    bool        operator==(UINT dpi) const { return IsDpiEqual( dpi ); }
    bool        operator==(const DpiScaler& dpi ) const { return IsDpiEqual( dpi ); }

    DpiScaler&  operator=(WORD dpi);
    DpiScaler&  operator=(const DpiScaler& dpi);
    DpiScaler&  operator=(DpiScaler&& dpi);
    void        OnDpiChanged(const DpiScaler& dpi);

    int         Scale(int n) const;
    float       ScaleF(float n) const;

    int         ScaleTo(int n, DWORD dpi) const;
    int         ScaleTo(int n, const DpiScaler& dpi) const;
    int         ScaleFrom(int n, DWORD dpi) const;
    int         ScaleFrom(int n, const DpiScaler& dpi) const;

    int         PointSizeToHeight(int nPointSize) const;
    int         PointSizeToHeight(float pointSize) const;

    int         GetSystemMetrics(int nIndex) const;
    bool        SystemParametersInfo(UINT uiAction, UINT uiParam, PVOID pvParam, UINT fWinIni) const;

    WPARAM      MakeWParam() const;

private:
    WORD        m_logPixels;
};

