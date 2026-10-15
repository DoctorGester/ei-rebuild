// The game's heap in harness runs on the virtual clock (harness_heap.h).
#include "harness_heap.h"

#include "../../src/game/world.h"
#include "../../src/hook/log.h"
#include "ei/hook.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

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

// The heap made for the CRT and the lock that serializes it.
HANDLE g_heap;
CRITICAL_SECTION g_lock;

template <class T>
void PatchSlot(std::uintptr_t address, T replacement, T& original) {
    auto* slot = reinterpret_cast<T*>(address);
    DWORD old;
    VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old);
    original = *slot;
    *slot = replacement;
    VirtualProtect(slot, sizeof(*slot), old, &old);
}

// Holds the lock for g_heap's calls; other heaps go straight through.
struct Locked {
    explicit Locked(HANDLE heap) : held(heap == g_heap) { if (held) EnterCriticalSection(&g_lock); }
    ~Locked() { if (held) LeaveCriticalSection(&g_lock); }
    Locked(const Locked&) = delete;
    Locked& operator=(const Locked&) = delete;
    bool held;
};

HANDLE WINAPI RepeatableHeapCreate(DWORD options, SIZE_T initial, SIZE_T maximum) {
    if (g_heap) return g_heapCreate(options, initial, maximum);
    g_heap = g_heapCreate(options | HEAP_NO_SERIALIZE, initial, maximum);
    Log("harness: the CRT's heap %p without the low-fragmentation heap (repeatable addresses)", g_heap);
    return g_heap;
}

BOOL WINAPI RepeatableHeapDestroy(HANDLE heap) {
    if (heap == g_heap) g_heap = nullptr;
    return g_heapDestroy(heap);
}

LPVOID WINAPI RepeatableHeapAlloc(HANDLE heap, DWORD flags, SIZE_T bytes) {
    Locked lock(heap);
    return g_heapAlloc(heap, flags, bytes);
}

BOOL WINAPI RepeatableHeapFree(HANDLE heap, DWORD flags, LPVOID memory) {
    Locked lock(heap);
    return g_heapFree(heap, flags, memory);
}

LPVOID WINAPI RepeatableHeapReAlloc(HANDLE heap, DWORD flags, LPVOID memory, SIZE_T bytes) {
    Locked lock(heap);
    return g_heapReAlloc(heap, flags, memory, bytes);
}

SIZE_T WINAPI RepeatableHeapSize(HANDLE heap, DWORD flags, LPCVOID memory) {
    Locked lock(heap);
    return g_heapSize(heap, flags, memory);
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
