// The platform layer on Windows (platform/platform.h; docs/notes/platform.md), in its sections' order. Most calls go
// through game.exe's import slots (gameapi::X), as the game made them: the harness's routes and the image's UTF-8
// routes are in those slots. A call that never went through a slot calls the system directly.
#include <winsock2.h>   // before anything includes windows.h (and with it winsock.h)

#include "platform/platform.h"

#include "platform/win32/gameapi.h"
#include "platform/win32/platform_win32.h"
#include "lib/text_case.h"

#include <windows.h>

#define DIRECTINPUT_VERSION 0x0700
#include <dinput.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <io.h>
#include <process.h>
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

// Through game.exe's import slots: the harness's clocks replace them.

namespace platform {

std::uint32_t Milliseconds() { return gameapi::timeGetTime(); }

std::uint32_t TickCount() { return gameapi::GetTickCount(); }

void Sleep(std::uint32_t ms) { gameapi::Sleep(ms); }

DateTime LocalTime() {
    SYSTEMTIME local;
    gameapi::GetLocalTime(&local);
    return {local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond};
}

} // namespace platform

// ==== Output for a developer =========================================================================================

namespace platform {

void DebugOutput(const char* text) { gameapi::OutputDebugStringA(text); }

} // namespace platform

// ==== The message box ================================================================================================

// Through game.exe's import slot, which the image routes to the UTF-8 call.

namespace platform {

MessageAnswer ShowMessageBox(void* owner, const char* text, const char* caption, MessageButtons buttons,
                             MessageIcon icon) {
    const UINT style = (buttons == MessageButtons::OkCancel ? MB_OKCANCEL : MB_OK) |
                       (icon == MessageIcon::Error ? MB_ICONHAND : MB_ICONEXCLAMATION);
    const int answer = gameapi::MessageBoxA(static_cast<HWND>(owner), text, caption, style);
    return answer == IDCANCEL ? MessageAnswer::Cancel : MessageAnswer::Ok;
}

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
        ok = ReadFile(handle, bytes->data(), static_cast<DWORD>(bytes->size()), &read, nullptr) && read == bytes->size();
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

std::string ErrorText(unsigned long osError, std::string_view path) {
    return "file error: OS error " + std::to_string(osError) + ", file '" + std::string(path) + "'";
}

[[noreturn]] void ThrowLastError(std::string_view path) { throw FileError(gameapi::GetLastError(), path); }
// The host CRT's Win32 error of its last failed call.
[[noreturn]] void ThrowCrtError(std::string_view path) { throw FileError(static_cast<unsigned long>(_doserrno), path); }

// A closed file's calls fail as on INVALID_HANDLE_VALUE (MFC's hFileNull).
HANDLE Win32(void* handle) { return handle ? handle : INVALID_HANDLE_VALUE; }

} // namespace

unsigned long LastError() { return gameapi::GetLastError(); }

FileError::FileError(unsigned long error, std::string_view path) : std::runtime_error(ErrorText(error, path)), osError(error) {}

// ---- File -----------------------------------------------------------------------------------------

File::File(std::string_view path, unsigned flags) {
    if (!Open(path, flags)) ThrowLastError(path);
}

File::~File() { Close(); }

bool File::Open(std::string_view filePath, unsigned flags) {
    Close();
    path = filePath;
    DWORD access = 0;
    switch (flags & 3) {
    case modeRead: access = GENERIC_READ; break;
    case modeWrite: access = GENERIC_WRITE; break;
    case modeReadWrite: access = GENERIC_READ | GENERIC_WRITE; break;
    default: break;
    }
    DWORD share = 0;   // exclusive (and the undefined values)
    switch (flags & 0x70) {
    case shareDenyWrite: share = FILE_SHARE_READ; break;
    case shareDenyRead: share = FILE_SHARE_WRITE; break;
    case shareDenyNone: share = FILE_SHARE_READ | FILE_SHARE_WRITE; break;
    default: break;
    }
    DWORD disposition = OPEN_EXISTING;
    if (flags & modeCreate) disposition = (flags & modeNoTruncate) ? OPEN_ALWAYS : CREATE_ALWAYS;
    DWORD attributes = FILE_ATTRIBUTE_NORMAL;
    if (flags & osNoBuffer) attributes |= FILE_FLAG_NO_BUFFERING;
    if (flags & osRandomAccess) attributes |= FILE_FLAG_RANDOM_ACCESS;
    const HANDLE opened = gameapi::CreateFileA(path.c_str(), access, share, nullptr, disposition, attributes, nullptr);
    handle = opened == INVALID_HANDLE_VALUE ? nullptr : opened;
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

long File::Seek(long offset, unsigned from) {
    const DWORD position = gameapi::SetFilePointer(Win32(handle), offset, nullptr, from);
    if (position == INVALID_SET_FILE_POINTER) ThrowLastError(path);
    return static_cast<long>(position);
}

unsigned long File::GetPosition() const {
    const DWORD position = gameapi::SetFilePointer(Win32(handle), 0, nullptr, FILE_CURRENT);
    if (position == INVALID_SET_FILE_POINTER) ThrowLastError(path);
    return position;
}

unsigned long File::GetLength() const {
    const DWORD size = gameapi::GetFileSize(Win32(handle), nullptr);
    if (size == INVALID_FILE_SIZE) ThrowLastError(path);
    return size;
}

bool File::ReadAt(long offset, void* buffer, unsigned count) {
    DWORD read = 0;
    return gameapi::SetFilePointer(Win32(handle), offset, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
           gameapi::ReadFile(Win32(handle), buffer, count, &read, nullptr) && read == count;
}

bool File::WriteAt(long offset, const void* buffer, unsigned count) {
    DWORD written = 0;
    return gameapi::SetFilePointer(Win32(handle), offset, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
           gameapi::WriteFile(Win32(handle), buffer, count, &written, nullptr) && written == count;
}

// The game also read the file's information here and did not use it.
void File::SetWriteTimeNow() {
    SYSTEMTIME now;
    gameapi::GetSystemTime(&now);
    BY_HANDLE_FILE_INFORMATION info;
    gameapi::GetFileInformationByHandle(Win32(handle), &info);
    FILETIME time;
    gameapi::SystemTimeToFileTime(&now, &time);
    gameapi::SetFileTime(Win32(handle), nullptr, nullptr, &time);
}

// ---- FileMapping ----------------------------------------------------------------------------------

FileMapping::~FileMapping() { Close(); }

bool FileMapping::Create(const File& file, bool writable) {
    Close();
    mapping = gameapi::CreateFileMappingA(Win32(file.handle), nullptr, writable ? PAGE_READWRITE : PAGE_READONLY, 0, 0,
                                          nullptr);
    return mapping != nullptr;
}

void* FileMapping::MapView(bool writable) {
    view = gameapi::MapViewOfFile(mapping, writable ? FILE_MAP_WRITE : FILE_MAP_READ, 0, 0, 0);
    return view;
}

void FileMapping::Flush() { gameapi::FlushViewOfFile(view, 0); }

void FileMapping::Close() {
    if (view) gameapi::UnmapViewOfFile(view);
    view = nullptr;
    if (mapping) gameapi::CloseHandle(mapping);
    mapping = nullptr;
}

// The sector size stays 0xFFFFFFFF (larger than any page) when GetDiskFreeSpaceA fails.
bool SectorsFitPages(const std::string& path) {
    char root[4] = {};
    path.copy(root, 3);
    const char* rootPath = root;
    if (!text::IsAlpha(root[0]) || root[1] != ':' || root[2] != '\\') rootPath = nullptr;
    DWORD bytesPerSector = 0xFFFFFFFF, sectorsPerCluster, freeClusters, totalClusters;
    gameapi::GetDiskFreeSpaceA(rootPath, &sectorsPerCluster, &bytesPerSector, &freeClusters, &totalClusters);
    SYSTEM_INFO info;
    gameapi::GetSystemInfo(&info);
    return info.dwPageSize >= bytesPerSector && info.dwPageSize % bytesPerSector == 0;
}

// ---- TextFile -------------------------------------------------------------------------------------

TextFile::~TextFile() { Close(); }

bool TextFile::Open(std::string_view filePath, const char* mode) {
    Close();
    path = filePath;
    stream = OpenFile(path, mode);
    return stream != nullptr;
}

void TextFile::Close() {
    if (stream) std::fclose(stream);
    stream = nullptr;
}

// Read 128 characters at a time (fgets is given 129, as MFC's ReadString read into the string's buffer).
bool TextFile::ReadLine(std::string& line) {
    constexpr int kPiece = 128;
    char piece[kPiece + 1];
    line.clear();
    char* result;
    for (;;) {
        result = std::fgets(piece, kPiece + 1, stream);
        if (!result) {
            if (!std::feof(stream)) {
                std::clearerr(stream);
                ThrowCrtError(path);
            }
            break;
        }
        const std::size_t length = std::strlen(piece);
        line.append(piece, length);
        if (length < kPiece || piece[length - 1] == '\n') break;
    }
    if (!line.empty() && line.back() == '\n') line.pop_back();
    return result != nullptr;
}

char* TextFile::ReadString(char* buffer, unsigned size) {
    char* result = std::fgets(buffer, static_cast<int>(size), stream);
    if (!result && !std::feof(stream)) {
        std::clearerr(stream);
        ThrowCrtError(path);
    }
    return result;
}

void TextFile::WriteString(std::string_view text) {
    if (std::fwrite(text.data(), 1, text.size(), stream) != text.size()) ThrowCrtError(path);
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
bool FileFind::IsHidden() const { return hasFound && (attributes & FILE_ATTRIBUTE_HIDDEN) != 0; }
bool FileFind::IsSystem() const { return hasFound && (attributes & FILE_ATTRIBUTE_SYSTEM) != 0; }

bool FileFind::IsDots() const { return IsDirectory() && (name == "." || name == ".."); }

// ---- fopen / _access ------------------------------------------------------------------------------

std::FILE* OpenFile(const std::string& path, const char* mode) {
    const char kind = mode[0];
    if (kind != 'r' && kind != 'w' && kind != 'a') return nullptr;
    const bool update = std::strchr(mode + 1, '+') != nullptr;
    const bool binary = std::strchr(mode + 1, 'b') != nullptr;
    const DWORD access = update ? GENERIC_READ | GENERIC_WRITE : kind == 'r' ? GENERIC_READ : GENERIC_WRITE;
    const DWORD disposition = kind == 'r' ? OPEN_EXISTING : kind == 'w' ? CREATE_ALWAYS : OPEN_ALWAYS;
    const HANDLE handle = gameapi::CreateFileA(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, disposition,
                                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return nullptr;
    // The descriptor takes the translation mode and the append flag, the stream the direction.
    const int fd = _open_osfhandle(reinterpret_cast<std::intptr_t>(handle), (binary ? 0 : _O_TEXT) | (kind == 'a' ? _O_APPEND : 0));
    if (fd == -1) {
        CloseHandle(handle);
        return nullptr;
    }
    const char streamMode[3] = {kind, update ? '+' : '\0', '\0'};
    std::FILE* file = _fdopen(fd, streamMode);
    if (!file) _close(fd);
    return file;
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

// ==== Threads ========================================================================================================

// Through game.exe's import slots, as the game called them.

namespace platform {

bool Thread::Start(ThreadProcedure procedure, void* parameter, int priority) {
    Close();
    unsigned id = 0;
    HANDLE thread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, procedure, parameter, CREATE_SUSPENDED, &id));
    if (!thread) return false;
    gameapi::SetThreadPriority(thread, priority);
    gameapi::ResumeThread(thread);
    handle = thread;
    return true;
}

void Thread::Close() {
    if (handle) gameapi::CloseHandle(static_cast<HANDLE>(handle));
    handle = nullptr;
}

void* Thread::Release() {
    void* released = handle;
    handle = nullptr;
    return released;
}

void Signal::Create() {
    Close();
    handle = gameapi::CreateEventA(nullptr, FALSE, FALSE, nullptr);
}

void Signal::Set() { gameapi::SetEvent(static_cast<HANDLE>(handle)); }

void Signal::Wait() { gameapi::WaitForSingleObject(static_cast<HANDLE>(handle), INFINITE); }

void Signal::Close() {
    if (handle) gameapi::CloseHandle(static_cast<HANDLE>(handle));
    handle = nullptr;
}

} // namespace platform

// ==== The window and its events ======================================================================================

// Through game.exe's import slots: the harness hooks the window's creation and showing and the message calls (its
// input arrives as posted messages, its frame steps run in PeekMessageA), and the image routes the class, the
// messages and the default procedure to the wide calls. The screen mapping and the monitor cover never went through a
// slot and call the system directly.

namespace platform {
namespace {

EventHandler g_handler = nullptr;
// The registered class, which stands for its name in FindWindowA and CreateWindowExA.
ATOM g_classAtom = 0;

// The "QueryCancelAutoPlay" message's id, registered the first time a message is not one of the others.
UINT g_autoPlayMessage = 0;
bool g_autoPlayRegistered = false;
UINT AutoPlayMessage() {
    if (!g_autoPlayRegistered) {
        g_autoPlayRegistered = true;
        g_autoPlayMessage = gameapi::RegisterWindowMessageA("QueryCancelAutoPlay");
    }
    return g_autoPlayMessage;
}

Activation ActivationOf(WPARAM state) {
    switch (state & 0xFFFF) {
    case WA_INACTIVE: return Activation::Inactive;
    case WA_ACTIVE: return Activation::Active;
    case WA_CLICKACTIVE: return Activation::ClickActive;
    }
    return Activation::Unknown;
}

SystemCommand SystemCommandOf(WPARAM command) {
    switch (command & 0xFFF0) {
    case SC_HOTKEY:
    case SC_NEXTWINDOW:
    case SC_PREVWINDOW: return SystemCommand::TaskSwitch;
    case SC_SCREENSAVE:
    case SC_MONITORPOWER: return SystemCommand::ScreenSaver;
    }
    return SystemCommand::Other;
}

void SetKey(Event& event, EventType type, WPARAM key, LPARAM data) {
    event.type = type;
    event.key = static_cast<int>(key);
    event.repeat = static_cast<int>(data & 0xFFFF);
    event.keyFlags = static_cast<int>(data) >> 16;
}

void SetMouse(Event& event, EventType type, WPARAM buttons, LPARAM at) {
    event.type = type;
    event.buttons = static_cast<int>(buttons);
    event.x = static_cast<short>(LOWORD(at));
    event.y = static_cast<short>(HIWORD(at));
}

Event Translate(HWND window, UINT id, WPARAM first, LPARAM second) {
    Event event;
    event.native = {window, id, first, second};
    switch (id) {
    case WM_KEYDOWN: SetKey(event, EventType::KeyDown, first, second); break;
    case WM_KEYUP: SetKey(event, EventType::KeyUp, first, second); break;
    case WM_SYSKEYDOWN: SetKey(event, EventType::SystemKeyDown, first, second); break;
    case WM_SYSKEYUP: SetKey(event, EventType::SystemKeyUp, first, second); break;
    case WM_CHAR: SetKey(event, EventType::Character, first & 0xFFFF, second); break;
    case WM_MOUSEMOVE: SetMouse(event, EventType::MouseMove, first, second); break;
    case WM_LBUTTONDOWN: SetMouse(event, EventType::LeftDown, first, second); break;
    case WM_LBUTTONUP: SetMouse(event, EventType::LeftUp, first, second); break;
    case WM_LBUTTONDBLCLK: SetMouse(event, EventType::LeftDouble, first, second); break;
    case WM_RBUTTONDOWN: SetMouse(event, EventType::RightDown, first, second); break;
    case WM_RBUTTONUP: SetMouse(event, EventType::RightUp, first, second); break;
    case WM_RBUTTONDBLCLK: SetMouse(event, EventType::RightDouble, first, second); break;
    case WM_MOUSEWHEEL:
        SetMouse(event, EventType::Wheel, LOWORD(first), second);
        event.wheelDelta = static_cast<short>(HIWORD(first));
        break;
    case WM_PAINT: event.type = EventType::Paint; break;
    case WM_DESTROY: event.type = EventType::Destroy; break;
    case WM_ACTIVATE:
        event.type = EventType::Activate;
        event.activation = ActivationOf(first);
        event.minimized = HIWORD(first) != 0;
        break;
    case WM_SETCURSOR: event.type = EventType::SetCursor; break;
    case WM_SYSCOMMAND:
        event.type = EventType::SystemCommand;
        event.command = SystemCommandOf(first);
        break;
    case WM_QUIT:
        event.type = EventType::Quit;
        event.quitCode = static_cast<int>(first);
        break;
    default:
        if (id != 0 && id == AutoPlayMessage()) event.type = EventType::AutoPlay;
        break;
    }
    return event;
}

Event Translate(const MSG& message) { return Translate(message.hwnd, message.message, message.wParam, message.lParam); }

LRESULT CALLBACK WindowProcedure(HWND window, UINT id, WPARAM first, LPARAM second) {
    return g_handler(Translate(window, id, first, second));
}

RECT ToRECT(const Rect& r) { return RECT{r.left, r.top, r.right, r.bottom}; }
Rect FromRECT(const RECT& r) {
    return Rect{static_cast<int>(r.left), static_cast<int>(r.top), static_cast<int>(r.right), static_cast<int>(r.bottom)};
}

} // namespace

// ---- The window --------------------------------------------------------------------------------------------------

void RegisterWindowClass(NativeInstance instance, const char* className, EventHandler handler) {
    g_handler = handler;
    WNDCLASSEXA windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_DBLCLKS;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = static_cast<HINSTANCE>(instance);
    windowClass.lpszClassName = className;
    g_classAtom = gameapi::RegisterClassExA(&windowClass);
}

NativeWindow FindClassWindow(const char* title) { return gameapi::FindWindowA(MAKEINTRESOURCEA(g_classAtom), title); }

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

NativeWindow CreateGameWindow(NativeInstance instance, const char* title, int width, int height) {
    return gameapi::CreateWindowExA(0, MAKEINTRESOURCEA(g_classAtom), title, WS_POPUP | WS_CLIPSIBLINGS, 0, 0, width,
                                    height, nullptr, nullptr, static_cast<HINSTANCE>(instance), nullptr);
}

void ShowGameWindow(NativeWindow window, int show) {
    gameapi::ShowWindow(static_cast<HWND>(window), show);
    gameapi::UpdateWindow(static_cast<HWND>(window));
}

bool IsMinimized(NativeWindow window) { return gameapi::IsIconic(static_cast<HWND>(window)) != FALSE; }

void Minimize(NativeWindow window) { gameapi::ShowWindow(static_cast<HWND>(window), SW_MINIMIZE); }

void Restore(NativeWindow window) { gameapi::ShowWindow(static_cast<HWND>(window), SW_RESTORE); }

void MarkPainted(NativeWindow window) {
    RECT rect;
    if (!gameapi::GetUpdateRect(static_cast<HWND>(window), &rect, FALSE)) return;
    PAINTSTRUCT paint;
    gameapi::BeginPaint(static_cast<HWND>(window), &paint);
    gameapi::EndPaint(static_cast<HWND>(window), &paint);
}

void RepaintDesktop() { gameapi::InvalidateRect(nullptr, nullptr, TRUE); }

void ScreenToClient(NativeWindow window, Point* point) {
    POINT p{point->x, point->y};
    ::ScreenToClient(static_cast<HWND>(window), &p);
    *point = Point{static_cast<int>(p.x), static_cast<int>(p.y)};
}

void ClientToScreen(NativeWindow window, Point* point) {
    POINT p{point->x, point->y};
    ::ClientToScreen(static_cast<HWND>(window), &p);
    *point = Point{static_cast<int>(p.x), static_cast<int>(p.y)};
}

bool CoverMonitor(NativeWindow window, Rect* was, Rect* monitor) {
    HWND handle = static_cast<HWND>(window);
    if (::IsIconic(handle)) return false;
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoA(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &info)) return false;
    RECT now{};
    if (!GetWindowRect(handle, &now) || EqualRect(&now, &info.rcMonitor)) return false;
    *was = FromRECT(now);
    *monitor = FromRECT(info.rcMonitor);
    const RECT m = ToRECT(*monitor);
    SetWindowPos(handle, nullptr, m.left, m.top, m.right - m.left, m.bottom - m.top,
                 SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
    return true;
}

// ---- Events ------------------------------------------------------------------------------------------------------

bool EventWaits() {
    MSG message;
    return gameapi::PeekMessageA(&message, nullptr, 0, 0, PM_NOREMOVE) != FALSE;
}

bool NextEvent(Event* event) {
    MSG message;
    if (!gameapi::GetMessageA(&message, nullptr, 0, 0)) {
        *event = Translate(message);
        return false;
    }
    gameapi::TranslateMessage(&message);
    *event = Translate(message);
    return true;
}

bool TakeEvent(Event* event) {
    MSG message;
    if (!gameapi::PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) return false;
    *event = Translate(message);
    return true;
}

std::intptr_t DefaultHandling(const Event& event) {
    const NativeMessage& m = event.native;
    return gameapi::DefWindowProcA(static_cast<HWND>(m.window), m.id, m.first, m.second);
}

void DispatchEvent(const Event& event) {
    MSG message{};
    message.hwnd = static_cast<HWND>(event.native.window);
    message.message = event.native.id;
    message.wParam = event.native.first;
    message.lParam = event.native.second;
    gameapi::TranslateMessage(&message);
    gameapi::DispatchMessageA(&message);
}

void PostQuit(int code) { gameapi::PostQuitMessage(code); }

} // namespace platform

// ==== The cursor =====================================================================================================

// Through game.exe's import slots: the harness's own mouse answers GetCursorPos and keeps SetCursorPos from moving the
// user's cursor.

namespace platform {
namespace {

PointMap g_toGame = nullptr;
PointMap g_toScreen = nullptr;

} // namespace

CursorShape CurrentCursor() { return gameapi::GetCursor(); }

void SetCursor(CursorShape shape) { gameapi::SetCursor(static_cast<HCURSOR>(shape)); }

void MapCursorPoints(PointMap toGame, PointMap toScreen) {
    g_toGame = toGame;
    g_toScreen = toScreen;
}

bool CursorPosition(Point* at) {
    POINT p;
    if (!gameapi::GetCursorPos(&p)) return false;
    Point point{static_cast<int>(p.x), static_cast<int>(p.y)};
    if (g_toGame) g_toGame(&point);
    *at = point;
    return true;
}

void SetCursorPosition(Point at) {
    if (g_toScreen) g_toScreen(&at);
    gameapi::SetCursorPos(at.x, at.y);
}

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

// ==== The mouse ======================================================================================================

// DirectInput 7 of the system's dinput.dll.

namespace {

// The game's own copies of the interface ids, the system mouse's id and c_dfDIMouse's axes.
constexpr GUID kGuidSysMouse = /*00744DD0*/ {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr GUID kGuidZAxis = /*00744E60*/ {0xA36D02E2, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr GUID kGuidYAxis = /*00744E70*/ {0xA36D02E1, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr GUID kGuidXAxis = /*00744E80*/ {0xA36D02E0, 0xC9F3, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
constexpr IID kIidDirectInputDevice7 = /*00744EA0*/ {0x57D7C6BC, 0x2356, 0x11D3, {0x8E, 0x9D, 0x00, 0xC0, 0x4F, 0x68, 0x44, 0xAE}};
constexpr IID kIidDirectInput7 = /*00744F10*/ {0x9A4CB684, 0x236D, 0x11D3, {0x8E, 0x9D, 0x00, 0xC0, 0x4F, 0x68, 0x44, 0xAE}};

constexpr DWORD kInputVersion = 0x0700;

// DirectInputCreateEx of the system's dinput.dll (System32: Rebirth's dinput.dll in the game folder is a proxy that
// loads addon.dll, which patches game.exe's code), loaded on the first call; null when it is missing. The SDK has
// no import library of DirectInput 7's dinput.dll.
decltype(&::DirectInputCreateEx) SystemDirectInputCreate() {
    static const auto create = [] {
        HMODULE dinput = LoadLibraryExW(L"dinput.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return dinput ? reinterpret_cast<decltype(&::DirectInputCreateEx)>(GetProcAddress(dinput, "DirectInputCreateEx"))
                      : nullptr;
    }();
    return create;
}

// The mouse data format the device gets (dinput.lib's c_dfDIMouse, as a DIDATAFORMAT), our own copy.
const DIDATAFORMAT* MouseDataFormat() {
    constexpr DWORD kOptional = 0x80000000;   // DIDFT_OPTIONAL (DirectInput 8's headers only)
    static DIOBJECTDATAFORMAT objects[] = {
        {&kGuidXAxis, 0, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
        {&kGuidYAxis, 4, DIDFT_AXIS | DIDFT_ANYINSTANCE, 0},
        {&kGuidZAxis, 8, DIDFT_AXIS | DIDFT_ANYINSTANCE | kOptional, 0},
        {nullptr, 12, DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
        {nullptr, 13, DIDFT_BUTTON | DIDFT_ANYINSTANCE, 0},
        {nullptr, 14, DIDFT_BUTTON | DIDFT_ANYINSTANCE | kOptional, 0},
        {nullptr, 15, DIDFT_BUTTON | DIDFT_ANYINSTANCE | kOptional, 0},
    };
    static const DIDATAFORMAT format = {sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_RELAXIS, 16,
                                        sizeof(objects) / sizeof(objects[0]), objects};
    return &format;
}

// A counted pointer taking `value` (the game's smart pointer assignment, then the temporary's
// release, which is not null-checked).
template <typename T>
void Adopt(T*& slot, T* value) {
    T* previous = slot;
    slot = value;
    if (value) value->AddRef();
    if (previous) previous->Release();
    slot->Release();
}

template <typename T>
void Drop(T*& slot) {
    T* held = slot;
    slot = nullptr;
    if (held) held->Release();
}

} // namespace

void (*g_directInputObserver)(IDirectInput7A* input) = nullptr;

namespace platform {

int MouseDevice::Open(NativeWindow window, NativeInstance instance, MouseReport report) {
    void* newInput = nullptr;
    const auto create = SystemDirectInputCreate();
    HRESULT result = create ? create(static_cast<HINSTANCE>(instance), kInputVersion, kIidDirectInput7, &newInput, nullptr)
                            : HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
    if (result < 0) return report(MouseStep::Create, result);
    if (g_directInputObserver) g_directInputObserver(static_cast<IDirectInput7A*>(newInput));
    Adopt(input, static_cast<IDirectInput7A*>(newInput));

    void* newDevice = nullptr;
    result = input->CreateDeviceEx(kGuidSysMouse, kIidDirectInputDevice7, &newDevice, nullptr);
    if (result < 0) return report(MouseStep::Device, result);
    Adopt(device, static_cast<IDirectInputDevice7A*>(newDevice));

    result = device->SetDataFormat(MouseDataFormat());
    if (result < 0) return report(MouseStep::Format, result);
    result = device->SetCooperativeLevel(static_cast<HWND>(window), DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
    if (result < 0) return report(MouseStep::Cooperation, result);
    if (device) {
        result = device->Acquire();
        if (result < 0) report(MouseStep::Acquire, result);
    }
    return 1;
}

void MouseDevice::Close() {
    if (device) device->Unacquire();
    Drop(device);
    Drop(input);
}

MouseDevice::~MouseDevice() { Close(); }

int MouseDevice::SetActive(bool active, MouseReport report) {
    if (!device) return 0;
    if (active) {
        HRESULT result = device->Acquire();
        if (result < 0) return report(MouseStep::Acquire, result);
    } else {
        device->Unacquire();
    }
    return 1;
}

void MouseDevice::Read(std::int32_t state[4]) {
    static_assert(sizeof(std::int32_t[4]) == sizeof(DIMOUSESTATE));
    if (device->GetDeviceState(sizeof(DIMOUSESTATE), state) < 0) std::memset(state, 0, sizeof(DIMOUSESTATE));
}

} // namespace platform

// ==== The clipboard ==================================================================================================

// Unicode text. Direct calls (never through game.exe's slots: the clipboard came with Rebirth's console).

namespace platform {

void CopyText(std::string_view text) {
    const std::wstring wide = win32::Utf16(text);
    const std::size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, bytes);
    if (!memory) return;
    void* locked = GlobalLock(memory);
    if (!locked) {
        GlobalFree(memory);
        return;
    }
    std::memcpy(locked, wide.c_str(), bytes);
    GlobalUnlock(memory);
    if (!OpenClipboard(nullptr)) {
        GlobalFree(memory);
        return;
    }
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();
}

std::string ClipboardText() {
    std::wstring wide;
    if (OpenClipboard(nullptr)) {
        if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
            if (const auto* text = static_cast<const wchar_t*>(GlobalLock(data))) {
                wide = text;
                GlobalUnlock(data);
            }
        }
        CloseClipboard();
    }
    return win32::Utf8(wide);
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
