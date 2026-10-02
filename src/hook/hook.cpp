#include "ei/hook.h"
#include "log.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <iterator>
#include <vector>

namespace ei {

static HookEntry* g_hooks = nullptr;

HookRegistrar::HookRegistrar(std::uintptr_t address, void* replacement, const char* name)
    : entry{address, replacement, name, g_hooks} {
    g_hooks = &entry;
}

HookRegistrar::HookRegistrar(std::uintptr_t address, void* wrapper, const char* name, int stolen,
                             void** original)
    : entry{address, wrapper, name, g_hooks, stolen, original, true} {
    g_hooks = &entry;
}

// Functions listed in verified.txt (next to the DLL) have passed verification and run without
// their verify wrapper: the plain replacement is installed instead. One hex address per line.
static std::vector<std::uintptr_t> g_verified;

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
        if (a) g_verified.push_back(a);
    }
    std::fclose(f);
}

static bool IsVerified(std::uintptr_t address) {
    return std::find(g_verified.begin(), g_verified.end(), address) != g_verified.end();
}

static bool HasVerifyEntry(std::uintptr_t address) {
    if (IsVerified(address)) return false;
    for (HookEntry* h = g_hooks; h; h = h->next)
        if (h->verify && h->address == address) return true;
    return false;
}

// Copies the first `stolen` bytes of the original function into executable memory followed by a
// JMP back to the rest of it, giving a callable copy of the original.
static void* MakeTrampoline(std::uintptr_t address, int stolen) {
    auto* t = static_cast<unsigned char*>(
        VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
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

// The .text section has identical file and virtual layout (raw 0x1000 == VA 0x1000).
static const unsigned char* FileBytesAt(const std::vector<unsigned char>& file, std::uintptr_t va) {
    std::uintptr_t off = va - kImageBase;
    return off + 16 <= file.size() ? file.data() + off : nullptr;
}

int InstallHooks() {
    // EI_NO_HOOKS=1 (ei_launch --no-hooks): run the original code only, as a baseline for harness runs.
    char noHooks[4];
    if (GetEnvironmentVariableA("EI_NO_HOOKS", noHooks, sizeof(noHooks)) && noHooks[0] == '1') {
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

    LoadVerifiedList();
    if (!g_verified.empty()) Log("%u function(s) marked verified: running without verify wrappers", unsigned(g_verified.size()));

    int installed = 0;
    for (HookEntry* h = g_hooks; h; h = h->next) {
        if (h->verify ? IsVerified(h->address) : HasVerifyEntry(h->address)) continue;
        auto* target = reinterpret_cast<unsigned char*>(h->address);
        const unsigned char* orig = FileBytesAt(file, h->address);
        std::uint32_t size = FunctionSizeAt(h->address);
        if (!orig || size < 5) {
            Log("SKIP %s at %08X: not a known function start", h->name, static_cast<unsigned>(h->address));
            continue;
        }
        // Another mod (e.g. Rebirth's addon.dll) may have patched code anywhere in the body; replacing
        // the function would silently drop that patch.
        std::uint32_t diff = 0;
        while (diff < size && target[diff] == orig[diff]) ++diff;
        if (diff < size) {
            Log("SKIP %s at %08X: byte +0x%X differs from game.exe on disk (patched by another mod)",
                h->name, static_cast<unsigned>(h->address), diff);
            continue;
        }
        // A pointer to a virtual member function is a dispatch thunk (MOV EAX,[ECX]; JMP [EAX+n]),
        // which would jump back into the hooked function forever.
        const auto* code = static_cast<const unsigned char*>(h->replacement);
        for (int hops = 0; code[0] == 0xE9 && hops < 4; ++hops)   // incremental-link jump stubs
            code += 5 + *reinterpret_cast<const std::int32_t*>(code + 1);
        if (code[0] == 0x8B && code[1] == 0x01 && code[2] == 0xFF && (code[3] == 0x20 || code[3] == 0x60 || code[3] == 0xA0)) {
            Log("SKIP %s at %08X: replacement is a virtual-call thunk (register a non-virtual shim instead)",
                h->name, static_cast<unsigned>(h->address));
            continue;
        }
        if (h->stolen > 0) {
            if (h->stolen < 5 || std::memcmp(target, orig, h->stolen) != 0 ||
                !(*h->original = MakeTrampoline(h->address, h->stolen))) {
                Log("SKIP %s at %08X: cannot build trampoline", h->name, static_cast<unsigned>(h->address));
                continue;
            }
        }
        DWORD old;
        VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &old);
        target[0] = 0xE9; // JMP rel32
        std::int32_t rel = static_cast<std::int32_t>(
            reinterpret_cast<std::uintptr_t>(h->replacement) - (h->address + 5));
        std::memcpy(target + 1, &rel, 4);
        VirtualProtect(target, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), target, 5);
        Log(h->verify ? "verifying %s at %08X -> %p" : "hooked %s at %08X -> %p", h->name, static_cast<unsigned>(h->address), h->replacement);
        ++installed;
    }
    return installed;
}

// Hooks are installed when WinMain starts, not when the DLL loads: Rebirth's addon applies its own
// patches from a hook on the WinMain call, and they must be in place before we compare bytes.
static constexpr std::uintptr_t kWinMain = 0x00456440;
static unsigned char g_winMainBytes[5];

static void __stdcall OnWinMain() {
    DWORD old;
    auto* p = reinterpret_cast<unsigned char*>(kWinMain);
    VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(p, g_winMainBytes, 5);
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    Log("%d hook(s) installed", InstallHooks());
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
