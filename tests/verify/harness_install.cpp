// No harness run writes into a game install: kernel32's slots send the writes to the run's overlay, ntdll's
// calls refuse whatever still reaches an install. See harness_install.h.
#include "harness_install.h"

#include "../../src/hook/log.h"

#include <windows.h>
#include <winternl.h>
#include <intrin.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <set>
#include <vector>

namespace ei::verify::install {
namespace {

using ei::Log;

Overlay g_overlay;
SRWLOCK g_lock = SRWLOCK_INIT;

// The module's state, made on first use (the harness's static constructor installs it) and never destroyed:
// the guard still sees calls after the static destructors (other modules' DLL_PROCESS_DETACH).
struct State {
    std::set<std::string> problemSet;
    std::vector<std::string> problems;
    std::set<std::string> refused;     // calls and paths refused (each reported once)
    std::vector<std::wstring> roots;   // each install root as a DOS path and as a device path, no trailing backslash
};
State& S() {
    static State* state = new State;
    return *state;
}

constexpr std::size_t kMaxProblems = 24;

void Problem(const std::string& what) {
    AcquireSRWLockExclusive(&g_lock);
    const bool fresh = S().problems.size() < kMaxProblems && S().problemSet.insert(what).second;
    if (fresh) S().problems.push_back(what);
    ReleaseSRWLockExclusive(&g_lock);
    if (fresh) Log("harness: INSTALL: %s", what.c_str());
}

// ---- the redirect: kernel32's slots ----

// `path` in the ANSI code page; false when it does not convert exactly (the guard then sees the write).
bool ToAnsi(const wchar_t* path, char (&out)[MAX_PATH]) {
    BOOL lossy = FALSE;
    return path && WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, path, -1, out, MAX_PATH, nullptr, &lossy) > 0 &&
           !lossy;
}

bool ToWide(const char* path, wchar_t (&out)[MAX_PATH]) {
    return MultiByteToWideChar(CP_ACP, 0, path, -1, out, MAX_PATH) > 0;
}

bool KeepsFile(DWORD disposition) {
    return disposition == OPEN_EXISTING || disposition == OPEN_ALWAYS;
}

// The file a write of `path` goes to: the overlay's for an install file, else `path`.
const char* WriteA(const char* path, char (&buffer)[MAX_PATH], bool keep) {
    if (!path || !g_overlay.inInstall(path)) return path;
    return g_overlay.write(path, buffer, keep);
}

const wchar_t* WriteW(const wchar_t* path, wchar_t (&buffer)[MAX_PATH], bool keep) {
    char narrow[MAX_PATH], b[MAX_PATH];
    if (!ToAnsi(path, narrow) || !g_overlay.inInstall(narrow)) return path;
    const char* target = g_overlay.write(narrow, b, keep);
    return target != narrow && ToWide(target, buffer) ? buffer : path;
}

using CreateFile2Fn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, DWORD, void*);
using MoveFileWithProgressTransactedWFn = BOOL(WINAPI*)(LPCWSTR, LPCWSTR, LPPROGRESS_ROUTINE, LPVOID, DWORD, HANDLE);

decltype(&::CreateFileA) g_createFileA;
decltype(&::CreateFileW) g_createFileW;
CreateFile2Fn g_createFile2;
decltype(&::DeleteFileA) g_deleteFileA;
decltype(&::DeleteFileW) g_deleteFileW;
decltype(&::CreateDirectoryA) g_createDirectoryA;
decltype(&::CreateDirectoryW) g_createDirectoryW;
decltype(&::RemoveDirectoryA) g_removeDirectoryA;
decltype(&::RemoveDirectoryW) g_removeDirectoryW;
decltype(&::SetFileAttributesA) g_setFileAttributesA;
decltype(&::SetFileAttributesW) g_setFileAttributesW;
decltype(&::CopyFileW) g_copyFileW;
decltype(&::CopyFileExW) g_copyFileExW;
decltype(&::MoveFileExW) g_moveFileExW;
decltype(&::MoveFileWithProgressW) g_moveFileWithProgressW;
MoveFileWithProgressTransactedWFn g_moveFileWithProgressTransactedW;

HANDLE WINAPI CreateFileAHook(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition,
                              DWORD flags, HANDLE templ) {
    char b[MAX_PATH];
    if (WritesFile(access, disposition)) name = WriteA(name, b, KeepsFile(disposition));
    return g_createFileA(name, access, share, sa, disposition, flags, templ);
}
HANDLE WINAPI CreateFileWHook(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition,
                              DWORD flags, HANDLE templ) {
    wchar_t b[MAX_PATH];
    if (WritesFile(access, disposition)) name = WriteW(name, b, KeepsFile(disposition));
    return g_createFileW(name, access, share, sa, disposition, flags, templ);
}
HANDLE WINAPI CreateFile2Hook(LPCWSTR name, DWORD access, DWORD share, DWORD disposition, void* parameters) {
    wchar_t b[MAX_PATH];
    if (WritesFile(access, disposition)) name = WriteW(name, b, KeepsFile(disposition));
    return g_createFile2(name, access, share, disposition, parameters);
}
BOOL WINAPI DeleteFileAHook(LPCSTR name) {
    char b[MAX_PATH];
    return g_deleteFileA(WriteA(name, b, false));
}
BOOL WINAPI DeleteFileWHook(LPCWSTR name) {
    wchar_t b[MAX_PATH];
    return g_deleteFileW(WriteW(name, b, false));
}
BOOL WINAPI CreateDirectoryAHook(LPCSTR name, LPSECURITY_ATTRIBUTES sa) {
    char b[MAX_PATH];
    return g_createDirectoryA(WriteA(name, b, false), sa);
}
BOOL WINAPI CreateDirectoryWHook(LPCWSTR name, LPSECURITY_ATTRIBUTES sa) {
    wchar_t b[MAX_PATH];
    return g_createDirectoryW(WriteW(name, b, false), sa);
}
BOOL WINAPI RemoveDirectoryAHook(LPCSTR name) {
    char b[MAX_PATH];
    return g_removeDirectoryA(WriteA(name, b, false));
}
BOOL WINAPI RemoveDirectoryWHook(LPCWSTR name) {
    wchar_t b[MAX_PATH];
    return g_removeDirectoryW(WriteW(name, b, false));
}
BOOL WINAPI SetFileAttributesAHook(LPCSTR name, DWORD attributes) {
    char b[MAX_PATH];
    return g_setFileAttributesA(WriteA(name, b, true), attributes);
}
BOOL WINAPI SetFileAttributesWHook(LPCWSTR name, DWORD attributes) {
    wchar_t b[MAX_PATH];
    return g_setFileAttributesW(WriteW(name, b, true), attributes);
}
BOOL WINAPI CopyFileWHook(LPCWSTR from, LPCWSTR to, BOOL failIfExists) {
    wchar_t b[MAX_PATH];
    return g_copyFileW(from, WriteW(to, b, false), failIfExists);
}
BOOL WINAPI CopyFileExWHook(LPCWSTR from, LPCWSTR to, LPPROGRESS_ROUTINE progress, LPVOID data, LPBOOL cancel,
                            DWORD flags) {
    wchar_t b[MAX_PATH];
    return g_copyFileExW(from, WriteW(to, b, false), progress, data, cancel, flags);
}

// A move of `from` to `to` (each kernel32 move ends in one of three calls, `move`): the target goes to the
// overlay; an install file is never moved away: its overlay copy is, or without one it is copied (as
// harness.cpp's RedirectedMoveFile does for the game).
template <class Move>
BOOL MoveW(LPCWSTR from, LPCWSTR to, DWORD flags, Move move) {
    wchar_t sourceBuffer[MAX_PATH], targetBuffer[MAX_PATH];
    const wchar_t* target = to ? WriteW(to, targetBuffer, false) : to;
    char narrow[MAX_PATH];
    if (ToAnsi(from, narrow) && g_overlay.inInstall(narrow)) {
        char b[MAX_PATH];
        const char* copy = g_overlay.write(narrow, b, false);
        if (copy == narrow || GetFileAttributesA(copy) == INVALID_FILE_ATTRIBUTES) {
            if (!target) {   // a delete at the next restart
                SetLastError(ERROR_ACCESS_DENIED);
                return FALSE;
            }
            Log("harness: install file moved: copied into the overlay instead (%s)", narrow);
            return g_copyFileW(from, target, (flags & MOVEFILE_REPLACE_EXISTING) == 0);
        }
        if (ToWide(copy, sourceBuffer)) from = sourceBuffer;
    }
    return move(from, target);
}
BOOL WINAPI MoveFileExWHook(LPCWSTR from, LPCWSTR to, DWORD flags) {
    return MoveW(from, to, flags, [&](LPCWSTR f, LPCWSTR t) { return g_moveFileExW(f, t, flags); });
}
BOOL WINAPI MoveFileWithProgressWHook(LPCWSTR from, LPCWSTR to, LPPROGRESS_ROUTINE progress, LPVOID data, DWORD flags) {
    return MoveW(from, to, flags,
                 [&](LPCWSTR f, LPCWSTR t) { return g_moveFileWithProgressW(f, t, progress, data, flags); });
}
BOOL WINAPI MoveFileWithProgressTransactedWHook(LPCWSTR from, LPCWSTR to, LPPROGRESS_ROUTINE progress, LPVOID data,
                                                DWORD flags, HANDLE transaction) {
    return MoveW(from, to, flags, [&](LPCWSTR f, LPCWSTR t) {
        return g_moveFileWithProgressTransactedW(f, t, progress, data, flags, transaction);
    });
}

struct Slot {
    const char* name;   // the import's name in kernel32's import table (from whichever API set)
    void* replacement;
    void** original;
    bool taken;
};

template <class T>
Slot Take(const char* name, T replacement, T& original) {
    return {name, reinterpret_cast<void*>(replacement), reinterpret_cast<void**>(&original), false};
}

// Every slot of kernel32's import table whose import is named in `slots` gets its replacement.
void TakeSlots(HMODULE module, std::vector<Slot>& slots) {
    auto* base = reinterpret_cast<BYTE*>(module);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto* d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress); d->Name; ++d) {
        if (!d->OriginalFirstThunk) continue;
        const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + d->OriginalFirstThunk);
        auto* addresses = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++addresses) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            const char* name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name;
            for (Slot& slot : slots) {
                if (std::strcmp(name, slot.name) != 0) continue;
                auto* entry = reinterpret_cast<void**>(&addresses->u1.Function);
                DWORD old;
                VirtualProtect(entry, sizeof(*entry), PAGE_READWRITE, &old);
                if (!slot.taken) *slot.original = *entry;
                *entry = slot.replacement;
                VirtualProtect(entry, sizeof(*entry), old, &old);
                slot.taken = true;
            }
        }
    }
}

void TakeKernel32() {
    std::vector<Slot> slots = {
        Take("CreateFileA", &CreateFileAHook, g_createFileA),
        Take("CreateFileW", &CreateFileWHook, g_createFileW),
        Take("CreateFile2", &CreateFile2Hook, g_createFile2),
        Take("DeleteFileA", &DeleteFileAHook, g_deleteFileA),
        Take("DeleteFileW", &DeleteFileWHook, g_deleteFileW),
        Take("CreateDirectoryA", &CreateDirectoryAHook, g_createDirectoryA),
        Take("CreateDirectoryW", &CreateDirectoryWHook, g_createDirectoryW),
        Take("RemoveDirectoryA", &RemoveDirectoryAHook, g_removeDirectoryA),
        Take("RemoveDirectoryW", &RemoveDirectoryWHook, g_removeDirectoryW),
        Take("SetFileAttributesA", &SetFileAttributesAHook, g_setFileAttributesA),
        Take("SetFileAttributesW", &SetFileAttributesWHook, g_setFileAttributesW),
        Take("CopyFileW", &CopyFileWHook, g_copyFileW),
        Take("CopyFileExW", &CopyFileExWHook, g_copyFileExW),   // CopyFileA's
        Take("MoveFileExW", &MoveFileExWHook, g_moveFileExW),
        Take("MoveFileWithProgressW", &MoveFileWithProgressWHook, g_moveFileWithProgressW),   // MoveFileW's
        // MoveFileA's, MoveFileExA's, MoveFileWithProgressA's
        Take("MoveFileWithProgressTransactedW", &MoveFileWithProgressTransactedWHook, g_moveFileWithProgressTransactedW),
    };
    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    if (!kernel32) {
        Problem("kernel32.dll not loaded: other modules' writes are not redirected");
        return;
    }
    TakeSlots(kernel32, slots);
    for (const Slot& slot : slots)
        if (!slot.taken) Problem(std::string("kernel32 does not import ") + slot.name + ": writes through it are not redirected");
}

// ---- the guard: ntdll's calls ----

constexpr NTSTATUS kAccessDenied = static_cast<NTSTATUS>(0xC0000022L);
constexpr ULONG kFileOpen = 1;                  // FILE_OPEN: the only disposition that never creates or replaces
constexpr ULONG kFileDeleteOnClose = 0x1000;    // FILE_DELETE_ON_CLOSE
constexpr ACCESS_MASK kWriteAccess = FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | DELETE |
                                     WRITE_DAC | WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL;
// FILE_INFORMATION_CLASS values that rename, link or delete.
constexpr ULONG kFileRename = 10, kFileLink = 11, kFileDisposition = 13, kFileDispositionEx = 64, kFileRenameEx = 65,
                kFileLinkEx = 72;

// The target of a rename or link (FILE_RENAME_INFORMATION / FILE_LINK_INFORMATION, the Ex forms alike).
struct RenameInformation {
    ULONG flags;   // ReplaceIfExists in the plain forms
    HANDLE root;
    ULONG nameBytes;
    WCHAR name[1];
};

using NtCreateFileFn = NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG,
                                        ULONG, ULONG, ULONG, PVOID, ULONG);
using NtOpenFileFn = NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK, ULONG, ULONG);
using NtSetInformationFileFn = NTSTATUS(NTAPI*)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, ULONG);
using NtDeleteFileFn = NTSTATUS(NTAPI*)(POBJECT_ATTRIBUTES);
NtCreateFileFn g_ntCreateFile;
NtOpenFileFn g_ntOpenFile;
NtSetInformationFileFn g_ntSetInformationFile;
NtDeleteFileFn g_ntDeleteFile;

// The guard's own calls (GetFinalPathNameByHandleW, the report) pass through: a thread-local depth.
DWORD g_busySlot = TLS_OUT_OF_INDEXES;
struct Busy {
    bool outer;
    Busy() : outer(TlsGetValue(g_busySlot) == nullptr) {
        if (outer) TlsSetValue(g_busySlot, reinterpret_cast<void*>(1));
    }
    ~Busy() {
        if (outer) TlsSetValue(g_busySlot, nullptr);
    }
};

constexpr std::size_t kPathChars = 1024;

// The path of an open handle ("F:\..."); false when it has none.
bool HandlePath(HANDLE handle, wchar_t (&out)[kPathChars]) {
    const DWORD n = GetFinalPathNameByHandleW(handle, out, kPathChars, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (n == 0 || n >= kPathChars) return false;
    if (std::wcsncmp(out, L"\\\\?\\", 4) == 0 && out[5] == L':') std::wmemmove(out, out + 4, n - 4 + 1);
    return true;
}

// The path an NT name gives (`name`, `bytes` long, below the directory `root` when there is one): "F:\..." for
// \??\F:\..., else as written (a device path); false when it cannot be told.
bool NamePath(HANDLE root, const WCHAR* name, std::size_t bytes, wchar_t (&out)[kPathChars]) {
    std::size_t at = 0;
    if (root) {
        if (!HandlePath(root, out)) return false;
        at = std::wcslen(out);
        if (bytes && at && out[at - 1] != L'\\') out[at++] = L'\\';
    }
    const std::size_t chars = bytes / sizeof(WCHAR);
    if (!name || at + chars >= kPathChars) return name == nullptr && root;
    std::wmemcpy(out + at, name, chars);
    out[at + chars] = L'\0';
    for (const wchar_t* prefix : {L"\\??\\", L"\\DosDevices\\", L"\\GLOBAL??\\"}) {
        const std::size_t n = std::wcslen(prefix);
        if (_wcsnicmp(out, prefix, n) == 0) {
            std::wmemmove(out, out + n, std::wcslen(out + n) + 1);
            break;
        }
    }
    return true;
}

bool UnderRoot(const wchar_t* path) {
    for (const std::wstring& root : S().roots) {
        const std::size_t n = root.size();
        if (_wcsnicmp(path, root.c_str(), n) == 0 && (path[n] == L'\0' || path[n] == L'\\')) return true;
    }
    return false;
}

// The code addresses on the stack from `stack` (the guarded call's return address) up, outside ntdll,
// kernelbase and kernel32 (the first five, "module+offset"): found by scanning for values that point into a
// module's executable code, since the system's frames keep no EBP chain (RtlCaptureStackBackTrace found none).
std::string Callers(const void* stack) {
    const HMODULE system[] = {GetModuleHandleA("ntdll.dll"), GetModuleHandleA("kernelbase.dll"), GetModuleHandleA("kernel32.dll")};
    const auto* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    const auto* end = static_cast<const std::uintptr_t*>(tib->StackBase);
    std::string text;
    int found = 0;
    for (auto* p = static_cast<const std::uintptr_t*>(stack); p < end && found < 5; ++p) {
        MEMORY_BASIC_INFORMATION info;
        constexpr DWORD kExecutable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if (*p < 0x10000 || !VirtualQuery(reinterpret_cast<const void*>(*p), &info, sizeof(info)) || info.Type != MEM_IMAGE ||
            !(info.Protect & kExecutable))
            continue;
        const auto module = static_cast<HMODULE>(info.AllocationBase);
        if (module == system[0] || module == system[1] || module == system[2]) continue;
        char name[MAX_PATH];
        if (!GetModuleFileNameA(module, name, MAX_PATH)) continue;
        const char* base = std::strrchr(name, '\\');
        char entry[MAX_PATH + 16];
        std::snprintf(entry, sizeof(entry), "%s%s+%X", text.empty() ? "" : ", ", base ? base + 1 : name,
                      unsigned(*p - reinterpret_cast<std::uintptr_t>(module)));
        text += entry;
        ++found;
    }
    return text.empty() ? "no caller found" : "stack: " + text;
}

// Whether `path` lies in an install (then the call is refused and the run fails; each call and path reported once).
bool Refuse(const char* call, const char* label, unsigned long value, const wchar_t* path, const void* stack) {
    if (!UnderRoot(path)) return false;
    char narrow[kPathChars];
    WideCharToMultiByte(CP_ACP, 0, path, -1, narrow, sizeof(narrow), nullptr, nullptr);
    const std::string what = std::string(call) + " " + narrow;
    AcquireSRWLockExclusive(&g_lock);
    const bool fresh = S().refused.insert(what).second;
    ReleaseSRWLockExclusive(&g_lock);
    if (!fresh) return true;
    char detail[64];
    std::snprintf(detail, sizeof(detail), " (%s %lX; ", label, value);
    Problem("write refused: " + what + detail + Callers(stack) + ")");
    return true;
}

bool RefuseName(const char* call, unsigned long access, const OBJECT_ATTRIBUTES* attributes, const void* stack) {
    if (!attributes || !attributes->ObjectName) return false;
    Busy busy;
    if (!busy.outer) return false;
    wchar_t path[kPathChars];
    return NamePath(attributes->RootDirectory, attributes->ObjectName->Buffer, attributes->ObjectName->Length, path) &&
           Refuse(call, "access", access, path, stack);
}

NTSTATUS NTAPI GuardedCreateFile(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PIO_STATUS_BLOCK io,
                                 PLARGE_INTEGER size, ULONG fileAttributes, ULONG share, ULONG disposition, ULONG options,
                                 PVOID ea, ULONG eaBytes) {
    const bool writes = (access & kWriteAccess) || disposition != kFileOpen || (options & kFileDeleteOnClose);
    if (writes && RefuseName("NtCreateFile", access, attributes, _AddressOfReturnAddress())) return kAccessDenied;
    return g_ntCreateFile(handle, access, attributes, io, size, fileAttributes, share, disposition, options, ea, eaBytes);
}

NTSTATUS NTAPI GuardedOpenFile(PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes, PIO_STATUS_BLOCK io,
                               ULONG share, ULONG options) {
    const bool writes = (access & kWriteAccess) || (options & kFileDeleteOnClose);
    if (writes && RefuseName("NtOpenFile", access, attributes, _AddressOfReturnAddress())) return kAccessDenied;
    return g_ntOpenFile(handle, access, attributes, io, share, options);
}

NTSTATUS NTAPI GuardedSetInformationFile(HANDLE handle, PIO_STATUS_BLOCK io, PVOID information, ULONG bytes, ULONG kind) {
    const bool names = kind == kFileRename || kind == kFileLink || kind == kFileRenameEx || kind == kFileLinkEx;
    const bool removes = kind == kFileRename || kind == kFileRenameEx || kind == kFileDisposition || kind == kFileDispositionEx;
    if (names || removes) {
        Busy busy;
        if (busy.outer) {
            wchar_t path[kPathChars];
            if (removes && HandlePath(handle, path) && Refuse("NtSetInformationFile (rename or delete)", "class", kind, path, _AddressOfReturnAddress()))
                return kAccessDenied;
            const auto* target = static_cast<const RenameInformation*>(information);
            if (names && target && bytes >= offsetof(RenameInformation, name) &&
                NamePath(target->root, target->name, target->nameBytes, path) &&
                Refuse("NtSetInformationFile (rename or link target)", "class", kind, path, _AddressOfReturnAddress()))
                return kAccessDenied;
        }
    }
    return g_ntSetInformationFile(handle, io, information, bytes, kind);
}

NTSTATUS NTAPI GuardedDeleteFile(POBJECT_ATTRIBUTES attributes) {
    if (RefuseName("NtDeleteFile", DELETE, attributes, _AddressOfReturnAddress())) return kAccessDenied;
    return g_ntDeleteFile(attributes);
}

// ntdll's system call stubs in a 32-bit process on 64-bit Windows: MOV EAX,<number>; MOV EDX,<the WOW64 call>;
// CALL EDX; RET <bytes>: 15 position-independent bytes, copied whole as the call's original.
constexpr std::size_t kStubBytes = 15;
bool IsStub(const BYTE* p) {
    return p && p[0] == 0xB8 && p[5] == 0xBA && p[10] == 0xFF && p[11] == 0xD2 && p[12] == 0xC2;
}

struct Stub {
    const char* name;
    void* replacement;
    void** original;
    BYTE* code;
};

template <class T>
Stub Guard(const char* name, T replacement, T& original) {
    return {name, reinterpret_cast<void*>(replacement), reinterpret_cast<void**>(&original), nullptr};
}

// A JMP to the replacement over the stub's first five bytes (one 8-byte exchange: no thread sees half of it).
void JumpFrom(BYTE* code, void* replacement) {
    DWORD old;
    VirtualProtect(code, 8, PAGE_EXECUTE_READWRITE, &old);
    BYTE bytes[8];
    std::memcpy(bytes, code, sizeof(bytes));
    bytes[0] = 0xE9;
    const auto relative = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(replacement) -
                                                    reinterpret_cast<std::uintptr_t>(code + 5));
    std::memcpy(bytes + 1, &relative, sizeof(relative));
    LONG64 value;
    std::memcpy(&value, bytes, sizeof(value));
    if (reinterpret_cast<std::uintptr_t>(code) % 8 == 0) InterlockedExchange64(reinterpret_cast<LONG64*>(code), value);
    else std::memcpy(code, bytes, 5);
    VirtualProtect(code, 8, old, &old);
    FlushInstructionCache(GetCurrentProcess(), code, 8);
}

void GuardNtdll() {
    g_busySlot = TlsAlloc();
    if (g_busySlot == TLS_OUT_OF_INDEXES) {
        Problem("no TLS slot: install writes are not guarded");
        return;
    }
    Stub stubs[] = {
        Guard("NtCreateFile", &GuardedCreateFile, g_ntCreateFile),
        Guard("NtOpenFile", &GuardedOpenFile, g_ntOpenFile),
        Guard("NtSetInformationFile", &GuardedSetInformationFile, g_ntSetInformationFile),
        Guard("NtDeleteFile", &GuardedDeleteFile, g_ntDeleteFile),
    };
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    for (Stub& stub : stubs) {
        stub.code = ntdll ? reinterpret_cast<BYTE*>(GetProcAddress(ntdll, stub.name)) : nullptr;
        if (!IsStub(stub.code)) {
            Problem(std::string("ntdll's ") + stub.name + " is not the expected system call stub: install writes are not guarded");
            return;
        }
    }
    auto* copies = static_cast<BYTE*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!copies) {
        Problem("no memory for ntdll's stubs: install writes are not guarded");
        return;
    }
    for (std::size_t i = 0; i < std::size(stubs); ++i) {
        std::memcpy(copies + 16 * i, stubs[i].code, kStubBytes);
        *stubs[i].original = copies + 16 * i;
    }
    DWORD old;
    VirtualProtect(copies, 4096, PAGE_EXECUTE_READ, &old);
    for (Stub& stub : stubs) JumpFrom(stub.code, stub.replacement);
}

// An install root in every form a guarded call may name it: the full path, its long and short forms, and the
// device path ("\Device\HarddiskVolume3\games\...").
void AddRoot(const wchar_t* folder) {
    wchar_t full[kPathChars], other[kPathChars];
    DWORD n = GetFullPathNameW(folder, kPathChars, full, nullptr);
    if (n == 0 || n >= kPathChars) return;
    while (n > 3 && full[n - 1] == L'\\') full[--n] = L'\0';
    std::vector<std::wstring> forms = {full};
    if (GetLongPathNameW(full, other, kPathChars)) forms.push_back(other);
    if (GetShortPathNameW(full, other, kPathChars)) forms.push_back(other);
    for (std::size_t i = 0, count = forms.size(); i < count; ++i) {
        const std::wstring& form = forms[i];
        if (form.size() < 2 || form[1] != L':') continue;
        wchar_t device[kPathChars];
        if (QueryDosDeviceW(form.substr(0, 2).c_str(), device, kPathChars)) forms.push_back(device + form.substr(2));
    }
    for (std::wstring& form : forms) {
        while (form.size() > 3 && form.back() == L'\\') form.pop_back();
        bool known = false;
        for (const std::wstring& root : S().roots) known = known || _wcsicmp(root.c_str(), form.c_str()) == 0;
        if (!known && !form.empty()) S().roots.push_back(form);
    }
}

void FindRoots() {
    wchar_t folder[kPathChars];
    if (GetCurrentDirectoryW(kPathChars, folder)) AddRoot(folder);
    DWORD n = GetEnvironmentVariableW(L"EI_GAME_ROOT", folder, kPathChars);
    if (n && n < kPathChars) AddRoot(folder);
    n = GetEnvironmentVariableW(L"EI_INSTALL_ROOTS", folder, kPathChars);
    if (n && n < kPathChars) {
        for (wchar_t* context = nullptr, *part = wcstok_s(folder, L";", &context); part; part = wcstok_s(nullptr, L";", &context))
            if (*part) AddRoot(part);
    }
    std::string list;
    for (const std::wstring& root : S().roots) {
        char narrow[kPathChars];
        WideCharToMultiByte(CP_ACP, 0, root.c_str(), -1, narrow, sizeof(narrow), nullptr, nullptr);
        list += (list.empty() ? "" : "; ") + std::string(narrow);
    }
    Log("harness: install roots no run writes in: %s", list.c_str());
}

} // namespace

bool WritesFile(unsigned long access, unsigned long disposition) {
    constexpr DWORD kWrites = GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA | DELETE |
                              FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA;
    return (access & kWrites) != 0 || disposition == CREATE_ALWAYS || disposition == CREATE_NEW ||
           disposition == TRUNCATE_EXISTING || disposition == OPEN_ALWAYS;
}

void Install(const Overlay& overlay) {
    g_overlay = overlay;
    FindRoots();
    TakeKernel32();
    GuardNtdll();
}

std::string Failure() {
    AcquireSRWLockShared(&g_lock);
    std::string text;
    for (const std::string& p : S().problems) text += (text.empty() ? "install: " : "; ") + p;
    ReleaseSRWLockShared(&g_lock);
    return text;
}

} // namespace ei::verify::install
