#include "ei/hook.h"
#include "ei/rebirth.h"
#include "log.h"
#include "rebirth_hook.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <iterator>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ei {

static HookEntry* g_hooks = nullptr;

// The standalone image's code table (src/standalone/image_code.cpp, S18i).
void SetCodeTable(const std::vector<std::pair<std::uintptr_t, void*>>& installed);
bool RedirectedByHarness(std::uintptr_t address);

// Every address InstallHooks took, in install order, with its code (the replacement, or a recorder in game.exe,
// where a jump is written): the standalone image's code table (SetCodeTable) and its check (standalone.cpp CheckCode).
static std::vector<std::pair<std::uintptr_t, void*>> g_installed;
const std::vector<std::pair<std::uintptr_t, void*>>& InstalledHooks() { return g_installed; }

HookRegistrar::HookRegistrar(std::uintptr_t address, void* replacement, const char* name)
    : entry{address, replacement, name, g_hooks} {
    g_hooks = &entry;
}

HookRegistrar::HookRegistrar(std::uintptr_t address, void* recorder, const char* name, int stolen,
                             void** original)
    : entry{address, recorder, name, g_hooks, stolen, original, true} {
    g_hooks = &entry;
}

static StartupAction* g_startupActions = nullptr;

StartupActionRegistrar::StartupActionRegistrar(void (*run)(), const char* name) : entry{run, name, g_startupActions} {
    g_startupActions = &entry;
}

// The registered hooks by address, each address's entries in list order (the order the list is
// walked in), so the lookups below do not scan the whole list per hook.
static std::unordered_map<std::uintptr_t, std::vector<HookEntry*>> g_hooksByAddress;

static void IndexHooks() {
    g_hooksByAddress.clear();
    for (HookEntry* h = g_hooks; h; h = h->next) g_hooksByAddress[h->address].push_back(h);
}

// Whether `code` (following incremental-link jump stubs) is MSVC's virtual-call thunk:
// MOV EAX,[ECX] then JMP [EAX], JMP [EAX+disp8] or JMP [EAX+disp32].
static bool IsVirtualCallThunk(const void* replacement) {
    const auto* code = static_cast<const unsigned char*>(replacement);
    for (int hops = 0; code[0] == 0xE9 && hops < 4; ++hops)
        code += 5 + *reinterpret_cast<const std::int32_t*>(code + 1);
    return code[0] == 0x8B && code[1] == 0x01 && code[2] == 0xFF &&
           (code[3] == 0x20 || code[3] == 0x60 || code[3] == 0xA0);
}

// game.exe's dispatch-only functions with their bytes, from game.exe at build time (tools/gen_image_cells.py
// --dispatch, build/<cfg>/image/dispatch_bytes.inc; S18k: the image reads no game.exe).
struct DispatchBytes {
    std::uintptr_t address;
    unsigned char bytes[8];
};
static const DispatchBytes kDispatchBytes[] = {
#include "dispatch_bytes.inc"
};

// Whether the thunk `replacement` has the same bytes as game.exe's dispatch-only function at `address`
// (5 with a disp8, 4 with [EAX], 8 with a disp32).
static bool SameDispatch(const void* replacement, std::uintptr_t address) {
    const auto* code = static_cast<const unsigned char*>(replacement);
    for (int hops = 0; code[0] == 0xE9 && hops < 4; ++hops)
        code += 5 + *reinterpret_cast<const std::int32_t*>(code + 1);
    const int size = code[3] == 0x20 ? 4 : code[3] == 0x60 ? 5 : 8;
    for (const DispatchBytes& d : kDispatchBytes)
        if (d.address == address) return std::memcmp(code, d.bytes, size) == 0;
    return false;
}

// The first entry registered for `address` whose replacement or recorder is such a thunk
// (EI_REPLACE_DISPATCH entries are meant to be).
static const HookEntry* ThunkEntryAt(std::uintptr_t address) {
    auto it = g_hooksByAddress.find(address);
    if (it == g_hooksByAddress.end()) return nullptr;
    for (const HookEntry* h : it->second)
        if (!h->dispatch && IsVirtualCallThunk(h->replacement)) return h;
    return nullptr;
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
// game.exe's code section (.text): where a registration's address lies.
static constexpr std::uintptr_t kCodeBegin = 0x00401000;
static constexpr std::uintptr_t kCodeEnd = 0x0073A670;

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

// Logs a registration InstallHooks leaves out. Expected: the harness's or another mod's patch is in
// the function. Any other reason is a bug that leaves the rewrite unrun: counted for the harness.
static void LogSkip(const HookEntry* h, bool expected, const char* why) {
    Log("SKIP %s at %08X: %s%s", h->name, static_cast<unsigned>(h->address), why, expected ? "" : " (unexpected)");
    if (!expected) ++g_unexpectedSkips;
}

// game.exe's own file (the original's process: the recorders' stolen bytes are compared with it).
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

struct FunctionSize {
    std::uintptr_t address;
    std::uint32_t size;
};
static const FunctionSize kFunctionSizes[] = {
#include "function_sizes.inc"
};

static std::uint32_t FunctionSizeAt(std::uintptr_t address) {
    const FunctionSize* end = kFunctionSizes + std::size(kFunctionSizes);
    const FunctionSize* it = std::lower_bound(kFunctionSizes, end, address,
        [](const FunctionSize& f, std::uintptr_t a) { return f.address < a; });
    return it != end && it->address == address ? it->size : 0;
}

// The start of the known function whose body holds `address`, or 0 (library code, which the table
// leaves out).
std::uintptr_t FunctionContaining(std::uintptr_t address) {
    const FunctionSize* end = kFunctionSizes + std::size(kFunctionSizes);
    const FunctionSize* it = std::upper_bound(kFunctionSizes, end, address,
        [](std::uintptr_t a, const FunctionSize& f) { return a < f.address; });
    if (it == kFunctionSizes || address >= (it - 1)->address + (it - 1)->size) return 0;
    return (it - 1)->address;
}

// The bytes from `address` to the next known function start.
static std::uintptr_t RoomAt(std::uintptr_t address) {
    const FunctionSize* end = kFunctionSizes + std::size(kFunctionSizes);
    const FunctionSize* it = std::upper_bound(kFunctionSizes, end, address,
        [](std::uintptr_t a, const FunctionSize& f) { return a < f.address; });
    return it == end ? 16 : it->address - address;
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

// The Rebirth flags in the standalone image: ei_rebirth.ini next to the DLL, EI_REBIRTH and the harness's
// specs (rebirth::LoadSettings); no addon.dll or EI_Plugin.asi to follow (rebirth_hook::SettingsOnly).
static void LoadRebirthSettings() {
    char ini[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&LoadRebirthSettings), &self);
    GetModuleFileNameA(self, ini, MAX_PATH);
    if (char* slash = std::strrchr(ini, '\\')) strcpy_s(slash + 1, ini + MAX_PATH - (slash + 1), "ei_rebirth.ini");
    rebirth::LoadSettings(ini);
    if (rebirth::PluginSecondMaster()[0]) Log("Rebirth: NewMaster's second master server: %s", rebirth::PluginSecondMaster());
    rebirth_hook::SettingsOnly(ini);
}

int InstallHooks() {
    // The standalone image: every plain replacement goes in (there is no original code to run). game.exe
    // runs only as the original game (the original's harness: ei_launch sets EI_NO_HOOKS=1 for it), so
    // nothing of ours is installed there, except with EI_VECTORS=1 the vector recorders (tests/vectors):
    // each runs the original through its trampoline and samples its calls. Since S21e there is no mode
    // that runs our rewrites inside game.exe.
    const bool standalone = Standalone();
    if (!standalone) {
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
    }
    // game.exe's file, in the original's process (the recorders' stolen bytes). The image reads none (S18k): its
    // data and the dispatch bytes come from a 1.06 game.exe at build time (tools/gen_image_cells.py checks the
    // timestamp), and a registration's address is a function start of game.exe's code.
    std::vector<unsigned char> file;
    const IMAGE_NT_HEADERS32* nt = nullptr;
    if (!standalone) {
        if (!ReadExeFromDisk(file) || file.size() < 0x1000) {
            Log("could not read game.exe from disk, no hooks installed");
            return 0;
        }
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
        nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(file.data() + dos->e_lfanew);
        if (nt->FileHeader.TimeDateStamp != kExpectedTimestamp) {
            Log("game.exe timestamp %08lX does not match expected %08lX, no hooks installed",
                nt->FileHeader.TimeDateStamp, kExpectedTimestamp);
            return 0;
        }
    }

    if (standalone) {
        LoadRebirthSettings();
        unsigned short cw;
        __asm fnstcw cw
        Log("FPU control word at WinMain: %04X", cw);
        for (StartupAction* action = g_startupActions; action; action = action->next) {
            Log("startup action %s", action->name);
            action->run();
        }
    }

    IndexHooks();
    // The standalone image writes nothing into game.exe's code section (guarded since S18i): the replacements
    // become the code for their addresses (SetCodeTable, src/standalone/image_code.cpp). game.exe's recorders
    // write their jumps.
    std::optional<TextUnprotect> text;
    if (!standalone) text.emplace(nt);
    int installed = 0;
    g_unexpectedSkips = 0;
    std::unordered_map<std::uintptr_t, const char*> hookedBy;   // address -> the entry installed there
    std::string installedLines;   // logged at the end in one write (LogLines)
    for (HookEntry* h = g_hooks; h; h = h->next) {
        // The image takes the replacements, game.exe (recording) the recorders.
        if (h->record == standalone) continue;
        // In the image the harness replaced a registered address before the hooks (its fixed ISAAC seed at 0049F660).
        if (standalone && RedirectedByHarness(h->address)) {
            LogSkip(h, true, "replaced before the hooks (the harness)");
            continue;
        }
        auto* target = reinterpret_cast<unsigned char*>(h->address);
        const unsigned char* orig = standalone ? nullptr : FileBytesAt(file, h->address);
        const bool inCode = standalone ? h->address - kCodeBegin < kCodeEnd - kCodeBegin : orig != nullptr;
        std::uint32_t size = FunctionSizeAt(h->address);
        char why[256];
        // One hook per address: a second registration of the same kind (two plain replacements, two
        // recorders) would never run.
        if (auto first = hookedBy.find(h->address); first != hookedBy.end()) {
            std::snprintf(why, sizeof(why), "a second registration (%s is installed there)", first->second);
            LogSkip(h, false, why);
            continue;
        }
        // The standalone image has no original code to keep: a short function, or a start the size
        // table does not know, gets its jump when the next known function leaves room for it. A
        // recorder's function needs its own five bytes.
        if (!inCode || (size < 5 && !(standalone && RoomAt(h->address) >= 5))) {
            LogSkip(h, false, size ? "under five bytes with no room after it" : "not a known function start");
            continue;
        }
        // A recorder runs the original's body, harness patches included: only its stolen bytes count.
        if (const HarnessPatch* patch = standalone ? nullptr : HarnessPatchIn(h->address, static_cast<std::size_t>((std::max)(h->stolen, 5)))) {
            std::snprintf(why, sizeof(why), "the harness patches +0x%X (%s)",
                          static_cast<unsigned>((std::max)(patch->address, h->address) - h->address), patch->what);
            LogSkip(h, true, why);
            continue;
        }
        // A pointer to a virtual member function is a dispatch thunk (MOV EAX,[ECX]; JMP [EAX+n]),
        // which would jump back into the hooked function forever. Every entry for the address is
        // checked: a second registration of a method through a thunk calls the same dispatch.
        if (const HookEntry* thunk = ThunkEntryAt(h->address)) {
            std::snprintf(why, sizeof(why), "%s is a virtual-call thunk (register a non-virtual shim instead)", thunk->name);
            LogSkip(h, false, why);
            continue;
        }
        if (h->dispatch && !SameDispatch(h->replacement, h->address)) {
            LogSkip(h, false, "its dispatch bytes differ from the original's");
            continue;
        }
        if (h->stolen > 0) {
            if (h->stolen < 5 || std::memcmp(target, orig, h->stolen) != 0 ||
                !(*h->original = MakeTrampoline(h->address, h->stolen))) {
                LogSkip(h, false, "cannot build trampoline");
                continue;
            }
        }
        if (!standalone) {
            DWORD old = 0;
            const bool inText = text->Contains(h->address, 5);
            if (!inText) VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &old);
            target[0] = 0xE9; // JMP rel32
            std::int32_t rel = static_cast<std::int32_t>(
                reinterpret_cast<std::uintptr_t>(h->replacement) - (h->address + 5));
            std::memcpy(target + 1, &rel, 4);
            if (!inText) VirtualProtect(target, 5, old, &old);
            FlushInstructionCache(GetCurrentProcess(), target, 5);
        }
        char line[320];
        std::snprintf(line, sizeof(line), h->record ? "recording %s at %08X -> %p\n" : "hooked %s at %08X -> %p\n", h->name,
                      static_cast<unsigned>(h->address), h->replacement);
        installedLines += line;
        g_installed.emplace_back(h->address, h->replacement);
        hookedBy.emplace(h->address, h->name);
        ++installed;
    }
    if (text) text->Restore();
    if (standalone) SetCodeTable(g_installed);
    LogLines(installedLines.c_str());
    if (g_unexpectedSkips) Log("%d unexpected SKIP(s) above: those rewrites never run", g_unexpectedSkips);
    return installed;
}

// game.exe (the original game's harness): the recorders (EI_VECTORS), the coverage breakpoints (EI_COVER)
// and the profiler (EI_PROFILE) go in when WinMain starts, not when the DLL loads, after Rebirth's addon
// has applied its own patches from a hook on the WinMain call. The standalone image calls InstallHooks
// from EiStandaloneMain (standalone.cpp).
static constexpr std::uintptr_t kWinMain = 0x00456440;
void InstallCoverage();   // cover.cpp
static unsigned char g_winMainBytes[5];
static void (*g_originalWinMainAction)();

void SetOriginalWinMainAction(void (*action)()) { g_originalWinMainAction = action; }

static void __stdcall OnWinMain() {
    // System DPI aware before the game makes its window, as the HIGHDPIAWARE compatibility flag on
    // run/vanilla and run/rebirth already makes it (the standalone host does the same in
    // EiStandaloneMain).
    SetProcessDPIAware();
    DWORD old;
    auto* p = reinterpret_cast<unsigned char*>(kWinMain);
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
    auto* p = reinterpret_cast<unsigned char*>(kWinMain);
    DWORD old;
    VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(g_winMainBytes, p, 5);
    p[0] = 0xE9;
    std::int32_t rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&WinMainStub) - (kWinMain + 5));
    std::memcpy(p + 1, &rel, 4);
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
}

} // namespace ei
