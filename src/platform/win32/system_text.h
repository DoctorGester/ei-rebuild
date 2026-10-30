#pragma once

// Text at the system boundary in UTF-8 (docs/notes/standalone.md section 4). Every string inside the game is UTF-8,
// so the Windows calls that take or give text (file names, the window title, the message box, key names) are the
// wide ones, converting at the call:
//   - the game's imports (gameapi.h, and through it the platform's File, TextFile and FileFind (platform/file.h)): the
//     image's slot of each ANSI call below that our code calls through gameapi:: takes its UTF-8 version (text::utf8,
//     SystemRoute's list; image_imports.cpp asks for it), so such a call passes and receives UTF-8. A new
//     gameapi:: call of another import that takes or gives text needs its version and route here. The harness
//     takes those slots after, as it does the ANSI exports' (its redirects work on the bytes).
//   - the direct calls of our own code: text::utf8 by name.
// Text going in that is not valid UTF-8 is taken in the ANSI code page, as the ANSI call took it (8-bit text
// that has not crossed text::FromAssetText). Text coming out is UTF-8.
// The windows are Unicode ones: RegisterClassExA registers the class wide, and DefWindowProcA,
// GetMessageA, PeekMessageA and DispatchMessageA's slots lead to the wide calls (the same arguments), so a typed
// character arrives as UTF-16 (app.cpp's DispatchInput passes it on as a code point; asset_text.h, "Typing").
// Case: text::utf8's CharUpperA / CharLowerA change UTF-8 case by lib/text_case.h's table.
// Not routed (still ANSI): the profile calls (GetPrivateProfile*, WritePrivateProfileString), whose values
// round-trip as bytes in the ANSI calls (the config\*.ini files are the game's own and their names are
// ASCII).

#include <string>
#include <string_view>
#include <vector>

#include "asset_text.h"
#include "lib/win_api.h"

namespace text {

// `text` (UTF-8, or else ANSI-code-page bytes) as UTF-16.
std::wstring Wide(std::string_view text);
// UTF-16 as UTF-8.
std::string Narrow(std::wstring_view wide);

// The UTF-8 version the image's slot of `function` takes, else nullptr (the names are unique among the game's
// imports).
const void* SystemRoute(const char* function);

// The UTF-8 versions of the ANSI calls: the same arguments and results, with the text in UTF-8 (the sizes of
// text buffers in bytes, as the ANSI calls count them).
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
DWORD WINAPI GetCurrentDirectoryA(DWORD size, LPSTR buffer);
DWORD WINAPI GetFullPathNameA(LPCSTR name, DWORD size, LPSTR buffer, LPSTR* filePart);
DWORD WINAPI GetModuleFileNameA(HMODULE module, LPSTR buffer, DWORD size);
BOOL WINAPI GetDiskFreeSpaceA(LPCSTR root, LPDWORD sectorsPerCluster, LPDWORD bytesPerSector, LPDWORD freeClusters,
                              LPDWORD clusters);
BOOL WINAPI GetVolumeInformationA(LPCSTR root, LPSTR volumeName, DWORD volumeNameSize, LPDWORD serial,
                                  LPDWORD maxComponentLength, LPDWORD fileSystemFlags, LPSTR fileSystemName,
                                  DWORD fileSystemNameSize);
HCURSOR WINAPI LoadCursorFromFileA(LPCSTR name);
HMMIO WINAPI mmioOpenA(LPSTR name, _MMIOINFO* info, DWORD flags);

// The window class (registered wide: the window is a Unicode one), the window title, the message box, key
// names.
ATOM WINAPI RegisterClassExA(const tagWNDCLASSEXA* windowClass);
// The window's messages: the wide calls (a Unicode window's messages are taken and passed on wide; WM_CHAR then
// carries UTF-16).
LRESULT WINAPI DefWindowProcA(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
BOOL WINAPI GetMessageA(tagMSG* message, HWND window, UINT first, UINT last);
BOOL WINAPI PeekMessageA(tagMSG* message, HWND window, UINT first, UINT last, UINT remove);
LRESULT WINAPI DispatchMessageA(const tagMSG* message);
HWND WINAPI CreateWindowExA(DWORD exStyle, LPCSTR className, LPCSTR title, DWORD style, int x, int y, int width,
                            int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID parameter);
HWND WINAPI FindWindowA(LPCSTR className, LPCSTR title);
int WINAPI MessageBoxA(HWND owner, LPCSTR text, LPCSTR caption, UINT type);
int WINAPI GetKeyNameTextA(LONG key, LPSTR buffer, int size);


// Case: a UTF-8 string upper / lower cased in place by lib/text_case.h's table (a character in the
// pointer's low word: an ASCII one changed, any other byte as it is).
LPSTR WINAPI CharUpperA(LPSTR text);
LPSTR WINAPI CharLowerA(LPSTR text);

} // namespace utf8

} // namespace text
