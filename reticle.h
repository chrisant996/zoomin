// Copyright (c) 2024 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#include <memory>
#include <functional>

template <typename T> T clamp(const T value, const T low, const T high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

struct ZoomReticleSettings
{
    COLORREF m_mainColor = RGB(255, 0, 0);
    COLORREF m_borderColor = RGB(255, 255, 255);
    int m_mainThickness = 2;
    int m_borderThickness = 1;
    int m_opacity = 75;
};

class ZoomReticle
{
public:
    ZoomReticle() = default;
    virtual ~ZoomReticle() = default;
    virtual bool InitReticle() = 0;
    virtual void UpdateReticlePosition(const POINT& ptScreen) = 0;
    virtual void Invoke(const std::function<void()>& func) = 0;
};

std::unique_ptr<ZoomReticle> CreateZoomReticle(HINSTANCE hinst, LONG cx, LONG cy, ZoomReticleSettings& settings = ZoomReticleSettings());
