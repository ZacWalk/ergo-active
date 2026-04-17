// win.h - Precompiled / common header. Sets up Unicode, Win32 target
// version macros, includes Windows and C++ standard library headers,
// and links required system libraries (comctl32, shell32).

#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WINVER
#define WINVER _WIN32_WINNT_WIN10
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT _WIN32_WINNT_WIN10
#endif

#ifndef _WIN32_IE
#define _WIN32_IE _WIN32_IE_IE60
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <strsafe.h>
#include <tchar.h>
#include <wtsapi32.h>
#include <shlobj.h>

#pragma comment(lib, "Wtsapi32.lib")

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <cwchar>
#include <format>
#include <numbers>
#include <string>
#include <vector>
