// Based on the Mouse Pointer Crosshairs PowerToy code at:
// https://github.com/microsoft/PowerToys/blob/main/src/modules/MouseUtils/MousePointerCrosshairs/InclusiveCrosshairs.cpp
// License: http://opensource.org/licenses/MIT

//#define COMPOSITION

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <versionhelpers.h>

#ifdef COMPOSITION
#include <windows.ui.composition.interop.h>
#include <DispatcherQueue.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#endif

#include "reticle.h"
#include "dpi.h"
#include "assert.h"
#include "res.h"

#ifdef COMPOSITION
namespace winrt
{
    using namespace winrt::Windows::System;
    using namespace winrt::Windows::UI::Composition;
}

namespace ABI
{
    using namespace ABI::Windows::System;
    using namespace ABI::Windows::UI::Composition::Desktop;
}
#endif

struct ZoomReticleImpl : public ZoomReticle
{
    enum ZoomReticleMode
    {
        ZRM_XOR,
        ZRM_FOURWINDOWS,
#ifdef COMPOSITION
        ZRM_COMPOSITOR,
#endif
    };

public:
    ZoomReticleImpl(HINSTANCE hinst, LONG cx, LONG cy, const ZoomReticleSettings& settings);
    ~ZoomReticleImpl() override;
    bool InitReticle() override;
    void UpdateReticlePosition(const POINT& ptScreen) override;
    void Invoke(const std::function<void()>& func) override;

private:
    void GetReticleRect(RECT& rc) const;
    void InvertReticle();
#ifdef COMPOSITION
    static LRESULT CALLBACK WndProcComposition(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept;
#endif
    static LRESULT CALLBACK WndProcEdge(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept;

    ZoomReticleSettings m_settings;

    HINSTANCE m_hinst = NULL;
    POINT m_pt = {};
    LONG m_cx = 0;
    LONG m_cy = 0;
    ZoomReticleMode m_mode = ZRM_XOR;

    // ZRM_XOR.
    bool m_visible = false;
    LONG m_thick = 1;

    // ZRM_FOURWINDOWS.
    HWND m_hwndLeft = NULL;
    HWND m_hwndTop = NULL;
    HWND m_hwndRight = NULL;
    HWND m_hwndBottom = NULL;
    WORD m_monitorDpi = 0;

#ifdef COMPOSITION
    // ZRM_COMPOSITOR.
    HWND m_hwndOwner = NULL;
    HWND m_hwnd = NULL;
    winrt::DispatcherQueueController m_dispatcherQueueController{ nullptr };
    winrt::Compositor m_compositor{ nullptr };
    winrt::Desktop::DesktopWindowTarget m_target{ nullptr };
    winrt::ContainerVisual m_root{ nullptr };
    winrt::LayerVisual m_reticle_border_layer{ nullptr };
    winrt::LayerVisual m_reticle_layer{ nullptr };
    winrt::SpriteVisual m_left_reticle_border{ nullptr };
    winrt::SpriteVisual m_left_reticle{ nullptr };
    winrt::SpriteVisual m_top_reticle_border{ nullptr };
    winrt::SpriteVisual m_top_reticle{ nullptr };
    winrt::SpriteVisual m_right_reticle_border{ nullptr };
    winrt::SpriteVisual m_right_reticle{ nullptr };
    winrt::SpriteVisual m_bottom_reticle_border{ nullptr };
    winrt::SpriteVisual m_bottom_reticle{ nullptr };
#endif

    static ZoomReticleImpl* s_instance;
};

#ifdef COMPOSITION
static constexpr WCHAR c_className[] = L"ZoominReticleWindow";
#endif
static constexpr WCHAR c_classNameEdge[] = L"ZoominReticleWindowEdge";
static constexpr WCHAR c_windowTitle[] = L"Zoomin Reticle";

ZoomReticleImpl* ZoomReticleImpl::s_instance = nullptr;

#ifdef COMPOSITION
union CM_CDQC
{
    FARPROC proc;
    HRESULT (WINAPI* CreateDispatcherQueueController)(DispatcherQueueOptions options, PDISPATCHERQUEUECONTROLLER* dispatcherQueueController);
};

static const HMODULE s_hlib = LoadLibrary(L"CoreMessaging.dll");
static const CM_CDQC s_proc = { s_hlib ? GetProcAddress(s_hlib, "CreateDispatcherQueueController") : nullptr };
#endif

static bool EnsureWindowClass(HINSTANCE hinst, const WCHAR* name, WNDPROC wndproc)
{
    WNDCLASS wc = {};

    if (!GetClassInfoW(hinst, name, &wc))
    {
        wc.lpfnWndProc = wndproc;
        wc.hInstance = hinst;
        wc.hIcon = LoadIcon(hinst, MAKEINTRESOURCE(IDI_MAIN));
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
        wc.lpszClassName = name;
        if (!RegisterClassW(&wc))
            return false;
    }

    return true;
}

ZoomReticleImpl::ZoomReticleImpl(HINSTANCE hinst, LONG cx, LONG cy, const ZoomReticleSettings& settings)
{
    assert(!s_instance);
    s_instance = this;

    m_hinst = hinst;
    m_settings = settings;
    m_cx = cx;
    m_cy = cy;

    // First assume XOR, since it works on all OS versions.
    m_mode = ZRM_XOR;

#ifdef COMPOSITION
    // Next check if composition can be used.
    if (s_proc.proc)
    {
// TODO:  PROBLEMS...
//  1.  OnMouseMove cannot wait for render to finish before PaintZoomRect.
//  2.  There is a slight visible darkening of the area under the bounding
//      box of the SpriteVisual.
        if (EnsureWindowClass(hinst, c_className, WndProcComposition))
        {
            m_hwndOwner = CreateWindowW(L"static", nullptr, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, hinst, nullptr);
            if (m_hwndOwner)
            {
                constexpr DWORD exStyle = WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW;
                const HWND hwnd = CreateWindowExW(exStyle, c_className, c_windowTitle, WS_POPUP, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, m_hwndOwner, nullptr, m_hinst, nullptr);
                assert(hwnd == m_hwnd);
            }

            if (m_hwnd)
            {
                m_mode = ZRM_COMPOSITOR;
                return;
            }

            if (hwnd)
                DestroyWindow(hwnd);
            if (m_hwndOwner)
                DestroyWindow(m_hwndOwner);
            m_hwnd = NULL;
            m_hwndOwner = NULL;
        }
    }
#endif

    // Finally, check if four layered windows can be used.
    if (IsWindows8OrGreater())
    {
        if (EnsureWindowClass(hinst, c_classNameEdge, WndProcEdge))
        {
            constexpr DWORD exStyle = WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
            m_hwndLeft = CreateWindowExW(exStyle, c_classNameEdge, c_windowTitle, WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, hinst, nullptr);
            m_hwndTop = CreateWindowExW(exStyle, c_classNameEdge, c_windowTitle, WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, hinst, nullptr);
            m_hwndRight = CreateWindowExW(exStyle, c_classNameEdge, c_windowTitle, WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, hinst, nullptr);
            m_hwndBottom = CreateWindowExW(exStyle, c_classNameEdge, c_windowTitle, WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, hinst, nullptr);
            if (m_hwndLeft && m_hwndTop && m_hwndRight && m_hwndBottom)
            {
                m_mode = ZRM_FOURWINDOWS;
                return;
            }
            else
            {
                if (m_hwndLeft)
                    DestroyWindow(m_hwndLeft);
                if (m_hwndTop)
                    DestroyWindow(m_hwndTop);
                if (m_hwndRight)
                    DestroyWindow(m_hwndRight);
                if (m_hwndBottom)
                    DestroyWindow(m_hwndBottom);
                m_hwndLeft = NULL;
                m_hwndTop = NULL;
                m_hwndRight = NULL;
                m_hwndBottom = NULL;
            }
        }
    }
}

ZoomReticleImpl::~ZoomReticleImpl()
{
    if (m_visible && m_mode == ZRM_XOR)
        InvertReticle();

    if (m_hwndLeft)
        DestroyWindow(m_hwndLeft);
    if (m_hwndTop)
        DestroyWindow(m_hwndTop);
    if (m_hwndRight)
        DestroyWindow(m_hwndRight);
    if (m_hwndBottom)
        DestroyWindow(m_hwndBottom);

#ifdef COMPOSITION
    if (m_hwnd)
        DestroyWindow(m_hwnd);
    if (m_hwndOwner)
        DestroyWindow(m_hwndOwner);
#endif

    s_instance = nullptr;
}

#ifdef COMPOSITION
static winrt::Windows::UI::Color ToUiColor(COLORREF cr)
{
    return winrt::Windows::UI::ColorHelpers::FromArgb(255, GetRValue(cr), GetGValue(cr), GetBValue(cr));
}
#endif

bool ZoomReticleImpl::InitReticle()
{
    switch (m_mode)
    {
    case ZRM_XOR:
        break;

    case ZRM_FOURWINDOWS:
        {
            const BYTE alpha = clamp<BYTE>(255 * m_settings.m_opacity / 100, 0, 255);
            SetLayeredWindowAttributes(m_hwndLeft, 0, alpha, LWA_ALPHA);
            SetLayeredWindowAttributes(m_hwndTop, 0, alpha, LWA_ALPHA);
            SetLayeredWindowAttributes(m_hwndRight, 0, alpha, LWA_ALPHA);
            SetLayeredWindowAttributes(m_hwndBottom, 0, alpha, LWA_ALPHA);
        }
        break;

#ifdef COMPOSITION
    case ZRM_COMPOSITOR:
        {
            try
            {
                // We need a dispatcher queue.
                DispatcherQueueOptions options = {
                    sizeof(options),
                    DQTYPE_THREAD_CURRENT,
                    DQTAT_COM_ASTA,
                };
                ABI::IDispatcherQueueController* controller;
                assert(s_proc.CreateDispatcherQueueController);
                winrt::check_hresult(s_proc.CreateDispatcherQueueController(options, &controller));
                *winrt::put_abi(m_dispatcherQueueController) = controller;

                // Create the compositor for our window.
                m_compositor = winrt::Compositor();
                ABI::IDesktopWindowTarget* target;
                winrt::check_hresult(m_compositor.as<ABI::ICompositorDesktopInterop>()->CreateDesktopWindowTarget(m_hwnd, false, &target));
                *winrt::put_abi(m_target) = target;

                // Our composition tree:
                //
                // [root] ContainerVisual
                // \ [reticle border layer] LayerVisual
                //   \ [reticle border sprites]
                //     [reticle layer] LayerVisual
                //     \ [reticle sprites]

                m_root = m_compositor.CreateContainerVisual();
                m_root.RelativeSizeAdjustment({ 1.0f, 1.0f });
                m_target.Root(m_root);

                m_root.Opacity(clamp<float>(float(m_settings.m_opacity) / 100.0f, 0.0f, 1.0f));

                m_reticle_border_layer = m_compositor.CreateLayerVisual();
                m_reticle_border_layer.RelativeSizeAdjustment({ 1.0f, 1.0f });
                m_root.Children().InsertAtTop(m_reticle_border_layer);
                m_reticle_border_layer.Opacity(1.0f);

                m_reticle_layer = m_compositor.CreateLayerVisual();
                m_reticle_layer.RelativeSizeAdjustment({ 1.0f, 1.0f });

                // Create the reticle sprites.
                m_left_reticle_border = m_compositor.CreateSpriteVisual();
                m_left_reticle_border.AnchorPoint({ 0.0f, 0.0f });
                m_left_reticle_border.Brush(m_compositor.CreateColorBrush(ToUiColor(m_settings.m_borderColor)));
                m_reticle_border_layer.Children().InsertAtTop(m_left_reticle_border);
                m_left_reticle = m_compositor.CreateSpriteVisual();
                m_left_reticle.AnchorPoint({ 0.0f, 0.0f });
                m_left_reticle.Brush(m_compositor.CreateColorBrush(ToUiColor(m_settings.m_mainColor)));
                m_reticle_layer.Children().InsertAtTop(m_left_reticle);

                m_top_reticle_border = m_compositor.CreateSpriteVisual();
                m_top_reticle_border.AnchorPoint({ 0.0f, 0.0f });
                m_top_reticle_border.Brush(m_compositor.CreateColorBrush(ToUiColor(m_settings.m_borderColor)));
                m_reticle_border_layer.Children().InsertAtTop(m_top_reticle_border);
                m_top_reticle = m_compositor.CreateSpriteVisual();
                m_top_reticle.AnchorPoint({ 0.0f, 0.0f });
                m_top_reticle.Brush(m_compositor.CreateColorBrush(ToUiColor(m_settings.m_mainColor)));
                m_reticle_layer.Children().InsertAtTop(m_top_reticle);

                m_right_reticle_border = m_compositor.CreateSpriteVisual();
                m_right_reticle_border.AnchorPoint({ 0.0f, 0.0f });
                m_right_reticle_border.Brush(m_compositor.CreateColorBrush(ToUiColor(m_settings.m_borderColor)));
                m_reticle_border_layer.Children().InsertAtTop(m_right_reticle_border);
                m_right_reticle = m_compositor.CreateSpriteVisual();
                m_right_reticle.AnchorPoint({ 0.0f, 0.0f });
                m_right_reticle.Brush(m_compositor.CreateColorBrush(ToUiColor(m_settings.m_mainColor)));
                m_reticle_layer.Children().InsertAtTop(m_right_reticle);

                m_bottom_reticle_border = m_compositor.CreateSpriteVisual();
                m_bottom_reticle_border.AnchorPoint({ 0.0f, 0.0f });
                m_bottom_reticle_border.Brush(m_compositor.CreateColorBrush(ToUiColor(m_settings.m_borderColor)));
                m_reticle_border_layer.Children().InsertAtTop(m_bottom_reticle_border);
                m_bottom_reticle = m_compositor.CreateSpriteVisual();
                m_bottom_reticle.AnchorPoint({ 0.0f, 0.0f });
                m_bottom_reticle.Brush(m_compositor.CreateColorBrush(ToUiColor(m_settings.m_mainColor)));
                m_reticle_layer.Children().InsertAtTop(m_bottom_reticle);

                m_reticle_border_layer.Children().InsertAtTop(m_reticle_layer);
                m_reticle_layer.Opacity(1.0f);
            }
            catch (...)
            {
                return false;
            }
        }
        break;
#endif
    }

    return true;
}

static bool SetOrDeferWindowPos(HDWP& hdwp, HWND hwnd, HWND hwndInsertAfter, int x, int y, int cx, int cy, UINT flags)
{
    if (hdwp)
    {
        hdwp = DeferWindowPos(hdwp, hwnd, hwndInsertAfter, x, y, cx, cy, flags);
        return !!hdwp;
    }
    SetWindowPos(hwnd, hwndInsertAfter, x, y, cx, cy, flags);
    return true;
}

void ZoomReticleImpl::UpdateReticlePosition(const POINT& ptScreen)
{
    if (ptScreen.x == m_pt.x && ptScreen.y == m_pt.y)
    {
        // Don't reposition the reticle to where it already is; that can
        // starve painting.
        return;
    }

    switch (m_mode)
    {
    case ZRM_XOR:
        {
            if (m_visible)
                InvertReticle();

            m_pt = ptScreen;

            const HMONITOR cursorMonitor = MonitorFromPoint(m_pt, MONITOR_DEFAULTTONEAREST);
            if (cursorMonitor)
            {
                const DpiScaler dpi(__GetDpiForMonitor(cursorMonitor));
                m_thick = dpi.Scale(1);
            }

            InvertReticle();
        }
        break;

    case ZRM_FOURWINDOWS:
        {
            m_pt = ptScreen;

            m_monitorDpi = 96;
            const HMONITOR cursorMonitor = MonitorFromPoint(m_pt, MONITOR_DEFAULTTONEAREST);
            if (cursorMonitor)
                m_monitorDpi = __GetDpiForMonitor(cursorMonitor);

            DpiScaler dpi(m_monitorDpi);
            const LONG border_thickness = dpi.Scale(m_settings.m_borderThickness);
            const LONG main_thickness = dpi.Scale(m_settings.m_mainThickness);
            const LONG thick = border_thickness + main_thickness + border_thickness;

            RECT rc;
            GetReticleRect(rc);

            constexpr DWORD c_flags = SWP_NOACTIVATE;
            HDWP hdwp = BeginDeferWindowPos(4);
            if (SetOrDeferWindowPos(hdwp, m_hwndLeft, HWND_TOPMOST, rc.left - thick, rc.top, thick, m_cy, c_flags) &&
                SetOrDeferWindowPos(hdwp, m_hwndTop, HWND_TOPMOST, rc.left - thick, rc.top - thick, thick + m_cx + thick, thick, c_flags) &&
                SetOrDeferWindowPos(hdwp, m_hwndRight, HWND_TOPMOST, rc.right, rc.top, thick, m_cy, c_flags) &&
                SetOrDeferWindowPos(hdwp, m_hwndBottom, HWND_TOPMOST, rc.left - thick, rc.bottom, thick + m_cx + thick, thick, c_flags))
            {
                if (hdwp)
                    EndDeferWindowPos(hdwp);

                if (!m_visible)
                {
                    ShowWindow(m_hwndLeft, SW_SHOWNOACTIVATE);
                    ShowWindow(m_hwndTop, SW_SHOWNOACTIVATE);
                    ShowWindow(m_hwndRight, SW_SHOWNOACTIVATE);
                    ShowWindow(m_hwndBottom, SW_SHOWNOACTIVATE);
                    UpdateWindow(m_hwndLeft);
                    UpdateWindow(m_hwndTop);
                    UpdateWindow(m_hwndRight);
                    UpdateWindow(m_hwndBottom);
                    m_visible = true;
                }
            }
        }
        break;

#ifdef COMPOSITION
    case ZRM_COMPOSITOR:
        {
            if (!m_hwnd || !m_dispatcherQueueController)
                return;

            if (!m_visible)
            {
                ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
                m_visible = true;
            }

            // HACK: Draw with 1 pixel off. Otherwise Windows glitches the task bar transparency when a transparent window fill the whole screen.
            SetWindowPos(m_hwnd, HWND_TOPMOST, GetSystemMetrics(SM_XVIRTUALSCREEN) + 1, GetSystemMetrics(SM_YVIRTUALSCREEN) + 1, GetSystemMetrics(SM_CXVIRTUALSCREEN) - 2, GetSystemMetrics(SM_CYVIRTUALSCREEN) - 2, 0);

            const HMONITOR cursorMonitor = MonitorFromPoint(m_pt, MONITOR_DEFAULTTONEAREST);
            if (cursorMonitor == NULL)
                return;

            MONITORINFO monitorInfo;
            monitorInfo.cbSize = sizeof(monitorInfo);
            if (!GetMonitorInfo(cursorMonitor, &monitorInfo))
                return;

            const DpiScaler dpi(__GetDpiForMonitor(cursorMonitor));

            POINT ptMonitorUpperLeft;
            ptMonitorUpperLeft.x = monitorInfo.rcMonitor.left;
            ptMonitorUpperLeft.y = monitorInfo.rcMonitor.top;

            POINT ptMonitorBottomRight;
            ptMonitorBottomRight.x = monitorInfo.rcMonitor.right;
            ptMonitorBottomRight.y = monitorInfo.rcMonitor.bottom;

            // Convert everything to client coordinates.
            POINT ptClient = ptScreen;
            m_pt = ptScreen;
            ScreenToClient(m_hwnd, &ptClient);
            ScreenToClient(m_hwnd, &ptMonitorUpperLeft);
            ScreenToClient(m_hwnd, &ptMonitorBottomRight);

            const float border_thickness = float(m_settings.m_borderThickness);
            const float main_thickness = float(dpi.Scale(m_settings.m_mainThickness));
            const float inner_thickness = border_thickness + main_thickness;
            const float full_thickness = border_thickness + main_thickness + border_thickness;

            const float leftHalf = float(m_cx / 2);
            const float rightHalf = float(m_cx - m_cx / 2);
            const float topHalf = float(m_cy / 2);
            const float bottomHalf = float(m_cy - m_cy / 2);

            {
                const float vertLength = inner_thickness + m_cy + inner_thickness;
                const float vertBorderLength = full_thickness + m_cy + full_thickness;
                m_left_reticle_border.Offset({ float(ptClient.x - leftHalf - full_thickness), float(ptClient.y - topHalf - full_thickness), 0.0f });
                m_left_reticle_border.Size({ full_thickness, vertBorderLength });
                m_left_reticle.Offset({ float(ptClient.x - leftHalf - inner_thickness), float(ptClient.y - topHalf - inner_thickness), 0.0f });
                m_left_reticle.Size({ main_thickness, vertLength });
                m_right_reticle_border.Offset({ float(ptClient.x + rightHalf), float(ptClient.y - topHalf - full_thickness), 0.0f });
                m_right_reticle_border.Size({ full_thickness, vertBorderLength });
                m_right_reticle.Offset({ float(ptClient.x + rightHalf + border_thickness), float(ptClient.y - topHalf - inner_thickness), 0.0f });
                m_right_reticle.Size({ main_thickness, vertLength });
            }

            {
                const float horzLength = inner_thickness + m_cx + inner_thickness;
                const float horzBorderLength = full_thickness + m_cx + full_thickness;
                m_top_reticle_border.Offset({ float(ptClient.x - leftHalf - full_thickness), float(ptClient.y - topHalf - full_thickness), 0.0f });
                m_top_reticle_border.Size({ horzBorderLength, full_thickness });
                m_top_reticle.Offset({ float(ptClient.x - leftHalf - inner_thickness), float(ptClient.y - topHalf - inner_thickness), 0.0f });
                m_top_reticle.Size({ horzLength, main_thickness });
                m_bottom_reticle_border.Offset({ float(ptClient.x - leftHalf - full_thickness), float(ptClient.y + bottomHalf), 0.0f });
                m_bottom_reticle_border.Size({ horzBorderLength, full_thickness });
                m_bottom_reticle.Offset({ float(ptClient.x - leftHalf - inner_thickness), float(ptClient.y + bottomHalf + border_thickness), 0.0f });
                m_bottom_reticle.Size({ horzLength, main_thickness });
            }
        }
        break;
#endif
    }
}

void ZoomReticleImpl::Invoke(const std::function<void()>& func)
{
#ifdef COMPOSITION
    if (m_mode == ZRM_COMPOSITOR)
    {
        assert(m_dispatcherQueueController);
// TODO: PROBLEM -- This can end up running before composition is complete,
// and then PaintZoomRect captures the reticle itself.
        const winrt::Windows::System::DispatcherQueuePriority prio = winrt::Windows::System::DispatcherQueuePriority::Normal;
        m_dispatcherQueueController.DispatcherQueue().TryEnqueue(prio, func);
    }
    else
#endif
    {
        func();
    }
}

void ZoomReticleImpl::GetReticleRect(RECT& rc) const
{
    rc.left = m_pt.x - (m_cx / 2);
    rc.top = m_pt.y - (m_cy / 2);
    rc.right = rc.left + m_cx;
    rc.bottom = rc.top + m_cy;
}

void ZoomReticleImpl::InvertReticle()
{
    assert(m_mode == ZRM_XOR);

    RECT rc;
    GetReticleRect(rc);

    const LONG thick = m_thick;
    InflateRect(&rc, thick, thick);

    HDC hdc = GetDC(NULL);
    SaveDC(hdc);

    PatBlt(hdc, rc.left, rc.top, thick, rc.bottom - rc.top, DSTINVERT);
    PatBlt(hdc, rc.right - thick, rc.top, thick, rc.bottom - rc.top, DSTINVERT);
    PatBlt(hdc, rc.left + thick, rc.top, (rc.right - thick) - (rc.left + thick), thick, DSTINVERT);
    PatBlt(hdc, rc.left + thick, rc.bottom - thick, (rc.right - thick) - (rc.left + thick), thick, DSTINVERT);
    m_visible = !m_visible;

    RestoreDC(hdc, -1);
    ReleaseDC(NULL, hdc);
}

#ifdef COMPOSITION
LRESULT CALLBACK ZoomReticleImpl::WndProcComposition(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    switch (message)
    {
    case WM_NCCREATE:
        s_instance->m_hwnd = hwnd;
        return DefWindowProc(hwnd, message, wParam, lParam);
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_NCDESTROY:
        s_instance->m_hwnd = NULL;
        break;
    default:
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
}
#endif

LRESULT CALLBACK ZoomReticleImpl::WndProcEdge(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
    switch (message)
    {
    case WM_ERASEBKGND:
        return true;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);

            const COLORREF old_bk = GetBkColor(ps.hdc);

            RECT rc;
            GetClientRect(hwnd, &rc);
            const DpiScaler dpi(s_instance->m_monitorDpi);

            const auto border_thickness = dpi.Scale(s_instance->m_settings.m_borderThickness);
            const auto main_thickness = dpi.Scale(s_instance->m_settings.m_mainThickness);

            SetBkColor(ps.hdc, s_instance->m_settings.m_borderColor);
            ExtTextOut(ps.hdc, 0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);

            SetBkColor(ps.hdc, s_instance->m_settings.m_mainColor);
            rc.left += border_thickness;
            rc.right -= border_thickness;
            if (hwnd == s_instance->m_hwndLeft || hwnd == s_instance->m_hwndRight)
            {
                ExtTextOut(ps.hdc, 0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);
            }
            else
            {
                rc.top += border_thickness;
                rc.bottom -= border_thickness;
                ExtTextOut(ps.hdc, 0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);

                RECT rcL = rc;
                if (hwnd == s_instance->m_hwndTop)
                {
                    rcL.top = rcL.bottom;
                    rcL.bottom += border_thickness;
                }
                else
                {
                    rcL.bottom = rcL.top;
                    rcL.top -= border_thickness;
                }

                rcL.right = rc.left + main_thickness;
                ExtTextOut(ps.hdc, 0, 0, ETO_OPAQUE, &rcL, nullptr, 0, nullptr);
                rcL.right = rc.right;
                rcL.left = rc.right - main_thickness;
                ExtTextOut(ps.hdc, 0, 0, ETO_OPAQUE, &rcL, nullptr, 0, nullptr);
            }

            SetBkColor(ps.hdc, old_bk);
            EndPaint(hwnd, &ps);
        }
        break;
    default:
        return DefWindowProc(hwnd, message, wParam, lParam);
    }
    return 0;
}

std::unique_ptr<ZoomReticle> CreateZoomReticle(HINSTANCE hinst, LONG cx, LONG cy, ZoomReticleSettings& settings)
{
    return std::make_unique<ZoomReticleImpl>(hinst, cx, cy, settings);
}
