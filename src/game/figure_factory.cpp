// 005AA8E0 (virtual, slot of the figure object factory 0073FBC0; thiscall, RET 4, `this` unused):
// creates a scene object of a class id. 0x41-0x43 are the figure part meshes built on the common
// base (005AABC0): 0x41 with two empty arrays (vtable 0073FBF0), 0x42 with three more and three
// small members (0073C1C4), 0x43 with six constructed members (0073FC48); 0x44 (004D9110) and
// 0x300B-0x300D (0050A5D0, 0050AF40, 0050C5F0) have their own constructors. The new object's
// +0x10 gets the class id; an unknown id or a failed allocation returns null. See
// docs/notes/models.md.
#include "ei/hook.h"
#include "lib/crt.h"

#include <cstdint>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}
void* Member(void* object, int offset) { return static_cast<std::uint8_t*>(object) + offset; }

__declspec(naked) void* __fastcall ConstructPartBase(void*) EI_ORIGINAL(0x005AABC0)
__declspec(naked) void* __fastcall ConstructMember1C0(void*) EI_ORIGINAL(0x0056FD00)
__declspec(naked) void* __fastcall ConstructMember1E0(void*) EI_ORIGINAL(0x005AD870)
__declspec(naked) void* __fastcall ConstructMember1F0(void*) EI_ORIGINAL(0x004D9E90)
__declspec(naked) void* __fastcall ConstructMember200(void*) EI_ORIGINAL(0x004D9E80)
__declspec(naked) void* __fastcall ConstructMember210(void*) EI_ORIGINAL(0x004D9EA0)
__declspec(naked) void* __fastcall Construct44(void*) EI_ORIGINAL(0x004D9110)
__declspec(naked) void* __fastcall Construct300B(void*) EI_ORIGINAL(0x0050A5D0)
__declspec(naked) void* __fastcall Construct300C(void*) EI_ORIGINAL(0x0050AF40)
__declspec(naked) void* __fastcall Construct300D(void*) EI_ORIGINAL(0x0050C5F0)

void ZeroArrays(void* object, int first, int count) {
    for (int i = 0; i < count; ++i) At<std::uint32_t>(object, first + i * 4) = 0;
}

void* NewPart(std::size_t size) {
    void* object = gamecrt::operator_new(size);
    if (object) ConstructPartBase(object);
    return object;
}

void* Create(int classId) {
    switch (classId) {
    case 0x41: {
        void* o = NewPart(0x1E0);
        if (!o) return nullptr;
        ZeroArrays(o, 0x1C0, 8);
        At<std::uint32_t>(o, 0) = 0x0073FBF0;
        return o;
    }
    case 0x42: {
        void* o = NewPart(0x220);
        if (!o) return nullptr;
        ZeroArrays(o, 0x1C0, 12);
        ConstructMember1F0(Member(o, 0x1F0));
        ConstructMember200(Member(o, 0x200));
        ConstructMember210(Member(o, 0x210));
        At<std::uint32_t>(o, 0) = 0x0073C1C4;
        return o;
    }
    case 0x43: {
        void* o = NewPart(0x284);
        if (!o) return nullptr;
        ConstructMember1C0(Member(o, 0x1C0));
        ConstructMember1C0(Member(o, 0x1D0));
        ConstructMember1E0(Member(o, 0x1E0));
        ConstructMember1F0(Member(o, 0x1F0));
        ConstructMember200(Member(o, 0x200));
        ConstructMember210(Member(o, 0x210));
        At<std::uint32_t>(o, 0) = 0x0073FC48;
        return o;
    }
    case 0x44: {
        void* o = gamecrt::operator_new(0x230);
        return o ? Construct44(o) : nullptr;
    }
    case 0x300B: {
        void* o = gamecrt::operator_new(0x78);
        return o ? Construct300B(o) : nullptr;
    }
    case 0x300C: {
        void* o = gamecrt::operator_new(0xA0);
        return o ? Construct300C(o) : nullptr;
    }
    case 0x300D: {
        void* o = gamecrt::operator_new(0x78);
        return o ? Construct300D(o) : nullptr;
    }
    default:
        return nullptr;
    }
}

} // namespace

void* __fastcall CreateFigureObject(void*, void*, int classId) {
    void* object = Create(classId);
    if (!object) return nullptr;
    At<int>(object, 0x10) = classId;
    return object;
}

EI_REPLACE(0x005AA8E0, CreateFigureObject);
