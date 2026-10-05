// CFakeUnit (vtable 0073BFE8, class id 0x50, 0x580 bytes; derives from CUnit): constructor,
// destructor, Destroy (slot 3), the scene load (slot 62) and the class id. It adds a member at
// +0x4E0 (constructor 00508700, destructor 005087B0) and four CArrays: the figures (+0x530, 0x220
// bytes, vtable 0073C108), 0x18C-byte records (+0x544, 0073C0FC), bone links (+0x558, 0x14 bytes,
// 0073C0F0) and 0x9C-byte records (+0x56C, 0073C0E4). Every callee is reached through its original
// address. See docs/notes/objects.md ("Sixth round").
#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "originals_engine.h"
#include "originals_units.h"

#include <cstdint>
#include <cstring>

namespace fakeunit {

namespace {

template <typename T>
T& At(void* base, std::size_t offset) { return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + offset); }
void SetVtable(void* object, std::uintptr_t vtable) { std::memcpy(object, &vtable, sizeof(vtable)); }

constexpr std::uintptr_t kFakeUnitVtable = 0x0073BFE8;

// An MFC CArray as laid out in game.exe.
struct Array {
    void* vtable;
    std::uint8_t* data;
    int size;
    int maxSize;
    int growBy;
};

struct ArrayInfo {
    std::size_t offset;
    std::uintptr_t vtable;
    std::size_t elementSize;
    int destructorSlot;   // the element's slot called with 0 (its destructor)
};
// In the order the destructor destroys them (the reverse of construction).
constexpr ArrayInfo kArrays[] = {
    {0x56C, 0x0073C0E4, 0x9C, 0},
    {0x558, 0x0073C0F0, 0x14, 1},
    {0x544, 0x0073C0FC, 0x18C, 0},
    {0x530, 0x0073C108, 0x220, 0},
};

__declspec(naked) void __fastcall UnitConstruct(void*) EI_ORIGINAL(0x0052E110)
__declspec(naked) void __fastcall UnitDestroy(void*) EI_ORIGINAL(0x0052E590)
__declspec(naked) void __fastcall MemberConstruct(void*) EI_ORIGINAL(0x00508700)
__declspec(naked) void __fastcall MemberDestruct(void*) EI_ORIGINAL(0x005087B0)
__declspec(naked) void __fastcall MemberClear(void*) EI_ORIGINAL(0x005088E0)
__declspec(naked) void __fastcall MemberRelease(void*, void*, int) EI_ORIGINAL(0x00508930)
// CArray<CFigure>::SetSize (library).
__declspec(naked) void __fastcall FiguresSetSize(void*, void*, int, int) EI_ORIGINAL(0x0044FCD0)
__declspec(naked) void* __fastcall FakeUnitDestructAt(void*) EI_ORIGINAL(0x00446910)
// 00446C10 (__stdcall file, regFile, unit; ECX the unit as well): the scene loader.
__declspec(naked) int __fastcall LoadScene(void* /*ecx unit*/, void*, const char*, const char*, void*) EI_ORIGINAL(0x00446C10)
__declspec(naked) void __fastcall FigureAttach(void* /*figure*/, void*, void*) EI_ORIGINAL(0x0059F390)

// The member at +0x4E0 emptied and the unit's own Destroy (the start of both the destructor and
// slot 3).
void ReleaseParts(std::uint8_t* self) {
    MemberClear(self + 0x4E0);
    MemberRelease(self + 0x4E0, nullptr, 0);
    UnitDestroy(self);
}

} // namespace

// 004466D0: the unit, the member, four empty arrays.
void* __fastcall FakeUnitConstruct(std::uint8_t* self) {
    UnitConstruct(self);
    MemberConstruct(self + 0x4E0);
    for (int i = 3; i >= 0; --i) {
        auto& a = At<Array>(self, kArrays[i].offset);
        SetVtable(&a, kArrays[i].vtable);
        a.data = nullptr;
        a.growBy = 0;
        a.maxSize = 0;
        a.size = 0;
    }
    SetVtable(self, kFakeUnitVtable);
    return self;
}
EI_REPLACE(0x004466D0, FakeUnitConstruct);

// 00446910: the destructor. After the figures' SetSize(0, -1) the original walks the (now empty)
// figure array dropping each figure's count at +4; kept as written.
void __fastcall FakeUnitDestruct(std::uint8_t* self) {
    SetVtable(self, kFakeUnitVtable);
    ReleaseParts(self);
    FiguresSetSize(self + 0x530, nullptr, 0, -1);
    auto& figures = At<Array>(self, 0x530);
    for (int i = 0; i < figures.size; ++i) --At<int>(figures.data + i * 0x220, 4);
    for (const ArrayInfo& info : kArrays) {
        auto& a = At<Array>(self, info.offset);
        SetVtable(&a, info.vtable);
        if (!a.data) continue;
        for (int i = 0, n = a.size; i < n; ++i) ei::VCall<void*>(a.data + i * info.elementSize, info.destructorSlot, 0);
        gamecrt::operator_delete(a.data);
    }
    MemberDestruct(self + 0x4E0);
    UnitDestructBody(self);
}
EI_REPLACE(0x00446910, FakeUnitDestruct);

// 004468F0: the scalar deleting destructor.
void* __fastcall FakeUnitDelete(std::uint8_t* self, void*, unsigned flags) {
    FakeUnitDestructAt(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x004468F0, FakeUnitDelete);

// 00446B20 (slot 3): Destroy: as the destructor's start, then the figures destroyed (slot 0 with 0)
// and their array emptied.
void __fastcall FakeUnitDestroy(std::uint8_t* self) {
    ReleaseParts(self);
    auto& figures = At<Array>(self, 0x530);
    if (figures.data) {
        for (int i = 0, n = figures.size; i < n; ++i) ei::VCall<void*>(figures.data + i * 0x220, 0, 0);
        gamecrt::operator_delete(figures.data);
        figures.data = nullptr;
    }
    figures.maxSize = 0;
    figures.size = 0;
}
EI_REPLACE(0x00446B20, FakeUnitDestroy);

// 00446BA0 (slot 62, RET 8): loads the scene; on success each figure gets a new object of class
// 0x45 from the world's slot-21 manager (its slot 23). Returns the loader's 0, or 1.
int __fastcall FakeUnitLoadScene(std::uint8_t* self, void*, const char* file, const char* regFile) {
    if (!LoadScene(self, nullptr, file, regFile, self)) return 0;
    for (int i = 0; i < At<Array>(self, 0x530).size; ++i) {
        void* manager = ei::VCall<void*>(ManagerContextOf(self), 21);
        void* object = ei::VCall<void*>(manager, 23, 0x45, -1);
        FigureAttach(At<Array>(self, 0x530).data + i * 0x220, nullptr, object);
    }
    return 1;
}
EI_REPLACE(0x00446BA0, FakeUnitLoadScene);

// 004468E0: the class id.
int __fastcall FakeUnitClassId(void*) { return 0x50; }
EI_REPLACE(0x004468E0, FakeUnitClassId);

} // namespace fakeunit
