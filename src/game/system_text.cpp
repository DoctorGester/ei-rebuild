// The Windows calls that take or give text, in UTF-8. See system_text.h.
#include "system_text.h"

#include "lib/text_case.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cstring>
#include <string>

#pragma comment(lib, "winmm.lib")   // mmioOpenW

namespace text {

namespace {

bool IsAscii(const char* text) {
    for (; *text; ++text) {
        if (static_cast<unsigned char>(*text) >= 0x80) return false;
    }
    return true;
}

// A text argument as UTF-16 (null stays null).
class WideArg {
public:
    explicit WideArg(const char* text) : null(text == nullptr) {
        if (text) wide = Wide(text);
    }
    operator LPCWSTR() const { return null ? nullptr : wide.c_str(); }
    LPWSTR Writable() { return null ? nullptr : wide.data(); }

private:
    bool null;
    std::wstring wide;
};

// A window class argument: an atom (MAKEINTATOM) as it is, a name as UTF-16.
class ClassArg {
public:
    explicit ClassArg(const char* newName) : name(newName), wide(IS_INTRESOURCE(newName) ? nullptr : newName) {}
    operator LPCWSTR() const {
        return IS_INTRESOURCE(name) ? reinterpret_cast<LPCWSTR>(name) : static_cast<LPCWSTR>(wide);
    }

private:
    const char* name;
    WideArg wide;
};

// The bytes of `utf8` that fit `size` bytes with the terminator, cut at a character's start.
std::size_t FittingBytes(const std::string& utf8, std::size_t size) {
    if (utf8.size() < size) return utf8.size();
    std::size_t n = size - 1;
    while (n > 0 && (static_cast<unsigned char>(utf8[n]) & 0xC0) == 0x80) --n;
    return n;
}

// `wide` into `buffer` (`size` bytes) as UTF-8, cut to fit and terminated; the bytes written without the
// terminator (0 when there is no room at all).
int CopyOut(std::wstring_view wide, char* buffer, int size) {
    if (!buffer || size <= 0) return 0;
    const std::string utf8 = Narrow(wide);
    const std::size_t n = FittingBytes(utf8, static_cast<std::size_t>(size));
    std::memcpy(buffer, utf8.data(), n);
    buffer[n] = '\0';
    return static_cast<int>(n);
}

// A path result as the ANSI calls give it: the length without the terminator when it fits `size` bytes,
// else the size it needs with the terminator (nothing written).
DWORD PathOut(std::wstring_view wide, LPSTR buffer, DWORD size) {
    const std::string utf8 = Narrow(wide);
    if (!buffer || utf8.size() >= size) return static_cast<DWORD>(utf8.size() + 1);
    std::memcpy(buffer, utf8.data(), utf8.size());
    buffer[utf8.size()] = '\0';
    return static_cast<DWORD>(utf8.size());
}

void CopyFindData(const WIN32_FIND_DATAW& wide, WIN32_FIND_DATAA* data) {
    data->dwFileAttributes = wide.dwFileAttributes;
    data->ftCreationTime = wide.ftCreationTime;
    data->ftLastAccessTime = wide.ftLastAccessTime;
    data->ftLastWriteTime = wide.ftLastWriteTime;
    data->nFileSizeHigh = wide.nFileSizeHigh;
    data->nFileSizeLow = wide.nFileSizeLow;
    data->dwReserved0 = wide.dwReserved0;
    data->dwReserved1 = wide.dwReserved1;
    CopyOut(wide.cFileName, data->cFileName, static_cast<int>(sizeof(data->cFileName)));
    CopyOut(wide.cAlternateFileName, data->cAlternateFileName, static_cast<int>(sizeof(data->cAlternateFileName)));
}

struct Route {
    const char* function;
    const void* utf8;
};
#define TEXT_ROUTE(name) {#name, reinterpret_cast<const void*>(&utf8::name)}
// The game's imports that take or give text and that a call reaches through the slot, and their UTF-8 versions.
const Route kRoutes[] = {
    TEXT_ROUTE(CreateFileA),       TEXT_ROUTE(FindFirstFileA),        TEXT_ROUTE(FindNextFileA),
    TEXT_ROUTE(DeleteFileA),       TEXT_ROUTE(MoveFileA),             TEXT_ROUTE(CopyFileA),
    TEXT_ROUTE(CreateDirectoryA),  TEXT_ROUTE(RemoveDirectoryA),      TEXT_ROUTE(GetFileAttributesA),
    TEXT_ROUTE(GetCurrentDirectoryA), TEXT_ROUTE(GetFullPathNameA),   TEXT_ROUTE(GetModuleFileNameA),
    TEXT_ROUTE(GetDiskFreeSpaceA), TEXT_ROUTE(GetVolumeInformationA), TEXT_ROUTE(mmioOpenA),
    TEXT_ROUTE(CreateWindowExA),   TEXT_ROUTE(FindWindowA),           TEXT_ROUTE(MessageBoxA),
    TEXT_ROUTE(GetKeyNameTextA),
    // The windows are Unicode ones: their class registered wide, their messages taken, passed on and
    // handled wide, so WM_CHAR carries UTF-16.
    TEXT_ROUTE(RegisterClassExA),  TEXT_ROUTE(DefWindowProcA),        TEXT_ROUTE(GetMessageA),
    TEXT_ROUTE(PeekMessageA),      TEXT_ROUTE(DispatchMessageA),
};
#undef TEXT_ROUTE

} // namespace

std::wstring Wide(std::string_view text) {
    if (text.empty()) return {};
    const int size = static_cast<int>(text.size());
    UINT codepage = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    int count = ::MultiByteToWideChar(codepage, flags, text.data(), size, nullptr, 0);
    if (count <= 0) {   // not UTF-8: the ANSI code page's bytes, as the ANSI calls take them
        codepage = CP_ACP;
        flags = 0;
        count = ::MultiByteToWideChar(codepage, flags, text.data(), size, nullptr, 0);
    }
    std::wstring wide(static_cast<std::size_t>(count > 0 ? count : 0), L'\0');
    if (count > 0) ::MultiByteToWideChar(codepage, flags, text.data(), size, wide.data(), count);
    return wide;
}

std::string Narrow(std::wstring_view wide) {
    if (wide.empty()) return {};
    const int size = static_cast<int>(wide.size());
    const int count = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), size, nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(count > 0 ? count : 0), '\0');
    if (count > 0) ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), size, utf8.data(), count, nullptr, nullptr);
    return utf8;
}

const void* SystemRoute(const char* function) {
    for (const Route& r : kRoutes)
        if (std::strcmp(r.function, function) == 0) return r.utf8;
    return nullptr;
}

namespace utf8 {


HANDLE WINAPI CreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD disposition,
                          DWORD flags, HANDLE templateFile) {
    return ::CreateFileW(WideArg(name), access, share, security, disposition, flags, templateFile);
}

HANDLE WINAPI FindFirstFileA(LPCSTR name, LPWIN32_FIND_DATAA data) {
    WIN32_FIND_DATAW wide;
    const HANDLE find = ::FindFirstFileW(WideArg(name), &wide);
    if (find != INVALID_HANDLE_VALUE && data) CopyFindData(wide, data);
    return find;
}

BOOL WINAPI FindNextFileA(HANDLE find, LPWIN32_FIND_DATAA data) {
    WIN32_FIND_DATAW wide;
    const BOOL found = ::FindNextFileW(find, &wide);
    if (found && data) CopyFindData(wide, data);
    return found;
}

BOOL WINAPI DeleteFileA(LPCSTR name) { return ::DeleteFileW(WideArg(name)); }

BOOL WINAPI MoveFileA(LPCSTR from, LPCSTR to) { return ::MoveFileW(WideArg(from), WideArg(to)); }

BOOL WINAPI CopyFileA(LPCSTR from, LPCSTR to, BOOL failIfExists) {
    return ::CopyFileW(WideArg(from), WideArg(to), failIfExists);
}

BOOL WINAPI CreateDirectoryA(LPCSTR name, LPSECURITY_ATTRIBUTES security) {
    return ::CreateDirectoryW(WideArg(name), security);
}

BOOL WINAPI RemoveDirectoryA(LPCSTR name) { return ::RemoveDirectoryW(WideArg(name)); }

DWORD WINAPI GetFileAttributesA(LPCSTR name) { return ::GetFileAttributesW(WideArg(name)); }

DWORD WINAPI GetCurrentDirectoryA(DWORD size, LPSTR buffer) {
    const DWORD needed = ::GetCurrentDirectoryW(0, nullptr);
    if (needed == 0) return 0;
    std::wstring wide(needed, L'\0');
    const DWORD length = ::GetCurrentDirectoryW(needed, wide.data());
    if (length == 0 || length >= needed) return 0;
    wide.resize(length);
    return PathOut(wide, buffer, size);
}

DWORD WINAPI GetFullPathNameA(LPCSTR name, DWORD size, LPSTR buffer, LPSTR* filePart) {
    const WideArg wideName(name);
    const DWORD needed = ::GetFullPathNameW(wideName, 0, nullptr, nullptr);
    if (needed == 0) return 0;
    std::wstring wide(needed, L'\0');
    LPWSTR wideFile = nullptr;
    const DWORD length = ::GetFullPathNameW(wideName, needed, wide.data(), &wideFile);
    if (length == 0 || length >= needed) return 0;
    const std::size_t fileAt = wideFile ? static_cast<std::size_t>(wideFile - wide.data()) : 0;
    wide.resize(length);
    const DWORD result = PathOut(wide, buffer, size);
    if (filePart && result < size) {
        *filePart = wideFile ? buffer + Narrow(std::wstring_view(wide).substr(0, fileAt)).size() : nullptr;
    }
    return result;
}

DWORD WINAPI GetModuleFileNameA(HMODULE module, LPSTR buffer, DWORD size) {
    std::wstring wide(32768, L'\0');
    const DWORD length = ::GetModuleFileNameW(module, wide.data(), static_cast<DWORD>(wide.size()));
    if (length == 0 || length >= wide.size()) return 0;
    wide.resize(length);
    if (!buffer || size == 0) {
        ::SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    // As the ANSI call: a name that does not fit is cut, terminated, and the result is `size`.
    const std::string utf8 = Narrow(wide);
    const std::size_t n = FittingBytes(utf8, size);
    std::memcpy(buffer, utf8.data(), n);
    buffer[n] = '\0';
    if (n < utf8.size()) {
        ::SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return size;
    }
    return static_cast<DWORD>(n);
}

BOOL WINAPI GetDiskFreeSpaceA(LPCSTR root, LPDWORD sectorsPerCluster, LPDWORD bytesPerSector, LPDWORD freeClusters,
                              LPDWORD clusters) {
    return ::GetDiskFreeSpaceW(WideArg(root), sectorsPerCluster, bytesPerSector, freeClusters, clusters);
}

BOOL WINAPI GetVolumeInformationA(LPCSTR root, LPSTR volumeName, DWORD volumeNameSize, LPDWORD serial,
                                  LPDWORD maxComponentLength, LPDWORD fileSystemFlags, LPSTR fileSystemName,
                                  DWORD fileSystemNameSize) {
    std::wstring volume(volumeName ? (std::max)(volumeNameSize, DWORD{1}) : 0, L'\0');
    std::wstring fileSystem(fileSystemName ? (std::max)(fileSystemNameSize, DWORD{1}) : 0, L'\0');
    const BOOL ok = ::GetVolumeInformationW(WideArg(root), volumeName ? volume.data() : nullptr, volumeNameSize, serial,
                                            maxComponentLength, fileSystemFlags,
                                            fileSystemName ? fileSystem.data() : nullptr, fileSystemNameSize);
    if (!ok) return ok;
    if (volumeName) CopyOut(volume.c_str(), volumeName, static_cast<int>(volumeNameSize));
    if (fileSystemName) CopyOut(fileSystem.c_str(), fileSystemName, static_cast<int>(fileSystemNameSize));
    return ok;
}

HCURSOR WINAPI LoadCursorFromFileA(LPCSTR name) { return ::LoadCursorFromFileW(WideArg(name)); }

HMMIO WINAPI mmioOpenA(LPSTR name, LPMMIOINFO info, DWORD flags) {
    // MMIO_PARSE and MMIO_GETTEMP write a name back into `name`: those stay ANSI (the game uses neither).
    if (flags & (MMIO_PARSE | MMIO_GETTEMP)) return ::mmioOpenA(name, info, flags);
    WideArg wide(name);
    return ::mmioOpenW(wide.Writable(), info, flags);
}

// ---- The window title, the message box, key names -----------------------------------------------------

HWND WINAPI CreateWindowExA(DWORD exStyle, LPCSTR className, LPCSTR title, DWORD style, int x, int y, int width,
                            int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID parameter) {
    if (!title || IsAscii(title))
        return ::CreateWindowExA(exStyle, className, title, style, x, y, width, height, parent, menu, instance, parameter);
    // The title is set as UTF-16 past the window procedure: CreateWindowExA would pass it in the ANSI code page
    // (and a window of an ANSI class takes it so).
    const std::wstring wide = Wide(title);
    HWND window = ::CreateWindowExA(exStyle, className, "", style, x, y, width, height, parent, menu, instance, parameter);
    if (window) ::DefWindowProcW(window, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(wide.c_str()));
    return window;
}

ATOM WINAPI RegisterClassExA(const WNDCLASSEXA* windowClass) {
    if (!windowClass) return ::RegisterClassExA(windowClass);
    const ClassArg menu(windowClass->lpszMenuName);
    const ClassArg name(windowClass->lpszClassName);
    WNDCLASSEXW wide{};
    wide.cbSize = sizeof(wide);
    wide.style = windowClass->style;
    wide.lpfnWndProc = windowClass->lpfnWndProc;
    wide.cbClsExtra = windowClass->cbClsExtra;
    wide.cbWndExtra = windowClass->cbWndExtra;
    wide.hInstance = windowClass->hInstance;
    wide.hIcon = windowClass->hIcon;
    wide.hCursor = windowClass->hCursor;
    wide.hbrBackground = windowClass->hbrBackground;
    wide.lpszMenuName = menu;
    wide.lpszClassName = name;
    wide.hIconSm = windowClass->hIconSm;
    return ::RegisterClassExW(&wide);
}

// The wide calls themselves (the same arguments). Our own functions rather than the exports' addresses: the
// routes are read during the image's static initialisation, before an import's address could be stored.
LRESULT WINAPI DefWindowProcA(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    return ::DefWindowProcW(window, message, wParam, lParam);
}

BOOL WINAPI GetMessageA(LPMSG message, HWND window, UINT first, UINT last) {
    return ::GetMessageW(message, window, first, last);
}

BOOL WINAPI PeekMessageA(LPMSG message, HWND window, UINT first, UINT last, UINT remove) {
    return ::PeekMessageW(message, window, first, last, remove);
}

LRESULT WINAPI DispatchMessageA(const MSG* message) { return ::DispatchMessageW(message); }

HWND WINAPI FindWindowA(LPCSTR className, LPCSTR title) {
    return ::FindWindowW(ClassArg(className), WideArg(title));
}

int WINAPI MessageBoxA(HWND owner, LPCSTR text, LPCSTR caption, UINT type) {
    return ::MessageBoxW(owner, WideArg(text), WideArg(caption), type);
}

int WINAPI GetKeyNameTextA(LONG key, LPSTR buffer, int size) {
    if (!buffer || size <= 0) return 0;
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    const int length = ::GetKeyNameTextW(key, wide.data(), size);
    if (length <= 0) {
        buffer[0] = '\0';
        return 0;
    }
    return CopyOut(std::wstring_view(wide.data(), static_cast<std::size_t>(length)), buffer, size);
}

namespace {
// CharUpperA / CharLowerA's two forms: a string changed in place (the pointer back), or one character in the
// pointer's low word (its other case back in the same form).
LPSTR ChangeCase(LPSTR text, unsigned (*map)(unsigned), void (*inPlace)(char*, std::size_t)) {
    const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(text);
    if ((value >> 16) == 0) {
        const unsigned c = static_cast<unsigned>(value & 0xFF);
        return reinterpret_cast<LPSTR>(static_cast<std::uintptr_t>(c < 0x80 ? map(c) : c));
    }
    inPlace(text, static_cast<std::size_t>(-1));
    return text;
}
} // namespace

LPSTR WINAPI CharUpperA(LPSTR text) { return ChangeCase(text, UpperCodePoint, UpperInPlace); }
LPSTR WINAPI CharLowerA(LPSTR text) { return ChangeCase(text, LowerCodePoint, LowerInPlace); }

} // namespace utf8

bool IsFile(const std::string& path) {
    const DWORD attributes = utf8::GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::vector<FoundEntry> FindAll(const std::string& pattern) {
    std::vector<FoundEntry> entries;
    WIN32_FIND_DATAA found;
    const HANDLE find = utf8::FindFirstFileA(pattern.c_str(), &found);
    if (find == INVALID_HANDLE_VALUE) return entries;
    do {
        entries.push_back({found.cFileName, (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0});
    } while (utf8::FindNextFileA(find, &found));
    ::FindClose(find);
    return entries;
}

} // namespace text
