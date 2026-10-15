// The game's heap in harness runs on the virtual clock (harness_heap.h).
#include "harness_heap.h"

#include "../../src/game/world.h"
#include "../../src/hook/log.h"
#include "ei/hook.h"

#include <windows.h>
#include <intrin.h>

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>

namespace ei::verify::heap {

namespace {

// game.exe's import table.
constexpr std::uintptr_t kHeapCreateSlot = 0x0073B3C0;
constexpr std::uintptr_t kHeapDestroySlot = 0x0073B3BC;
constexpr std::uintptr_t kHeapAllocSlot = 0x0073B360;
constexpr std::uintptr_t kHeapFreeSlot = 0x0073B35C;
constexpr std::uintptr_t kHeapReAllocSlot = 0x0073B374;
constexpr std::uintptr_t kHeapSizeSlot = 0x0073B37C;

// The CRT's heap handle (_crtheap), set by _heap_init (006F54E6).
HANDLE& CrtHeap() { return *reinterpret_cast<HANDLE*>(0x007CAF08); }

decltype(&::HeapCreate) g_heapCreate;
decltype(&::HeapDestroy) g_heapDestroy;
decltype(&::HeapAlloc) g_heapAlloc;
decltype(&::HeapFree) g_heapFree;
decltype(&::HeapReAlloc) g_heapReAlloc;
decltype(&::HeapSize) g_heapSize;

// The heap made for the CRT, its reservation (below) and the lock that serializes it.
HANDLE g_heap;
std::uintptr_t g_begin;
std::uintptr_t g_end;
CRITICAL_SECTION g_lock;

bool InReservation(const void* p) {
    const auto a = reinterpret_cast<std::uintptr_t>(p);
    return a >= g_begin && a < g_end;
}

template <class T>
void PatchSlot(std::uintptr_t address, T replacement, T& original) {
    auto* slot = reinterpret_cast<T*>(address);
    DWORD old;
    VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old);
    original = *slot;
    *slot = replacement;
    VirtualProtect(slot, sizeof(*slot), old, &old);
}

// The trace ("heaptrace"): one line per call on g_heap until world update g_traceUntil.
FILE* g_trace;
unsigned g_traceSeq;
unsigned g_traceUntil;
DWORD g_mainThread;
std::uintptr_t g_selfBase;
std::uintptr_t g_selfEnd;

} // namespace

unsigned TracePlace(const void* p) {
    return InReservation(p) ? 0x80000000u | unsigned(reinterpret_cast<std::uintptr_t>(p) - g_begin)
                            : unsigned(reinterpret_cast<std::uintptr_t>(p));
}

namespace {

// A value on the stack that looks like a return address into game.exe or ours (after a CALL).
bool ReturnAddress(std::uintptr_t v) {
    const bool game = v >= 0x00401000 && v < 0x0073B000;
    const bool self = v >= g_selfBase + 0x1000 && v < g_selfEnd;
    if (!game && !self) return false;
    const auto* code = reinterpret_cast<const unsigned char*>(v);
    return code[-5] == 0xE8 || code[-6] == 0xFF || code[-3] == 0xFF || code[-2] == 0xFF;
}

// The sequence (EI_HEAPSEQ): the trace's events kept in memory (no file writes while the game runs,
// which changed its timing), written to heapseq.txt at the end.
struct SeqEvent {
    char op;
    bool other;
    unsigned bytes, in, out;
    unsigned callers[6];
};
SeqEvent* g_seq;
unsigned g_seqCount;
unsigned g_seqCapacity;
std::string* g_seqNotes;

void Sequence(char op, SIZE_T bytes, const void* in, const void* out, void* const* frame) {
    if (g_seqCount >= g_seqCapacity) return;
    SeqEvent& e = g_seq[g_seqCount++];
    e.op = op;
    e.other = GetCurrentThreadId() != g_mainThread;
    e.bytes = unsigned(bytes);
    e.in = TracePlace(in);
    e.out = TracePlace(out);
    int found = 0;
    const auto* top = static_cast<void* const*>(reinterpret_cast<NT_TIB*>(NtCurrentTeb())->StackBase);
    for (int i = 0; i < 96 && found < 6 && frame + i < top; ++i) {
        const auto v = reinterpret_cast<std::uintptr_t>(frame[i]);
        if (ReturnAddress(v)) e.callers[found++] = unsigned(v >= g_selfBase && v < g_selfEnd ? v - g_selfBase + 0x10000000 : v);
    }
    for (; found < 6; ++found) e.callers[found] = 0;
}

// `op` a(lloc), f(ree), r(ealloc); the callers: the first return addresses on the stack from `frame`.
void Trace(char op, SIZE_T bytes, const void* in, const void* out, void* const* frame) {
    if (g_seq) Sequence(op, bytes, in, out, frame);
    if (!g_trace) return;
    std::uintptr_t callers[10] = {};
    int found = 0;
    const auto* top = static_cast<void* const*>(reinterpret_cast<NT_TIB*>(NtCurrentTeb())->StackBase);
    for (int i = 0; i < 128 && found < 10 && frame + i < top; ++i) {
        const auto v = reinterpret_cast<std::uintptr_t>(frame[i]);
        if (ReturnAddress(v)) callers[found++] = v >= g_selfBase && v < g_selfEnd ? v - g_selfBase + 0x10000000 : v;
    }
    const DWORD thread = GetCurrentThreadId();
    std::fprintf(g_trace, "%u %c%s %u %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X\n", g_traceSeq++, op,
                 thread == g_mainThread ? "" : "*", unsigned(bytes), TracePlace(in), TracePlace(out), unsigned(callers[0]),
                 unsigned(callers[1]), unsigned(callers[2]), unsigned(callers[3]), unsigned(callers[4]),
                 unsigned(callers[5]), unsigned(callers[6]), unsigned(callers[7]), unsigned(callers[8]), unsigned(callers[9]));
}

// Holds the lock for g_heap's calls; other heaps go straight through.
struct Locked {
    explicit Locked(HANDLE heap) : held(heap == g_heap) { if (held) EnterCriticalSection(&g_lock); }
    ~Locked() { if (held) LeaveCriticalSection(&g_lock); }
    Locked(const Locked&) = delete;
    Locked& operator=(const Locked&) = delete;
    bool held;
};

// The CRT's heap is two (SV2b). A growable heap takes more address space in segments of its own as
// it fills, wherever VirtualAlloc finds room, and that depends on everything else in the process
// (the renderer's threads, the driver): in one run a later segment lies above an earlier one, in the
// next below it, so the blocks' address order, which the game reads (a script group's members are
// kept in address order), changed between runs of ours. So the blocks below kLarge come from a heap
// of one fixed reservation (g_heap: HeapCreate with a maximum), where the same calls give the same
// order (the addresses differ by the reservation's base only), and larger ones from a growable heap
// (g_large): a fixed heap refuses blocks above 0x7FFF8, and a growable heap gives every block from
// 0x7F000 on a VirtualAlloc of its own anyway, as the game's heap did. Blocks that other threads
// than the game's main one allocate (the sound thread's stream switch reads the registry; the CRT
// makes its locks when a thread first takes them) go to g_large too: when they come is up to the
// thread scheduler, and between the main thread's blocks they would move every later one. So do the
// blocks of MFC's temporary handle objects (InHandleMap). When the reservation is full, small blocks
// go to g_large too (logged once).
constexpr SIZE_T kReserve = 256u << 20;
constexpr SIZE_T kLarge = 0x7F000;
HANDLE g_large;
bool g_full;

// The heap that holds `p` (g_heap for null).
HANDLE Owner(const void* p) { return !p || InReservation(p) ? g_heap : g_large; }

// Whether the stack from `frame` up holds a return into CHandleMap::FromHandle (0070367C, 174
// bytes), which makes a temporary object for a handle that is in no map (CDC::FromHandle(GetDC(..)),
// CWnd::FromHandle): its CDC or CWnd, the temporary map's entry block and hash table. Whether the
// handle is in the map already, so whether they are made, depends on the handle values Windows gives
// (GetDC's cache of DCs), and the idle cleanup frees them in hash order of those values (SV3i).
bool InHandleMap(void* const* frame) {
    constexpr std::uintptr_t kFromHandle = 0x0070367C;
    constexpr std::uintptr_t kFromHandleEnd = kFromHandle + 174;
    const auto* top = static_cast<void* const*>(reinterpret_cast<NT_TIB*>(NtCurrentTeb())->StackBase);
    for (int i = 0; i < 96 && frame + i < top; ++i) {
        const auto v = reinterpret_cast<std::uintptr_t>(frame[i]);
        if (v > kFromHandle + 5 && v <= kFromHandleEnd && reinterpret_cast<const unsigned char*>(v)[-5] == 0xE8)
            return true;
    }
    return false;
}

// Whether a block of `bytes` goes into the reservation: the main thread's small blocks.
bool Reserved(SIZE_T bytes, void* const* frame) {
    return bytes < kLarge && GetCurrentThreadId() == g_mainThread && !InHandleMap(frame);
}

void* Allocate(DWORD flags, SIZE_T bytes, bool reserved) {
    if (reserved) {
        if (void* p = g_heapAlloc(g_heap, flags, bytes)) return p;
        if (!g_full) Log("harness: the CRT heap's reservation (%u MB) is full, small blocks from the growable heap",
                         unsigned(kReserve >> 20));
        g_full = true;
    }
    return g_heapAlloc(g_large, flags, bytes);
}

HANDLE WINAPI RepeatableHeapCreate(DWORD options, SIZE_T initial, SIZE_T maximum) {
    if (g_heap) return g_heapCreate(options, initial, maximum);
    g_mainThread = GetCurrentThreadId();   // _heap_init, at the CRT's start on the game's main thread
    g_heap = g_heapCreate(options | HEAP_NO_SERIALIZE, initial, kReserve);
    g_large = g_heapCreate(options | HEAP_NO_SERIALIZE, 0, 0);
    if (!g_heap || !g_large) {   // no room for the reservation: the game's heap as it was
        if (g_heap) g_heapDestroy(g_heap);
        if (g_large) g_heapDestroy(g_large);
        g_heap = g_large = nullptr;
        Log("harness: no %u MB reservation for the CRT's heap, its addresses will not repeat", unsigned(kReserve >> 20));
        return g_heapCreate(options, initial, maximum);
    }
    // The reservation: every region from the heap's allocation base on.
    MEMORY_BASIC_INFORMATION info{};
    VirtualQuery(g_heap, &info, sizeof(info));
    g_begin = reinterpret_cast<std::uintptr_t>(info.AllocationBase);
    g_end = g_begin;
    while (VirtualQuery(reinterpret_cast<void*>(g_end), &info, sizeof(info)) &&
           reinterpret_cast<std::uintptr_t>(info.AllocationBase) == g_begin)
        g_end += info.RegionSize;
    Log("harness: the CRT's heap %p, %08X-%08X, without the low-fragmentation heap (repeatable addresses; thread %u)",
        g_heap, unsigned(g_begin), unsigned(g_end), unsigned(g_mainThread));
    return g_heap;
}

BOOL WINAPI RepeatableHeapDestroy(HANDLE heap) {
    if (heap == g_heap) {
        g_heapDestroy(g_large);
        g_heap = g_large = nullptr;
    }
    return g_heapDestroy(heap);
}

LPVOID WINAPI RepeatableHeapAlloc(HANDLE heap, DWORD flags, SIZE_T bytes) {
    if (heap != g_heap) return g_heapAlloc(heap, flags, bytes);
    Locked lock(heap);
    auto* const frame = static_cast<void* const*>(_AddressOfReturnAddress());
    void* result = Allocate(flags, bytes, Reserved(bytes, frame));
    Trace('a', bytes, nullptr, result, frame);
    return result;
}

BOOL WINAPI RepeatableHeapFree(HANDLE heap, DWORD flags, LPVOID memory) {
    if (heap != g_heap) return g_heapFree(heap, flags, memory);
    Locked lock(heap);
    Trace('f', 0, memory, nullptr, static_cast<void* const*>(_AddressOfReturnAddress()));
    return g_heapFree(Owner(memory), flags, memory);
}

// A block that has to change heaps (it crosses kLarge, another thread resizes it, or the reservation
// is full) is moved.
LPVOID WINAPI RepeatableHeapReAlloc(HANDLE heap, DWORD flags, LPVOID memory, SIZE_T bytes) {
    if (heap != g_heap) return g_heapReAlloc(heap, flags, memory, bytes);
    Locked lock(heap);
    HANDLE from = Owner(memory);
    void* result = nullptr;
    auto* const frame = static_cast<void* const*>(_AddressOfReturnAddress());
    const bool reserved = Reserved(bytes, frame);
    if ((from == g_heap) == reserved) result = g_heapReAlloc(from, flags, memory, bytes);
    if (!result && !(flags & HEAP_REALLOC_IN_PLACE_ONLY) && !(from == g_large && bytes >= kLarge)) {
        const SIZE_T old = g_heapSize(from, 0, memory);
        result = Allocate(flags & HEAP_ZERO_MEMORY, bytes, reserved);
        if (result) {
            std::memcpy(result, memory, old < bytes ? old : bytes);
            g_heapFree(from, 0, memory);
        }
    }
    Trace('r', bytes, memory, result, frame);
    return result;
}

SIZE_T WINAPI RepeatableHeapSize(HANDLE heap, DWORD flags, LPCVOID memory) {
    if (heap != g_heap) return g_heapSize(heap, flags, memory);
    Locked lock(heap);
    return g_heapSize(Owner(memory), flags, memory);
}

// Where the original code spreads a unit's update countdown by its address: `SAR r,5 / AND r,0xF`
// with r a copy of the object pointer, then `ADD r,0x80` and the store to +0x174.
struct CountdownSite {
    std::uintptr_t address;
    int reg;   // 0 EAX, 1 ECX, 2 EDX
};
constexpr CountdownSite kCountdownSites[] = {
    {0x0058D4FA, 2},   // WakeUnitsAround (0058D3D0)
    {0x0058D5E2, 0},   // AddToUpdateMap (0058D5D0)
    {0x0058D8F7, 2},   // UpdateUnits (0058D670): a busy unit's countdown starts again
    {0x00592B14, 1},   // CreateObject (005927C0), three kinds of object
    {0x00592C81, 1},
    {0x00593131, 1},
    {0x005940DD, 1},   // InvokeAlarm (00593FC0)
    {0x00596BB1, 1},   // the update map from a save (00596900)
};

// Each site's `SAR r,5 / AND r,0xF` (6 bytes) becomes `MOV r,[r+0xC] / AND r,0xF`: the object's id
// (CEngineObject::id_) instead of its address, as CWorldServer::UpdateCountdownFor does.
// The standalone image has none of these sites (INT3): there the eight functions are ours only.
void SpreadCountdownsById() {
    if (ei::Standalone()) {
        CWorldServer::SpreadUpdateCountdownsById();
        Log("harness: update countdowns from object ids (the rewrites only: no original code in the image)");
        return;
    }
    int patched = 0;
    for (const CountdownSite& site : kCountdownSites) {
        auto* code = reinterpret_cast<unsigned char*>(site.address);
        const unsigned char r = static_cast<unsigned char>(site.reg);
        const unsigned char expected[6] = {0xC1, static_cast<unsigned char>(0xF8 + r), 0x05,
                                           0x83, static_cast<unsigned char>(0xE0 + r), 0x0F};
        if (std::memcmp(code, expected, sizeof(expected)) != 0) {
            Log("harness: update countdown site %08X is not as expected, left alone", unsigned(site.address));
            continue;
        }
        const unsigned char byId[6] = {0x8B, static_cast<unsigned char>(0x40 | (r << 3) | r), 0x0C,
                                       0x83, static_cast<unsigned char>(0xE0 + r), 0x0F};
        ei::NoteHarnessPatch(site.address, sizeof(byId), "the update countdown from the object's id, SV1b");
        DWORD old;
        VirtualProtect(code, sizeof(byId), PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(code, byId, sizeof(byId));
        VirtualProtect(code, sizeof(byId), old, &old);
        FlushInstructionCache(GetCurrentProcess(), code, sizeof(byId));
        ++patched;
    }
    CWorldServer::SpreadUpdateCountdownsById();
    Log("harness: update countdowns from object ids (%d of %d original sites)", patched,
        int(sizeof(kCountdownSites) / sizeof(kCountdownSites[0])));
}

} // namespace

void StartTrace(std::uintptr_t selfBase, std::uintptr_t selfEnd) {
    if (CrtHeap()) return;
    char path[MAX_PATH];
    ei::OutputPath("heaptrace.txt", path);
    if (fopen_s(&g_trace, path, "w") != 0 || !g_trace) return;
    std::setvbuf(g_trace, nullptr, _IOFBF, 1 << 20);
    char until[16] = "";
    GetEnvironmentVariableA("EI_HEAPTRACE", until, sizeof(until));
    g_traceUntil = until[0] ? static_cast<unsigned>(std::strtoul(until, nullptr, 10)) : 40;
    g_selfBase = selfBase;
    g_selfEnd = selfEnd;
    Log("harness: heap trace into heaptrace.txt until world update %u", g_traceUntil);
}

void StartSequence(std::uintptr_t selfBase, std::uintptr_t selfEnd) {
    char until[16] = "";
    if (CrtHeap() || g_trace || !GetEnvironmentVariableA("EI_HEAPSEQ", until, sizeof(until))) return;
    g_traceUntil = static_cast<unsigned>(std::strtoul(until, nullptr, 10));
    if (!g_traceUntil) g_traceUntil = 40;
    g_seqCapacity = 4u << 20;
    g_seq = static_cast<SeqEvent*>(VirtualAlloc(nullptr, g_seqCapacity * sizeof(SeqEvent), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!g_seq) return;
    g_seqNotes = new std::string;
    g_selfBase = selfBase;
    g_selfEnd = selfEnd;
    Log("harness: heap sequence in memory until world update %u (heapseq.txt)", g_traceUntil);
}

unsigned TraceUntil() { return g_trace || g_seq ? g_traceUntil : 0; }

void Note(const char* text) {
    if (g_seq) {
        EnterCriticalSection(&g_lock);
        *g_seqNotes += "# ";
        *g_seqNotes += text;
        *g_seqNotes += '\n';
        LeaveCriticalSection(&g_lock);
    }
    if (!g_trace) return;
    EnterCriticalSection(&g_lock);
    std::fprintf(g_trace, "# %s\n", text);
    LeaveCriticalSection(&g_lock);
}

void StopTrace() {
    if (g_seq) {
        EnterCriticalSection(&g_lock);
        SeqEvent* seq = g_seq;
        const unsigned count = g_seqCount;
        g_seq = nullptr;
        LeaveCriticalSection(&g_lock);
        char path[MAX_PATH];
        ei::OutputPath("heapseq.txt", path);
        FILE* f = nullptr;
        if (fopen_s(&f, path, "w") == 0 && f) {
            for (unsigned i = 0; i < count; ++i) {
                const SeqEvent& e = seq[i];
                std::fprintf(f, "%u %c%s %u %08X %08X %08X %08X %08X %08X %08X %08X\n", i, e.op, e.other ? "*" : "",
                             e.bytes, e.in, e.out, e.callers[0], e.callers[1], e.callers[2], e.callers[3],
                             e.callers[4], e.callers[5]);
            }
            std::fputs(g_seqNotes->c_str(), f);
            std::fclose(f);
        }
        VirtualFree(seq, 0, MEM_RELEASE);
    }
    if (!g_trace) return;
    EnterCriticalSection(&g_lock);
    std::fclose(g_trace);
    g_trace = nullptr;
    LeaveCriticalSection(&g_lock);
}

// The CRT's locks (_locktable 00797C00, _TOTAL_LOCKS 0x30) are made on their first _lock (006F0741,
// a malloc of 0x18) and the time zone is read once (006F8AA1, __tzset under lock 0xB), by whichever
// thread gets there first: the main thread converting a file time, or the sound thread's stream
// switch (SV2b). Done here first on the main thread, both keep their place in its blocks.
void SettleCrt() {
    static bool done = false;
    if (done || !g_heap || ei::Standalone()) return;
    done = true;
    using LockNumber = void(__cdecl*)(int);
    const auto lock = reinterpret_cast<LockNumber>(0x006F0741);
    const auto unlock = reinterpret_cast<LockNumber>(0x006F07A2);
    for (int n = 0; n < 0x30; ++n) {
        lock(n);
        unlock(n);
    }
    reinterpret_cast<void(__cdecl*)()>(0x006F8AA1)();
}

void Install() {
    SpreadCountdownsById();
    if (CrtHeap()) {
        Log("harness: the CRT's heap exists already, its addresses will not repeat");
        return;
    }
    InitializeCriticalSection(&g_lock);
    PatchSlot(kHeapCreateSlot, &RepeatableHeapCreate, g_heapCreate);
    PatchSlot(kHeapDestroySlot, &RepeatableHeapDestroy, g_heapDestroy);
    PatchSlot(kHeapAllocSlot, &RepeatableHeapAlloc, g_heapAlloc);
    PatchSlot(kHeapFreeSlot, &RepeatableHeapFree, g_heapFree);
    PatchSlot(kHeapReAllocSlot, &RepeatableHeapReAlloc, g_heapReAlloc);
    PatchSlot(kHeapSizeSlot, &RepeatableHeapSize, g_heapSize);
}

} // namespace ei::verify::heap
