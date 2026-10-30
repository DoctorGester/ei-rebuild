// The platform layer on Windows (platform/platform.h; docs/notes/platform.md), in its sections' order: every area but
// those of SDL3 (src/platform/sdl; in the harness's builds platform_win32_window.cpp), the sound and the movies. Most
// calls go through game.exe's import slots (gameapi::X), as the game made them: the harness's routes and the image's
// UTF-8 routes are in those slots. A call that never went through a slot calls the system directly.
#include <winsock2.h>   // before anything includes windows.h (and with it winsock.h)

#include "platform/platform.h"

#include "platform/win32/gameapi.h"
#include "platform/win32/platform_win32.h"
#include "lib/text_case.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <utility>

#pragma comment(lib, "ws2_32.lib")   // WSAGetLastError, which game.exe does not import

// ==== UTF-8 and the system's UTF-16 ==================================================================================

// For the wide calls (text::Wide also takes text that is not UTF-8: the UTF-8 routes' section).

namespace platform::win32 {

namespace {

std::wstring Utf16(std::string_view text) {
    if (text.empty()) return {};
    const int size = static_cast<int>(text.size());
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), size, nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(count > 0 ? count : 0), L'\0');
    if (count > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), size, wide.data(), count);
    return wide;
}

std::string Utf8(std::wstring_view wide) {
    if (wide.empty()) return {};
    const int size = static_cast<int>(wide.size());
    const int count = WideCharToMultiByte(CP_UTF8, 0, wide.data(), size, nullptr, 0, nullptr, nullptr);
    std::string text(static_cast<std::size_t>(count > 0 ? count : 0), '\0');
    if (count > 0) WideCharToMultiByte(CP_UTF8, 0, wide.data(), size, text.data(), count, nullptr, nullptr);
    return text;
}

} // namespace

} // namespace platform::win32

// ==== Clocks =========================================================================================================

// The C library's local time (the clocks themselves are the window layer's: platform_sdl.cpp,
// platform_win32_window.cpp).

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

void DebugOutput(const char* text) { gameapi::OutputDebugStringA(text); }

} // namespace platform

// ==== Paths ==========================================================================================================

// ExePath and CurrentFolder go through game.exe's import slots, which the image routes to the UTF-8 calls;
// ProgramPath and UserDataFolder call the system directly.

namespace platform {

namespace {

// Room for any path the calls return (the long-path limit, in UTF-8 bytes).
constexpr DWORD kPathBufferSize = 0x8000;

// `path` cut to the length a call returned: none when the call failed or the path did not fit.
std::string Fitted(std::string path, DWORD length) {
    path.resize(length < kPathBufferSize ? length : 0);
    return path;
}

} // namespace

std::string ExePath() {
    std::string path(kPathBufferSize, '\0');
    const DWORD length = gameapi::GetModuleFileNameA(nullptr, path.data(), kPathBufferSize);
    return Fitted(std::move(path), length);
}

std::string CurrentFolder() {
    std::string folder(kPathBufferSize, '\0');
    const DWORD length = gameapi::GetCurrentDirectoryA(kPathBufferSize, folder.data());
    return Fitted(std::move(folder), length);
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
    return win32::Utf8(path);
}

bool SetCurrentFolder(const std::string& path, std::uint32_t* error) {
    if (SetCurrentDirectoryW(win32::Utf16(path).c_str())) return true;
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
    return win32::Utf8(folder);
}

} // namespace platform

// ==== The machine ====================================================================================================

// Through game.exe's import slots: the harness fixes the desktop settings the game reads.

namespace platform {

std::uint32_t PageSize() {
    SYSTEM_INFO info;
    gameapi::GetSystemInfo(&info);
    return info.dwPageSize;
}

bool FontSmoothing() {
    BOOL smoothing = FALSE;
    if (!gameapi::SystemParametersInfoA(SPI_GETFONTSMOOTHING, 0, &smoothing, 0)) return true;
    return smoothing != FALSE;
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

// The folder and file calls and the volume label go through game.exe's import slots (the harness redirects folder
// creation and the saves; the image routes them to the UTF-8 calls); the free space was never a slot (the original
// looked GetDiskFreeSpaceExA up by name) and calls the system's wide function; IsFile and FolderEntries call the
// UTF-8 versions (text::utf8) directly.

namespace platform {

bool CreateFolder(const char* path) { return gameapi::CreateDirectoryA(path, nullptr) != FALSE; }

bool RemoveFile(const char* path) { return gameapi::DeleteFileA(path) != FALSE; }

bool RenameFile(const char* from, const char* to) { return gameapi::MoveFileA(from, to) != FALSE; }

bool CopyFileOver(const char* from, const char* to) { return gameapi::CopyFileA(from, to, FALSE) != FALSE; }

bool RemoveFolder(const char* path) { return gameapi::RemoveDirectoryA(path) != FALSE; }

bool PathExists(const std::string& path) {
    return GetFileAttributesW(win32::Utf16(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool IsFile(const std::string& path) {
    const DWORD attributes = text::utf8::GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

WholeFile ReadWholeFile(const std::string& path, std::string* bytes, std::uint32_t* error) {
    const std::wstring wide = win32::Utf16(path);
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
    const std::wstring file = win32::Utf16(path);
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
    WIN32_FIND_DATAA found;
    const HANDLE find = text::utf8::FindFirstFileA(pattern.c_str(), &found);
    if (find == INVALID_HANDLE_VALUE) return entries;
    do {
        entries.push_back({found.cFileName, (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0});
    } while (text::utf8::FindNextFileA(find, &found));
    ::FindClose(find);
    return entries;
}

double FreeDiskSpace(const char* path) {
    ULARGE_INTEGER available;
    if (!GetDiskFreeSpaceExW(win32::Utf16(path).c_str(), &available, nullptr, nullptr)) return 0.0;
    return static_cast<double>(static_cast<std::int64_t>(available.QuadPart));
}

std::string VolumeLabel(const char* root) {
    char label[MAX_PATH + 1] = {};
    if (!gameapi::GetVolumeInformationA(root, label, sizeof(label), nullptr, nullptr, nullptr, nullptr, 0)) return {};
    return label;
}

} // namespace platform

// ==== Files ==========================================================================================================

// Every call goes through game.exe's import slots, as the game made them.

namespace platform {

namespace {

// Every open's sharing: other readers and writers may hold the file too.
constexpr DWORD kShareReadWrite = FILE_SHARE_READ | FILE_SHARE_WRITE;

std::string ErrorText(unsigned long code, std::string_view path) {
    return "file error: OS error " + std::to_string(code) + ", file '" + std::string(path) + "'";
}

[[noreturn]] void ThrowLastError(std::string_view path) { throw FileError(gameapi::GetLastError(), path); }

// A closed file's calls fail as on INVALID_HANDLE_VALUE (MFC's hFileNull).
HANDLE Win32(void* handle) { return handle ? handle : INVALID_HANDLE_VALUE; }

// `path` opened through the CreateFileA slot; null when it does not open.
HANDLE OpenHandle(const std::string& path, DWORD access, DWORD disposition) {
    const HANDLE handle =
        gameapi::CreateFileA(path.c_str(), access, kShareReadWrite, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    return handle == INVALID_HANDLE_VALUE ? nullptr : handle;
}

} // namespace

FileError::FileError(unsigned long code, std::string_view path) : std::runtime_error(ErrorText(code, path)) {}

// ---- File -----------------------------------------------------------------------------------------

File::~File() { Close(); }

bool File::Open(std::string_view filePath, FileMode mode) {
    Close();
    path = filePath;
    switch (mode) {
    case FileMode::Read: handle = OpenHandle(path, GENERIC_READ, OPEN_EXISTING); break;
    case FileMode::Create: handle = OpenHandle(path, GENERIC_WRITE, CREATE_ALWAYS); break;
    case FileMode::WriteExisting: handle = OpenHandle(path, GENERIC_WRITE, OPEN_EXISTING); break;
    }
    return IsOpen();
}

void File::Close() {
    if (IsOpen()) gameapi::CloseHandle(handle);
    handle = nullptr;
}

unsigned File::Read(void* buffer, unsigned count) {
    if (count == 0) return 0;
    DWORD read;
    if (!gameapi::ReadFile(Win32(handle), buffer, count, &read, nullptr)) ThrowLastError(path);
    return read;
}

void File::Write(const void* buffer, unsigned count) {
    if (count == 0) return;
    DWORD written;
    if (!gameapi::WriteFile(Win32(handle), buffer, count, &written, nullptr)) ThrowLastError(path);
    if (written != count) throw FileError(ERROR_DISK_FULL, path);
}

void File::WriteLine(std::string_view line) {
    std::string text(line);
    text += "\r\n";
    Write(text.data(), static_cast<unsigned>(text.size()));
}

long File::Seek(long offset, SeekFrom from) {
    const DWORD method = from == SeekFrom::Begin ? FILE_BEGIN : from == SeekFrom::Current ? FILE_CURRENT : FILE_END;
    const DWORD position = gameapi::SetFilePointer(Win32(handle), offset, nullptr, method);
    if (position == INVALID_SET_FILE_POINTER) ThrowLastError(path);
    return static_cast<long>(position);
}

std::uint32_t File::Position() const {
    const DWORD position = gameapi::SetFilePointer(Win32(handle), 0, nullptr, FILE_CURRENT);
    if (position == INVALID_SET_FILE_POINTER) ThrowLastError(path);
    return position;
}

std::uint32_t File::Size() const {
    const DWORD size = gameapi::GetFileSize(Win32(handle), nullptr);
    if (size == INVALID_FILE_SIZE) ThrowLastError(path);
    return size;
}

bool File::ReadAt(long offset, void* buffer, unsigned count) {
    DWORD read = 0;
    return gameapi::SetFilePointer(Win32(handle), offset, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
           gameapi::ReadFile(Win32(handle), buffer, count, &read, nullptr) && read == count;
}

// ---- MappedFile -----------------------------------------------------------------------------------

MappedFile::~MappedFile() { Close(); }

bool MappedFile::Open(std::string_view path) {
    Close();
    file = OpenHandle(std::string(path), GENERIC_READ, OPEN_EXISTING);
    if (!file) return false;
    size = gameapi::GetFileSize(file, nullptr);
    if (size != INVALID_FILE_SIZE) mapping = gameapi::CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping) view = gameapi::MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (view) return true;
    Close();
    return false;
}

void MappedFile::Close() {
    if (view) gameapi::UnmapViewOfFile(view);
    view = nullptr;
    if (mapping) gameapi::CloseHandle(mapping);
    mapping = nullptr;
    if (file) gameapi::CloseHandle(file);
    file = nullptr;
    size = 0;
}

// ---- FileFind -------------------------------------------------------------------------------------

struct FileFind::Search {
    HANDLE handle = INVALID_HANDLE_VALUE;
    WIN32_FIND_DATAA next{};   // the entry read ahead
};

FileFind::FileFind() = default;
FileFind::~FileFind() { Close(); }

void FileFind::Close() {
    if (search) gameapi::FindClose(search->handle);
    search.reset();
    hasFound = false;
}

bool FileFind::FindFile(std::string_view pattern) {
    Close();
    const std::string text(pattern);
    auto started = std::make_unique<Search>();
    started->handle = gameapi::FindFirstFileA(text.c_str(), &started->next);
    if (started->handle == INVALID_HANDLE_VALUE) return false;
    search = std::move(started);
    // The root: the pattern made absolute (the current folder for an empty one), up to its last separator.
    char full[MAX_PATH];
    const DWORD length = text.empty() ? gameapi::GetCurrentDirectoryA(MAX_PATH, full)
                                      : gameapi::GetFullPathNameA(text.c_str(), MAX_PATH, full, nullptr);
    if (length == 0 || length >= MAX_PATH) {
        Close();
        gameapi::SetLastError(ERROR_INVALID_NAME);
        return false;
    }
    root.assign(full, length);
    if (const std::size_t cut = root.find_last_of("\\/"); cut != std::string::npos) root.resize(cut);
    return true;
}

bool FileFind::FindNext() {
    if (!search) return false;
    hasFound = true;
    name = search->next.cFileName;
    attributes = search->next.dwFileAttributes;
    size = search->next.nFileSizeLow;
    return gameapi::FindNextFileA(search->handle, &search->next) != 0;
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

bool FileExists(const std::string& path) { return gameapi::GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

} // namespace platform

// ==== The .ini files =================================================================================================

// The profile calls through game.exe's import slots.

namespace platform {

std::vector<std::string> IniSectionEntries(const char* section, const char* path) {
    std::vector<std::string> entries;
    char buffer[0x5000];
    gameapi::GetPrivateProfileSectionA(section, buffer, sizeof(buffer), path);
    // Each entry ends with a zero byte, the last one with a second.
    const char* start = buffer;
    while (start < buffer + sizeof(buffer) && *start != '\0') {
        entries.emplace_back(start);
        start += entries.back().size() + 1;
    }
    return entries;
}

std::string IniString(const char* section, const char* key, const char* defaultValue, const char* path) {
    char buffer[0x400] = {};
    gameapi::GetPrivateProfileStringA(section, key, defaultValue, buffer, 0x3FF, path);
    return std::string(buffer);
}

int IniInt(const char* section, const char* key, int defaultValue, const char* path) {
    return static_cast<int>(GetPrivateProfileIntA(section, key, static_cast<INT>(defaultValue), path));
}

} // namespace platform

// ==== The other game window ==========================================================================================

// A game that runs already (its window's class and title) is brought to the front: through game.exe's import slots
// (the harness answers FindWindowA). The program instance is asked of the system directly.

namespace platform {

NativeWindow FindClassWindow(const char* className, const char* title) { return gameapi::FindWindowA(className, title); }

std::uint32_t WindowProcess(NativeWindow window) {
    DWORD process = 0;
    gameapi::GetWindowThreadProcessId(static_cast<HWND>(window), &process);
    return process;
}

void BringToFront(NativeWindow window) {
    HWND other = static_cast<HWND>(window);
    const DWORD foregroundThread = gameapi::GetWindowThreadProcessId(gameapi::GetForegroundWindow(), nullptr);
    const DWORD otherThread = gameapi::GetWindowThreadProcessId(other, nullptr);
    if (otherThread == foregroundThread && !gameapi::IsIconic(other)) return;
    HWND popup = gameapi::GetLastActivePopup(other);
    if (gameapi::IsIconic(popup)) gameapi::ShowWindow(popup, SW_RESTORE);
    gameapi::BringWindowToTop(popup);
    gameapi::SetForegroundWindow(popup);
}

NativeInstance ProgramInstance() { return GetModuleHandleA(nullptr); }

} // namespace platform

// ==== The keyboard ===================================================================================================

// Through game.exe's import slots; the image routes the key names to the UTF-8 call.

namespace platform {

bool KeyName(int scan, char* name, int size) { return gameapi::GetKeyNameTextA(scan << 16, name, size) != 0; }

// Directly: the game's editor called it so (game.exe has no slot of it).
bool IsLetter(std::uint32_t character) { return character <= 0xFFFF && IsCharAlphaW(static_cast<WCHAR>(character)); }

void ActivateKeyboardLayout(std::uint32_t language, std::uint32_t fallback) {
    constexpr int kMaxLayouts = 16;
    HKL layouts[kMaxLayouts];
    const int count = gameapi::GetKeyboardLayoutList(kMaxLayouts, layouts);
    int fallbackIndex = -1;
    int found = -1;
    HKL chosen = gameapi::GetKeyboardLayout(0);
    for (int i = 0; i < count; ++i) {
        gameapi::ActivateKeyboardLayout(layouts[i], 0);
        char name[KL_NAMELENGTH + 3];
        if (!gameapi::GetKeyboardLayoutNameA(name)) continue;
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
    gameapi::ActivateKeyboardLayout(chosen, 0);
}

} // namespace platform

// ==== Sockets ========================================================================================================

// Winsock 1.1 through game.exe's import slots (WSOCK32), which the harness routes (binds and datagrams kept on
// 127.0.0.1, a pair's network: tests/verify/harness_net.h).

namespace platform::net {

namespace {

static_assert(sizeof(Address) == sizeof(sockaddr_in) && sizeof(Socket) == sizeof(SOCKET));
static_assert(SocketGroup::kCapacity == FD_SETSIZE);

constexpr int kAddressLength = sizeof(sockaddr_in);

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
    return gameapi::WSAStartup(0x101, &data) == 0;
}

void Stop() { gameapi::WSACleanup(); }

Socket Open(Kind kind) {
    if (kind == Kind::Udp) return gameapi::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    return gameapi::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

void Close(Socket socket) { gameapi::closesocket(socket); }

bool Bind(Socket socket, const Address& address) { return gameapi::bind(socket, &address, kAddressLength) == 0; }

void SetNonBlocking(Socket socket, bool on) {
    std::uint32_t value = on ? 1 : 0;
    gameapi::ioctlsocket(socket, static_cast<long>(FIONBIO), &value);
}

void AllowBroadcast(Socket socket) {
    const std::uint32_t on = 1;
    gameapi::setsockopt(socket, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
}

bool Connect(Socket socket, const Address& address) {
    return gameapi::connect(socket, &address, kAddressLength) == 0;
}

int SendTo(Socket socket, const void* data, int length, const Address& to) {
    return gameapi::sendto(socket, data, length, 0, &to, kAddressLength);
}

int ReceiveFrom(Socket socket, void* data, int length, Address* from) {
    int fromLength = kAddressLength;
    return gameapi::recvfrom(socket, data, length, 0, from, &fromLength);
}

int Send(Socket socket, const void* data, int length) { return gameapi::send(socket, data, length, 0); }

int Receive(Socket socket, void* data, int length) { return gameapi::recv(socket, data, length, 0); }

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
    const int ready = gameapi::select(0, ToNative(read, &sets[0]), ToNative(write, &sets[1]),
                                      ToNative(failed, &sets[2]), &timeout);
    FromNative(sets[0], read);
    FromNative(sets[1], write);
    FromNative(sets[2], failed);
    return ready;
}

bool HostName(char* name, int size) { return gameapi::gethostname(name, size) == 0; }

bool LookUp(const char* name, std::uint32_t* ip) {
    const auto* host = static_cast<const hostent*>(gameapi::gethostbyname(name));
    if (!host || !host->h_addr_list[0]) return false;
    std::memcpy(ip, host->h_addr_list[0], sizeof(*ip));
    return true;
}

std::string HostNameAt(const void* address, int length) {
    const auto* host = static_cast<const hostent*>(gameapi::gethostbyaddr(address, length, AF_INET));
    if (!host || !host->h_name) return {};
    return host->h_name;
}

std::uint32_t ParseIpv4(const char* text) { return gameapi::inet_addr(text); }

std::uint16_t ToNetwork(std::uint16_t value) { return gameapi::htons(value); }

std::uint16_t FromNetwork(std::uint16_t value) { return gameapi::ntohs(value); }

} // namespace platform::net

// ==== Text at the system boundary in UTF-8 ===========================================================================

// The Windows calls that take or give text, in UTF-8: see platform_win32.h.

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
    TEXT_ROUTE(GetDiskFreeSpaceA), TEXT_ROUTE(GetVolumeInformationA),
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

} // namespace text
