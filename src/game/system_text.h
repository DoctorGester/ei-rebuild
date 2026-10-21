#pragma once

// Text at the system boundary in UTF-8 (docs/notes/standalone.md section 4, ticket U2). With EI_UTF8 every
// string inside the game is UTF-8, so the Windows calls that take or give text (file names, the registry,
// the window title, the message box, key names, resource strings) are the wide ones, converting at the call:
//   - game.exe's imports (gameapi.h, and through it CFile, CFileFind and the CRT's file calls): the image's
//     slot of each ANSI call below takes its UTF-8 version (text::utf8, SystemRoute; image_imports.cpp asks
//     for it), so a call through gameapi:: passes and receives UTF-8. The harness takes those slots after,
//     as it does the ANSI exports' (its redirects work on the bytes).
//   - the direct calls of our own code: text::sys, the UTF-8 version with EI_UTF8 and the ANSI call itself
//     without.
// Without EI_UTF8 nothing is routed and text::sys is the ANSI calls: nothing changes.
//
// Text going in that is not valid UTF-8 is taken in the ANSI code page, as the ANSI call took it (8-bit text
// that has not crossed text::FromAssetText yet: typed characters until U4). Text coming out is UTF-8.
//
// Not routed (still ANSI): the profile calls (GetPrivateProfile*, WritePrivateProfileString), whose values
// round-trip as bytes in the ANSI calls (the config\*.ini files are the game's own and their names are
// ASCII); GDI's text draw and extent (U3); the window's messages (WM_CHAR: U4).

#include <windows.h>
#include <mmsystem.h>

#include <string>
#include <string_view>

#include "asset_text.h"

namespace text {

// `text` (UTF-8, or else ANSI-code-page bytes) as UTF-16.
std::wstring Wide(std::string_view text);
// UTF-16 as UTF-8.
std::string Narrow(std::wstring_view wide);

// The UTF-8 version the image's slot of `dll`!`function` takes (with EI_UTF8), else nullptr.
const void* SystemRoute(const char* dll, const char* function);

// The UTF-8 versions of the ANSI calls: the same arguments and results, with the text in UTF-8 (the sizes of
// text buffers in bytes, as the ANSI calls count them). Compiled in both builds; used with EI_UTF8.
namespace utf8 {

// Files.
HANDLE WINAPI CreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD disposition,
                          DWORD flags, HANDLE templateFile);
HANDLE WINAPI FindFirstFileA(LPCSTR name, LPWIN32_FIND_DATAA data);
BOOL WINAPI FindNextFileA(HANDLE find, LPWIN32_FIND_DATAA data);
BOOL WINAPI DeleteFileA(LPCSTR name);
BOOL WINAPI MoveFileA(LPCSTR from, LPCSTR to);
BOOL WINAPI CopyFileA(LPCSTR from, LPCSTR to, BOOL failIfExists);
BOOL WINAPI CreateDirectoryA(LPCSTR name, LPSECURITY_ATTRIBUTES security);
BOOL WINAPI RemoveDirectoryA(LPCSTR name);
DWORD WINAPI GetFileAttributesA(LPCSTR name);
BOOL WINAPI SetFileAttributesA(LPCSTR name, DWORD attributes);
DWORD WINAPI GetCurrentDirectoryA(DWORD size, LPSTR buffer);
DWORD WINAPI GetFullPathNameA(LPCSTR name, DWORD size, LPSTR buffer, LPSTR* filePart);
DWORD WINAPI GetModuleFileNameA(HMODULE module, LPSTR buffer, DWORD size);
BOOL WINAPI GetDiskFreeSpaceA(LPCSTR root, LPDWORD sectorsPerCluster, LPDWORD bytesPerSector, LPDWORD freeClusters,
                              LPDWORD clusters);
BOOL WINAPI GetVolumeInformationA(LPCSTR root, LPSTR volumeName, DWORD volumeNameSize, LPDWORD serial,
                                  LPDWORD maxComponentLength, LPDWORD fileSystemFlags, LPSTR fileSystemName,
                                  DWORD fileSystemNameSize);
HMODULE WINAPI LoadLibraryA(LPCSTR name);
HCURSOR WINAPI LoadCursorFromFileA(LPCSTR name);
HMMIO WINAPI mmioOpenA(LPSTR name, LPMMIOINFO info, DWORD flags);

// The registry (key and value names, string values).
LSTATUS WINAPI RegOpenKeyExA(HKEY key, LPCSTR subKey, DWORD options, REGSAM access, PHKEY result);
LSTATUS WINAPI RegCreateKeyExA(HKEY key, LPCSTR subKey, DWORD reserved, LPSTR keyClass, DWORD options, REGSAM access,
                               const LPSECURITY_ATTRIBUTES security, PHKEY result, LPDWORD disposition);
LSTATUS WINAPI RegQueryValueExA(HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size);
LSTATUS WINAPI RegSetValueExA(HKEY key, LPCSTR name, DWORD reserved, DWORD type, const BYTE* data, DWORD size);

// The window title, the message box, key names, resource strings.
HWND WINAPI CreateWindowExA(DWORD exStyle, LPCSTR className, LPCSTR title, DWORD style, int x, int y, int width,
                            int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID parameter);
HWND WINAPI FindWindowA(LPCSTR className, LPCSTR title);
int WINAPI MessageBoxA(HWND owner, LPCSTR text, LPCSTR caption, UINT type);
int WINAPI GetKeyNameTextA(LONG key, LPSTR buffer, int size);
int WINAPI LoadStringA(HINSTANCE instance, UINT id, LPSTR buffer, int size);

} // namespace utf8

// The calls our own code makes directly (not through game.exe's imports): UTF-8 with EI_UTF8, else ANSI.
namespace sys {
#if EI_UTF8
using utf8::FindFirstFileA;
using utf8::FindNextFileA;
using utf8::GetFileAttributesA;
using utf8::LoadCursorFromFileA;
using utf8::RegOpenKeyExA;
using utf8::RegQueryValueExA;
#else
using ::FindFirstFileA;
using ::FindNextFileA;
using ::GetFileAttributesA;
using ::LoadCursorFromFileA;
using ::RegOpenKeyExA;
using ::RegQueryValueExA;
#endif
} // namespace sys

} // namespace text
