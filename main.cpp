// Copyright (c) 2024 Christopher Antos
// License: http://opensource.org/licenses/MIT

#include "main.h"
#include <stdlib.h>

static const WCHAR c_reg_root[] = TEXT("Software\\chrisant996\\Zoomin");

int PASCAL WinMain(
    _In_ HINSTANCE hinstCurrent,
    _In_opt_ HINSTANCE /*hinstPrevious*/,
    _In_ LPSTR /*lpszCmdLine*/,
    _In_ int /*nCmdShow*/)
{
    MSG msg = { 0 };

    // Create window.

    HWND hwnd = 0;

    // Main message loop.

    if (hwnd)
    {
        while (true)
        {
            if (!GetMessage(&msg, nullptr, 0, 0))
                    break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    // Cleanup.

    {
        MSG tmp;
        do {} while(PeekMessage(&tmp, 0, WM_QUIT, WM_QUIT, PM_REMOVE));
    }

    return int(msg.wParam);
}

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

