// The platform layer on Windows (platform/platform.h; docs/notes/platform.md), in its sections' order: every area but
// those of SDL3 (src/platform/sdl), the sound and the movies, and the calls a harness run answers (game_calls.cpp).
// Text goes to the system's wide calls (platform_win32.h).
#define _WINSOCK_DEPRECATED_NO_WARNINGS   // gethostbyname, gethostbyaddr, inet_addr: the look-ups the game made
#include <winsock2.h>   // before anything includes windows.h (and with it winsock.h)

#include "platform/platform.h"

#include "platform/win32/platform_win32.h"

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <utility>

#pragma comment(lib, "ws2_32.lib")

// ==== UTF-8 and the system's UTF-16 ==================================================================================

namespace text {

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

} // namespace text

// ==== Clocks =========================================================================================================

// The C library's local time (the clocks themselves are platform_sdl.cpp's).

namespace platform {

std::string FormatLocalTime(std::int64_t seconds, const char* format) {
    char text[128];
    std::tm local{};
    const std::time_t time = seconds;
    if (localtime_s(&local, &time) != 0 || std::strftime(text, sizeof(text), format, &local) == 0) text[0] = '\0';
    return text;
}

} // namespace platform

// ==== Output for a developer =========================================================================================

namespace platform {

void DebugOutput(const char* text) { ::OutputDebugStringA(text); }

} // namespace platform

// ==== Paths ==========================================================================================================

namespace platform {

namespace {

// The current folder (none when the call fails).
std::wstring CurrentFolderWide() {
    const DWORD needed = GetCurrentDirectoryW(0, nullptr);
    std::wstring folder(needed, L'\0');
    const DWORD length = needed ? GetCurrentDirectoryW(needed, folder.data()) : 0;
    folder.resize(length < needed ? length : 0);
    return folder;
}

// `path` made absolute (none when the call fails).
std::wstring FullPath(const std::wstring& path) {
    const DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    std::wstring full(needed, L'\0');
    const DWORD length = needed ? GetFullPathNameW(path.c_str(), needed, full.data(), nullptr) : 0;
    full.resize(length < needed ? length : 0);
    return full;
}

} // namespace

// On Windows the program's path as the game names it is ProgramPath's.
std::string ExePath() { return ProgramPath(); }

std::string CurrentFolder() { return text::Narrow(CurrentFolderWide()); }

std::string ArgumentsAfterSeparator(int, char**) {
    const char* p = ::GetCommandLineA();
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        const char* token = p;
        bool quoted = false;
        for (; *p && (quoted || (*p != ' ' && *p != '\t')); ++p)
            if (*p == '"') quoted = !quoted;
        if (p - token == 2 && token[0] == '-' && token[1] == '-') {
            while (*p == ' ' || *p == '\t') ++p;
            return p;
        }
    }
    return {};
}

std::string ProgramPath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length < path.size()) {
            path.resize(length);
            break;
        }
        path.resize(path.size() * 2);
    }
    return text::Narrow(path);
}

bool SetCurrentFolder(const std::string& path, std::uint32_t* error) {
    if (SetCurrentDirectoryW(text::Wide(path).c_str())) return true;
    *error = GetLastError();
    return false;
}

std::string UserDataFolder() {
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (size == 0) return {};
    std::wstring folder(size, L'\0');
    folder.resize(GetEnvironmentVariableW(L"LOCALAPPDATA", folder.data(), size));
    if (folder.empty()) return {};
    if (folder.back() != L'\\') folder += L'\\';
    return text::Narrow(folder);
}

} // namespace platform

// ==== The machine ====================================================================================================

// FontSmoothing is game_calls.cpp's (the harness fixes it off).

namespace platform {

std::uint32_t PageSize() {
    SYSTEM_INFO info;
    ::GetSystemInfo(&info);
    return info.dwPageSize;
}

void PrimaryDesktopSize(int* width, int* height) {
    MONITORINFOEXW monitor{};
    monitor.cbSize = sizeof(monitor);
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor) &&
        EnumDisplaySettingsW(monitor.szDevice, ENUM_CURRENT_SETTINGS, &mode)) {
        *width = static_cast<int>(mode.dmPelsWidth);
        *height = static_cast<int>(mode.dmPelsHeight);
    }
}

std::string EnvironmentVariable(const char* name) {
    const DWORD size = GetEnvironmentVariableA(name, nullptr, 0);
    if (size == 0) return {};
    std::string value(size, '\0');
    value.resize(GetEnvironmentVariableA(name, value.data(), size));
    return value;
}

void EndProcess(std::uint32_t exitCode) {
    TerminateProcess(GetCurrentProcess(), exitCode);
    for (;;) ::Sleep(INFINITE);
}

} // namespace platform

// ==== Folders, drives and whole files ================================================================================

// The game's folder and file calls and the volume label are game_calls.cpp's (the harness redirects the saves and
// the install's files); these call the system directly.

namespace platform {

bool PathExists(const std::string& path) {
    return GetFileAttributesW(text::Wide(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool IsFile(const std::string& path) {
    const DWORD attributes = GetFileAttributesW(text::Wide(path).c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

WholeFile ReadWholeFile(const std::string& path, std::string* bytes, std::uint32_t* error) {
    const std::wstring wide = text::Wide(path);
    HANDLE handle = CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        *error = GetLastError();
        return *error == ERROR_FILE_NOT_FOUND || *error == ERROR_PATH_NOT_FOUND ? WholeFile::missing : WholeFile::failed;
    }
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(handle, &size) && size.QuadPart < (1 << 24);
    if (ok) {
        bytes->resize(static_cast<std::size_t>(size.QuadPart));
        DWORD read = 0;
        ok = ::ReadFile(handle, bytes->data(), static_cast<DWORD>(bytes->size()), &read, nullptr) && read == bytes->size();
    }
    *error = ok ? 0 : GetLastError();
    CloseHandle(handle);
    return ok ? WholeFile::read : WholeFile::failed;
}

bool WriteWholeFile(const std::string& path, const std::string& bytes, std::uint32_t* error) {
    const std::wstring file = text::Wide(path);
    const std::wstring temporary = file + L".tmp";
    CreateDirectoryW(file.substr(0, file.find_last_of(L'\\')).c_str(), nullptr);
    HANDLE handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD written = 0;
    bool ok = handle != INVALID_HANDLE_VALUE;
    if (ok) {
        ok = WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
        CloseHandle(handle);
    }
    ok = ok && MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) {
        *error = GetLastError();
        DeleteFileW(temporary.c_str());
    }
    return ok;
}

std::vector<FolderEntry> FolderEntries(const std::string& pattern) {
    std::vector<FolderEntry> entries;
    WIN32_FIND_DATAW found;
    const HANDLE find = FindFirstFileW(text::Wide(pattern).c_str(), &found);
    if (find == INVALID_HANDLE_VALUE) return entries;
    do {
        entries.push_back({text::Narrow(found.cFileName), (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0});
    } while (FindNextFileW(find, &found));
    ::FindClose(find);
    return entries;
}

double FreeDiskSpace(const char* path) {
    ULARGE_INTEGER available;
    if (!GetDiskFreeSpaceExW(text::Wide(path).c_str(), &available, nullptr, nullptr)) return 0.0;
    return static_cast<double>(static_cast<std::int64_t>(available.QuadPart));
}

} // namespace platform

// ==== Files ==========================================================================================================

// Opening, writing and the folder search's start through game_calls.cpp's calls (platform_win32.h), which the
// harness answers in its builds.

namespace platform {

namespace {

std::string ErrorText(unsigned long code, std::string_view path) {
    return "file error: OS error " + std::to_string(code) + ", file '" + std::string(path) + "'";
}

[[noreturn]] void ThrowLastError(std::string_view path) { throw FileError(::GetLastError(), path); }

// A closed file's calls fail as on INVALID_HANDLE_VALUE (MFC's hFileNull).
HANDLE Win32(void* handle) { return handle ? handle : INVALID_HANDLE_VALUE; }

} // namespace

FileError::FileError(unsigned long code, std::string_view path) : std::runtime_error(ErrorText(code, path)) {}

// ---- File -----------------------------------------------------------------------------------------

File::~File() { Close(); }

bool File::Open(std::string_view filePath, FileMode mode) {
    Close();
    path = filePath;
    switch (mode) {
    case FileMode::Read: handle = win32::OpenFile(path, GENERIC_READ, OPEN_EXISTING); break;
    case FileMode::Create: handle = win32::OpenFile(path, GENERIC_WRITE, CREATE_ALWAYS); break;
    case FileMode::WriteExisting: handle = win32::OpenFile(path, GENERIC_WRITE, OPEN_EXISTING); break;
    }
    return IsOpen();
}

void File::Close() {
    if (IsOpen()) ::CloseHandle(handle);
    handle = nullptr;
}

unsigned File::Read(void* buffer, unsigned count) {
    if (count == 0) return 0;
    DWORD read;
    if (!::ReadFile(Win32(handle), buffer, count, &read, nullptr)) ThrowLastError(path);
    return read;
}

void File::Write(const void* buffer, unsigned count) {
    if (count == 0) return;
    DWORD written;
    if (!win32::WriteFile(Win32(handle), buffer, count, &written)) ThrowLastError(path);
    if (written != count) throw FileError(ERROR_DISK_FULL, path);
}

void File::WriteLine(std::string_view line) {
    std::string text(line);
    text += "\r\n";
    Write(text.data(), static_cast<unsigned>(text.size()));
}

long File::Seek(long offset, SeekFrom from) {
    const DWORD method = from == SeekFrom::Begin ? FILE_BEGIN : from == SeekFrom::Current ? FILE_CURRENT : FILE_END;
    const DWORD position = ::SetFilePointer(Win32(handle), offset, nullptr, method);
    if (position == INVALID_SET_FILE_POINTER) ThrowLastError(path);
    return static_cast<long>(position);
}

std::uint32_t File::Position() const {
    const DWORD position = ::SetFilePointer(Win32(handle), 0, nullptr, FILE_CURRENT);
    if (position == INVALID_SET_FILE_POINTER) ThrowLastError(path);
    return position;
}

std::uint32_t File::Size() const {
    const DWORD size = ::GetFileSize(Win32(handle), nullptr);
    if (size == INVALID_FILE_SIZE) ThrowLastError(path);
    return size;
}

bool File::ReadAt(long offset, void* buffer, unsigned count) {
    DWORD read = 0;
    return ::SetFilePointer(Win32(handle), offset, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
           ::ReadFile(Win32(handle), buffer, count, &read, nullptr) && read == count;
}

// ---- MappedFile -----------------------------------------------------------------------------------

MappedFile::~MappedFile() { Close(); }

bool MappedFile::Open(std::string_view path) {
    Close();
    file = win32::OpenFile(std::string(path), GENERIC_READ, OPEN_EXISTING);
    if (!file) return false;
    size = ::GetFileSize(file, nullptr);
    if (size != INVALID_FILE_SIZE) mapping = ::CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping) view = ::MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (view) return true;
    Close();
    return false;
}

void MappedFile::Close() {
    if (view) ::UnmapViewOfFile(view);
    view = nullptr;
    if (mapping) ::CloseHandle(mapping);
    mapping = nullptr;
    if (file) ::CloseHandle(file);
    file = nullptr;
    size = 0;
}

// ---- FileFind -------------------------------------------------------------------------------------

struct FileFind::Search {
    HANDLE handle = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAW next{};   // the entry read ahead
};

FileFind::FileFind() = default;
FileFind::~FileFind() { Close(); }

void FileFind::Close() {
    if (search) ::FindClose(search->handle);
    search.reset();
    hasFound = false;
}

bool FileFind::FindFile(std::string_view pattern) {
    Close();
    const std::string text(pattern);
    auto started = std::make_unique<Search>();
    started->handle = win32::FindFirst(text, &started->next);
    if (started->handle == INVALID_HANDLE_VALUE) return false;
    search = std::move(started);
    // The root: the pattern made absolute (the current folder for an empty one), up to its last separator; a path of
    // MAX_PATH bytes or more fails.
    root = text::Narrow(text.empty() ? CurrentFolderWide() : FullPath(text::Wide(text)));
    if (root.empty() || root.size() >= MAX_PATH) {
        Close();
        ::SetLastError(ERROR_INVALID_NAME);
        return false;
    }
    if (const std::size_t cut = root.find_last_of("\\/"); cut != std::string::npos) root.resize(cut);
    return true;
}

bool FileFind::FindNext() {
    if (!search) return false;
    hasFound = true;
    name = text::Narrow(search->next.cFileName);
    attributes = search->next.dwFileAttributes;
    size = search->next.nFileSizeLow;
    return FindNextFileW(search->handle, &search->next) != 0;
}

std::string FileFind::GetFileName() const { return hasFound ? name : std::string(); }

std::string FileFind::GetFilePath() const {
    std::string path = root;
    if (path.empty() || (path.back() != '\\' && path.back() != '/')) path += '\\';
    return path + GetFileName();
}

std::string FileFind::GetTitle() const {
    const std::string path = GetFilePath();
    char title[MAX_PATH] = {};
    _splitpath_s(path.c_str(), nullptr, 0, nullptr, 0, title, MAX_PATH, nullptr, 0);
    return title;
}

unsigned long FileFind::GetLength() const { return hasFound ? size : 0; }

bool FileFind::IsDirectory() const { return hasFound && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0; }

bool FileFind::IsDots() const { return IsDirectory() && (name == "." || name == ".."); }

// ---- Whole files and _access ---------------------------------------------------------------------

bool ReadFile(const std::string& path, std::string* bytes) {
    bytes->clear();
    File file;
    if (!file.Open(path, FileMode::Read)) return false;
    bytes->resize(file.Size());
    if (file.ReadAt(0, bytes->data(), static_cast<unsigned>(bytes->size()))) return true;
    bytes->clear();
    return false;
}

} // namespace platform

// ==== The .ini files =================================================================================================

// The section and string reads are game_calls.cpp's (the harness reads its overlay's copies).

namespace platform {

std::vector<std::string> IniSectionEntries(const char* section, const char* path) {
    std::vector<std::string> entries;
    char buffer[0x5000];
    win32::IniSection(section, buffer, sizeof(buffer), path);
    // Each entry ends with a zero byte, the last one with a second.
    const char* start = buffer;
    while (start < buffer + sizeof(buffer) && *start != '\0') {
        entries.emplace_back(start);
        start += entries.back().size() + 1;
    }
    return entries;
}

int IniInt(const char* section, const char* key, int defaultValue, const char* path) {
    return static_cast<int>(GetPrivateProfileIntA(section, key, static_cast<INT>(defaultValue), path));
}

} // namespace platform

// ==== The other game window ==========================================================================================

// A game that runs already (its window's class and title: FindClassWindow, game_calls.cpp's, which the harness
// answers) is brought to the front.

namespace platform {

std::uint32_t WindowProcess(NativeWindow window) {
    DWORD process = 0;
    ::GetWindowThreadProcessId(static_cast<HWND>(window), &process);
    return process;
}

void BringToFront(NativeWindow window) {
    HWND other = static_cast<HWND>(window);
    const DWORD foregroundThread = ::GetWindowThreadProcessId(::GetForegroundWindow(), nullptr);
    const DWORD otherThread = ::GetWindowThreadProcessId(other, nullptr);
    if (otherThread == foregroundThread && !::IsIconic(other)) return;
    HWND popup = ::GetLastActivePopup(other);
    if (::IsIconic(popup)) ::ShowWindow(popup, SW_RESTORE);
    ::BringWindowToTop(popup);
    ::SetForegroundWindow(popup);
}

NativeInstance ProgramInstance() { return GetModuleHandleA(nullptr); }

} // namespace platform

// ==== The keyboard ===================================================================================================

namespace platform {

// The name in UTF-8, cut at a character's start to fit `size` bytes with the terminator.
bool KeyName(int scan, char* name, int size) {
    if (!name || size <= 0) return false;
    wchar_t wide[64] = {};
    const int length = GetKeyNameTextW(scan << 16, wide, 64);
    const std::string utf8 = text::Narrow(std::wstring_view(wide, static_cast<std::size_t>(length > 0 ? length : 0)));
    std::size_t n = utf8.size();
    if (n >= static_cast<std::size_t>(size)) {
        n = static_cast<std::size_t>(size) - 1;
        while (n > 0 && (static_cast<unsigned char>(utf8[n]) & 0xC0) == 0x80) --n;
    }
    std::memcpy(name, utf8.data(), n);
    name[n] = '\0';
    return n != 0;
}

// What Windows' layout maps the scan code to (the key messages' code).
int LayoutKey(int scan) {
    const UINT code = static_cast<UINT>(scan & 0xFF) | ((scan & 0x100) ? 0xE000u : 0u);
    return static_cast<int>(MapVirtualKeyExW(code, MAPVK_VSC_TO_VK_EX, GetKeyboardLayout(0)));
}

// As LayoutKey: what the layout types with Ctrl held (flag 4: a dead key's state left as it was); only a
// control character counts.
int LayoutControlCharacter(int key, int scan) {
    BYTE state[256] = {};
    state[VK_CONTROL] = 0x80;
    wchar_t out[8] = {};
    const int n = ToUnicodeEx(static_cast<UINT>(key), static_cast<UINT>(scan & 0xFF), state, out, 8, 4, GetKeyboardLayout(0));
    return n == 1 && (out[0] < 0x20 || out[0] == 0x7F) ? out[0] : 0;
}

bool IsLetter(std::uint32_t character) { return character <= 0xFFFF && IsCharAlphaW(static_cast<WCHAR>(character)); }

void ActivateKeyboardLayout(std::uint32_t language, std::uint32_t fallback) {
    constexpr int kMaxLayouts = 16;
    HKL layouts[kMaxLayouts];
    const int count = ::GetKeyboardLayoutList(kMaxLayouts, layouts);
    int fallbackIndex = -1;
    int found = -1;
    HKL chosen = ::GetKeyboardLayout(0);
    for (int i = 0; i < count; ++i) {
        ::ActivateKeyboardLayout(layouts[i], 0);
        char name[KL_NAMELENGTH + 3];
        if (!::GetKeyboardLayoutNameA(name)) continue;
        unsigned id = 0;
        sscanf_s(name, "%x", &id);
        id &= 0xFFFF;
        if (id == language) {
            found = i;
            break;
        }
        if (id == fallback) fallbackIndex = i;
    }
    if (found >= 0)
        chosen = layouts[found];
    else if (fallbackIndex >= 0)
        chosen = layouts[fallbackIndex];
    ::ActivateKeyboardLayout(chosen, 0);
}

} // namespace platform

// ==== Sockets ========================================================================================================

// Winsock (ws2_32). Closing, binding, connecting, sending and receiving are game_calls.cpp's (the harness keeps binds
// and datagrams on 127.0.0.1, a pair's network: tests/verify/harness_net.h), and so is select's call.

namespace platform::net {

namespace {

static_assert(sizeof(Address) == sizeof(sockaddr_in) && sizeof(Socket) == sizeof(SOCKET));
static_assert(SocketGroup::kCapacity == FD_SETSIZE);

// `set`'s sockets as an fd_set (or null for none).
fd_set* ToNative(const SocketGroup* set, fd_set* native) {
    if (!set) return nullptr;
    native->fd_count = static_cast<u_int>(set->count);
    for (int i = 0; i < set->count; ++i) native->fd_array[i] = set->sockets[i];
    return native;
}

// The sockets select left in `native` back into `set`.
void FromNative(const fd_set& native, SocketGroup* set) {
    if (!set) return;
    set->count = static_cast<int>(native.fd_count);
    for (int i = 0; i < set->count; ++i) set->sockets[i] = native.fd_array[i];
}

} // namespace

bool Start() {
    WSADATA data;
    return WSAStartup(0x101, &data) == 0;
}

void Stop() { WSACleanup(); }

Socket Open(Kind kind) {
    if (kind == Kind::Udp) return socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    return socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

void SetNonBlocking(Socket socket, bool on) {
    u_long value = on ? 1 : 0;
    ioctlsocket(socket, static_cast<long>(FIONBIO), &value);
}

void AllowBroadcast(Socket socket) {
    const BOOL on = TRUE;
    setsockopt(socket, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), sizeof(on));
}

Error LastError() {
    switch (WSAGetLastError()) {
    case 0: return Error::None;
    case WSAEWOULDBLOCK: return Error::WouldBlock;
    case WSAEADDRINUSE: return Error::AddressInUse;
    case WSAECONNREFUSED: return Error::Refused;
    default: return Error::Other;
    }
}

int Select(SocketGroup* read, SocketGroup* write, SocketGroup* failed, std::int32_t microseconds) {
    fd_set sets[3];
    const timeval timeout = {microseconds / 1000000, microseconds % 1000000};
    const int ready = win32::Select(ToNative(read, &sets[0]), ToNative(write, &sets[1]), ToNative(failed, &sets[2]),
                                    &timeout);
    FromNative(sets[0], read);
    FromNative(sets[1], write);
    FromNative(sets[2], failed);
    return ready;
}

bool HostName(char* name, int size) { return gethostname(name, size) == 0; }

bool LookUp(const char* name, std::uint32_t* ip) {
    const hostent* host = gethostbyname(name);
    if (!host || !host->h_addr_list[0]) return false;
    std::memcpy(ip, host->h_addr_list[0], sizeof(*ip));
    return true;
}

std::string HostNameAt(const void* address, int length) {
    const hostent* host = gethostbyaddr(static_cast<const char*>(address), length, AF_INET);
    if (!host || !host->h_name) return {};
    return host->h_name;
}

std::uint32_t ParseIpv4(const char* text) { return inet_addr(text); }

std::uint16_t ToNetwork(std::uint16_t value) { return htons(value); }

std::uint16_t FromNetwork(std::uint16_t value) { return ntohs(value); }

} // namespace platform::net
