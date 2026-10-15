// Checks for the game CRT's files and the rest of its heap (src/lib/crt_file.cpp, crt.cpp; S4b). The hook
// build leaves them to the exe (EI_REPLACE_STANDALONE), so a start-up check runs ours beside the
// originals (the forwarders in crt.h) on scratch files in a folder of its own under %TEMP%:
// - fopen in 20 modes (translation, update, append, the letters VC6 stops at), then the same reads,
//   seeks, tells, writes and fflush (S4d; also of every stream) on each side's own copy of the file:
//   every result, the bytes read, and the file's bytes after fclose (a text file opened for update loses
//   a trailing Ctrl-Z);
// - _open with 12 flag sets (created read-only, exclusive, missing), _filelength, _lseek, _read, _write,
//   _close the same way, and bad descriptors;
// - _access (modes 0, 2, 4, 6 on a file, a read-only file, a folder, a missing path) and _findfirst
//   (attributes, times, size, name), against the original.
// The heap ports are checked ours only: calloc zeroed, realloc keeps the bytes and frees at size 0,
// _msize, _strdup. Run at start-up by every verify scenario (smoke reaches it).
#include "verify.h"

#include "../../src/lib/crt.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

using ei::verify::Check;
using Trace = std::vector<long>;

// The stream functions of one side.
struct Streams {
    gamecrt::CrtFile*(__cdecl* open)(const char*, const char*);
    int(__cdecl* close)(gamecrt::CrtFile*);
    int(__cdecl* seek)(gamecrt::CrtFile*, long, int);
    long(__cdecl* tell)(gamecrt::CrtFile*);
    std::size_t(__cdecl* read)(void*, std::size_t, std::size_t, gamecrt::CrtFile*);
    std::size_t(__cdecl* write)(const void*, std::size_t, std::size_t, gamecrt::CrtFile*);
    int(__cdecl* flush)(gamecrt::CrtFile*);
};
const Streams kOriginalStreams{gamecrt::fopen, gamecrt::fclose, gamecrt::fseek, gamecrt::ftell, gamecrt::fread, gamecrt::fwrite, gamecrt::fflush};
const Streams kOurStreams{libcrt::Fopen, libcrt::Fclose, libcrt::Fseek, libcrt::Ftell, libcrt::Fread, libcrt::Fwrite, libcrt::Fflush};

// The descriptor functions of one side.
struct Descriptors {
    int(__cdecl* open)(const char*, int, ...);
    int(__cdecl* read)(int, void*, unsigned);
    int(__cdecl* write)(int, const void*, unsigned);
    long(__cdecl* seek)(int, long, int);
    int(__cdecl* close)(int);
    long(__cdecl* length)(int);
};
const Descriptors kOriginalDescriptors{gamecrt::open, gamecrt::read, gamecrt::write, gamecrt::lseek, gamecrt::close, gamecrt::filelength};
const Descriptors kOurDescriptors{libcrt::Open, libcrt::Read, libcrt::Write, libcrt::Lseek, libcrt::Close, libcrt::Filelength};

const char kText[] = "one\r\ntwo\nthree\r\r\nfour\x1A after\r\n";
const char kCtrlZAtEnd[] = "alpha\r\nbeta\x1A";

std::string g_folder;   // with a trailing backslash

std::string PathOf(const char* side, int number) {
    char name[32];
    std::snprintf(name, sizeof name, "%s%d.tmp", side, number);
    return g_folder + name;
}

void MakeFile(const std::string& path, const char* bytes) {
    SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    const HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, bytes, static_cast<DWORD>(std::strlen(bytes)), &written, nullptr);
    CloseHandle(file);
}

// The file's bytes (then its read-only bit), or -1 when it is missing.
Trace FileState(const std::string& path) {
    Trace state;
    const DWORD attributes = GetFileAttributesA(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) return {-1};
    // A scanner can hold a just-written file for a moment (seen once under load: ours read as empty).
    HANDLE file = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 50; ++attempt) {
        file = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file != INVALID_HANDLE_VALUE || GetLastError() != ERROR_SHARING_VIOLATION) break;
        Sleep(10);
    }
    if (file != INVALID_HANDLE_VALUE) {
        char buffer[256];
        DWORD got = 0;
        if (ReadFile(file, buffer, sizeof buffer, &got, nullptr))
            for (DWORD i = 0; i < got; ++i) state.push_back(buffer[i]);
        CloseHandle(file);
    }
    state.push_back((attributes & FILE_ATTRIBUTE_READONLY) ? 1 : 0);
    return state;
}

void CompareTraces(const char* name, const char* what, const Trace& expected, const Trace& actual) {
    std::size_t first = 0;
    while (first < expected.size() && first < actual.size() && expected[first] == actual[first]) ++first;
    const bool same = expected.size() == actual.size() && first == expected.size();
    Check(name, true, same, "%s: step %u: %ld, ours %ld (%u / %u steps)", what, static_cast<unsigned>(first),
          first < expected.size() ? expected[first] : -999L, first < actual.size() ? actual[first] : -999L,
          static_cast<unsigned>(expected.size()), static_cast<unsigned>(actual.size()));
}

Trace StreamScript(const Streams& s, const std::string& path, const char* mode) {
    Trace trace;
    gamecrt::CrtFile* file = s.open(path.c_str(), mode);
    trace.push_back(file != nullptr);
    if (!file) return trace;
    const auto tell = [&] { trace.push_back(s.tell(file)); };
    const auto read = [&](std::size_t count) {
        char buffer[64] = {};
        const std::size_t got = s.read(buffer, 1, count, file);
        trace.push_back(static_cast<long>(got));
        for (std::size_t i = 0; i < got; ++i) trace.push_back(buffer[i]);
        tell();
    };
    const auto write = [&](const char* text) {
        trace.push_back(static_cast<long>(s.write(text, 1, std::strlen(text), file)));
        tell();
    };
    const auto seek = [&](long offset, int origin) {
        trace.push_back(s.seek(file, offset, origin));
        tell();
    };
    tell();
    read(5);
    read(3);
    seek(0, SEEK_END);
    seek(3, SEEK_SET);
    read(40);
    seek(-4, SEEK_CUR);
    read(2);
    seek(2, SEEK_SET);
    write("A\nB");
    trace.push_back(s.flush(file));
    tell();
    seek(0, SEEK_CUR);
    read(4);
    seek(0, SEEK_END);
    write("end\n");
    trace.push_back(s.flush(file));
    trace.push_back(s.flush(nullptr));
    seek(0, SEEK_SET);
    read(60);
    trace.push_back(s.close(file));
    return trace;
}

Trace DescriptorScript(const Descriptors& d, const std::string& path, int flags, int permissions) {
    Trace trace;
    const int fd = d.open(path.c_str(), flags, permissions);
    trace.push_back(fd >= 0);
    if (fd < 0) return trace;
    const auto read = [&](unsigned count) {
        char buffer[64] = {};
        const int got = d.read(fd, buffer, count);
        trace.push_back(got);
        for (int i = 0; i < got; ++i) trace.push_back(buffer[i]);
        trace.push_back(d.seek(fd, 0, SEEK_CUR));
    };
    trace.push_back(d.length(fd));
    trace.push_back(d.seek(fd, 0, SEEK_END));
    trace.push_back(d.seek(fd, 3, SEEK_SET));
    read(50);
    trace.push_back(d.seek(fd, 1, SEEK_SET));
    trace.push_back(d.write(fd, "x\ny", 3));
    trace.push_back(d.seek(fd, 0, SEEK_CUR));
    trace.push_back(d.length(fd));
    trace.push_back(d.seek(fd, 0, SEEK_SET));
    read(60);
    trace.push_back(d.close(fd));
    return trace;
}

Trace BadDescriptors(const Descriptors& d) {
    char buffer[4];
    return {d.read(-1, buffer, 4), d.write(5000, buffer, 1), d.seek(12345, 0, SEEK_SET), d.length(-1), d.close(-5)};
}

int g_fileNumber = 0;

void CheckStreams(const char* mode, const char* contents) {
    const int number = ++g_fileNumber;
    const std::string original = PathOf("o", number);
    const std::string ours = PathOf("u", number);
    if (contents) {
        MakeFile(original, contents);
        MakeFile(ours, contents);
    }
    char what[64];
    std::snprintf(what, sizeof what, "\"%s\" on %s", mode, contents ? (contents == kText ? "text" : "Ctrl-Z end") : "missing");
    CompareTraces("fopen", what, StreamScript(kOriginalStreams, original, mode), StreamScript(kOurStreams, ours, mode));
    CompareTraces("fclose", what, FileState(original), FileState(ours));
}

void CheckDescriptors(int flags, int permissions, const char* contents) {
    const int number = ++g_fileNumber;
    const std::string original = PathOf("o", number);
    const std::string ours = PathOf("u", number);
    if (contents) {
        MakeFile(original, contents);
        MakeFile(ours, contents);
    }
    char what[64];
    std::snprintf(what, sizeof what, "flags %04X on %s", static_cast<unsigned>(flags),
                  contents ? (contents == kText ? "text" : "Ctrl-Z end") : "missing");
    CompareTraces("_open", what, DescriptorScript(kOriginalDescriptors, original, flags, permissions),
                  DescriptorScript(kOurDescriptors, ours, flags, permissions));
    CompareTraces("_close", what, FileState(original), FileState(ours));
}

void CheckAccess(const std::string& path) {
    for (int mode : {0, 2, 4, 6}) {
        Check("_access", gamecrt::access(path.c_str(), mode), libcrt::Access(path.c_str(), mode), "%s mode %d", path.c_str(), mode);
    }
}

void CheckFindFirst(const std::string& pattern) {
    gamecrt::FindData expected{};
    gamecrt::FindData actual{};
    const long expectedHandle = gamecrt::findfirst(pattern.c_str(), &expected);
    const long actualHandle = libcrt::Findfirst(pattern.c_str(), &actual);
    Check("_findfirst", expectedHandle != -1, actualHandle != -1, "%s", pattern.c_str());
    if (expectedHandle != -1) FindClose(reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(expectedHandle)));
    if (actualHandle != -1) FindClose(reinterpret_cast<HANDLE>(static_cast<std::intptr_t>(actualHandle)));
    if (expectedHandle == -1 || actualHandle == -1) return;
    Check("_findfirst", expected.attrib, actual.attrib, "%s: attributes", pattern.c_str());
    Check("_findfirst", expected.createTime, actual.createTime, "%s: created", pattern.c_str());
    Check("_findfirst", expected.accessTime, actual.accessTime, "%s: accessed", pattern.c_str());
    Check("_findfirst", expected.writeTime, actual.writeTime, "%s: written", pattern.c_str());
    Check("_findfirst", expected.size, actual.size, "%s: size", pattern.c_str());
    Check("_findfirst", true, std::strcmp(expected.name, actual.name) == 0, "%s: \"%s\", ours \"%s\"", pattern.c_str(),
          expected.name, actual.name);
}

void CheckHeap() {
    auto* zeroed = static_cast<unsigned char*>(libcrt::Calloc(10, 7));
    bool allZero = zeroed != nullptr;
    for (int i = 0; zeroed && i < 70; ++i) allZero = allZero && zeroed[i] == 0;
    Check("calloc", true, allZero, "10 x 7 bytes zeroed");
    Check("_msize", std::size_t{70}, zeroed ? libcrt::Msize(zeroed) : 0, "calloc(10, 7)");
    libcrt::Free(zeroed);

    auto* block = static_cast<unsigned char*>(libcrt::Realloc(nullptr, 16));
    for (int i = 0; block && i < 16; ++i) block[i] = static_cast<unsigned char>(i * 7);
    auto* grown = static_cast<unsigned char*>(libcrt::Realloc(block, 4000));
    bool kept = grown != nullptr;
    for (int i = 0; grown && i < 16; ++i) kept = kept && grown[i] == static_cast<unsigned char>(i * 7);
    Check("realloc", true, kept, "16 bytes kept when grown to 4000");
    Check("_msize", std::size_t{4000}, grown ? libcrt::Msize(grown) : 0, "realloc to 4000");
    Check("realloc", true, libcrt::Realloc(grown, 0) == nullptr, "size 0 frees and gives null");

    char* copy = libcrt::Strdup("strdup text");
    Check("_strdup", true, copy && std::strcmp(copy, "strdup text") == 0 && libcrt::Msize(copy) == 12, "a copy of 12 bytes");
    libcrt::Free(copy);
    Check("_strdup", true, libcrt::Strdup(nullptr) == nullptr, "null gives null");
}

void RemoveFolder() {
    WIN32_FIND_DATAA found;
    const HANDLE search = FindFirstFileA((g_folder + "*").c_str(), &found);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            const std::string path = g_folder + found.cFileName;
            if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            SetFileAttributesA(path.c_str(), FILE_ATTRIBUTE_NORMAL);
            DeleteFileA(path.c_str());
        } while (FindNextFileA(search, &found));
        FindClose(search);
    }
    RemoveDirectoryA(g_folder.c_str());
}

void CrtFileStartupCheck() {
    char temp[MAX_PATH];
    if (!GetTempPathA(MAX_PATH, temp)) return;
    char folder[MAX_PATH];
    std::snprintf(folder, sizeof folder, "%sei_crt_file_%lu\\", temp, GetCurrentProcessId());
    g_folder = folder;
    CreateDirectoryA(g_folder.c_str(), nullptr);

    for (const char* mode : {"rb", "rt", "r", "r+b", "r+t", "r+", "wb", "wt", "w+b", "w+t", "ab", "at", "a+b", "a+t",
                             "rbt", "rx+", "rc", "rS", "r+bR", "q"})
        CheckStreams(mode, kText);
    for (const char* mode : {"r+t", "r+b", "a+t", "rt"}) CheckStreams(mode, kCtrlZAtEnd);
    for (const char* mode : {"r", "w", "a+"}) CheckStreams(mode, nullptr);

    for (int flags : {_O_RDONLY | _O_BINARY, _O_RDONLY | _O_TEXT, _O_RDONLY, _O_RDWR | _O_BINARY, _O_RDWR | _O_TEXT,
                      _O_WRONLY | _O_APPEND | _O_TEXT, _O_RDWR | _O_CREAT | _O_TRUNC | _O_BINARY, _O_RDWR | _O_CREAT | _O_EXCL})
        CheckDescriptors(flags, _S_IREAD | _S_IWRITE, kText);
    CheckDescriptors(_O_RDWR | _O_TEXT, 0, kCtrlZAtEnd);
    CheckDescriptors(_O_RDONLY, 0, nullptr);
    CheckDescriptors(_O_RDWR | _O_CREAT | _O_BINARY, _S_IREAD, nullptr);   // created read-only
    CheckDescriptors(_O_WRONLY | _O_CREAT | _O_EXCL | _O_TEXT, _S_IREAD | _S_IWRITE, nullptr);
    CompareTraces("_read", "bad descriptors", BadDescriptors(kOriginalDescriptors), BadDescriptors(kOurDescriptors));

    const std::string readOnly = PathOf("readonly", 0);
    MakeFile(readOnly, kText);
    SetFileAttributesA(readOnly.c_str(), FILE_ATTRIBUTE_READONLY);
    CheckAccess(PathOf("o", 1));
    CheckAccess(readOnly);
    CheckAccess(g_folder);
    CheckAccess(PathOf("missing", 0));
    CheckFindFirst(PathOf("o", 1));
    CheckFindFirst(readOnly);
    CheckFindFirst(g_folder + "*.tmp");
    CheckFindFirst(PathOf("missing", 0));

    CheckHeap();
    RemoveFolder();
}
EI_STARTUP_CHECK(CrtFileStartupCheck);

} // namespace
