#pragma once

// The basic Windows types (windef.h: RECT, POINT, SIZE, HWND, HANDLE, DWORD, WPARAM, ...; minwinbase.h:
// CRITICAL_SECTION) for a header that holds or passes them, without the rest of <windows.h> (~0.2 s of every file
// that includes it). A file that calls Windows functions or uses its constants includes <windows.h> itself.
// windows.h names the target the same way before its own includes.

#include <sdkddkver.h>

#if defined(_M_IX86) && !defined(_X86_)
#define _X86_
#elif defined(_M_X64) && !defined(_AMD64_)
#define _AMD64_
#endif

#include <windef.h>
#include <minwinbase.h>
