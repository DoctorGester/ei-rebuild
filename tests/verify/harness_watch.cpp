// The orientation watch (harness_watch.h; tickets SV7b4 / SV3f).
#include "harness_watch.h"

#include "../../src/hook/log.h"
#include "ei/hook.h"
#include "../../src/game/unit.h"
#include "../../src/game/world.h"
#include "game/game_data.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ei::verify::watch {

namespace {

constexpr DWORD kArmCode = 0xE1A70001;   // our own exception: the handler loads the registers
constexpr int kCallers = 8;

unsigned g_id;
bool g_flags;                           // EI_WATCH_CUNIT=<id>,flags: the unit's flags_ instead of its orientation
std::uint32_t g_lastFlags = ~0u;        // the flags value last logged (only changes are logged)
std::uintptr_t g_slot;                  // EI_WATCH_ADDR: one dword watched for reads and writes
unsigned g_slotFrom = 0, g_slotTo = ~0u;  // the world updates it is logged in
bool g_slotArmed;
FILE* g_file;
CUnit* g_unit;          // the unit the registers watch (null: none)
CUnit* g_wanted;        // what the next arm exception loads (null: clear)
unsigned g_updates;
DWORD g_mainThread;
std::uintptr_t g_addonBase, g_addonEnd, g_selfBase, g_selfEnd;

CWorldClient* ClientWorld() { return ei::GamePointer<CWorldClient>(0x007AF5B8); }

void ModuleRange(HMODULE module, std::uintptr_t& base, std::uintptr_t& end) {
    if (!module) return;
    base = reinterpret_cast<std::uintptr_t>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    end = base + nt->OptionalHeader.SizeOfImage;
}

bool Code(std::uintptr_t v) {
    return (!ei::Standalone() && v >= 0x00401000 && v < 0x0073A670) || (v >= g_addonBase + 0x1000 && v < g_addonEnd) ||
           (v >= g_selfBase + 0x1000 && v < g_selfEnd);
}

bool ReturnAddress(std::uintptr_t v) {
    if (!Code(v)) return false;
    const auto* code = reinterpret_cast<const unsigned char*>(v);
    return code[-5] == 0xE8 || code[-6] == 0xFF || code[-3] == 0xFF || code[-2] == 0xFF;
}

void Print(std::uintptr_t v) {
    if (v >= g_addonBase && v < g_addonEnd)
        std::fprintf(g_file, " A+%X", unsigned(v - g_addonBase));
    else if (v >= g_selfBase && v < g_selfEnd)
        std::fprintf(g_file, " H+%X", unsigned(v - g_selfBase));
    else
        std::fprintf(g_file, " %08X", unsigned(v));
}

LONG CALLBACK Handler(EXCEPTION_POINTERS* info) {
    CONTEXT* c = info->ContextRecord;
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code == kArmCode) {
        c->ContextFlags |= CONTEXT_DEBUG_REGISTERS;
        if (g_slot) {
            c->Dr0 = g_slotArmed ? g_slot : 0;
            c->Dr7 = g_slotArmed ? (0x1u | (0xFu << 16)) : 0;   // L0; R/W 11 (reads and writes), LEN 11
        } else if (g_wanted && g_flags) {
            c->Dr0 = reinterpret_cast<std::uintptr_t>(&g_wanted->flags_);
            c->Dr7 = 0x1u | (0xDu << 16);   // L0; R/W 01 (writes), LEN 11
        } else if (g_wanted) {
            const auto base = reinterpret_cast<std::uintptr_t>(&g_wanted->orientation);
            c->Dr0 = base;
            c->Dr1 = base + 4;
            c->Dr2 = base + 8;
            c->Dr3 = base + 12;
            // L0..L3 on; R/W 01 (writes) and LEN 11 (4 bytes) for each.
            c->Dr7 = 0x55 | (0xDu << 16) | (0xDu << 20) | (0xDu << 24) | (0xDu << 28);
        } else {
            c->Dr0 = c->Dr1 = c->Dr2 = c->Dr3 = 0;
            c->Dr7 = 0;
        }
        c->Dr6 = 0;
        g_unit = g_wanted;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code == EXCEPTION_SINGLE_STEP && g_slot && (c->Dr6 & 1)) {
        if (GetCurrentThreadId() == g_mainThread) {
            std::fprintf(g_file, "u%u", g_updates);
            Print(c->Eip);
            std::fprintf(g_file, " v %08X esp-%X <", unsigned(*reinterpret_cast<const std::uint32_t*>(g_slot)),
                         unsigned(g_slot - c->Esp));
            const auto* stack = reinterpret_cast<const std::uintptr_t*>(c->Esp);
            int found = 0;
            for (int i = 0; i < 256 && found < 4; ++i) {
                if (!ReturnAddress(stack[i])) continue;
                Print(stack[i]);
                ++found;
            }
            std::fprintf(g_file, "\n");
        }
        c->Dr6 = 0;
        c->ContextFlags |= CONTEXT_DEBUG_REGISTERS;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (code != EXCEPTION_SINGLE_STEP || !g_unit || (c->Dr6 & 0xF) == 0) return EXCEPTION_CONTINUE_SEARCH;
    if (GetCurrentThreadId() != g_mainThread) return EXCEPTION_CONTINUE_SEARCH;
    if (g_flags) {
        const std::uint32_t value = g_unit->flags_;
        if (value != g_lastFlags) {
            g_lastFlags = value;
            std::fprintf(g_file, "u%u", g_updates);
            Print(c->Eip);
            std::fprintf(g_file, " flags %08X <", unsigned(value));
            const auto* stack = reinterpret_cast<const std::uintptr_t*>(c->Esp);
            int found = 0;
            for (int i = 0; i < 256 && found < kCallers; ++i) {
                if (!ReturnAddress(stack[i])) continue;
                Print(stack[i]);
                ++found;
            }
            std::fprintf(g_file, "\n");
        }
        c->Dr6 = 0;
        c->ContextFlags |= CONTEXT_DEBUG_REGISTERS;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    const auto* q = reinterpret_cast<const std::uint32_t*>(&g_unit->orientation);
    std::fprintf(g_file, "u%u dr%X", g_updates, unsigned(c->Dr6 & 0xF));
    Print(c->Eip);
    std::fprintf(g_file, " q %08X %08X %08X %08X fcw %04X <", unsigned(q[0]), unsigned(q[1]), unsigned(q[2]),
                 unsigned(q[3]), unsigned(c->FloatSave.ControlWord & 0xFFFF));
    const auto* stack = reinterpret_cast<const std::uintptr_t*>(c->Esp);
    int found = 0;
    for (int i = 0; i < 256 && found < kCallers; ++i) {
        if (!ReturnAddress(stack[i])) continue;
        Print(stack[i]);
        ++found;
    }
    std::fprintf(g_file, "\n");
    c->Dr6 = 0;
    c->ContextFlags |= CONTEXT_DEBUG_REGISTERS;
    return EXCEPTION_CONTINUE_EXECUTION;
}

CUnit* FindUnit() {
    CWorldClient* c = ClientWorld();
    if (!c || !c->objects_.head) return nullptr;
    for (auto* n = c->objects_.First(); n != c->objects_.End(); n = n->next) {
        if (n->data && static_cast<unsigned>(n->data->id_) == g_id && n->data->GetClassId() == world::kClassUnit)
            return static_cast<CUnit*>(n->data);
    }
    return nullptr;
}

void Arm(CUnit* unit) {
    g_wanted = unit;
    RaiseException(kArmCode, 0, 0, nullptr);
    std::fprintf(g_file, "u%u %s\n", g_updates, unit ? "armed" : "cleared");
}

int g_scrub = -1;   // EI_STACK_SCRUB=<byte>[,<from>,<to>]: after each world update, below the caller's frame
unsigned g_scrubFrom = 0, g_scrubTo = 0x10000;   // bytes below the area's top
unsigned g_skipFrom = 0, g_skipTo = 0;            // left alone within it

__declspec(noinline) void Scrub(int value) {
    volatile unsigned char area[0x10000];
    for (unsigned i = g_scrubFrom; i < g_scrubTo; ++i)
        if (i < g_skipFrom || i >= g_skipTo) area[sizeof(area) - 1 - i] = static_cast<unsigned char>(value);
    static bool noted;
    if (!noted) {
        noted = true;
        Log("harness: stack scrub %02X over %08X..%08X", unsigned(value),
            unsigned(reinterpret_cast<std::uintptr_t>(&area[sizeof(area) - g_scrubTo])),
            unsigned(reinterpret_cast<std::uintptr_t>(&area[sizeof(area) - g_scrubFrom])));
    }
}

} // namespace

void ScrubStack() {
    static long spin = -1;   // EI_SPIN_US=<n> (experiment): a busy wait of n microseconds instead
    if (spin < 0) {
        char value[16] = "";
        spin = GetEnvironmentVariableA("EI_SPIN_US", value, sizeof(value)) ? std::strtol(value, nullptr, 0) : 0;
    }
    if (spin > 0) {
        LARGE_INTEGER f, a, b;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&a);
        do QueryPerformanceCounter(&b);
        while ((b.QuadPart - a.QuadPart) * 1000000 / f.QuadPart < spin);
    }
    if (g_scrub < 0) {
        char value[48] = "";
        char* end = value;
        g_scrub = GetEnvironmentVariableA("EI_STACK_SCRUB", value, sizeof(value))
                      ? static_cast<int>(std::strtoul(value, &end, 0)) & 0xFF
                      : 0x100;
        if (*end == ',') g_scrubFrom = static_cast<unsigned>(std::strtoul(end + 1, &end, 0));
        if (*end == ',') g_scrubTo = static_cast<unsigned>(std::strtoul(end + 1, &end, 0));
        if (*end == ',') g_skipFrom = static_cast<unsigned>(std::strtoul(end + 1, &end, 0));
        if (*end == ',') g_skipTo = static_cast<unsigned>(std::strtoul(end + 1, &end, 0));
        if (g_scrubTo > 0x10000) g_scrubTo = 0x10000;
    }
    if (g_scrub < 0x100) Scrub(g_scrub);
}

bool Enabled() {
    char value[64] = "";
    if (GetEnvironmentVariableA("EI_WATCH_ADDR", value, sizeof(value))) {
        char* end = value;
        g_slot = static_cast<std::uintptr_t>(std::strtoul(value, &end, 16)) & ~std::uintptr_t(3);
        if (*end == ',') g_slotFrom = static_cast<unsigned>(std::strtoul(end + 1, &end, 0));
        if (*end == ',') g_slotTo = static_cast<unsigned>(std::strtoul(end + 1, &end, 0));
        if (g_slot) return true;
    }
    if (!GetEnvironmentVariableA("EI_WATCH_CUNIT", value, sizeof(value))) return false;
    char* end = value;
    g_id = static_cast<unsigned>(std::strtoul(value, &end, 10));
    g_flags = std::strcmp(end, ",flags") == 0;
    return g_id != 0;
}

void Install() {
    char path[MAX_PATH];
    ei::OutputPath("watch.txt", path);
    if (fopen_s(&g_file, path, "w") != 0 || !g_file) return;
    ModuleRange(GetModuleHandleA("addon.dll"), g_addonBase, g_addonEnd);
    HMODULE self = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&Install), &self);
    ModuleRange(self, g_selfBase, g_selfEnd);
    AddVectoredExceptionHandler(1, Handler);
    std::fprintf(g_file, "# watch %s %08X (addon %08X, ei_hook %08X)\n", g_slot ? "dword" : g_flags ? "cunit flags" : "cunit orientation",
                 unsigned(g_slot ? g_slot : g_id), unsigned(g_addonBase), unsigned(g_selfBase));
    Log("harness: watching %s (EI_WATCH_ADDR / EI_WATCH_CUNIT, watch.txt)", g_slot ? "a dword" : "a unit's orientation");
}

void AfterWorldUpdate() {
    if (!g_file) return;
    if (!g_mainThread) g_mainThread = GetCurrentThreadId();   // the thread of the world updates
    ++g_updates;
    if (!g_addonEnd) ModuleRange(GetModuleHandleA("addon.dll"), g_addonBase, g_addonEnd);
    if (g_slot) {
        // Logged from the end of update g_slotFrom to the end of update g_slotTo.
        const bool on = g_updates >= g_slotFrom && g_updates < g_slotTo;
        if (on != g_slotArmed) {
            g_slotArmed = on;
            RaiseException(kArmCode, 0, 0, nullptr);
            std::fprintf(g_file, "u%u %s %08X\n", g_updates, on ? "armed" : "cleared", unsigned(g_slot));
        }
        std::fflush(g_file);
        return;
    }
    CUnit* unit = FindUnit();
    if (unit != g_unit) Arm(unit);
    std::fflush(g_file);
}

} // namespace ei::verify::watch
