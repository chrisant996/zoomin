// Copyright (c) 2024 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "dpi.h"
#include <string>
#include <assert.h>

LONG ReadRegLong(const WCHAR* name, LONG default_value);
void WriteRegLong(const WCHAR* name, LONG value);

