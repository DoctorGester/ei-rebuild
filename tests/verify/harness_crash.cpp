// Crash reports and the "diskfull" test step (harness_crash.h; ticket X40).
#include "harness_crash.h"

#include "../../src/hook/log.h"

#include <cstdio>
#include <cstring>

namespace ei::verify::crash {

namespace {

// An x86 MSVC throw: ExceptionInformation[0] the magic (0x19930520..22), [1] the thrown object's
// address, [2] its throw information. The throw information lists the types a handler can catch it
// as, the thrown type first, each named by its decorated name (".PAVCFileException@@" is a
// CFileException*, ".?AVexception@std@@" a std::exception).
constexpr DWORD kMsvcThrow = 0xE06D7363;

struct TypeDescriptor {
    const void* vtable;
    void* spare;
    char name[1];
};
struct CatchableType {
    unsigned properties;
    const TypeDescriptor* type;
    int displacement[3];
    int size;
    const void* copy;
};
struct CatchableTypes {
    int count;
    const CatchableType* types[1];
};
struct ThrowInfo {
    unsigned attributes;
    const void* unwind;
    const void* forwardCompat;
    const CatchableTypes* catchable;
};

// MFC 4.2's file and archive exceptions (src/lib/mfc_exception.h), thrown as pointers.
struct FileException {
    const void* vtable;
    BOOL autoDelete;
    int cause;
    long osError;
    const char* fileName;   // CString
};
struct ArchiveException {
    const void* vtable;
    BOOL autoDelete;
    int cause;
    const char* fileName;   // CString
};
// std::exception, the game's (VC6) and ours alike: the vtable, then the message.
struct StdException {
    const void* vtable;
    const char* what;
};

enum class Kind { other, file, archive, standard };

struct Facts {
    char type[128];
    Kind kind;
    int cause;
    long osError;
    char text[MAX_PATH];   // the file's name or what()
};

// ".PAVCFileException@@" -> "CFileException*", ".?AVexception@std@@" -> "std::exception"; other
// decorations as they are.
void Undecorate(const char* name, char* out, std::size_t size) {
    const char* p = name;
    if (*p == '.') ++p;
    if (*p == '?') ++p;
    // "PAV<name>@@" a pointer to a class (P, cv-qualifier A none / B const, class V), "AV<name>@@" the class.
    bool pointer = false;
    if ((p[0] == 'P' || p[0] == 'Q') && (p[1] == 'A' || p[1] == 'B')) {
        pointer = true;
        p += 1;
    }
    if (!((p[0] == 'A' || p[0] == 'B') && (p[1] == 'V' || p[1] == 'U'))) {
        strncpy_s(out, size, name, _TRUNCATE);
        return;
    }
    p += 2;
    const char* parts[8];
    std::size_t lengths[8];
    int count = 0;
    while (*p && *p != '@' && count < 8) {
        const char* at = std::strchr(p, '@');
        if (!at) break;
        parts[count] = p;
        lengths[count++] = static_cast<std::size_t>(at - p);
        p = at + 1;
    }
    if (count == 0) {
        strncpy_s(out, size, name, _TRUNCATE);
        return;
    }
    out[0] = '\0';
    for (int i = count - 1; i >= 0; --i) {
        strncat_s(out, size, parts[i], lengths[i] < size ? lengths[i] : size - 1);
        if (i) strncat_s(out, size, "::", _TRUNCATE);
    }
    if (pointer) strncat_s(out, size, "*", _TRUNCATE);
}

void CopyText(char* out, std::size_t size, const char* text) {
    if (text) strncpy_s(out, size, text, _TRUNCATE);
}

// Reads the throw; false when anything in it is not readable. No C++ objects here (__try).
bool ReadThrow(const EXCEPTION_RECORD* record, Facts* facts) {
    __try {
        const auto* info = reinterpret_cast<const ThrowInfo*>(record->ExceptionInformation[2]);
        const auto* object = reinterpret_cast<const void*>(record->ExceptionInformation[1]);
        if (!info || !info->catchable || info->catchable->count <= 0) return false;
        const int count = info->catchable->count < 32 ? info->catchable->count : 32;
        Undecorate(info->catchable->types[0]->type->name, facts->type, sizeof(facts->type));
        for (int i = 0; i < count && facts->kind == Kind::other; ++i) {
            const char* name = info->catchable->types[i]->type->name;
            if (std::strcmp(name, ".PAVCFileException@@") == 0) facts->kind = Kind::file;
            else if (std::strcmp(name, ".PAVCArchiveException@@") == 0) facts->kind = Kind::archive;
            else if (std::strcmp(name, ".?AVexception@std@@") == 0 || std::strcmp(name, ".?AVexception@@") == 0)
                facts->kind = Kind::standard;
        }
        if (facts->kind == Kind::file) {
            const auto* e = *static_cast<const FileException* const*>(object);
            facts->cause = e->cause;
            facts->osError = e->osError;
            CopyText(facts->text, sizeof(facts->text), e->fileName);
        } else if (facts->kind == Kind::archive) {
            const auto* e = *static_cast<const ArchiveException* const*>(object);
            facts->cause = e->cause;
            CopyText(facts->text, sizeof(facts->text), e->fileName);
        } else if (facts->kind == Kind::standard) {
            CopyText(facts->text, sizeof(facts->text), static_cast<const StdException*>(object)->what);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return facts->type[0] != '\0';
    }
}

const char* FileCause(int cause) {
    static const char* const kCauses[] = {"none", "generic", "fileNotFound", "badPath", "tooManyOpenFiles",
                                          "accessDenied", "invalidFile", "removeCurrentDir", "directoryFull",
                                          "badSeek", "hardIO", "sharingViolation", "lockViolation", "diskFull",
                                          "endOfFile"};
    return cause >= 0 && cause < static_cast<int>(std::size(kCauses)) ? kCauses[cause] : "?";
}

const char* ArchiveCause(int cause) {
    static const char* const kCauses[] = {"none", "generic", "readOnly", "endOfFile", "writeOnly", "badIndex",
                                          "badClass", "badSchema"};
    return cause >= 0 && cause < static_cast<int>(std::size(kCauses)) ? kCauses[cause] : "?";
}

std::string OsErrorText(long error) {
    char text[160] = "";
    DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                             static_cast<DWORD>(error), MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), text,
                             sizeof(text), nullptr);
    while (n > 0 && (text[n - 1] == '\r' || text[n - 1] == '\n' || text[n - 1] == ' ' || text[n - 1] == '.')) text[--n] = '\0';
    return text;
}

// "diskfull": the text to look for (lower case), the handles of the files opened for it, the original WriteFile.
char g_failText[MAX_PATH];
constexpr int kMaxFailing = 64;
HANDLE g_failing[kMaxFailing];
SRWLOCK g_failingLock = SRWLOCK_INIT;
decltype(&::WriteFile) g_writeFile;

bool Failing(HANDLE handle) {
    AcquireSRWLockShared(&g_failingLock);
    bool found = false;
    for (HANDLE h : g_failing) found |= h == handle;
    ReleaseSRWLockShared(&g_failingLock);
    return found;
}

BOOL WINAPI FailingWriteFile(HANDLE file, LPCVOID buffer, DWORD size, LPDWORD written, LPOVERLAPPED overlapped) {
    if (file != INVALID_HANDLE_VALUE && Failing(file)) {
        static LONG logged = 0;
        if (InterlockedIncrement(&logged) <= 5) Log("harness: diskfull: a write of %lu bytes fails with ERROR_DISK_FULL", size);
        if (written) *written = 0;
        SetLastError(ERROR_DISK_FULL);
        return FALSE;
    }
    return g_writeFile(file, buffer, size, written, overlapped);
}

} // namespace

std::string DescribeThrow(const EXCEPTION_RECORD* record) {
    if (!record || record->ExceptionCode != kMsvcThrow || record->NumberParameters < 3) return "";
    Facts facts{};
    if (!ReadThrow(record, &facts)) return "a C++ throw whose type is unreadable";
    std::string out = facts.type;
    char detail[64];
    switch (facts.kind) {
    case Kind::file:
        std::snprintf(detail, sizeof(detail), " (cause %d %s, OS error %ld", facts.cause, FileCause(facts.cause), facts.osError);
        out += detail;
        if (facts.osError > 0) out += ": " + OsErrorText(facts.osError);
        out += std::string(", file '") + facts.text + "')";
        break;
    case Kind::archive:
        std::snprintf(detail, sizeof(detail), " (cause %d %s", facts.cause, ArchiveCause(facts.cause));
        out += detail;
        out += std::string(", file '") + facts.text + "')";
        break;
    case Kind::standard:
        out += std::string(" (what: ") + facts.text + ")";
        break;
    case Kind::other:
        break;
    }
    return out;
}

long long FreeMegabytes(const char* path) {
    ULARGE_INTEGER available{};
    if (!path || !GetDiskFreeSpaceExA(path, &available, nullptr, nullptr)) return -1;
    return static_cast<long long>(available.QuadPart / (1024 * 1024));
}

void FailWritesTo(const char* text, std::uintptr_t writeFileSlot) {
    strncpy_s(g_failText, text, _TRUNCATE);
    _strlwr_s(g_failText);
    if (g_writeFile) return;
    auto* slot = reinterpret_cast<decltype(&::WriteFile)*>(writeFileSlot);
    DWORD old;
    VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old);
    g_writeFile = *slot;
    *slot = &FailingWriteFile;
    VirtualProtect(slot, sizeof(*slot), old, &old);
    Log("harness: diskfull: writes to files whose path contains '%s' fail from now on", g_failText);
}

void OnFileOpened(const char* path, HANDLE handle, bool write) {
    if (!g_failText[0] || handle == INVALID_HANDLE_VALUE) return;
    char lower[MAX_PATH];
    strncpy_s(lower, path ? path : "", _TRUNCATE);
    _strlwr_s(lower);
    const bool fail = write && std::strstr(lower, g_failText) != nullptr;
    AcquireSRWLockExclusive(&g_failingLock);
    // A handle value comes back once its file is closed: one opened for another file is taken off.
    for (HANDLE& h : g_failing)
        if (h == handle) h = nullptr;
    if (fail) {
        for (HANDLE& h : g_failing) {
            if (h) continue;
            h = handle;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_failingLock);
}

} // namespace ei::verify::crash
