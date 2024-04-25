// Copyright (c) 2024 Christopher Antos
// License: http://opensource.org/licenses/MIT

#pragma once

#define VERSION_MAJOR           24
#define VERSION_MONTH           4
#define VERSION_DAY             25

#define VERSION_PATCH           0

#define COPYRIGHT_STR           "Copyright (C) 2024 Christopher Antos"

#define IND_VER4( a, b, c, d ) L#a ## L"." ## L#b ## L"." ## L#c ## L"." ## L#d
#define DOT_VER4( a, b, c, d ) IND_VER4( a, b, c, d )

#define RC_VERSION                VERSION_MAJOR, VERSION_MONTH, VERSION_DAY, VERSION_PATCH
#define RC_VERSION_STR  DOT_VER4( VERSION_MAJOR, VERSION_MONTH, VERSION_DAY, VERSION_PATCH )

#define    VERSION_STR  DOT_VER4( VERSION_MAJOR, VERSION_MONTH, VERSION_DAY, VERSION_PATCH )

