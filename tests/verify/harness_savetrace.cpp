// The save trace (harness_savetrace.h; ticket SV7a).
#include "harness_savetrace.h"

#include "../../src/hook/log.h"
#include "ei/hook.h"
#include "game/game_data.h"

#include <windows.h>
#include <intrin.h>

#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace ei::verify::savetrace {

namespace {

// The memory streams' vtables (CMemoryStream 0073BD90; the second one, 0073C960, with the same
// slots 1-7) and the slots the trace uses: 2 Write(buffer, size), 5 Position().
constexpr std::uintptr_t kStreamVtables[] = {0x0073BD90, 0x0073C960};
constexpr int kWriteSlot = 2;
constexpr int kPositionSlot = 5;
constexpr int kCallers = 4;
constexpr std::uint32_t kDumpAbove = 2048;

using WriteFn = void(__fastcall*)(void* stream, void* edx, const void* buffer, std::uint32_t size);
using PositionFn = std::uint32_t(__fastcall*)(void* stream, void* edx);

struct Entry {
    std::uint32_t position, size;
    std::uint32_t callers[kCallers];
};
struct Trace {
    std::vector<Entry> entries;
    std::uint32_t end = 0;
};

WriteFn g_writes[2];
std::uintptr_t g_selfBase;
std::uintptr_t g_selfEnd;
CRITICAL_SECTION g_lock;
std::unordered_map<void*, Trace>* g_streams;

bool ReturnAddress(std::uintptr_t v) {
    const bool game = v >= 0x00401000 && v < 0x0073A670;   // game.exe's .text
    const bool self = v >= g_selfBase + 0x1000 && v < g_selfEnd;
    if (!game && !self) return false;
    const auto* code = reinterpret_cast<const unsigned char*>(v);
    return code[-5] == 0xE8 || code[-6] == 0xFF || code[-3] == 0xFF || code[-2] == 0xFF;
}

std::uint32_t Shown(std::uintptr_t v) {
    return static_cast<std::uint32_t>(v >= g_selfBase && v < g_selfEnd ? v - g_selfBase + 0x10000000 : v);
}

void Note(void* stream, std::uint32_t size, void* const* frame) {
    const auto position = reinterpret_cast<PositionFn>((*static_cast<void***>(stream))[kPositionSlot])(stream, nullptr);
    Entry e{position, size, {}};
    int found = 0;
    const auto* top = static_cast<void* const*>(reinterpret_cast<NT_TIB*>(NtCurrentTeb())->StackBase);
    for (int i = 0; i < 64 && found < kCallers && frame + i < top; ++i) {
        const auto v = reinterpret_cast<std::uintptr_t>(frame[i]);
        if (ReturnAddress(v)) e.callers[found++] = Shown(v);
    }
    EnterCriticalSection(&g_lock);
    Trace& trace = (*g_streams)[stream];
    if (position == 0) {
        trace.entries.clear();
        trace.end = 0;
    }
    trace.entries.push_back(e);
    if (position + size > trace.end) trace.end = position + size;
    LeaveCriticalSection(&g_lock);
}

template <int kVtable>
void __fastcall TracedWrite(void* stream, void* edx, const void* buffer, std::uint32_t size) {
    Note(stream, size, static_cast<void* const*>(_AddressOfReturnAddress()));
    g_writes[kVtable](stream, edx, buffer, size);
}

} // namespace

bool Enabled() {
    char value[8] = "";
    return GetEnvironmentVariableA("EI_SAVETRACE", value, sizeof(value)) && value[0] && value[0] != '0';
}

void Install(std::uintptr_t selfBase, std::uintptr_t selfEnd) {
    g_selfBase = selfBase;
    g_selfEnd = selfEnd;
    InitializeCriticalSection(&g_lock);
    g_streams = new std::unordered_map<void*, Trace>;
    const WriteFn traced[2] = {&TracedWrite<0>, &TracedWrite<1>};
    for (int i = 0; i < 2; ++i) {
        auto* slot = ei::GamePointer<WriteFn>(kStreamVtables[i]) + kWriteSlot;
        DWORD old;
        VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old);
        g_writes[i] = *slot;
        *slot = traced[i];
        VirtualProtect(slot, sizeof(*slot), old, &old);
    }
    Log("harness: save trace (memory stream writes into savetrace.txt when a .sav file is written)");
}

void OnFileWrite(const char* path) {
    if (!g_streams || !path) return;
    const std::size_t length = std::strlen(path);
    if (length < 4 || _stricmp(path + length - 4, ".sav") != 0) return;
    char out[MAX_PATH];
    ei::OutputPath("savetrace.txt", out);
    FILE* f = nullptr;
    if (fopen_s(&f, out, "a") != 0 || !f) return;
    EnterCriticalSection(&g_lock);
    for (const auto& [stream, trace] : *g_streams) {
        if (trace.end <= kDumpAbove) continue;
        std::fprintf(f, "# save %s stream %p %u\n", path, stream, trace.end);
        for (const Entry& e : trace.entries)
            std::fprintf(f, "%u %u %08X %08X %08X %08X\n", e.position, e.size, e.callers[0], e.callers[1], e.callers[2],
                         e.callers[3]);
    }
    LeaveCriticalSection(&g_lock);
    std::fclose(f);
}

} // namespace ei::verify::savetrace
