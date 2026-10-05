// The CAIMap constructor (005B3C30) and its reset (005B5E10: every member destroyed in reverse
// order, then the constructor run again in place). The members are built through their own
// constructors (forwarders); the inlined ones (CArrays, CLists, the dirty rectangle) are stores.
// The original's exception frames only matter when a member constructor throws, which ends the
// game either way. See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"

#include "ei/hook.h"

#include <cstdint>
#include <initializer_list>
#include "originals_library.h"
#include "originals_ai.h"

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}
void* Member(void* object, int offset) { return static_cast<std::uint8_t*>(object) + offset; }

// Constructors of the members (thiscall).
__declspec(naked) void __fastcall Construct10C(void*, void*) EI_ORIGINAL(0x005BCCD0)
__declspec(naked) void __fastcall Construct134(void*, void*) EI_ORIGINAL(0x005BD1C0)
void __fastcall Construct1F4(void* a0, void*) { SearchPartConstructOriginal(a0); }
__declspec(naked) void __fastcall ConstructUnitLayer(void*, void*) EI_ORIGINAL(0x005B4110)
__declspec(naked) void __fastcall ConstructRegionMap(void*, void*) EI_ORIGINAL(0x005AF3D0)
__declspec(naked) void __fastcall ConstructList2598(void*, void*, int, int) EI_ORIGINAL(0x005BCC50)
__declspec(naked) void __fastcall ConstructListB(void*, void*, int, int) EI_ORIGINAL(0x005BCBD0)
__declspec(naked) void __fastcall Construct25BC(void*, void*) EI_ORIGINAL(0x005BD1E0)
__declspec(naked) void __fastcall ConstructSearch2614(void*, void*, int) EI_ORIGINAL(0x005B3FF0)
__declspec(naked) void __fastcall Construct2698(void*, void*, int, int) EI_ORIGINAL(0x005BCEE0)
__declspec(naked) void __fastcall Construct26A8(void*, void*, int, int) EI_ORIGINAL(0x005BCF00)
__declspec(naked) void __fastcall Construct26C8(void*, void*) EI_ORIGINAL(0x005B3F40)
// 005B40F0: resets the dirty rectangle (+4). 005B4150: fills the area lists (ai_area_lists.cpp).
__declspec(naked) void __fastcall ResetDirty(void*, void*) EI_ORIGINAL(0x005B40F0)
__declspec(naked) void __fastcall Initialise(void*, void*) EI_ORIGINAL(0x005B4150)

// Destructors of the members (thiscall).
__declspec(naked) void __fastcall Destroy26C8(void*, void*) EI_ORIGINAL(0x005C91F0)
__declspec(naked) void __fastcall DestroyA80(void*, void*) EI_ORIGINAL(0x00598A80)
__declspec(naked) void __fastcall DestroyB00(void*, void*) EI_ORIGINAL(0x00598B00)
__declspec(naked) void __fastcall Destroy2698(void*, void*) EI_ORIGINAL(0x005BD010)
__declspec(naked) void __fastcall Destroy180(void*, void*) EI_ORIGINAL(0x00599180)
__declspec(naked) void __fastcall DestroyA60(void*, void*) EI_ORIGINAL(0x00598A60)
__declspec(naked) void __fastcall DestroyAA60(void*, void*) EI_ORIGINAL(0x0058AA60)
__declspec(naked) void __fastcall DestroyAA0(void*, void*) EI_ORIGINAL(0x00598AA0)
__declspec(naked) void __fastcall Destroy5E0(void*, void*) EI_ORIGINAL(0x005995E0)
__declspec(naked) void __fastcall Destroy7A0(void*, void*) EI_ORIGINAL(0x005997A0)
__declspec(naked) void __fastcall Destroy5C0(void*, void*) EI_ORIGINAL(0x005995C0)
__declspec(naked) void __fastcall Destroy330(void*, void*) EI_ORIGINAL(0x00599330)
__declspec(naked) void __fastcall Destroy4A0(void*, void*) EI_ORIGINAL(0x005984A0)
__declspec(naked) void __fastcall Destroy780(void*, void*) EI_ORIGINAL(0x00599780)
__declspec(naked) void __fastcall Destroy0D0(void*, void*) EI_ORIGINAL(0x004600D0)
// 006E859F: the CRT's `eh vector destructor iterator` (array, element size, count, destructor).
void __stdcall VectorDestroy(void* a0, unsigned a1, int a2, void* a3) { EhVectorDestructorIterator(a0, a1, a2, a3); }

constexpr std::uint32_t kArrayVtable = 0x0073C3D0;      // +0xD4, +0xE8
constexpr std::uint32_t kSectorMapVtable = 0x0073F9DC;  // +0x124
constexpr std::uint32_t kAreaListVtable = 0x0073F9E0;   // +0x204 .. +0x268

void Zero(void* object, std::initializer_list<int> offsets) {
    for (int offset : offsets) At<std::uint32_t>(object, offset) = 0;
}

} // namespace

// 005B3C30 (thiscall). Returns this.
CAIMap* __fastcall ConstructAIMap(CAIMap* self) {
    At<int>(self, 4) = 10000;
    At<int>(self, 8) = 0;
    At<int>(self, 0xC) = 10000;
    At<int>(self, 0x10) = 0;
    At<std::uint32_t>(self, 0xD4) = kArrayVtable;
    Zero(self, {0xD8, 0xE4, 0xE0, 0xDC});
    At<std::uint32_t>(self, 0xE8) = kArrayVtable;
    Zero(self, {0xEC, 0xF8, 0xF4, 0xF0, 0xFC, 0x108, 0x104, 0x100});
    Construct10C(Member(self, 0x10C), nullptr);
    Construct134(Member(self, 0x134), nullptr);
    At<std::uint32_t>(self, 0x124) = kSectorMapVtable;
    Zero(self, {0x148, 0x128, 0x12C, 0x130, 0x14C, 0x158, 0x154, 0x150});
    At<std::uint8_t>(self, 0x19C) = 1;
    Zero(self, {0x1A0, 0x1AC, 0x1A8, 0x1A4});
    At<std::uint8_t>(self, 0x1F0) = 1;
    At<int>(self, 0x1FC) = 1;
    At<int>(self, 0x200) = 1;
    Construct1F4(Member(self, 0x1F4), nullptr);
    for (int list = 0x204; list <= 0x268; list += 0x14) {
        At<std::uint32_t>(self, list) = kAreaListVtable;
        Zero(self, {list + 4, list + 0x10, list + 0xC, list + 8});
    }
    ConstructUnitLayer(Member(self, 0x27C), nullptr);
    At<CAIMap*>(self, 0x27C + 0x1024) = self;
    At<std::uint32_t>(self, 0x27C) = 0x0073FD74;
    ConstructUnitLayer(Member(self, 0x12A4), nullptr);
    At<CAIMap*>(self, 0x12A4 + 0x1024) = self;
    At<std::uint32_t>(self, 0x12A4) = 0x0073FD6C;
    ConstructRegionMap(Member(self, 0x22F8), nullptr);
    ConstructList2598(Member(self, 0x2598), nullptr, 1, 1);
    ConstructListB(Member(self, 0x25A8), nullptr, 1, 1);
    Construct25BC(Member(self, 0x25BC), nullptr);
    At<std::uint32_t>(self, 0x25B8) = 0;
    ConstructSearch2614(Member(self, 0x2614), nullptr, 0);
    Construct2698(Member(self, 0x2698), nullptr, 1, 1);
    Construct26A8(Member(self, 0x26A8), nullptr, 1, 1);
    ConstructListB(Member(self, 0x26B8), nullptr, 1, 1);
    Construct26C8(Member(self, 0x26C8), nullptr);
    At<std::uint8_t>(self, 0) = 0;
    At<std::uint8_t>(self, 1) = 0;
    ResetDirty(Member(self, 4), nullptr);
    Initialise(self, nullptr);
    At<int>(self, 0x2694) = 0;
    At<std::uint8_t>(self, 2) = 0;
    return self;
}

// 005B5E10 (thiscall).
void __fastcall ResetAIMap(CAIMap* self) {
    Destroy26C8(Member(self, 0x26C8), nullptr);
    DestroyA80(Member(self, 0x26B8), nullptr);
    DestroyB00(Member(self, 0x26A8), nullptr);
    Destroy2698(Member(self, 0x2698), nullptr);
    Destroy180(Member(self, 0x263C), nullptr);
    DestroyA80(Member(self, 0x2628), nullptr);
    DestroyA60(Member(self, 0x2618), nullptr);
    Destroy180(Member(self, 0x25BC), nullptr);
    DestroyA80(Member(self, 0x25A8), nullptr);
    DestroyA60(Member(self, 0x2598), nullptr);
    void* regions = Member(self, 0x22F8);
    VectorDestroy(Member(regions, 0x21C), 0x10, 8, reinterpret_cast<void*>(0x00598480));
    VectorDestroy(Member(regions, 0x19C), 0x10, 8, reinterpret_cast<void*>(0x00598460));
    VectorDestroy(Member(regions, 0x11C), 0x10, 8, reinterpret_cast<void*>(0x00598440));
    DestroyAA60(Member(regions, 0x98), nullptr);
    DestroyAA0(Member(regions, 0x4C), nullptr);
    DestroyAA0(regions, nullptr);
    Destroy5E0(Member(self, 0x12A8), nullptr);
    Destroy5E0(Member(self, 0x280), nullptr);
    for (int list = 0x268; list >= 0x204; list -= 0x14) Destroy7A0(Member(self, list), nullptr);
    DestroyA80(Member(self, 0x1F4), nullptr);
    Destroy5C0(Member(self, 0x1A0), nullptr);
    Destroy5C0(Member(self, 0x14C), nullptr);
    At<std::uint32_t>(self, 0x124) = kSectorMapVtable;
    Destroy330(Member(self, 0x134), nullptr);
    Destroy4A0(Member(self, 0x10C), nullptr);
    Destroy780(Member(self, 0xFC), nullptr);
    Destroy0D0(Member(self, 0xE8), nullptr);
    Destroy0D0(Member(self, 0xD4), nullptr);
    if (self) ConstructAIMap(self);
}

EI_REPLACE(0x005B3C30, ConstructAIMap);
EI_REPLACE(0x005B5E10, ResetAIMap);
