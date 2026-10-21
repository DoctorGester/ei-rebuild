// The Windows calls that take or give text, in UTF-8 (EI_UTF8). See system_text.h.
#include "system_text.h"

#include <algorithm>
#include <cstring>

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
    explicit WideArg(const char* text) : null_(text == nullptr) {
        if (text) wide_ = Wide(text);
    }
    operator LPCWSTR() const { return null_ ? nullptr : wide_.c_str(); }
    LPWSTR Writable() { return null_ ? nullptr : wide_.data(); }

private:
    bool null_;
    std::wstring wide_;
};

// A window class argument: an atom (MAKEINTATOM) as it is, a name as UTF-16.
class ClassArg {
public:
    explicit ClassArg(const char* name) : name_(name), wide_(IS_INTRESOURCE(name) ? nullptr : name) {}
    operator LPCWSTR() const {
        return IS_INTRESOURCE(name_) ? reinterpret_cast<LPCWSTR>(name_) : static_cast<LPCWSTR>(wide_);
    }

private:
    const char* name_;
    WideArg wide_;
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

bool IsStringType(DWORD type) { return type == REG_SZ || type == REG_EXPAND_SZ || type == REG_MULTI_SZ; }

struct Route {
    const char* function;
    const void* utf8;
};
#define EI_UTF8_ROUTE(name) {#name, reinterpret_cast<const void*>(&utf8::name)}
// game.exe's imports that take or give text, and their UTF-8 versions.
const Route kRoutes[] = {
    EI_UTF8_ROUTE(CreateFileA),       EI_UTF8_ROUTE(FindFirstFileA),        EI_UTF8_ROUTE(FindNextFileA),
    EI_UTF8_ROUTE(DeleteFileA),       EI_UTF8_ROUTE(MoveFileA),             EI_UTF8_ROUTE(CopyFileA),
    EI_UTF8_ROUTE(CreateDirectoryA),  EI_UTF8_ROUTE(RemoveDirectoryA),      EI_UTF8_ROUTE(GetFileAttributesA),
    EI_UTF8_ROUTE(SetFileAttributesA), EI_UTF8_ROUTE(GetCurrentDirectoryA), EI_UTF8_ROUTE(GetFullPathNameA),
    EI_UTF8_ROUTE(GetModuleFileNameA), EI_UTF8_ROUTE(GetDiskFreeSpaceA),    EI_UTF8_ROUTE(GetVolumeInformationA),
    EI_UTF8_ROUTE(LoadLibraryA),      EI_UTF8_ROUTE(LoadCursorFromFileA),   EI_UTF8_ROUTE(mmioOpenA),
    EI_UTF8_ROUTE(RegOpenKeyExA),     EI_UTF8_ROUTE(RegCreateKeyExA),       EI_UTF8_ROUTE(RegQueryValueExA),
    EI_UTF8_ROUTE(RegSetValueExA),    EI_UTF8_ROUTE(CreateWindowExA),       EI_UTF8_ROUTE(FindWindowA),
    EI_UTF8_ROUTE(MessageBoxA),       EI_UTF8_ROUTE(GetKeyNameTextA),       EI_UTF8_ROUTE(LoadStringA),
};
#undef EI_UTF8_ROUTE

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

const void* SystemRoute(const char* dll, const char* function) {
    (void)dll;   // the names are unique among game.exe's imports
    if constexpr (!kUtf8) {
        (void)function;
        return nullptr;
    } else {
        for (const Route& r : kRoutes)
            if (std::strcmp(r.function, function) == 0) return r.utf8;
        return nullptr;
    }
}

namespace utf8 {

// ---- Files ---------------------------------------------------------------------------------------------

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

BOOL WINAPI SetFileAttributesA(LPCSTR name, DWORD attributes) { return ::SetFileAttributesW(WideArg(name), attributes); }

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

HMODULE WINAPI LoadLibraryA(LPCSTR name) { return ::LoadLibraryW(WideArg(name)); }

HCURSOR WINAPI LoadCursorFromFileA(LPCSTR name) { return ::LoadCursorFromFileW(WideArg(name)); }

HMMIO WINAPI mmioOpenA(LPSTR name, LPMMIOINFO info, DWORD flags) {
    // MMIO_PARSE and MMIO_GETTEMP write a name back into `name`: those stay ANSI (the game uses neither).
    if (flags & (MMIO_PARSE | MMIO_GETTEMP)) return ::mmioOpenA(name, info, flags);
    WideArg wide(name);
    return ::mmioOpenW(wide.Writable(), info, flags);
}

// ---- The registry --------------------------------------------------------------------------------------

LSTATUS WINAPI RegOpenKeyExA(HKEY key, LPCSTR subKey, DWORD options, REGSAM access, PHKEY result) {
    return ::RegOpenKeyExW(key, WideArg(subKey), options, access, result);
}

LSTATUS WINAPI RegCreateKeyExA(HKEY key, LPCSTR subKey, DWORD reserved, LPSTR keyClass, DWORD options, REGSAM access,
                               const LPSECURITY_ATTRIBUTES security, PHKEY result, LPDWORD disposition) {
    WideArg wideClass(keyClass);
    return ::RegCreateKeyExW(key, WideArg(subKey), reserved, wideClass.Writable(), options, access, security, result,
                             disposition);
}

LSTATUS WINAPI RegQueryValueExA(HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD size) {
    const WideArg wideName(name);
    DWORD valueType = 0;
    DWORD wideSize = 0;
    LSTATUS status = ::RegQueryValueExW(key, wideName, reserved, &valueType, nullptr, &wideSize);
    if (status != ERROR_SUCCESS) return status;
    if (!IsStringType(valueType)) return ::RegQueryValueExW(key, wideName, reserved, type, data, size);
    // A string: read as UTF-16 and given as UTF-8, its size in bytes (the stored terminators kept).
    std::wstring wide(wideSize / sizeof(wchar_t) + 1, L'\0');
    DWORD read = static_cast<DWORD>(wide.size() * sizeof(wchar_t));
    status = ::RegQueryValueExW(key, wideName, reserved, &valueType, reinterpret_cast<LPBYTE>(wide.data()), &read);
    if (status != ERROR_SUCCESS) return status;
    wide.resize(read / sizeof(wchar_t));
    const std::string utf8 = Narrow(wide);
    if (type) *type = valueType;
    if (!size) return data ? ERROR_INVALID_PARAMETER : ERROR_SUCCESS;
    const DWORD needed = static_cast<DWORD>(utf8.size());
    if (!data) {
        *size = needed;
        return ERROR_SUCCESS;
    }
    if (*size < needed) {
        *size = needed;
        return ERROR_MORE_DATA;
    }
    std::memcpy(data, utf8.data(), utf8.size());
    *size = needed;
    return ERROR_SUCCESS;
}

LSTATUS WINAPI RegSetValueExA(HKEY key, LPCSTR name, DWORD reserved, DWORD type, const BYTE* data, DWORD size) {
    const WideArg wideName(name);
    if (!IsStringType(type) || !data) return ::RegSetValueExW(key, wideName, reserved, type, data, size);
    const std::wstring wide = Wide(std::string_view(reinterpret_cast<const char*>(data), size));
    return ::RegSetValueExW(key, wideName, reserved, type, reinterpret_cast<const BYTE*>(wide.data()),
                            static_cast<DWORD>(wide.size() * sizeof(wchar_t)));
}

// ---- The window title, the message box, key names, resource strings -----------------------------------

HWND WINAPI CreateWindowExA(DWORD exStyle, LPCSTR className, LPCSTR title, DWORD style, int x, int y, int width,
                            int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID parameter) {
    if (!title || IsAscii(title))
        return ::CreateWindowExA(exStyle, className, title, style, x, y, width, height, parent, menu, instance, parameter);
    // The window stays an ANSI one (its class is, and so its messages until U4); its title is set as UTF-16
    // past the window procedure, which would take it in the ANSI code page.
    const std::wstring wide = Wide(title);
    HWND window = ::CreateWindowExA(exStyle, className, "", style, x, y, width, height, parent, menu, instance, parameter);
    if (window) ::DefWindowProcW(window, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(wide.c_str()));
    return window;
}

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

int WINAPI LoadStringA(HINSTANCE instance, UINT id, LPSTR buffer, int size) {
    if (!buffer || size <= 0) return ::LoadStringA(instance, id, buffer, size);
    const wchar_t* resource = nullptr;
    const int length = ::LoadStringW(instance, id, reinterpret_cast<LPWSTR>(&resource), 0);
    if (length <= 0 || !resource) {
        buffer[0] = '\0';
        return 0;
    }
    return CopyOut(std::wstring_view(resource, static_cast<std::size_t>(length)), buffer, size);
}

} // namespace utf8

} // namespace text
