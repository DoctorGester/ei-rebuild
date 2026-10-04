// A lever's state change (0052B7A0). See docs/notes/units.md ("Levers").
#include "lever.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

// The model's lever record by name [stlp::string from chars 0040C5A0, the map 0079C1AC's
// operator[] 00464750 (key) returning the stored pointer's slot, the string's destructor 0040C770].
struct RawString {
    void* words[3];
};
__declspec(naked) void __fastcall StringFromChars(RawString*, void*, const char*, const void*) EI_ORIGINAL(0x0040C5A0)
__declspec(naked) void** __fastcall LeverRecordAt(void*, void*, const RawString*) EI_ORIGINAL(0x00464750)
__declspec(naked) void __fastcall StringDestroy(RawString*) EI_ORIGINAL(0x0040C770)
// The object's world [00455950]; the sector map's units in a radius [005C0BD0: position, list,
// radius], Remove [005C1010], Add [005C0DF0]; the AI map's refresh [005B8B30 / 005B8B80], cell
// tests [005BA830 CanLeaveCell, 005BA9D0 FindFreeCellNear (unit, from, out)]; the figure's
// animation [005179E0 (model name)]; MarkChanged [0050E9F0 (part)]; the unit's controller resync
// [00554450, on +0x240].
__declspec(naked) std::uint8_t* __fastcall ObjectWorld(void*) EI_ORIGINAL(0x00455950)
__declspec(naked) void __fastcall UnitsInRadius(void*, void*, const float*, void*, float) EI_ORIGINAL(0x005C0BD0)
__declspec(naked) void __fastcall SectorRemove(void*, void*, void*) EI_ORIGINAL(0x005C1010)
__declspec(naked) void __fastcall SectorAdd(void*, void*, void*) EI_ORIGINAL(0x005C0DF0)
__declspec(naked) void __fastcall MapBeginRefresh(void*) EI_ORIGINAL(0x005B8B30)
__declspec(naked) void __fastcall MapEndRefresh(void*) EI_ORIGINAL(0x005B8B80)
__declspec(naked) bool __fastcall MapCanLeaveCell(void*, void*, void*) EI_ORIGINAL(0x005BA830)
__declspec(naked) void __fastcall MapFindFreeCellNear(void*, void*, void*, const float*, float*) EI_ORIGINAL(0x005BA9D0)
__declspec(naked) void __fastcall FigureAnimate(void*, void*, const char*) EI_ORIGINAL(0x005179E0)
__declspec(naked) void __fastcall MarkChanged(void*, void*, int) EI_ORIGINAL(0x0050E9F0)
__declspec(naked) void __fastcall ControllerResync(void*) EI_ORIGINAL(0x00554450)

constexpr int kSlotWorldActive = 0x68 / 4;   // world: () -> non-zero when the map is live

// MFC CPtrList (vtable 0073D454, blocks of 10) as the original keeps it on the stack.
struct PtrNode {
    PtrNode* next;
    PtrNode* prev;
    void* data;
};
struct PtrList {
    std::uintptr_t vtable = 0x0073D454;
    PtrNode* head = nullptr;
    PtrNode* tail = nullptr;
    int count = 0;
    PtrNode* free = nullptr;
    mfc::CPlex* blocks = nullptr;
    int blockSize = 10;
    // The destructor's RemoveAll (the elements need nothing).
    void RemoveAll() {
        count = 0;
        free = nullptr;
        tail = nullptr;
        head = nullptr;
        mfc::PlexFreeDataChain(blocks);
    }
};

} // namespace

void Lever::SetState(int state, float duration) {
    // FCOMP + TEST AH,1: a negative (or NaN) duration takes the model's default.
    if (x87::IsNaN(duration) || duration < 0.0f) {
        RawString key;
        char allocator;
        StringFromChars(&key, nullptr, At<const char*>(this, 0xA4), &allocator);
        void* record = *LeverRecordAt(reinterpret_cast<void*>(0x0079C1AC), nullptr, &key);
        StringDestroy(&key);
        duration = record ? static_cast<float>(static_cast<std::int64_t>(At<std::uint32_t>(record, 0x1C))) : 10.0f;
    }
    int& current = At<int>(this, 0x238);
    int states = At<int>(this, 0x23C);
    if (state < 0) {
        if (At<int>(this, 0x240) != 0) {
            current = (current + 1) % states;
        } else {
            int next = current + 1;
            current = next < states - 1 ? next : states - 1;
        }
    } else {
        int chosen = state < states - 1 ? state : states - 1;
        current = chosen <= 0 ? 0 : chosen;
    }

    std::uint8_t* world = ObjectWorld(this);
    if (!ei::VCall<int>(world, kSlotWorldActive)) return;
    PtrList around;
    void* sectors = world + 0x294;
    float radius = At<float>(this, 0x134) + 0.5f;
    UnitsInRadius(sectors, nullptr, &At<float>(this, 0x1C), &around, radius);
    for (PtrNode* node = around.head; node;) {
        PtrNode* next = node->next;
        SectorRemove(sectors, nullptr, node->data);
        node = next;
    }
    std::uint8_t* map = world + 0x170;
    map[2] = At<std::uint8_t>(this, 0x261);
    MapBeginRefresh(map);
    SectorRemove(sectors, nullptr, this);

    states = At<int>(this, 0x23C);
    float position = states > 1 ? static_cast<float>(static_cast<double>(At<int>(this, 0x238)) / (states - 1)) : 0.0f;
    // FCOMP + TEST AH,0x41: timed only for a duration above zero.
    if (!x87::IsNaN(duration) && duration > 0.0f) {
        At<float>(this, 0x254) = position;
        At<std::uint32_t>(this, 0x250) = At<std::uint32_t>(this, 0xC8);
        At<float>(this, 0x258) = (static_cast<float>(At<int>(world, 0x64)) + duration) + 1.0f;
        At<float>(this, 0x25C) = 1.0f / duration;
    } else {
        At<float>(this, 0x254) = position;
        At<float>(this, 0x250) = position;
        At<std::uint32_t>(this, 0x258) = 0;
        At<std::uint32_t>(this, 0x25C) = 0;
    }
    At<float>(this, 0xC8) = position;
    FigureAnimate(this, nullptr, At<const char*>(this, 0xA4));
    MarkChanged(this, nullptr, 5);
    SectorAdd(sectors, nullptr, this);
    MapEndRefresh(map);
    map[2] = 1;

    for (PtrNode* node = around.head; node;) {
        PtrNode* next = node->next;
        void* unit = node->data;
        if (!MapCanLeaveCell(map, nullptr, unit)) {
            float* at = &At<float>(unit, 0x1C);
            float free[3] = {at[0], at[1], at[2]};
            MapFindFreeCellNear(map, nullptr, unit, at, free);
            std::memcpy(at, free, sizeof(free));
            ControllerResync(At<void*>(unit, 0x240));
        }
        SectorAdd(sectors, nullptr, unit);
        node = next;
    }
    around.RemoveAll();
}

EI_REPLACE_METHOD(0x0052B7A0, Lever::SetState);
