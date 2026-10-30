#pragma once

// The Windows functions lib/gameapi.h names, declared as the Windows SDK declares them (C linkage, imported,
// WINAPI) over lib/win_types.h's types, so that a file calling only these through gameapi:: needs no <windows.h>
// (~0.2 s of every file that includes it). A structure a call only points to is declared by its SDK tag.
// tools/gen_iat.py reads the names: a Windows import our code calls as gameapi::X is declared here (it stops
// otherwise). lib/gameapi.cpp sees these and the SDK's together, so a declaration that differs from the SDK's does
// not compile.

#include <cstdarg>

#include "lib/win_types.h"

struct _BY_HANDLE_FILE_INFORMATION;
struct _GUID;
struct _MMIOINFO;
struct _SYSTEM_INFO;
struct tagMSG;
struct tagPAINTSTRUCT;
struct tagWNDCLASSEXA;
struct HMMIO__;
using HMMIO = HMMIO__*;

extern "C" {

// KERNEL32.dll: files.
__declspec(dllimport) HANDLE WINAPI CreateFileA(LPCSTR name, DWORD access, DWORD share,
                                                LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags,
                                                HANDLE templateFile);
__declspec(dllimport) BOOL WINAPI ReadFile(HANDLE file, LPVOID buffer, DWORD size, LPDWORD read,
                                           LPOVERLAPPED overlapped);
__declspec(dllimport) BOOL WINAPI WriteFile(HANDLE file, LPCVOID buffer, DWORD size, LPDWORD written,
                                            LPOVERLAPPED overlapped);
__declspec(dllimport) DWORD WINAPI SetFilePointer(HANDLE file, LONG distance, PLONG distanceHigh, DWORD method);
__declspec(dllimport) DWORD WINAPI GetFileSize(HANDLE file, LPDWORD sizeHigh);
__declspec(dllimport) BOOL WINAPI GetFileInformationByHandle(HANDLE file, _BY_HANDLE_FILE_INFORMATION* information);
__declspec(dllimport) BOOL WINAPI SetFileTime(HANDLE file, const FILETIME* creation, const FILETIME* access,
                                              const FILETIME* write);
__declspec(dllimport) BOOL WINAPI CloseHandle(HANDLE object);
__declspec(dllimport) BOOL WINAPI DeleteFileA(LPCSTR name);
__declspec(dllimport) BOOL WINAPI MoveFileA(LPCSTR from, LPCSTR to);
__declspec(dllimport) BOOL WINAPI CopyFileA(LPCSTR from, LPCSTR to, BOOL failIfExists);
__declspec(dllimport) BOOL WINAPI CreateDirectoryA(LPCSTR name, LPSECURITY_ATTRIBUTES security);
__declspec(dllimport) BOOL WINAPI RemoveDirectoryA(LPCSTR name);
__declspec(dllimport) DWORD WINAPI GetFileAttributesA(LPCSTR name);
__declspec(dllimport) HANDLE WINAPI FindFirstFileA(LPCSTR name, LPWIN32_FIND_DATAA data);
__declspec(dllimport) BOOL WINAPI FindNextFileA(HANDLE find, LPWIN32_FIND_DATAA data);
__declspec(dllimport) BOOL WINAPI FindClose(HANDLE find);
__declspec(dllimport) DWORD WINAPI GetCurrentDirectoryA(DWORD size, LPSTR buffer);
__declspec(dllimport) DWORD WINAPI GetFullPathNameA(LPCSTR name, DWORD size, LPSTR buffer, LPSTR* filePart);
__declspec(dllimport) BOOL WINAPI GetDiskFreeSpaceA(LPCSTR root, LPDWORD sectorsPerCluster, LPDWORD bytesPerSector,
                                                    LPDWORD freeClusters, LPDWORD clusters);
__declspec(dllimport) BOOL WINAPI GetVolumeInformationA(LPCSTR root, LPSTR volumeName, DWORD volumeNameSize,
                                                        LPDWORD serial, LPDWORD maxComponentLength,
                                                        LPDWORD fileSystemFlags, LPSTR fileSystemName,
                                                        DWORD fileSystemNameSize);
__declspec(dllimport) HANDLE WINAPI CreateFileMappingA(HANDLE file, LPSECURITY_ATTRIBUTES security, DWORD protect,
                                                       DWORD sizeHigh, DWORD sizeLow, LPCSTR name);
__declspec(dllimport) LPVOID WINAPI MapViewOfFile(HANDLE mapping, DWORD access, DWORD offsetHigh, DWORD offsetLow,
                                                  SIZE_T size);
__declspec(dllimport) BOOL WINAPI FlushViewOfFile(LPCVOID base, SIZE_T size);
__declspec(dllimport) BOOL WINAPI UnmapViewOfFile(LPCVOID base);
__declspec(dllimport) DWORD WINAPI GetPrivateProfileSectionA(LPCSTR section, LPSTR buffer, DWORD size, LPCSTR file);
__declspec(dllimport) DWORD WINAPI GetPrivateProfileStringA(LPCSTR section, LPCSTR key, LPCSTR fallback,
                                                            LPSTR buffer, DWORD size, LPCSTR file);

// KERNEL32.dll: the module, errors, time, threads and their synchronisation.
__declspec(dllimport) HMODULE WINAPI GetModuleHandleA(LPCSTR name);
__declspec(dllimport) DWORD WINAPI GetModuleFileNameA(HMODULE module, LPSTR buffer, DWORD size);
__declspec(dllimport) FARPROC WINAPI GetProcAddress(HMODULE module, LPCSTR name);
__declspec(dllimport) void WINAPI GetSystemInfo(_SYSTEM_INFO* info);
__declspec(dllimport) DWORD WINAPI GetLastError();
__declspec(dllimport) void WINAPI SetLastError(DWORD error);
__declspec(dllimport) void WINAPI OutputDebugStringA(LPCSTR text);
__declspec(dllimport) LPSTR WINAPI lstrcpynA(LPSTR to, LPCSTR from, int size);
__declspec(dllimport) DWORD WINAPI GetTickCount();
__declspec(dllimport) void WINAPI GetLocalTime(LPSYSTEMTIME time);
__declspec(dllimport) void WINAPI GetSystemTime(LPSYSTEMTIME time);
__declspec(dllimport) BOOL WINAPI SystemTimeToFileTime(const SYSTEMTIME* time, LPFILETIME fileTime);
__declspec(dllimport) void WINAPI Sleep(DWORD milliseconds);
__declspec(dllimport) DWORD WINAPI ResumeThread(HANDLE thread);
__declspec(dllimport) BOOL WINAPI SetThreadPriority(HANDLE thread, int priority);
__declspec(dllimport) void WINAPI InitializeCriticalSection(LPCRITICAL_SECTION section);
__declspec(dllimport) void WINAPI EnterCriticalSection(LPCRITICAL_SECTION section);
__declspec(dllimport) void WINAPI LeaveCriticalSection(LPCRITICAL_SECTION section);
__declspec(dllimport) void WINAPI DeleteCriticalSection(LPCRITICAL_SECTION section);
__declspec(dllimport) HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES security, BOOL manualReset, BOOL initialState,
                                                 LPCSTR name);
__declspec(dllimport) BOOL WINAPI SetEvent(HANDLE event);
__declspec(dllimport) DWORD WINAPI WaitForSingleObject(HANDLE object, DWORD milliseconds);

// USER32.dll: rectangles.
__declspec(dllimport) BOOL WINAPI CopyRect(LPRECT to, const RECT* from);

// USER32.dll: the window, its messages, the cursor, the keyboard layouts, text.
__declspec(dllimport) ATOM WINAPI RegisterClassExA(const tagWNDCLASSEXA* windowClass);
__declspec(dllimport) HWND WINAPI CreateWindowExA(DWORD exStyle, LPCSTR className, LPCSTR title, DWORD style, int x,
                                                  int y, int width, int height, HWND parent, HMENU menu,
                                                  HINSTANCE instance, LPVOID parameter);
__declspec(dllimport) HWND WINAPI FindWindowA(LPCSTR className, LPCSTR title);
__declspec(dllimport) BOOL WINAPI ShowWindow(HWND window, int show);
__declspec(dllimport) BOOL WINAPI UpdateWindow(HWND window);
__declspec(dllimport) BOOL WINAPI InvalidateRect(HWND window, const RECT* rect, BOOL erase);
__declspec(dllimport) BOOL WINAPI GetUpdateRect(HWND window, LPRECT rect, BOOL erase);
__declspec(dllimport) HDC WINAPI BeginPaint(HWND window, tagPAINTSTRUCT* paint);
__declspec(dllimport) BOOL WINAPI EndPaint(HWND window, const tagPAINTSTRUCT* paint);
__declspec(dllimport) BOOL WINAPI IsIconic(HWND window);
__declspec(dllimport) BOOL WINAPI BringWindowToTop(HWND window);
__declspec(dllimport) BOOL WINAPI SetForegroundWindow(HWND window);
__declspec(dllimport) HWND WINAPI GetForegroundWindow();
__declspec(dllimport) HWND WINAPI GetLastActivePopup(HWND window);
__declspec(dllimport) DWORD WINAPI GetWindowThreadProcessId(HWND window, LPDWORD process);
__declspec(dllimport) LRESULT WINAPI DefWindowProcA(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
__declspec(dllimport) BOOL WINAPI GetMessageA(tagMSG* message, HWND window, UINT first, UINT last);
__declspec(dllimport) BOOL WINAPI PeekMessageA(tagMSG* message, HWND window, UINT first, UINT last, UINT remove);
__declspec(dllimport) BOOL WINAPI TranslateMessage(const tagMSG* message);
__declspec(dllimport) LRESULT WINAPI DispatchMessageA(const tagMSG* message);
__declspec(dllimport) void WINAPI PostQuitMessage(int exitCode);
__declspec(dllimport) UINT WINAPI RegisterWindowMessageA(LPCSTR name);
__declspec(dllimport) int WINAPI MessageBoxA(HWND owner, LPCSTR text, LPCSTR caption, UINT type);
__declspec(dllimport) BOOL WINAPI SystemParametersInfoA(UINT action, UINT parameter, PVOID value, UINT update);
__declspec(dllimport) HCURSOR WINAPI GetCursor();
__declspec(dllimport) HCURSOR WINAPI SetCursor(HCURSOR cursor);
__declspec(dllimport) BOOL WINAPI GetCursorPos(LPPOINT point);
__declspec(dllimport) BOOL WINAPI SetCursorPos(int x, int y);
__declspec(dllimport) HKL WINAPI GetKeyboardLayout(DWORD thread);
__declspec(dllimport) int WINAPI GetKeyboardLayoutList(int size, HKL* layouts);
__declspec(dllimport) BOOL WINAPI GetKeyboardLayoutNameA(LPSTR name);
__declspec(dllimport) HKL WINAPI ActivateKeyboardLayout(HKL layout, UINT flags);
__declspec(dllimport) int WINAPI GetKeyNameTextA(LONG key, LPSTR buffer, int size);
__declspec(dllimport) int WINAPI wvsprintfA(LPSTR buffer, LPCSTR format, va_list args);

// WINMM.dll.
__declspec(dllimport) DWORD WINAPI timeGetTime();
__declspec(dllimport) HMMIO WINAPI mmioOpenA(LPSTR name, _MMIOINFO* info, DWORD flags);
__declspec(dllimport) LONG WINAPI mmioRead(HMMIO file, char* buffer, LONG size);
__declspec(dllimport) LONG WINAPI mmioSeek(HMMIO file, LONG offset, int origin);
__declspec(dllimport) UINT WINAPI mmioClose(HMMIO file, UINT flags);

// DSOUND.dll (not imported in the SDK's declaration: the call goes through dsound.lib's stub).
HRESULT WINAPI DirectSoundEnumerateA(BOOL(CALLBACK* callback)(_GUID* guid, LPCSTR description, LPCSTR module,
                                                               LPVOID context),
                                     LPVOID context);

} // extern "C"
