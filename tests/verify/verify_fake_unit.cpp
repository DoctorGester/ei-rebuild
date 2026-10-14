// Checks for fake_unit.cpp (ours only): after construction the vtable is CFakeUnit's and its four
// arrays are empty with their own vtables; after Destroy (slot 3) the figure array is empty.
#include "verify.h"

#include <cstdint>
#include <cstring>

namespace scene {
struct FakeUnitScene;
} // namespace scene
namespace fakeunit {
void* __fastcall FakeUnitConstruct(scene::FakeUnitScene* self);
void __fastcall FakeUnitDestroy(scene::FakeUnitScene* self);
} // namespace fakeunit

namespace {
void* s_unused;

std::uint32_t Dword(const std::uint8_t* p, std::size_t o) {
    std::uint32_t v;
    std::memcpy(&v, p + o, 4);
    return v;
}
bool EmptyArray(const std::uint8_t* p, std::size_t o, std::uint32_t vtable) {
    return Dword(p, o) == vtable && Dword(p, o + 4) == 0 && Dword(p, o + 8) == 0 && Dword(p, o + 0xC) == 0 && Dword(p, o + 0x10) == 0;
}

void* __fastcall ConstructVerify(std::uint8_t* self) {
    void* result = fakeunit::FakeUnitConstruct(reinterpret_cast<scene::FakeUnitScene*>(self));
    bool ok = result == self && Dword(self, 0) == 0x0073BFE8 && EmptyArray(self, 0x530, 0x0073C108) &&
              EmptyArray(self, 0x544, 0x0073C0FC) && EmptyArray(self, 0x558, 0x0073C0F0) && EmptyArray(self, 0x56C, 0x0073C0E4);
    ei::verify::Check("FakeUnitConstruct", true, ok, "");
    return result;
}

void __fastcall DestroyVerify(std::uint8_t* self) {
    fakeunit::FakeUnitDestroy(reinterpret_cast<scene::FakeUnitScene*>(self));
    bool ok = Dword(self, 0x534) == 0 && Dword(self, 0x538) == 0 && Dword(self, 0x53C) == 0;
    ei::verify::Check("FakeUnitDestroy", true, ok, "");
}
} // namespace

EI_VERIFY(0x004466D0, ConstructVerify, 0, s_unused);
EI_VERIFY(0x00446B20, DestroyVerify, 0, s_unused);
