// ei_standalone: runs the rebuilt game without game.exe's code (docs/notes/standalone.md section 2).
//
// The host is linked below game.exe's image base and holds game.exe's address range (0x400000 up to
// its SizeOfImage) inside its own image, as a zero-filled array. There it lays out game.exe's headers
// and its .rdata and .rsrc from the file on disk (nothing from the code section runs: it is filled with
// INT3; .data stays zero: ei_hook.dll keeps the section in its own storage, with bytes from the build,
// src/standalone/image_data.cpp) and resolves game.exe's imports into its import address table. Then it
// loads ei_hook.dll and hands over: the DLL writes a jump at each rewritten function's address
// (InstallHooks) and runs WinMain. Forwarders, function pointers in the data and vtable slots reach our
// code through those jumps; an INT3 hit names an address that has no rewrite yet.
//
// usage: ei_standalone [--exe <game.exe>] [--dll <ei_hook.dll>] [--run] [-- <game arguments>]
//   Run from the game folder (the working directory the game expects). Without --run the host builds
//   the image, installs the jumps, checks them and exits: no window, nothing of the game runs. What
//   follows "--" is the game's command line, as typed (WinMain's, and GetCommandLineA's after the exe).

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr std::uintptr_t kImageBase = 0x00400000;   // game.exe has no relocations
constexpr DWORD kExpectedTimestamp = 0x3ABF0DB3;    // game.exe 1.06 (Russian), 2001-03-26
constexpr DWORD kDataRva = 0x00383000;             // game.exe's .data (00783000): ei_hook.dll's (S18d)
// game.exe's SizeOfImage is 0x3D7000. The host's base (/BASE:0x200000) leaves room below 0x400000
// for its own code and data; the array then covers the whole range wherever the linker puts it.
constexpr std::size_t kReserve = 0x00640000;
unsigned char g_reserve[kReserve];

// game.exe's code section in the image (INT3 until ei_hook.dll writes its jumps).
std::uintptr_t g_textBegin, g_textEnd;

// An INT3 of the image hit before ei_hook.dll's own report is in place (its static constructors run
// at LoadLibrary): named on stderr (ei_launch's run notes) and the process ends, never silently
// (S16c). ei_hook.dll's handler, added later at the front, reports the hits after that itself.
LONG CALLBACK EarlyBreakpoint(EXCEPTION_POINTERS* e) {
    const auto at = reinterpret_cast<std::uintptr_t>(e->ExceptionRecord->ExceptionAddress);
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_BREAKPOINT || at < g_textBegin || at >= g_textEnd)
        return EXCEPTION_CONTINUE_SEARCH;
    std::fprintf(stderr, "ei_standalone: no code at %08X before ei_hook.dll's missing-code report, [esp] %08X\n",
                 static_cast<unsigned>(at), static_cast<unsigned>(*reinterpret_cast<const std::uint32_t*>(e->ContextRecord->Esp)));
    std::fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_CONTINUE_SEARCH;
}

int Fail(const char* what) {
    std::fprintf(stderr, "ei_standalone: %s (error %lu)\n", what, GetLastError());
    return 1;
}

bool ReadWholeFile(const std::string& path, std::vector<unsigned char>& out) {
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
    std::fseek(f, 0, SEEK_END);
    out.resize(static_cast<std::size_t>(std::ftell(f)));
    std::fseek(f, 0, SEEK_SET);
    const bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

bool Exists(const std::string& path) { return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::string Folder(const std::string& path) {
    const std::size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

// An import of game.exe: from game.exe's folder (Rebirth's dgVoodoo DDraw.dll), then the working
// directory (mss32.dll, binkw32.dll in the game root), else System32. Rebirth's dinput.dll beside
// game.exe is a proxy that loads addon.dll, which patches game.exe's code: always the system one.
HMODULE LoadImport(const char* name, const std::string& exeFolder) {
    if (_stricmp(name, "dinput.dll") != 0) {
        char cwd[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, cwd);
        for (const std::string& folder : {exeFolder, std::string(cwd)}) {
            const std::string path = folder + "\\" + name;
            if (Exists(path)) return LoadLibraryA(path.c_str());
        }
    }
    return LoadLibraryExA(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
}

// Fills game.exe's import address table, as the system loader would. Returns the number of
// functions that could not be found.
int ResolveImports(const IMAGE_NT_HEADERS32* nt, const std::string& exeFolder) {
    const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    int missing = 0, resolved = 0;
    for (auto* d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(kImageBase + dir.VirtualAddress); d->Name; ++d) {
        const char* dll = reinterpret_cast<const char*>(kImageBase + d->Name);
        HMODULE module = LoadImport(dll, exeFolder);
        if (!module) {
            std::fprintf(stderr, "ei_standalone: cannot load %s (error %lu)\n", dll, GetLastError());
            ++missing;
            continue;
        }
        const DWORD names = d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk;
        auto* name = reinterpret_cast<const IMAGE_THUNK_DATA32*>(kImageBase + names);
        auto* slot = reinterpret_cast<IMAGE_THUNK_DATA32*>(kImageBase + d->FirstThunk);
        for (; name->u1.AddressOfData; ++name, ++slot) {
            const char* symbol = IMAGE_SNAP_BY_ORDINAL32(name->u1.Ordinal)
                ? reinterpret_cast<const char*>(static_cast<std::uintptr_t>(IMAGE_ORDINAL32(name->u1.Ordinal)))
                : reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(kImageBase + name->u1.AddressOfData)->Name;
            FARPROC fn = GetProcAddress(module, symbol);
            if (!fn) {
                if (IMAGE_SNAP_BY_ORDINAL32(name->u1.Ordinal))
                    std::fprintf(stderr, "ei_standalone: %s has no #%lu\n", dll, IMAGE_ORDINAL32(name->u1.Ordinal));
                else
                    std::fprintf(stderr, "ei_standalone: %s has no %s\n", dll, symbol);
                ++missing;
                continue;
            }
            slot->u1.Function = reinterpret_cast<DWORD>(fn);
            ++resolved;
        }
    }
    std::printf("ei_standalone: %d imports resolved, %d missing\n", resolved, missing);
    return missing;
}

// Lays out game.exe at its base: headers, .rdata and .rsrc as in the file, the code section INT3, .data zero.
bool BuildImage(const std::vector<unsigned char>& file, const IMAGE_NT_HEADERS32*& nt) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    if (file.size() < 0x1000 || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* fileNt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(file.data() + dos->e_lfanew);
    if (fileNt->Signature != IMAGE_NT_SIGNATURE || fileNt->OptionalHeader.ImageBase != kImageBase) return false;
    if (fileNt->FileHeader.TimeDateStamp != kExpectedTimestamp) {
        std::fprintf(stderr, "ei_standalone: game.exe timestamp %08lX is not 1.06's %08lX\n",
                     fileNt->FileHeader.TimeDateStamp, kExpectedTimestamp);
        return false;
    }
    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(g_reserve);
    const std::uintptr_t end = kImageBase + fileNt->OptionalHeader.SizeOfImage;
    if (begin > kImageBase || begin + kReserve < end) {
        std::fprintf(stderr, "ei_standalone: the reserved range %08X-%08X does not cover %08X-%08X\n",
                     static_cast<unsigned>(begin), static_cast<unsigned>(begin + kReserve),
                     static_cast<unsigned>(kImageBase), static_cast<unsigned>(end));
        return false;
    }
    DWORD old;
    if (!VirtualProtect(reinterpret_cast<void*>(kImageBase), end - kImageBase, PAGE_EXECUTE_READWRITE, &old))
        return false;
    auto* image = reinterpret_cast<unsigned char*>(kImageBase);
    std::memcpy(image, file.data(), fileNt->OptionalHeader.SizeOfHeaders);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(fileNt);
    for (WORD i = 0; i < fileNt->FileHeader.NumberOfSections; ++i) {
        unsigned char* to = image + sec[i].VirtualAddress;
        if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) {
            std::memset(to, 0xCC, sec[i].Misc.VirtualSize);
            continue;
        }
        if (sec[i].VirtualAddress == kDataRva) continue;
        const DWORD n = (std::min)(sec[i].SizeOfRawData, sec[i].Misc.VirtualSize);
        if (sec[i].PointerToRawData + n > file.size()) return false;
        std::memcpy(to, file.data() + sec[i].PointerToRawData, n);
    }
    nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
    return true;
}

// The game's arguments: what follows the token "--" on the host's command line, as typed.
std::string GameArguments() {
    const char* p = GetCommandLineA();
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

} // namespace

int main(int argc, char** argv) {
    // Under ei_launch --standalone stdout is a pipe: unbuffered, so a run that ends on an INT3 hit
    // (TerminateProcess) loses none of it.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::string exe = R"(F:\games\Evil Islands (Rebirth)\starter\Engine\game.exe)";
    char self[MAX_PATH];
    GetModuleFileNameA(nullptr, self, MAX_PATH);
    std::string dll = Folder(self) + "\\ei_hook.dll";
    bool run = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--exe") && i + 1 < argc) exe = argv[++i];
        else if (!std::strcmp(argv[i], "--dll") && i + 1 < argc) dll = argv[++i];
        else if (!std::strcmp(argv[i], "--run")) run = true;
        else if (!std::strcmp(argv[i], "--")) break;
        else {
            std::fprintf(stderr, "usage: ei_standalone [--exe <game.exe>] [--dll <ei_hook.dll>] [--run] [-- <game arguments>]\n");
            return 2;
        }
    }

    std::vector<unsigned char> file;
    if (!ReadWholeFile(exe, file)) return Fail(("cannot read " + exe).c_str());
    const IMAGE_NT_HEADERS32* nt = nullptr;
    if (!BuildImage(file, nt)) return Fail("cannot lay out game.exe's image");
    if (ResolveImports(nt, Folder(exe)) != 0) return 1;

    const auto* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (sec[i].Characteristics & IMAGE_SCN_CNT_CODE) {
            g_textBegin = kImageBase + sec[i].VirtualAddress;
            g_textEnd = g_textBegin + sec[i].Misc.VirtualSize;
        }
    AddVectoredExceptionHandler(1, EarlyBreakpoint);
    HMODULE hook = LoadLibraryA(dll.c_str());
    if (!hook) return Fail(("cannot load " + dll).c_str());
    using Main = int(__cdecl*)(const char* exePath, int run, const char* arguments);
    auto start = reinterpret_cast<Main>(GetProcAddress(hook, "EiStandaloneMain"));
    if (!start) return Fail("ei_hook.dll has no EiStandaloneMain");
    const int result = start(exe.c_str(), run ? 1 : 0, GameArguments().c_str());
    std::printf("ei_standalone: %s %d\n", run ? "the game returned" : "check", result);
    return result;
}
