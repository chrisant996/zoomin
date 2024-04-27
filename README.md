# Zoomin

Zoomin magnifies a rectangle on the screen.

It's modeled after the old sample ZoomIn tool from an old Microsoft SDK.

![image](https://raw.githubusercontent.com/chrisant996/zoomin/master/assets/demo.png)

## Features

- Supports multiple monitors with different DPIs.
- Can show gridlines with up to two different intervals (minor and major).
- Can auto-refresh the magnified rectangle on a configurable timer.
- Can copy the magnified rectangle to clipboard.
- Can use arrow keys to move the magnified rectangle.
- Can use <kbd>Shift</kbd> + arrow keys to move the magnified rectangle faster.
- Can use <kbd>Ctrl</kbd> + arrow keys to jump the magnified rectangle to the corresponding edge of the current monitor.

## Why was it created?

I've used the old ZoomIn tool for decades.  But none of the spinoff versions were updated to support [Dynamic DPI](https://learn.microsoft.com/en-us/windows/win32/hidpi/high-dpi-desktop-application-development-on-windows) across multiple monitors, and that made them unable to zoom in on some regions of some monitors.  I needed to zoomin in anywhere.

So I wrote my own version from scratch, and implemented full DDPI support so it works seamlessly across multiple monitors with different DPI scaling factors.  It's released under the MIT license.

## Building Zoomin

Zoomin uses [Premake](http://premake.github.io) to generate Visual Studio solutions. Note that Premake >= 5.0-alpha12 is required.

1. Cd to your clone of zoomin.
2. Run <code>premake5.exe <em>toolchain</em></code> (where <em>toolchain</em> is one of Premake's actions - see `premake5.exe --help`).
3. Build scripts will be generated in <code>.build\\<em>toolchain</em></code>. For example `.build\vs2019\zoomin.sln`.
4. Call your toolchain of choice (Visual Studio, msbuild.exe, etc).

