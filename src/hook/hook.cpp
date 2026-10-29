// The hook framework's game.exe side (ei_harness.dll; the standalone image links it in its verify build only, where
// the shared harness names it): the vector recorders' installation (EI_VECTORS=1), the harness's patches of game.exe's
// code and the WinMain hook that installs them.
#include "ei/hook.h"
#include "log.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

namespace ei {

static HookEntry* g_hooks = nullptr;

HookRegistrar::HookRegistrar(std::uintptr_t address, void* recorder, const char* name, int stolen,
                             void** original)
    : entry{address, recorder, name, g_hooks, stolen, original} {
    g_hooks = &entry;
}

// Copies the first `stolen` bytes of the original function into executable memory followed by a
// JMP back to the rest of it, giving a callable copy of the original.
// Trampolines are 64-byte slots cut from executable blocks (one VirtualAlloc, a 64 KB granule, per block).
static void* AllocTrampoline() {
    static constexpr std::size_t kSlot = 64, kBlock = 64 * 1024;
    static unsigned char* next = nullptr;
    static unsigned char* end = nullptr;
    if (next == end) {
        next = static_cast<unsigned char*>(
            VirtualAlloc(nullptr, kBlock, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!next) return nullptr;
        end = next + kBlock;
    }
    void* slot = next;
    next += kSlot;
    return slot;
}

static void* MakeTrampoline(std::uintptr_t address, int stolen) {
    auto* t = static_cast<unsigned char*>(AllocTrampoline());
    if (!t) return nullptr;
    std::memcpy(t, reinterpret_cast<void*>(address), stolen);
    t[stolen] = 0xE9;
    std::int32_t rel = static_cast<std::int32_t>(
        (address + stolen) - (reinterpret_cast<std::uintptr_t>(t) + stolen + 5));
    std::memcpy(t + stolen + 1, &rel, 4);
    FlushInstructionCache(GetCurrentProcess(), t, 64);
    return t;
}

// game.exe 1.06 (Russian, GOG / Rebirth starter\Engine copy): PE timestamp 2001-03-26 09:36:51.
static constexpr DWORD kExpectedTimestamp = 0x3ABF0DB3;
// game.exe's WinMain: the harness installs the recorders, the coverage breakpoints and the profiler when it starts.
static constexpr std::uintptr_t kGameWinMain = 0x00456440;

// The harness's patches of game code (NoteHarnessPatch). A fixed array: the harness may note one
// before this file's dynamic initialisers have run.
struct HarnessPatch {
    std::uintptr_t address;
    std::size_t size;
    const char* what;
};
static HarnessPatch g_harnessPatches[256];
static int g_harnessPatchCount = 0;

void NoteHarnessPatch(std::uintptr_t address, std::size_t size, const char* what) {
    if (g_harnessPatchCount < static_cast<int>(std::size(g_harnessPatches)))
        g_harnessPatches[g_harnessPatchCount++] = {address, size, what};
    else
        Log("harness patch %08X (%s) not noted: the table is full", static_cast<unsigned>(address), what);
}

// The harness's patch overlapping [address, address + size), or nullptr.
static const HarnessPatch* HarnessPatchIn(std::uintptr_t address, std::size_t size) {
    for (int i = 0; i < g_harnessPatchCount; ++i) {
        const HarnessPatch& p = g_harnessPatches[i];
        if (p.address < address + size && address < p.address + p.size) return &p;
    }
    return nullptr;
}

const char* HarnessPatchAt(std::uintptr_t address, std::size_t size) {
    const HarnessPatch* p = HarnessPatchIn(address, size);
    return p ? p->what : nullptr;
}

static int g_unexpectedSkips = 0;
int UnexpectedSkips() { return g_unexpectedSkips; }

// Logs a recorder InstallHooks leaves out. Expected: the harness's patch is in its stolen bytes. Any other reason
// is a bug that leaves the recorder unrun: counted for the harness.
static void LogSkip(const HookEntry* h, bool expected, const char* why) {
    Log("SKIP %s at %08X: %s%s", h->name, static_cast<unsigned>(h->address), why, expected ? "" : " (unexpected)");
    if (!expected) ++g_unexpectedSkips;
}

// game.exe's own file (the recorders' stolen bytes are compared with it).
static bool ReadExeFromDisk(std::vector<unsigned char>& out) {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) return false;
    fseek(f, 0, SEEK_END);
    out.resize(ftell(f));
    fseek(f, 0, SEEK_SET);
    bool ok = fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

// The .text section has identical file and virtual layout (raw 0x1000 == VA 0x1000).
static const unsigned char* FileBytesAt(const std::vector<unsigned char>& file, std::uintptr_t va) {
    std::uintptr_t off = va - kImageBase;
    return off + 16 <= file.size() ? file.data() + off : nullptr;
}

// Makes the image's code section(s) writable for the whole installation (one VirtualProtect per
// region instead of two per hook) and puts the protections back afterwards.
class TextUnprotect {
public:
    explicit TextUnprotect(const IMAGE_NT_HEADERS32* nt) {
        const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            std::uintptr_t lo = kImageBase + sec[i].VirtualAddress;
            std::uintptr_t hi = lo + sec[i].Misc.VirtualSize;
            if (m_end == 0) { m_begin = lo; m_end = hi; }
            else { m_begin = (std::min)(m_begin, lo); m_end = (std::max)(m_end, hi); }
        }
        // The regions as they are now (they can differ from the section's flags), each made writable.
        for (std::uintptr_t a = m_begin; a < m_end;) {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(reinterpret_cast<void*>(a), &mbi, sizeof(mbi))) { m_begin = m_end = 0; break; }
            Region r{a, (std::min<std::uintptr_t>)(m_end, reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize), mbi.Protect};
            a = r.end;
            m_regions.push_back(r);
        }
        for (Region& r : m_regions) {
            DWORD old;
            if (!VirtualProtect(reinterpret_cast<void*>(r.begin), r.end - r.begin, PAGE_EXECUTE_READWRITE, &old)) {
                m_begin = m_end = 0; // the hooks fall back to protecting each patch site
                r.protect = 0;
            }
        }
    }
    ~TextUnprotect() { Restore(); }
    bool Contains(std::uintptr_t address, std::size_t size) const {
        return m_end != 0 && address >= m_begin && address + size <= m_end;
    }
    void Restore() {
        for (Region& r : m_regions) {
            if (!r.protect) continue;
            DWORD old;
            VirtualProtect(reinterpret_cast<void*>(r.begin), r.end - r.begin, r.protect, &old);
            r.protect = 0;
        }
        m_end = 0;
    }
private:
    struct Region { std::uintptr_t begin, end; DWORD protect; };
    std::uintptr_t m_begin = 0, m_end = 0;
    std::vector<Region> m_regions;
};

int InstallHooks() {
    // game.exe runs only as the original game (ei_launch sets EI_NO_HOOKS=1 for it), so nothing of ours is
    // installed there, except with EI_VECTORS=1 the vector recorders (tests/vectors): each runs the original through
    // its trampoline and samples its calls.
    char noHooksValue[4], vectorsValue[4];
    if (!GetEnvironmentVariableA("EI_NO_HOOKS", noHooksValue, sizeof(noHooksValue)) || noHooksValue[0] != '1')
        Log("game.exe without EI_NO_HOOKS=1: the original game code all the same (the harness's leftover patches "
            "follow EI_NO_HOOKS; ei_launch sets it)");
    if (!GetEnvironmentVariableA("EI_VECTORS", vectorsValue, sizeof(vectorsValue)) || vectorsValue[0] != '1') {
        Log("game.exe: running the original game code");
        return 0;
    }
    // The recordings are the vanilla code's: addon.dll's patches would be recorded with it.
    if (GetModuleHandleA("addon.dll")) {
        Log("EI_VECTORS set in a Rebirth launch (addon.dll): nothing recorded, the original game code runs");
        return 0;
    }
    Log("EI_VECTORS set: the original game code with the vector recorders only");
    std::vector<unsigned char> file;
    if (!ReadExeFromDisk(file) || file.size() < 0x1000) {
        Log("could not read game.exe from disk, no hooks installed");
        return 0;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(file.data() + dos->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != kExpectedTimestamp) {
        Log("game.exe timestamp %08lX does not match expected %08lX, no hooks installed",
            nt->FileHeader.TimeDateStamp, kExpectedTimestamp);
        return 0;
    }

    TextUnprotect text(nt);
    int installed = 0;
    g_unexpectedSkips = 0;
    std::unordered_map<std::uintptr_t, const char*> hookedBy;   // address -> the recorder installed there
    std::string installedLines;   // logged at the end in one write (LogLines)
    for (HookEntry* h = g_hooks; h; h = h->next) {
        auto* target = reinterpret_cast<unsigned char*>(h->address);
        const unsigned char* orig = FileBytesAt(file, h->address);
        char why[256];
        // One recorder per address: a second one would never run.
        if (auto first = hookedBy.find(h->address); first != hookedBy.end()) {
            std::snprintf(why, sizeof(why), "a second registration (%s is installed there)", first->second);
            LogSkip(h, false, why);
            continue;
        }
        if (!InGameText(h->address) || !orig) {
            LogSkip(h, false, "not in game.exe's code");
            continue;
        }
        // A recorder runs the original's body, harness patches included: only its stolen bytes count.
        if (const HarnessPatch* patch = HarnessPatchIn(h->address, static_cast<std::size_t>((std::max)(h->stolen, 5)))) {
            std::snprintf(why, sizeof(why), "the harness patches +0x%X (%s)",
                          static_cast<unsigned>((std::max)(patch->address, h->address) - h->address), patch->what);
            LogSkip(h, true, why);
            continue;
        }
        if (h->stolen < 5 || std::memcmp(target, orig, h->stolen) != 0 ||
            !(*h->original = MakeTrampoline(h->address, h->stolen))) {
            LogSkip(h, false, "cannot build trampoline");
            continue;
        }
        DWORD old = 0;
        const bool inText = text.Contains(h->address, 5);
        if (!inText) VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &old);
        target[0] = 0xE9; // JMP rel32
        std::int32_t rel = static_cast<std::int32_t>(
            reinterpret_cast<std::uintptr_t>(h->recorder) - (h->address + 5));
        std::memcpy(target + 1, &rel, 4);
        if (!inText) VirtualProtect(target, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), target, 5);
        char line[320];
        std::snprintf(line, sizeof(line), "recording %s at %08X -> %p\n", h->name, static_cast<unsigned>(h->address),
                      h->recorder);
        installedLines += line;
        hookedBy.emplace(h->address, h->name);
        ++installed;
    }
    text.Restore();
    LogLines(installedLines.c_str());
    if (g_unexpectedSkips) Log("%d unexpected SKIP(s) above: those recorders never run", g_unexpectedSkips);
    return installed;
}

// game.exe (the original game's harness): the recorders (EI_VECTORS), the coverage breakpoints (EI_COVER)
// and the profiler (EI_PROFILE) go in when WinMain starts, not when the DLL loads, after Rebirth's addon
// has applied its own patches from a hook on the WinMain call. The standalone image runs its start-up from
// StandaloneMain (src/standalone/standalone_main.cpp).
void InstallCoverage();
static unsigned char g_winMainBytes[5];
static void (*g_originalWinMainAction)();

void SetOriginalWinMainAction(void (*action)()) { g_originalWinMainAction = action; }

static void __stdcall OnWinMain() {
    // System DPI aware before the game makes its window, as the HIGHDPIAWARE compatibility flag on
    // run/vanilla and run/rebirth already makes it (the standalone image does the same in StandaloneMain).
    SetProcessDPIAware();
    DWORD old;
    auto* p = reinterpret_cast<unsigned char*>(kGameWinMain);
    VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(p, g_winMainBytes, 5);
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    if (g_originalWinMainAction) g_originalWinMainAction();
    Log("%d hook(s) installed", InstallHooks());
    InstallCoverage();
    InstallProfiler();
}

__declspec(naked) static void WinMainStub() {
    __asm {
        pushad
        pushfd
        call OnWinMain
        popfd
        popad
        mov eax, 0x00456440
        jmp eax
    }
}

void InstallHooksAtWinMain() {
    auto* p = reinterpret_cast<unsigned char*>(kGameWinMain);
    DWORD old;
    VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(g_winMainBytes, p, 5);
    p[0] = 0xE9;
    std::int32_t rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&WinMainStub) - (kGameWinMain + 5));
    std::memcpy(p + 1, &rel, 4);
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
}

} // namespace ei
