// POSIX files and sockets; SDL owns the window, input, clocks, threads and audio, std::filesystem the folder calls
// (src/platform/files.cpp).
#include "platform/platform.h"

#include "lib/text_case.h"
#include "platform/paths.h"
#include "platform/posix/platform_posix.h"
#include "platform/sdl/sdl_keys.h"

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
pid_t g_instanceOwner = 0;
}

// SDL cannot enumerate another process's windows. Hold an advisory lock for the same name and title instead.
bool posix::OtherInstanceRunning(const char* name, const char* title) {
    if (g_instanceOwner == getpid() && g_instanceLock >= 0) return false;
    if (g_instanceLock >= 0) close(g_instanceLock);   // a forked child must acquire its own lock
    g_instanceLock = -1;
    std::uint64_t hash = 14695981039346656037ull;
    for (const char* part : {name, title}) {
        for (; *part; ++part) hash = (hash ^ static_cast<unsigned char>(*part)) * 1099511628211ull;
        hash = (hash ^ 0xFFu) * 1099511628211ull;
    }
    const auto path =
        fs::temp_directory_path() / ("ei-rebuild-" + std::to_string(getuid()) + "-" + std::to_string(hash) + ".lock");
    const int fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return false;
    if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
        g_instanceLock = fd;
        g_instanceOwner = getpid();
        return false;
    }
    const int error = errno;
    close(fd);
    return error == EWOULDBLOCK || error == EAGAIN;
}

bool IsLetter(std::uint32_t c) {
    static locale_t locale = [] {
        locale_t found = newlocale(LC_CTYPE_MASK, "C.UTF-8", nullptr);
        return found ? found : newlocale(LC_CTYPE_MASK, "en_US.UTF-8", nullptr);
    }();
    return c <= 0xFFFF && (locale ? iswalpha_l(static_cast<wint_t>(c), locale) : std::iswalpha(static_cast<wint_t>(c)));
}

// ---- The keyboard: SDL's keymap (the system's active layout) read as Windows' layouts give their codes ----

namespace {

struct UsKeyName {
    int scan;
    const char* name;
};

// Windows' US layout's key names (GetKeyNameTextW), by set-1 scan code (0x100 extended). Of the keys a layout moves
// only the punctuation's, which KeyName takes when the active layout's key types no character there.
constexpr UsKeyName kUsKeyNames[] = {
    {0x01, "Esc"},        {0x0C, "-"},           {0x0D, "="},           {0x0E, "Backspace"},   {0x0F, "Tab"},
    {0x1A, "["},          {0x1B, "]"},           {0x1C, "Enter"},       {0x1D, "Ctrl"},        {0x27, ";"},
    {0x28, "'"},          {0x29, "`"},           {0x2A, "Shift"},       {0x2B, "\\"},          {0x33, ","},
    {0x34, "."},          {0x35, "/"},           {0x36, "Right Shift"}, {0x37, "Num *"},       {0x38, "Alt"},
    {0x39, "Space"},      {0x3A, "Caps Lock"},   {0x3B, "F1"},          {0x3C, "F2"},          {0x3D, "F3"},
    {0x3E, "F4"},         {0x3F, "F5"},          {0x40, "F6"},          {0x41, "F7"},          {0x42, "F8"},
    {0x43, "F9"},         {0x44, "F10"},         {0x45, "Pause"},       {0x46, "Scroll Lock"}, {0x47, "Num 7"},
    {0x48, "Num 8"},      {0x49, "Num 9"},       {0x4A, "Num -"},       {0x4B, "Num 4"},       {0x4C, "Num 5"},
    {0x4D, "Num 6"},      {0x4E, "Num +"},       {0x4F, "Num 1"},       {0x50, "Num 2"},       {0x51, "Num 3"},
    {0x52, "Num 0"},      {0x53, "Num Del"},     {0x54, "Sys Req"},     {0x56, "\\"},          {0x57, "F11"},
    {0x58, "F12"},        {0x11C, "Num Enter"},  {0x11D, "Right Ctrl"}, {0x135, "Num /"},      {0x137, "Prnt Scrn"},
    {0x138, "Right Alt"}, {0x145, "Num Lock"},   {0x146, "Break"},      {0x147, "Home"},       {0x148, "Up"},
    {0x149, "Page Up"},   {0x14B, "Left"},       {0x14D, "Right"},      {0x14F, "End"},        {0x150, "Down"},
    {0x151, "Page Down"}, {0x152, "Insert"},     {0x153, "Delete"},     {0x15B, "Left Windows"},
    {0x15C, "Right Windows"}, {0x15D, "Application"},
};

const char* UsName(int scan) {
    for (const UsKeyName& entry : kUsKeyNames)
        if (entry.scan == scan) return entry.name;
    return nullptr;
}

// What the active layout's key with the set-1 scan code `scan` types without modifiers: SDL's keymap as the system
// gave it (Cyrillic letters on a Russian layout, where SDL's key codes for the events are the US layout's).
SDL_Keycode LayoutTyped(int scan) { return SDL_GetKeyFromScancode(sdl_keys::ScancodeOf(scan), SDL_KMOD_NONE, false); }

} // namespace

void ActivateKeyboardLayout(std::uint32_t, std::uint32_t) {}   // Text input follows the user's active system layout.

// What the key types as SDL's key codes for the events name it (SDL_HINT_KEYCODE_OPTIONS' defaults): on a layout whose
// letter keys type Latin letters the layout's character (AZERTY's M key VK_M, its comma key VK_OEM_COMMA; a French
// digit row, digits with Shift, its digits), on another (Russian, Greek) the US layout's, as Windows' Russian layout
// keeps the US codes. A key that types none of KeyOfTyped's characters keeps its US code (KeyOf).
int LayoutKey(int scan) {
    if (!sdl_keys::LayoutMoves(scan)) return 0;
    return sdl_keys::KeyOfTyped(SDL_GetKeyFromScancode(sdl_keys::ScancodeOf(scan), SDL_KMOD_NONE, true));
}

// A letter key (its code a letter's) and the keys layouts do not move: sdl_keys' rule (-1). Another key types ESC, FS
// or GS with Ctrl where the layout's key types `[`, `\` or `]` (Windows' US layout's and xkb's rule), else nothing:
// Russian's х and ъ keys none, as on Windows.
int LayoutControlCharacter(int key, int scan) {
    if (!sdl_keys::LayoutMoves(scan) || (key >= 'A' && key <= 'Z')) return -1;
    switch (LayoutTyped(scan)) {
    case '[': return 0x1B;
    case '\\': return 0x1C;
    case ']': return 0x1D;
    default: return 0;
    }
}

// As Windows' GetKeyNameTextW names them: a letter key by its code's letter (Russian's Й key "Q"), another key layouts
// move by what it types (Russian's х key "х", a French digit row its characters), the other keys by Windows' US names
// (the system has no names in the layout's language). The name in UTF-8, cut at a character's start to fit `size`
// bytes with the terminator.
bool KeyName(int scan, char* name, int size) {
    if (!name || size <= 0) return false;
    char text[8] = {};
    const char* chosen = UsName(scan);
    if (sdl_keys::LayoutMoves(scan)) {
        const int key = sdl_keys::KeyOf(sdl_keys::ScancodeOf(scan), SDL_KMOD_NONE, LayoutKey).key;
        const SDL_Keycode typed = LayoutTyped(scan);
        if (key >= 'A' && key <= 'Z') {
            text[0] = static_cast<char>(key);
            chosen = text;
        } else if (typed >= 0x20 && typed != 0x7F && !(typed & (SDLK_SCANCODE_MASK | SDLK_EXTENDED_MASK))) {
            SDL_UCS4ToUTF8(typed, text);
            chosen = text;
        }
    }
    std::size_t n = chosen ? std::strlen(chosen) : 0;
    if (n >= static_cast<std::size_t>(size)) {
        n = static_cast<std::size_t>(size) - 1;
        while (n > 0 && (static_cast<unsigned char>(chosen[n]) & 0xC0) == 0x80) --n;
    }
    if (n) std::memcpy(name, chosen, n);
    name[n] = '\0';
    return n != 0;
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
