#include "ei/hook.h"
#include "log.h"

#include <windows.h>

#include <cstdio>
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

static bool HasVerifyEntry(std::uintptr_t address) {
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

// The .text section has identical file and virtual layout (raw 0x1000 == VA 0x1000).
static const unsigned char* FileBytesAt(const std::vector<unsigned char>& file, std::uintptr_t va) {
    std::uintptr_t off = va - kImageBase;
    return off + 16 <= file.size() ? file.data() + off : nullptr;
}

int InstallHooks() {
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

    int installed = 0;
    for (HookEntry* h = g_hooks; h; h = h->next) {
        if (!h->verify && HasVerifyEntry(h->address)) continue;
        auto* target = reinterpret_cast<unsigned char*>(h->address);
        const unsigned char* orig = FileBytesAt(file, h->address);
        // Another mod (e.g. Rebirth's addon.dll) may already have patched this function.
        if (!orig || std::memcmp(target, orig, 5) != 0) {
            Log("SKIP %s at %08X: code in memory differs from game.exe on disk (patched by another mod?)",
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

} // namespace ei
