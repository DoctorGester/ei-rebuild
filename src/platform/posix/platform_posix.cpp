// POSIX files and sockets; SDL owns the window, input, clocks, threads and audio.
#include "platform/platform.h"

#include "lib/text_case.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cwctype>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <locale.h>
#include <netdb.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <xlocale.h>
#else
#include <charconv>
#endif

namespace platform {
namespace {
namespace fs = std::filesystem;

// Asset names use backslashes and Windows case rules, including on case-sensitive volumes.
fs::path NativePath(std::string_view path) {
    std::string native(path);
    std::replace(native.begin(), native.end(), '\\', '/');
    fs::path resolved;
    for (const auto& part : fs::path(native)) {
        if (part == "/" || part == "." || part == "..") {
            resolved /= part;
            continue;
        }
        const fs::path exact = resolved / part;
        std::error_code error;
        if (fs::exists(exact, error)) {
            resolved = exact;
            continue;
        }
        fs::path found;
        for (const auto& entry : fs::directory_iterator(resolved.empty() ? fs::path(".") : resolved, error)) {
            if (text::CompareIgnoringCase(entry.path().filename().string(), part.string()) != 0) continue;
            // A stable choice if a volume contains two names that Windows would treat as identical.
            if (found.empty() || entry.path().filename().string() < found.filename().string()) found = entry.path();
        }
        resolved = found.empty() ? exact : found;
    }
    return resolved;
}

std::string GamePath(fs::path path) {
    std::string text = path.string();
    std::replace(text.begin(), text.end(), '/', '\\');
    return text;
}

int Descriptor(void* handle) { return handle ? static_cast<int>(reinterpret_cast<std::intptr_t>(handle) - 1) : -1; }
void* Handle(int fd) { return fd < 0 ? nullptr : reinterpret_cast<void*>(static_cast<std::intptr_t>(fd) + 1); }
[[noreturn]] void FileFailure(std::string_view path) { throw FileError(errno, path); }

bool Matches(std::string_view pattern, std::string_view value) {
    if (pattern == "*.*") pattern = "*";
    std::size_t p = 0, v = 0, star = pattern.npos, retry = 0;
    while (v < value.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || text::ToLower(pattern[p]) == text::ToLower(value[v]))) {
            ++p;
            ++v;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            retry = v;
        } else if (star != pattern.npos) {
            p = star + 1;
            v = ++retry;
        } else return false;
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

std::string_view Trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}
}

void DebugOutput(const char* text) { std::fputs(text, stderr); }
std::string ProgramPath() {
#if defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string path(size, '\0');
    if (_NSGetExecutablePath(path.data(), &size) != 0) return {};
    path.resize(std::strlen(path.c_str()));
    std::error_code error;
    return fs::canonical(path, error).string();
#else
    std::string path(PATH_MAX, '\0');
    const ssize_t size = readlink("/proc/self/exe", path.data(), path.size());
    return size > 0 ? path.substr(0, size) : std::string{};
#endif
}
std::string ExePath() { return GamePath(ProgramPath()); }
std::string CurrentFolder() { return GamePath(fs::current_path()); }
std::string UserDataFolder() {
    char* path = SDL_GetPrefPath("", "");
    if (!path) return {};
    const std::string result = GamePath(path) + "\\";
    SDL_free(path);
    return result;
}
bool SetCurrentFolder(const std::string& path, std::uint32_t* error) {
    if (chdir(NativePath(path).c_str()) == 0) return true;
    if (error) *error = errno;
    return false;
}
std::uint32_t PageSize() { return static_cast<std::uint32_t>(sysconf(_SC_PAGESIZE)); }
bool FontSmoothing() { return true; }
void PrimaryDesktopSize(int* width, int* height) {
    SDL_InitSubSystem(SDL_INIT_VIDEO);
    if (const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay())) {
        *width = mode->w;
        *height = mode->h;
    }
}
std::string EnvironmentVariable(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}
void EndProcess(std::uint32_t code) { std::_Exit(static_cast<int>(code)); }
bool CreateFolder(const char* path) {
    std::error_code e;
    return fs::create_directory(NativePath(path), e);
}
bool RemoveFile(const char* path) { return unlink(NativePath(path).c_str()) == 0; }
bool RemoveFolder(const char* path) { return rmdir(NativePath(path).c_str()) == 0; }
bool RenameFile(const char* from, const char* to) {
    const auto target = NativePath(to);
    std::error_code e;
    if (fs::exists(target, e)) return false;
    return rename(NativePath(from).c_str(), target.c_str()) == 0;
}
bool CopyFileOver(const char* from, const char* to) {
    std::error_code e;
    return fs::copy_file(NativePath(from), NativePath(to), fs::copy_options::overwrite_existing, e);
}
double FreeDiskSpace(const char* path) {
    std::error_code e;
    const auto space = fs::space(NativePath(path), e);
    return e ? 0.0 : static_cast<double>(space.available);
}
bool PathExists(const std::string& path) {
    std::error_code e;
    return fs::exists(NativePath(path), e);
}
bool IsFile(const std::string& path) {
    std::error_code e;
    return fs::is_regular_file(NativePath(path), e);
}
bool FileExists(const std::string& path) { return PathExists(path); }
std::string VolumeLabel(const char*) { return {}; }

FileError::FileError(unsigned long code, std::string_view path)
    : std::runtime_error(std::string(path) + ": " + std::strerror(static_cast<int>(code))) {}
File::~File() { Close(); }
bool File::Open(std::string_view filePath, FileMode mode) {
    Close();
    path = filePath;
    const int flags = mode == FileMode::Read     ? O_RDONLY
                      : mode == FileMode::Create ? O_WRONLY | O_CREAT | O_TRUNC
                                                 : O_WRONLY;
    handle = Handle(open(NativePath(path).c_str(), flags | O_CLOEXEC, 0666));
    return IsOpen();
}
void File::Close() {
    if (handle) close(Descriptor(handle));
    handle = nullptr;
}
unsigned File::Read(void* buffer, unsigned count) {
    ssize_t n;
    do {
        n = read(Descriptor(handle), buffer, count);
    } while (n < 0 && errno == EINTR);
    if (n < 0) FileFailure(path);
    return static_cast<unsigned>(n);
}
void File::Write(const void* buffer, unsigned count) {
    const auto* bytes = static_cast<const char*>(buffer);
    while (count) {
        const ssize_t n = write(Descriptor(handle), bytes, count);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            if (!n) errno = ENOSPC;
            FileFailure(path);
        }
        bytes += n;
        count -= static_cast<unsigned>(n);
    }
}
void File::WriteLine(std::string_view line) {
    Write(line.data(), static_cast<unsigned>(line.size()));
    Write("\r\n", 2);
}
long File::Seek(long offset, SeekFrom from) {
    const off_t n = lseek(Descriptor(handle), offset,
                          from == SeekFrom::Begin ? SEEK_SET
                          : from == SeekFrom::End ? SEEK_END
                                                  : SEEK_CUR);
    if (n < 0 || n > UINT32_MAX) FileFailure(path);
    return static_cast<long>(n);
}
std::uint32_t File::Position() const {
    const off_t n = lseek(Descriptor(handle), 0, SEEK_CUR);
    if (n < 0 || n > UINT32_MAX) FileFailure(path);
    return static_cast<std::uint32_t>(n);
}
std::uint32_t File::Size() const {
    struct stat s{};
    if (fstat(Descriptor(handle), &s) || s.st_size < 0 || s.st_size > UINT32_MAX) FileFailure(path);
    return static_cast<std::uint32_t>(s.st_size);
}
bool File::ReadAt(long offset, void* buffer, unsigned count) {
    // Preserve the stream-position side effect of the Windows implementation.
    try {
        Seek(offset, SeekFrom::Begin);
        return Read(buffer, count) == count;
    } catch (const FileError&) {
        return false;
    }
}
bool ReadFile(const std::string& path, std::string* bytes) {
    bytes->clear();
    File file;
    if (!file.Open(path, FileMode::Read)) return false;
    try {
        bytes->resize(file.Size());
        if (file.ReadAt(0, bytes->data(), static_cast<unsigned>(bytes->size()))) return true;
    } catch (const FileError&) {
    }
    bytes->clear();
    return false;
}
WholeFile ReadWholeFile(const std::string& path, std::string* bytes, std::uint32_t* error) {
    if (!PathExists(path)) {
        if (error) *error = ENOENT;
        return WholeFile::missing;
    }
    try {
        File file;
        if (file.Open(path, FileMode::Read) && file.Size() < (1 << 24)) {
            bytes->resize(file.Size());
            if (file.ReadAt(0, bytes->data(), static_cast<unsigned>(bytes->size()))) {
                if (error) *error = 0;
                return WholeFile::read;
            }
        }
    } catch (const FileError&) {
    }
    if (error) *error = errno ? errno : EIO;
    bytes->clear();
    return WholeFile::failed;
}
bool WriteWholeFile(const std::string& path, const std::string& bytes, std::uint32_t* error) {
    const auto target = NativePath(path);
    std::error_code e;
    if (!target.parent_path().empty()) fs::create_directories(target.parent_path(), e);
    std::string temp = target.string() + ".XXXXXX";
    const int fd = mkstemp(temp.data());
    if (fd < 0) {
        if (error) *error = errno;
        return false;
    }
    File file;
    bool ok = false;
    try {
        close(fd);
        if (file.Open(temp, FileMode::WriteExisting)) {
            file.Write(bytes.data(), static_cast<unsigned>(bytes.size()));
            file.Close();
            ok = rename(temp.c_str(), target.c_str()) == 0;
        }
    } catch (const FileError&) {
    }
    const int saved = errno;
    unlink(temp.c_str());
    if (error) *error = ok ? 0 : saved;
    return ok;
}
MappedFile::~MappedFile() { Close(); }
bool MappedFile::Open(std::string_view path) {
    Close();
    const int fd = open(NativePath(path).c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    file = Handle(fd);
    struct stat s{};
    if (fstat(fd, &s) || s.st_size <= 0 || s.st_size > UINT32_MAX) {
        Close();
        return false;
    }
    size = static_cast<std::uint32_t>(s.st_size);
    void* mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) {
        Close();
        return false;
    }
    view = mapped;
    return true;
}
void MappedFile::Close() {
    if (view) munmap(const_cast<void*>(view), size);
    if (file) close(Descriptor(file));
    file = mapping = nullptr;
    view = nullptr;
    size = 0;
}

struct FileFind::Search {
    std::vector<fs::directory_entry> entries;
    std::size_t next = 0;
};
FileFind::FileFind() = default;
FileFind::~FileFind() = default;
void FileFind::Close() {
    search.reset();
    hasFound = false;
}
bool FileFind::FindFile(std::string_view pattern) {
    Close();
    std::string native(pattern);
    std::replace(native.begin(), native.end(), '\\', '/');
    const fs::path path(native);
    const auto folder = NativePath(path.has_parent_path() ? path.parent_path().string() : ".");
    const auto mask = path.filename().string();
    std::error_code e;
    root = GamePath(fs::absolute(folder, e));
    auto started = std::make_unique<Search>();
    for (const auto& entry : fs::directory_iterator(folder, e))
        if (Matches(mask, entry.path().filename().string())) started->entries.push_back(entry);
    // Windows directory enumeration on NTFS follows name order.
    std::sort(started->entries.begin(), started->entries.end(), [](const auto& a, const auto& b) {
        return text::CompareIgnoringCase(a.path().filename().string(), b.path().filename().string()) < 0;
    });
    if (started->entries.empty()) return false;
    search = std::move(started);
    return true;
}
bool FileFind::FindNext() {
    if (!search || search->next == search->entries.size()) return false;
    const auto& entry = search->entries[search->next++];
    name = entry.path().filename().string();
    std::error_code e;
    attributes = entry.is_directory(e) ? 0x10 : 0;
    if (!name.empty() && name.front() == '.') attributes |= 2;
    size = attributes & 0x10 ? 0 : static_cast<std::uint32_t>(entry.file_size(e));
    hasFound = true;
    return search->next < search->entries.size();
}
std::string FileFind::GetFileName() const { return hasFound ? name : ""; }
std::string FileFind::GetFilePath() const { return root + "\\" + GetFileName(); }
std::string FileFind::GetTitle() const { return fs::path(GetFileName()).stem().string(); }
unsigned long FileFind::GetLength() const { return hasFound ? size : 0; }
bool FileFind::IsDirectory() const { return hasFound && (attributes & 0x10); }
bool FileFind::IsHidden() const { return hasFound && (attributes & 2); }
bool FileFind::IsSystem() const { return false; }
bool FileFind::IsDots() const { return IsDirectory() && (name == "." || name == ".."); }
std::vector<FolderEntry> FolderEntries(const std::string& pattern) {
    FileFind find;
    std::vector<FolderEntry> entries;
    if (find.FindFile(pattern)) {
        bool more;
        do {
            more = find.FindNext();
            entries.push_back({find.GetFileName(), find.IsDirectory()});
        } while (more);
    }
    return entries;
}
std::vector<std::string> IniSectionEntries(const char* section, const char* path) {
    std::string bytes;
    std::vector<std::string> entries;
    if (!ReadFile(path, &bytes)) return entries;
    bool active = false;
    std::size_t start = 0, used = 0;
    while (start < bytes.size()) {
        const auto end = bytes.find('\n', start);
        const auto line = Trim(std::string_view(bytes).substr(start, (end == bytes.npos ? bytes.size() : end) - start));
        start = end == bytes.npos ? bytes.size() : end + 1;
        if (line.empty() || line.front() == ';') continue;
        if (line.front() == '[') {
            const auto close = line.find(']');
            active = close != line.npos && text::CompareIgnoringCase(line.substr(1, close - 1), section) == 0;
        } else if (active && line.find('=') != line.npos) {
            used += line.size() + 1;
            if (used + 1 >= 0x5000) break;
            entries.emplace_back(line);
        }
    }
    return entries;
}
std::string IniString(const char* section, const char* key, const char* fallback, const char* path) {
    for (const auto& entry : IniSectionEntries(section, path)) {
        const auto equal = entry.find('=');
        if (text::CompareIgnoringCase(Trim(std::string_view(entry).substr(0, equal)), key) != 0) continue;
        auto value = Trim(std::string_view(entry).substr(equal + 1));
        if (value.size() >= 2 && (value.front() == '\'' || value.front() == '"') && value.front() == value.back())
            value = value.substr(1, value.size() - 2);
        return std::string(value.substr(0, 1022));
    }
    return std::string(fallback ? fallback : "").substr(0, 1022);
}
int IniInt(const char* section, const char* key, int fallback, const char* path) {
    const auto value = IniString(section, key, "", path);
    if (value.empty()) return fallback;
    char* end;
    const long n = std::strtol(value.c_str(), &end, 10);
    return end == value.c_str() ? fallback : static_cast<int>(n);
}

namespace {
int g_instanceLock = -1;
pid_t g_instanceOwner = 0, g_otherProcess = 0;
NativeWindow const kOtherInstance = reinterpret_cast<NativeWindow>(-1);
}

// SDL cannot enumerate another process's windows. Hold an advisory lock for the same class/title instead.
NativeWindow FindClassWindow(const char* className, const char* title) {
    if (g_instanceOwner == getpid() && g_instanceLock >= 0) return nullptr;
    if (g_instanceLock >= 0) close(g_instanceLock);   // a forked child must acquire its own lock
    g_instanceLock = -1;
    std::uint64_t hash = 14695981039346656037ull;
    for (const char* part : {className, title}) {
        for (; *part; ++part) hash = (hash ^ static_cast<unsigned char>(*part)) * 1099511628211ull;
        hash = (hash ^ 0xFFu) * 1099511628211ull;
    }
    const auto path =
        fs::temp_directory_path() / ("ei-rebuild-" + std::to_string(getuid()) + "-" + std::to_string(hash) + ".lock");
    const int fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return nullptr;
    if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
        g_instanceLock = fd;
        g_instanceOwner = getpid();
        const std::string pid = std::to_string(getpid());
        ftruncate(fd, 0);
        pwrite(fd, pid.data(), pid.size(), 0);
        return nullptr;
    }
    const int error = errno;
    char pid[32] = {};
    pread(fd, pid, sizeof(pid) - 1, 0);
    close(fd);
    g_otherProcess = static_cast<pid_t>(std::strtol(pid, nullptr, 10));
    return error == EWOULDBLOCK || error == EAGAIN ? kOtherInstance : nullptr;
}
std::uint32_t WindowProcess(NativeWindow window) {
    return static_cast<std::uint32_t>(window == kOtherInstance ? g_otherProcess : getpid());
}
void BringToFront(NativeWindow window) {
    if (window && window != kOtherInstance) SDL_RaiseWindow(static_cast<SDL_Window*>(window));
}
NativeInstance ProgramInstance() { return nullptr; }
bool IsLetter(std::uint32_t c) {
    static locale_t locale = newlocale(LC_CTYPE_MASK, "en_US.UTF-8", nullptr);
    return c <= 0xFFFF && (locale ? iswalpha_l(static_cast<wint_t>(c), locale) : std::iswalpha(static_cast<wint_t>(c)));
}
void ActivateKeyboardLayout(std::uint32_t, std::uint32_t) {}   // Text input follows the user's active system layout.
// Set-1 scan codes used by the binding file, mapped to SDL's physical keys.
bool KeyName(int scan, char* name, int size) {
    static constexpr SDL_Scancode codes[] = {SDL_SCANCODE_UNKNOWN,     SDL_SCANCODE_ESCAPE,
                                             SDL_SCANCODE_1,           SDL_SCANCODE_2,
                                             SDL_SCANCODE_3,           SDL_SCANCODE_4,
                                             SDL_SCANCODE_5,           SDL_SCANCODE_6,
                                             SDL_SCANCODE_7,           SDL_SCANCODE_8,
                                             SDL_SCANCODE_9,           SDL_SCANCODE_0,
                                             SDL_SCANCODE_MINUS,       SDL_SCANCODE_EQUALS,
                                             SDL_SCANCODE_BACKSPACE,   SDL_SCANCODE_TAB,
                                             SDL_SCANCODE_Q,           SDL_SCANCODE_W,
                                             SDL_SCANCODE_E,           SDL_SCANCODE_R,
                                             SDL_SCANCODE_T,           SDL_SCANCODE_Y,
                                             SDL_SCANCODE_U,           SDL_SCANCODE_I,
                                             SDL_SCANCODE_O,           SDL_SCANCODE_P,
                                             SDL_SCANCODE_LEFTBRACKET, SDL_SCANCODE_RIGHTBRACKET,
                                             SDL_SCANCODE_RETURN,      SDL_SCANCODE_LCTRL,
                                             SDL_SCANCODE_A,           SDL_SCANCODE_S,
                                             SDL_SCANCODE_D,           SDL_SCANCODE_F,
                                             SDL_SCANCODE_G,           SDL_SCANCODE_H,
                                             SDL_SCANCODE_J,           SDL_SCANCODE_K,
                                             SDL_SCANCODE_L,           SDL_SCANCODE_SEMICOLON,
                                             SDL_SCANCODE_APOSTROPHE,  SDL_SCANCODE_GRAVE,
                                             SDL_SCANCODE_LSHIFT,      SDL_SCANCODE_BACKSLASH,
                                             SDL_SCANCODE_Z,           SDL_SCANCODE_X,
                                             SDL_SCANCODE_C,           SDL_SCANCODE_V,
                                             SDL_SCANCODE_B,           SDL_SCANCODE_N,
                                             SDL_SCANCODE_M,           SDL_SCANCODE_COMMA,
                                             SDL_SCANCODE_PERIOD,      SDL_SCANCODE_SLASH,
                                             SDL_SCANCODE_RSHIFT,      SDL_SCANCODE_KP_MULTIPLY,
                                             SDL_SCANCODE_LALT,        SDL_SCANCODE_SPACE,
                                             SDL_SCANCODE_CAPSLOCK,    SDL_SCANCODE_F1,
                                             SDL_SCANCODE_F2,          SDL_SCANCODE_F3,
                                             SDL_SCANCODE_F4,          SDL_SCANCODE_F5,
                                             SDL_SCANCODE_F6,          SDL_SCANCODE_F7,
                                             SDL_SCANCODE_F8,          SDL_SCANCODE_F9,
                                             SDL_SCANCODE_F10,         SDL_SCANCODE_NUMLOCKCLEAR,
                                             SDL_SCANCODE_SCROLLLOCK,  SDL_SCANCODE_HOME,
                                             SDL_SCANCODE_UP,          SDL_SCANCODE_PAGEUP,
                                             SDL_SCANCODE_KP_MINUS,    SDL_SCANCODE_LEFT,
                                             SDL_SCANCODE_KP_5,        SDL_SCANCODE_RIGHT,
                                             SDL_SCANCODE_KP_PLUS,     SDL_SCANCODE_END,
                                             SDL_SCANCODE_DOWN,        SDL_SCANCODE_PAGEDOWN,
                                             SDL_SCANCODE_INSERT,      SDL_SCANCODE_DELETE};
    const int index = scan & 0xFF;
    SDL_Scancode code = index < static_cast<int>(std::size(codes)) ? codes[index] : SDL_SCANCODE_UNKNOWN;
    if (index == 0x57) code = SDL_SCANCODE_F11;
    if (index == 0x58) code = SDL_SCANCODE_F12;
    if (code == SDL_SCANCODE_UNKNOWN || size <= 0) return false;
    const char* text = SDL_GetScancodeName(code);
    std::snprintf(name, static_cast<std::size_t>(size), "%s", text);
    return *name != 0;
}
}   // namespace platform

namespace platform::net {
namespace {
int Fd(Socket s) { return s == kNoSocket ? -1 : static_cast<int>(s); }
sockaddr_in Native(const Address& a) {
    sockaddr_in n{};
#if defined(__APPLE__)
    n.sin_len = sizeof(n);
#endif
    n.sin_family = AF_INET;
    n.sin_port = a.port;
    n.sin_addr.s_addr = a.ip;
    return n;
}
Address Public(const sockaddr_in& n) { return {kIpv4, n.sin_port, n.sin_addr.s_addr, {}}; }
}
bool Start() { return true; }
void Stop() {}
Socket Open(Kind kind) {
    const int fd = socket(AF_INET, kind == Kind::Udp ? SOCK_DGRAM : SOCK_STREAM, 0);
    if (fd < 0) return kNoSocket;
#ifdef SO_NOSIGPIPE
    const int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    return static_cast<Socket>(fd);
}
void Close(Socket s) { close(Fd(s)); }
bool Bind(Socket s, const Address& a) {
    const auto n = Native(a);
    return a.family == kIpv4 && bind(Fd(s), reinterpret_cast<const sockaddr*>(&n), sizeof(n)) == 0;
}
void SetNonBlocking(Socket s, bool on) {
    const int flags = fcntl(Fd(s), F_GETFL);
    if (flags >= 0) fcntl(Fd(s), F_SETFL, on ? flags | O_NONBLOCK : flags & ~O_NONBLOCK);
}
void AllowBroadcast(Socket s) {
    const int yes = 1;
    setsockopt(Fd(s), SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
}
bool Connect(Socket s, const Address& a) {
    const auto n = Native(a);
    return a.family == kIpv4 && connect(Fd(s), reinterpret_cast<const sockaddr*>(&n), sizeof(n)) == 0;
}
int SendTo(Socket s, const void* p, int n, const Address& a) {
    if (n < 0 || a.family != kIpv4) {
        errno = EINVAL;
        return -1;
    }
    const auto to = Native(a);
    return static_cast<int>(sendto(Fd(s), p, n, 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to)));
}
int ReceiveFrom(Socket s, void* p, int n, Address* from) {
    if (n < 0) {
        errno = EINVAL;
        return -1;
    }
    sockaddr_in a{};
    socklen_t len = sizeof(a);
    const int got = static_cast<int>(recvfrom(Fd(s), p, n, 0, reinterpret_cast<sockaddr*>(&a), &len));
    if (got >= 0 && from) *from = Public(a);
    return got;
}
int Send(Socket s, const void* p, int n) {
    if (n < 0) {
        errno = EINVAL;
        return -1;
    }
#ifdef MSG_NOSIGNAL
    return static_cast<int>(send(Fd(s), p, n, MSG_NOSIGNAL));
#else
    return static_cast<int>(send(Fd(s), p, n, 0));
#endif
}
int Receive(Socket s, void* p, int n) {
    if (n < 0) {
        errno = EINVAL;
        return -1;
    }
    return static_cast<int>(recv(Fd(s), p, n, 0));
}
Error LastError() {
    switch (errno) {
    case 0:
        return Error::None;
    case EAGAIN:
    case EINPROGRESS:
    case EALREADY:
        return Error::WouldBlock;
    case EADDRINUSE:
        return Error::AddressInUse;
    case ECONNREFUSED:
        return Error::Refused;
    default:
        return Error::Other;
    }
}
int Select(SocketGroup* read, SocketGroup* write, SocketGroup* failed, std::int32_t microseconds) {
    SocketGroup* groups[] = {read, write, failed};
    fd_set sets[3];
    int highest = -1;
    for (int i = 0; i < 3; ++i) {
        FD_ZERO(&sets[i]);
        if (!groups[i]) continue;
        for (int j = 0; j < groups[i]->count; ++j) {
            const int fd = Fd(groups[i]->sockets[j]);
            if (fd < 0 || fd >= FD_SETSIZE) {
                errno = EBADF;
                return -1;
            }
            FD_SET(fd, &sets[i]);
            highest = (std::max)(highest, fd);
        }
    }
    if (highest < 0 || microseconds < 0) {
        errno = EINVAL;
        return -1;
    }
    timeval timeout{microseconds / 1000000, microseconds % 1000000};
    const int ready = select(highest + 1, read ? &sets[0] : nullptr, write ? &sets[1] : nullptr,
                             failed ? &sets[2] : nullptr, &timeout);
    if (ready < 0) return ready;
    for (int i = 0; i < 3; ++i)
        if (groups[i]) {
            int count = 0;
            for (int j = 0; j < groups[i]->count; ++j)
                if (FD_ISSET(Fd(groups[i]->sockets[j]), &sets[i])) groups[i]->sockets[count++] = groups[i]->sockets[j];
            groups[i]->count = count;
        }
    return ready;
}
bool HostName(char* name, int size) { return size > 0 && gethostname(name, size) == 0; }
bool LookUp(const char* name, std::uint32_t* ip) {
    addrinfo hint{};
    hint.ai_family = AF_INET;
    hint.ai_socktype = SOCK_DGRAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(name, nullptr, &hint, &found) || !found) return false;
    *ip = reinterpret_cast<sockaddr_in*>(found->ai_addr)->sin_addr.s_addr;
    freeaddrinfo(found);
    return true;
}
std::string HostNameAt(const void* address, int length) {
    if (length != 4) return {};
    sockaddr_in a{};
    a.sin_family = AF_INET;
    lib::LoadBytes(a.sin_addr.s_addr, address);
    char name[NI_MAXHOST];
    return getnameinfo(reinterpret_cast<sockaddr*>(&a), sizeof(a), name, sizeof(name), nullptr, 0, NI_NAMEREQD) == 0
               ? name
               : "";
}
std::uint32_t ParseIpv4(const char* text) { return inet_addr(text); }
std::uint16_t ToNetwork(std::uint16_t v) { return htons(v); }
std::uint16_t FromNetwork(std::uint16_t v) { return ntohs(v); }
}   // namespace platform::net

namespace platform {

std::string FormatLocalTime(std::int32_t seconds, const char* format) {
    char buffer[128];
    std::tm at{};
    const std::time_t value = seconds;
    const bool valid = localtime_r(&value, &at) != nullptr;
    if (!valid || !std::strftime(buffer, sizeof(buffer), format, &at)) buffer[0] = '\0';
    return buffer;
}

}   // namespace platform

namespace platform {

bool ParseFloat(std::string_view text, float* value) {
#if defined(__APPLE__)
    // Apple libc++ has no floating-point from_chars. strtof_l needs a terminator and an explicit C locale.
    if (text.empty() || text.front() == '+' || text::IsSpace(static_cast<unsigned char>(text.front()))) return false;
    const std::size_t sign = text.front() == '-' ? 1 : 0;
    if (text.size() > sign + 1 && text[sign] == '0' && (text[sign + 1] == 'x' || text[sign + 1] == 'X')) return false;
    static locale_t locale = newlocale(LC_NUMERIC_MASK, "C", nullptr);
    const std::string terminated(text);
    char* end = nullptr;
    errno = 0;
    const float number = strtof_l(terminated.c_str(), &end, locale);
    // strtof reports ERANGE for representable subnormals too; from_chars accepts those.
    if ((errno == ERANGE && (number == 0.0f || !std::isfinite(number))) || end == terminated.c_str() ||
        end != terminated.c_str() + terminated.size())
        return false;
#else
    float number = 0;
    const auto read = std::from_chars(text.data(), text.data() + text.size(), number);
    if (text.empty() || read.ec != std::errc() || read.ptr != text.data() + text.size()) return false;
#endif
    *value = number;
    return true;
}

}   // namespace platform
