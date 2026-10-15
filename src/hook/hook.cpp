#include "ei/hook.h"
#include "ei/rebirth.h"
#include "log.h"
#include "rebirth_hook.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ei {

static HookEntry* g_hooks = nullptr;

// Every address InstallHooks patched, in install order, with the plain replacement registered for
// it (nullptr when there is none): EI_COVER=hooks (cover.cpp) watches both.
static std::vector<std::pair<std::uintptr_t, void*>> g_installed;
const std::vector<std::pair<std::uintptr_t, void*>>& InstalledHooks() { return g_installed; }

HookRegistrar::HookRegistrar(std::uintptr_t address, void* replacement, const char* name)
    : entry{address, replacement, name, g_hooks} {
    g_hooks = &entry;
}

HookRegistrar::HookRegistrar(std::uintptr_t address, void* wrapper, const char* name, int stolen,
                             void** original)
    : entry{address, wrapper, name, g_hooks, stolen, original, true} {
    g_hooks = &entry;
}

static StartupCheck* g_startupChecks = nullptr;

static StartupAction* g_startupActions = nullptr;

StartupActionRegistrar::StartupActionRegistrar(void (*run)(), const char* name) : entry{run, name, g_startupActions} {
    g_startupActions = &entry;
}

StartupCheckRegistrar::StartupCheckRegistrar(void (*run)(), const char* name) : entry{run, name, g_startupChecks} {
    g_startupChecks = &entry;
}

// Functions listed in verified.txt (next to the DLL) have passed verification and run without
// their verify wrapper: the plain replacement is installed instead. One hex address per line.
static std::unordered_set<std::uintptr_t> g_verified;

static void LoadVerifiedList() {
    char path[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&LoadVerifiedList), &self);
    GetModuleFileNameA(self, path, MAX_PATH);
    if (char* slash = std::strrchr(path, '\\')) strcpy_s(slash + 1, path + MAX_PATH - (slash + 1), "verified.txt");
    FILE* f = nullptr;
    if (fopen_s(&f, path, "r") != 0 || !f) return;
    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        unsigned long a = std::strtoul(line, nullptr, 16);
        if (a) g_verified.insert(a);
    }
    std::fclose(f);
}

// Functions listed in disabled.txt (next to the DLL, from tests/verify/disabled.txt) are not hooked
// at all: the original runs. For rewrites found broken in game, until they are fixed.
static std::unordered_set<std::uintptr_t> g_disabled;

static void LoadDisabledList() {
    char path[MAX_PATH];
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&LoadDisabledList), &self);
    GetModuleFileNameA(self, path, MAX_PATH);
    if (char* slash = std::strrchr(path, '\\')) strcpy_s(slash + 1, path + MAX_PATH - (slash + 1), "disabled.txt");
    FILE* f = nullptr;
    if (fopen_s(&f, path, "r") != 0 || !f) return;
    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        unsigned long a = std::strtoul(line, nullptr, 16);
        if (a) g_disabled.insert(a);
    }
    std::fclose(f);
}

static bool IsDisabled(std::uintptr_t address) {
    return g_disabled.count(address) != 0;
}

static bool IsVerified(std::uintptr_t address) {
    return g_verified.count(address) != 0;
}

// The registered hooks by address, each address's entries in list order (the order the list is
// walked in), so the lookups below do not scan the whole list per hook.
static std::unordered_map<std::uintptr_t, std::vector<HookEntry*>> g_hooksByAddress;

static void IndexHooks() {
    g_hooksByAddress.clear();
    for (HookEntry* h = g_hooks; h; h = h->next) g_hooksByAddress[h->address].push_back(h);
}

// The first verify entry registered for `address`.
static HookEntry* VerifyEntry(std::uintptr_t address) {
    auto it = g_hooksByAddress.find(address);
    if (it == g_hooksByAddress.end()) return nullptr;
    for (HookEntry* h : it->second)
        if (h->verify) return h;
    return nullptr;
}

// Our replacement registered for `address` (EI_REPLACE), or nullptr.
static void* PlainReplacement(std::uintptr_t address) {
    auto it = g_hooksByAddress.find(address);
    if (it == g_hooksByAddress.end()) return nullptr;
    for (HookEntry* h : it->second)
        if (!h->verify && !h->record) return h->replacement;
    return nullptr;
}

// Whether a vector recorder (EI_RECORD) is registered for `address`.
static bool HasRecorder(std::uintptr_t address) {
    auto it = g_hooksByAddress.find(address);
    if (it == g_hooksByAddress.end()) return false;
    for (HookEntry* h : it->second)
        if (h->record) return true;
    return false;
}

// The body behind `code`'s incremental-link jump stubs (JMP rel32).
static unsigned char* FollowJumpStubs(void* code) {
    auto* p = static_cast<unsigned char*>(code);
    for (int hops = 0; p[0] == 0xE9 && hops < 4; ++hops) p += 5 + *reinterpret_cast<const std::int32_t*>(p + 1);
    return p;
}

// EI_RECORD_OURS in record mode: our replacement for the recorder's address starts with a JMP to the
// recorder, so calls from our code (which never pass the game's address) run the original and are
// recorded. Refused when another address's replacement has the same body (a folded function).
static void RedirectOurs(const HookEntry* recorder) {
    void* ours = PlainReplacement(recorder->address);
    if (!ours) return;
    unsigned char* body = FollowJumpStubs(ours);
    for (HookEntry* h = g_hooks; h; h = h->next) {
        if (h->address == recorder->address || h->verify || h->record || !h->replacement) continue;
        if (FollowJumpStubs(h->replacement) == body) {
            Log("recording %s: our replacement is shared with %s, our calls are not recorded", recorder->name, h->name);
            return;
        }
    }
    DWORD old = 0;
    VirtualProtect(body, 5, PAGE_EXECUTE_READWRITE, &old);
    body[0] = 0xE9;
    std::int32_t rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(recorder->replacement) -
                                                 (reinterpret_cast<std::uintptr_t>(body) + 5));
    std::memcpy(body + 1, &rel, 4);
    VirtualProtect(body, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), body, 5);
    Log("recording %s: our calls too (%p)", recorder->name, static_cast<void*>(body));
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

// Whether the thunk `replacement` has the same bytes as the original code at `original`
// (5 with a disp8, 4 with [EAX], 8 with a disp32).
static bool SameDispatch(const void* replacement, const unsigned char* original) {
    const auto* code = static_cast<const unsigned char*>(replacement);
    for (int hops = 0; code[0] == 0xE9 && hops < 4; ++hops)
        code += 5 + *reinterpret_cast<const std::int32_t*>(code + 1);
    const int size = code[3] == 0x20 ? 4 : code[3] == 0x60 ? 5 : 8;
    return original && std::memcmp(code, original, size) == 0;
}

// The first entry registered for `address` whose replacement or wrapper is such a thunk
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

static bool ReadExeFromDisk(std::vector<unsigned char>& out) {
    char path[MAX_PATH];
    if (Standalone()) strcpy_s(path, StandaloneExePath());
    else GetModuleFileNameA(nullptr, path, MAX_PATH);
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

// Writes patched_functions.txt next to the DLL: every game function whose code in memory differs
// from game.exe on disk when hooks are installed, i.e. what other mods (Rebirth's addon.dll) patch.
// Rewritten code must call those through their original addresses (rewrite-guide rule 9).
static void DumpPatchedFunctions(const std::vector<unsigned char>& file) {
    char path[MAX_PATH];
    OutputPath("patched_functions.txt", path);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "w") != 0 || !f) return;
    std::fprintf(f, "# game functions whose code differs from game.exe on disk at WinMain (address, size, first differing offset)\n");
    int count = 0;
    for (const FunctionSize& fn : kFunctionSizes) {
        const unsigned char* orig = FileBytesAt(file, fn.address);
        if (!orig || fn.address + fn.size - kImageBase > file.size()) continue;
        const auto* live = reinterpret_cast<const unsigned char*>(fn.address);
        for (std::uint32_t i = 0; i < fn.size; ++i) {
            if (live[i] != orig[i]) {
                std::fprintf(f, "%08X %u +0x%X\n", static_cast<unsigned>(fn.address), fn.size, i);
                ++count;
                break;
            }
        }
    }
    std::fclose(f);
    Log("%d game function(s) differ from game.exe on disk (patched_functions.txt)", count);
}

// Makes the image's code section(s) writable for the whole installation (one VirtualProtect per
// region instead of two per hook) and puts the protections back afterwards.
class TextUnprotect {
public:
    explicit TextUnprotect(IMAGE_NT_HEADERS32* nt) {
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
    // In the standalone image there is no original to run, compare with or detect patches in: plain
    // replacements only, no Rebirth detection, no start-up checks.
    const bool standalone = Standalone();
    // EI_NO_HOOKS=1 (ei_launch --no-hooks): run the original code only, as a baseline for harness runs.
    char noHooks[4];
    if (!standalone && GetEnvironmentVariableA("EI_NO_HOOKS", noHooks, sizeof(noHooks)) && noHooks[0] == '1') {
        Log("EI_NO_HOOKS set: running the original game code");
        return 0;
    }
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(kImageBase);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(kImageBase + dos->e_lfanew);
    if (nt->FileHeader.TimeDateStamp != kExpectedTimestamp) {
        Log("game.exe timestamp %08lX does not match expected %08lX, no hooks installed",
            nt->FileHeader.TimeDateStamp, kExpectedTimestamp);
        return 0;
    }

    std::vector<unsigned char> file;
    if (!ReadExeFromDisk(file)) {
        Log("could not read game.exe from disk, no hooks installed");
        return 0;
    }

    if (!standalone) DumpPatchedFunctions(file);
    LoadVerifiedList();
    LoadDisabledList();
    if (!g_disabled.empty()) Log("%u function(s) disabled (disabled.txt): the original runs", unsigned(g_disabled.size()));
    if (!g_verified.empty()) Log("%u function(s) marked verified: running without verify wrappers", unsigned(g_verified.size()));

    // Rebirth's patches: the flags (ei_rebirth.ini, EI_REBIRTH, the harness), then what addon.dll
    // has applied (and EI_Plugin.asi will), which the flags follow in a Rebirth launch.
    {
        char ini[MAX_PATH];
        HMODULE self = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&InstallHooks), &self);
        GetModuleFileNameA(self, ini, MAX_PATH);
        if (char* slash = std::strrchr(ini, '\\')) strcpy_s(slash + 1, ini + MAX_PATH - (slash + 1), "ei_rebirth.ini");
        rebirth::LoadSettings(ini);
        if (!standalone) rebirth_hook::Detect(file);
        unsigned short cw;
        __asm fnstcw cw
        Log("FPU control word at WinMain: %04X", cw);
    }
    for (StartupAction* action = g_startupActions; action; action = action->next) {
        Log("startup action %s", action->name);
        action->run();
    }

    // EI_PLAIN=1: no verify wrappers at all, every rewrite runs as it will without them (for timing runs).
    char plainValue[4];
    const bool plain = standalone || (GetEnvironmentVariableA("EI_PLAIN", plainValue, sizeof(plainValue)) && plainValue[0] == '1');
    if (plain) Log(standalone ? "standalone: no verify wrappers" : "EI_PLAIN set: no verify wrappers");
    // EI_VECTORS=1: record mode (tests/vectors): each function with a recorder runs the original
    // through it, and its calls are sampled as test vectors.
    char vectorsValue[4];
    const bool recording = !standalone && GetEnvironmentVariableA("EI_VECTORS", vectorsValue, sizeof(vectorsValue)) && vectorsValue[0] == '1';
    if (recording) Log("EI_VECTORS set: recording function vectors");
    IndexHooks();
    // In the standalone image the code section is INT3 until the jumps go in: a registered address that
    // already holds something else was patched by the harness (its fixed ISAAC seed at 0049F660), which in
    // the hook build keeps it the same way (rebirth_hook::Check skips a patched start).
    std::unordered_set<std::uintptr_t> patchedBefore;
    if (standalone)
        for (const HookEntry* h = g_hooks; h; h = h->next)
            if (*reinterpret_cast<const unsigned char*>(h->address) != 0xCC) patchedBefore.insert(h->address);
    TextUnprotect text(nt);
    int installed = 0, shortLeft = 0;
    for (HookEntry* h = g_hooks; h; h = h->next) {
        if (IsDisabled(h->address)) continue;
        if (patchedBefore.count(h->address)) {
            Log("SKIP %s at %08X: patched before the hooks (the harness)", h->name, static_cast<unsigned>(h->address));
            continue;
        }
        if (h->shortFunction && !standalone) {
            ++shortLeft;
            continue;
        }
        // In record mode a recorder is the only hook at its address; otherwise recorders are left out.
        if (h->record != (recording && HasRecorder(h->address))) continue;
        auto* target = reinterpret_cast<unsigned char*>(h->address);
        const unsigned char* orig = FileBytesAt(file, h->address);
        std::uint32_t size = FunctionSizeAt(h->address);
        // The verify wrapper runs unless the function is verified, or it compares with the original
        // (stolen bytes) and the rewrite applies an enabled Rebirth patch that the original here
        // lacks (a vanilla launch with the flag on): then the plain replacement runs. Wrappers that
        // run ours only and check the state keep running.
        HookEntry* verifyEntry = VerifyEntry(h->address);
        bool wrapper = verifyEntry && !IsVerified(h->address) && !plain;
        char why[256];
        if (wrapper && verifyEntry->stolen > 0 && orig && size &&
            rebirth_hook::FlagWithoutOriginal(h->address, size, orig, why, sizeof(why))) {
            if (h->verify)
                Log("UNVERIFIED %s at %08X: Rebirth patch %s is on but the original does not have it", h->name,
                    static_cast<unsigned>(h->address), why);
            wrapper = false;
        }
        if (!h->record && h->verify != wrapper) continue;
        // The standalone image has no original code to keep: a short function, or a start the size
        // table does not know, gets its jump when the next known function leaves room for it.
        if (!orig || (size < 5 && !(standalone && RoomAt(h->address) >= 5))) {
            Log("SKIP %s at %08X: not a known function start", h->name, static_cast<unsigned>(h->address));
            continue;
        }
        // Another mod (Rebirth's addon.dll) may have patched code anywhere in the body; replacing
        // the function would silently drop that patch, unless the rewrite reproduces it (behind its
        // flag, which must be on). EI_Plugin.asi patches after this point; its sites are known too.
        rebirth_hook::Verdict verdict = standalone ? rebirth_hook::Verdict::Unpatched
            : rebirth_hook::Check(h->address, size, orig, h->verify || h->record ? h->stolen : 0, why, sizeof(why));
        if (verdict == rebirth_hook::Verdict::Skip) {
            Log("SKIP %s at %08X: %s", h->name, static_cast<unsigned>(h->address), why);
            continue;
        }
        if (verdict == rebirth_hook::Verdict::Implemented)
            Log("Rebirth-patched %s at %08X: %s", h->name, static_cast<unsigned>(h->address), why);
        // A pointer to a virtual member function is a dispatch thunk (MOV EAX,[ECX]; JMP [EAX+n]),
        // which would jump back into the hooked function forever. Every entry for the address is
        // checked: a verify wrapper of a method registered through a thunk calls the same
        // dispatch (CWorldClient::Create, 00561E50, overflowed the stack that way).
        if (const HookEntry* thunk = ThunkEntryAt(h->address)) {
            Log("SKIP %s at %08X: %s is a virtual-call thunk (register a non-virtual shim instead)", h->name,
                static_cast<unsigned>(h->address), thunk->name);
            continue;
        }
        if (h->dispatch && !SameDispatch(h->replacement, orig)) {
            Log("SKIP %s at %08X: its dispatch bytes differ from the original's", h->name,
                static_cast<unsigned>(h->address));
            continue;
        }
        if (h->stolen > 0) {
            // In a patched function the stolen bytes may hold addon's JMP to its code: the
            // trampoline gets it re-targeted, so calling it still runs the patched original.
            bool patchedStart = verdict == rebirth_hook::Verdict::Implemented;
            if (h->stolen < 5 || (!patchedStart && std::memcmp(target, orig, h->stolen) != 0) ||
                !(*h->original = MakeTrampoline(h->address, h->stolen)) ||
                (patchedStart && !rebirth_hook::RelocateStolen(h->address, h->stolen,
                                                               static_cast<unsigned char*>(*h->original)))) {
                Log("SKIP %s at %08X: cannot build trampoline", h->name, static_cast<unsigned>(h->address));
                continue;
            }
        }
        DWORD old = 0;
        const bool inText = text.Contains(h->address, 5);
        if (!inText) VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &old);
        target[0] = 0xE9; // JMP rel32
        std::int32_t rel = static_cast<std::int32_t>(
            reinterpret_cast<std::uintptr_t>(h->replacement) - (h->address + 5));
        std::memcpy(target + 1, &rel, 4);
        if (!inText) VirtualProtect(target, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), target, 5);
        Log(h->record ? "recording %s at %08X -> %p" : h->verify ? "verifying %s at %08X -> %p" : "hooked %s at %08X -> %p", h->name,
            static_cast<unsigned>(h->address), h->replacement);
        g_installed.emplace_back(h->address, PlainReplacement(h->address));
        if (h->recordOurs) RedirectOurs(h);
        ++installed;
    }
    text.Restore();
    if (shortLeft) Log("%d function(s) under 5 bytes left to the original (EI_REPLACE_SHORT)", shortLeft);
    if (standalone) return installed;
    // The game's static initialisers have run and nothing else yet: their results are compared now.
    for (StartupCheck* check = g_startupChecks; check; check = check->next) {
        Log("startup check %s", check->name);
        check->run();
    }
    return installed;
}

// Hooks are installed when WinMain starts, not when the DLL loads: Rebirth's addon applies its own
// patches from a hook on the WinMain call, and they must be in place before we compare bytes.
static constexpr std::uintptr_t kWinMain = 0x00456440;
void InstallCoverage();   // cover.cpp
void InstallProfiler();   // profile.cpp
static unsigned char g_winMainBytes[5];

static void __stdcall OnWinMain() {
    // System DPI aware before the game makes its window, as the HIGHDPIAWARE compatibility flag on
    // run/vanilla and run/rebirth already makes it. Without it, at 200% scaling Windows stretches the
    // window while our own swap chain (`present`, run/native) sizes itself in physical pixels, so
    // only a quarter of the frame shows.
    SetProcessDPIAware();
    DWORD old;
    auto* p = reinterpret_cast<unsigned char*>(kWinMain);
    VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(p, g_winMainBytes, 5);
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
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
