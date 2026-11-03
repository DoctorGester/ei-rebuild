// POSIX files and sockets; SDL owns the window, input, clocks, threads and audio, std::filesystem the folder calls
// (src/platform/files.cpp).
#include "platform/platform.h"

#include "lib/text_case.h"
#include "platform/paths.h"
#include "platform/posix/platform_posix.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <fcntl.h>
#include <filesystem>
#include <locale.h>
#include <netdb.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace platform {
namespace {
namespace fs = std::filesystem;

std::string GamePath(fs::path path) {
    std::string text = path.string();
    std::replace(text.begin(), text.end(), '/', '\\');
    return text;
}

int Descriptor(void* handle) { return handle ? static_cast<int>(reinterpret_cast<std::intptr_t>(handle) - 1) : -1; }

void* Handle(int fd) { return fd < 0 ? nullptr : reinterpret_cast<void*>(static_cast<std::intptr_t>(fd) + 1); }
[[noreturn]] void FileFailure(std::string_view path) { throw FileError(errno, path); }

// `path` for `use` as the system names it (paths.h).
fs::path SystemNative(std::string_view path, PathUse use) { return NativePath(SystemPath(path, use)); }

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
}

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
    return size > 0 ? path.substr(0, static_cast<std::size_t>(size)) : std::string{};
#endif
}

std::string ArgumentsAfterSeparator(int argc, char** argv) {
    std::string arguments;
    int i = 1;
    while (i < argc && std::strcmp(argv[i], "--") != 0) ++i;
    for (++i; i < argc; ++i) {
        if (!arguments.empty()) arguments += ' ';
        arguments += argv[i];
    }
    return arguments;
}

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

void PrimaryDesktopSize(int* width, int* height) {
    SDL_InitSubSystem(SDL_INIT_VIDEO);
    if (const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay())) {
#if defined(__APPLE__)
        // In pixels, as the game's sizes are (a Retina display's points times its density; platform_sdl.cpp's
        // CreateGameWindow makes the window in points).
        *width = static_cast<int>(std::lround(mode->w * mode->pixel_density));
        *height = static_cast<int>(std::lround(mode->h * mode->pixel_density));
#else
        *width = mode->w;
        *height = mode->h;
#endif
    }
}

std::string EnvironmentVariable(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

void EndProcess(std::uint32_t code) { std::_Exit(static_cast<int>(code)); }

// As Windows' text (platform_win32.cpp), the system's own words added.
FileError::FileError(unsigned long code, std::string_view path)
    : std::runtime_error("file error: OS error " + std::to_string(code) + " (" + std::strerror(static_cast<int>(code)) +
                         "), file '" + std::string(path) + "'") {}
File::~File() { Close(); }

bool File::Open(std::string_view filePath, FileMode mode) {
    Close();
    path = filePath;
    const int flags = mode == FileMode::Read     ? O_RDONLY
                      : mode == FileMode::Create ? O_WRONLY | O_CREAT | O_TRUNC
                                                 : O_WRONLY;
    const PathUse use = mode == FileMode::Read ? PathUse::Read : mode == FileMode::Create ? PathUse::Write : PathUse::Update;
    handle = Handle(open(SystemNative(path, use).c_str(), flags | O_CLOEXEC, 0666));
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

bool ReadWholeFile(std::string_view path, std::string* bytes, std::uint32_t* error) {
    bytes->clear();
    errno = 0;
    try {
        File file;
        if (file.Open(path, FileMode::Read)) {
            bytes->resize(file.Size());
            if (file.ReadAt(0, bytes->data(), static_cast<unsigned>(bytes->size()))) {
                if (error) *error = 0;
                return true;
            }
        }
    } catch (const FileError&) {
    }
    if (error) *error = errno ? errno : EIO;
    bytes->clear();
    return false;
}

bool WriteWholeFile(std::string_view path, const std::string& bytes, std::uint32_t* error) {
    const auto target = SystemNative(path, PathUse::Write);
    std::error_code e;
    if (!target.parent_path().empty()) fs::create_directories(target.parent_path(), e);
    std::string temp = target.string() + ".XXXXXX";
    const int fd = mkstemp(temp.data());
    if (fd < 0) {
        if (error) *error = errno;
        return false;
    }
    bool ok = false;
    const auto* data = bytes.data();
    std::size_t left = bytes.size();
    while (left) {
        const ssize_t n = write(fd, data, left);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        data += n;
        left -= static_cast<std::size_t>(n);
    }
    ok = left == 0 && close(fd) == 0 && rename(temp.c_str(), target.c_str()) == 0;
    if (left) close(fd);
    const int saved = errno;
    unlink(temp.c_str());
    if (error) *error = ok ? 0 : saved;
    return ok;
}

MappedFile::~MappedFile() { Close(); }

bool MappedFile::Open(std::string_view path) {
    Close();
    const int fd = open(SystemNative(path, PathUse::Read).c_str(), O_RDONLY | O_CLOEXEC);
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

// The pattern's folder through the mapping, its entries matched with Windows' wildcards and case rules, in NTFS's name
// order (Windows' order on its volumes); no "." or "..".
std::vector<FolderEntry> FolderEntries(std::string_view pattern) {
    // The folder in the game's form (backslashes), as every other call asks SystemPath for its path.
    const std::string game(pattern);
    const std::size_t slash = game.find_last_of("\\/");
    const std::string folderName = slash == std::string::npos ? "." : game.substr(0, slash == 0 ? 1 : slash);
    const fs::path folder = SystemNative(folderName, PathUse::Read);
    const std::string mask = slash == std::string::npos ? game : game.substr(slash + 1);
    std::error_code e;
    const std::string root = GamePath(fs::absolute(NativePath(folderName), e));
    std::vector<fs::directory_entry> found;
    for (const auto& entry : fs::directory_iterator(folder, e))
        if (Matches(mask, entry.path().filename().string())) found.push_back(entry);
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        return text::CompareIgnoringCase(a.path().filename().string(), b.path().filename().string()) < 0;
    });
    std::vector<FolderEntry> entries;
    for (const fs::directory_entry& entry : found) {
        FolderEntry& out = entries.emplace_back();
        out.name = entry.path().filename().string();
        out.path = root + "\\" + out.name;
        out.folder = entry.is_directory(e);
        out.size = out.folder ? 0 : static_cast<std::uint32_t>(entry.file_size(e));
    }
    return entries;
}

namespace {
int g_instanceLock = -1;
pid_t g_instanceOwner = 0, g_otherProcess = 0;
NativeWindow const kOtherInstance = reinterpret_cast<NativeWindow>(-1);
}

// SDL cannot enumerate another process's windows. Hold an advisory lock for the same class/title instead.
NativeWindow posix::OtherInstance(const char* className, const char* title) {
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
        if (ftruncate(fd, 0) != 0 || pwrite(fd, pid.data(), pid.size(), 0) < 0)
            std::fputs("platform: the instance lock holds no process id\n", stderr);
        return nullptr;
    }
    const int error = errno;
    char pid[32] = {};
    if (pread(fd, pid, sizeof(pid) - 1, 0) < 0) pid[0] = 0;
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
    static locale_t locale = [] {
        locale_t found = newlocale(LC_CTYPE_MASK, "C.UTF-8", nullptr);
        return found ? found : newlocale(LC_CTYPE_MASK, "en_US.UTF-8", nullptr);
    }();
    return c <= 0xFFFF && (locale ? iswalpha_l(static_cast<wint_t>(c), locale) : std::iswalpha(static_cast<wint_t>(c)));
}

void ActivateKeyboardLayout(std::uint32_t, std::uint32_t) {}   // Text input follows the user's active system layout.
// No layout's own key codes or control characters here: sdl_keys.cpp's rules (US keys, Ctrl+letter).
int LayoutKey(int) { return 0; }

int LayoutControlCharacter(int, int) { return -1; }
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
} // namespace platform

namespace platform::net {
namespace {
int Fd(Socket s) { return s == kNoSocket ? -1 : static_cast<int>(s); }
}

// Closing, binding, connecting, sending, receiving and the wait (select) are game_calls.cpp's (the harness keeps binds
// and datagrams on 127.0.0.1, a pair's network: tests/verify/harness_net.h).

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

void SetNonBlocking(Socket s, bool on) {
    const int flags = fcntl(Fd(s), F_GETFL);
    if (flags >= 0) fcntl(Fd(s), F_SETFL, on ? flags | O_NONBLOCK : flags & ~O_NONBLOCK);
}

void AllowBroadcast(Socket s) {
    const int yes = 1;
    setsockopt(Fd(s), SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
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
} // namespace platform::net
