// ei_standalone: runs the rebuilt game without game.exe's code (docs/notes/standalone.md section 2).
//
// The host is linked below game.exe's image base and holds game.exe's address range (0x400000 up to
// its SizeOfImage) inside its own image, as a zero-filled array. There it lays out game.exe's headers from the
// file on disk and no section: the code section is filled with INT3 (nothing of it runs); .rdata and .data
// stay zero (ei_hook.dll keeps both sections in its own storage, with bytes from the build,
// src/standalone/image_data.cpp); .rsrc stays zero and is guarded (the image's resources come from game.exe
// loaded as a data file, hook/standalone.cpp's ImageRoute). Then it loads ei_hook.dll, which resolves game.exe's
// imports into a table
// of its own as it initialises (src/standalone/image_imports.cpp; game.exe's path comes from this exe's
// export EiStandaloneExe), and hands over: the DLL writes a jump at each rewritten function's address
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
// game.exe's SizeOfImage is 0x3D7000. The host's base (/BASE:0x200000) leaves room below 0x400000
// for its own code and data; the array then covers the whole range wherever the linker puts it.
constexpr std::size_t kReserve = 0x00640000;
unsigned char g_reserve[kReserve];

// game.exe's code section in the image (INT3 until ei_hook.dll writes its jumps).
std::uintptr_t g_textBegin, g_textEnd;
// game.exe's .rsrc in the image: zero and PAGE_NOACCESS (OnOldResources).
std::uintptr_t g_rsrcBegin, g_rsrcEnd;

std::string g_exe = R"(F:\games\Evil Islands (Rebirth)\starter\Engine\game.exe)";   // --exe

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

// A read of the image's .rsrc: nothing of the image reads game.exe's resources at their address (the resource
// imports go to game.exe loaded as a data file), so a reader is named and the process ends.
LONG CALLBACK OnOldResources(EXCEPTION_POINTERS* e) {
    const EXCEPTION_RECORD* r = e->ExceptionRecord;
    if (r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || r->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
    const std::uintptr_t data = r->ExceptionInformation[1];
    if (data < g_rsrcBegin || data >= g_rsrcEnd) return EXCEPTION_CONTINUE_SEARCH;
    std::fprintf(stderr, "ei_standalone: %s of game.exe's .rsrc at %08X by %08X: the image lays out no resources\n",
                 r->ExceptionInformation[0] == 1 ? "write" : "read", static_cast<unsigned>(data),
                 static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(r->ExceptionAddress)));
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

std::string Folder(const std::string& path) {
    const std::size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

// Lays out game.exe at its base: the headers as in the file, the code section INT3, every other section zero.
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
        if (std::memcmp(sec[i].Name, ".rsrc", 6) == 0) {
            g_rsrcBegin = kImageBase + sec[i].VirtualAddress;
            g_rsrcEnd = g_rsrcBegin + ((sec[i].Misc.VirtualSize + 0xFFF) & ~DWORD{0xFFF});
        }
        // .rdata and .data (ei_hook.dll's, S18h, S18d) and .rsrc: nothing to copy.
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

// game.exe's path for ei_hook.dll (the folder its imports load from first), asked for while it initialises.
extern "C" __declspec(dllexport) const char* __cdecl EiStandaloneExe() { return g_exe.c_str(); }

int main(int argc, char** argv) {
    // Under ei_launch --standalone stdout is a pipe: unbuffered, so a run that ends on an INT3 hit
    // (TerminateProcess) loses none of it.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::string& exe = g_exe;
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

    const auto* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (sec[i].Characteristics & IMAGE_SCN_CNT_CODE) {
            g_textBegin = kImageBase + sec[i].VirtualAddress;
            g_textEnd = g_textBegin + sec[i].Misc.VirtualSize;
        }
    AddVectoredExceptionHandler(1, EarlyBreakpoint);
    DWORD was;
    if (g_rsrcEnd > g_rsrcBegin &&
        !VirtualProtect(reinterpret_cast<void*>(g_rsrcBegin), g_rsrcEnd - g_rsrcBegin, PAGE_NOACCESS, &was))
        return Fail("cannot guard game.exe's .rsrc");
    AddVectoredExceptionHandler(1, OnOldResources);
    HMODULE hook = LoadLibraryA(dll.c_str());
    if (!hook) return Fail(("cannot load " + dll).c_str());
    using Main = int(__cdecl*)(const char* exePath, int run, const char* arguments);
    auto start = reinterpret_cast<Main>(GetProcAddress(hook, "EiStandaloneMain"));
    if (!start) return Fail("ei_hook.dll has no EiStandaloneMain");
    const int result = start(exe.c_str(), run ? 1 : 0, GameArguments().c_str());
    std::printf("ei_standalone: %s %d\n", run ? "the game returned" : "check", result);
    return result;
}
