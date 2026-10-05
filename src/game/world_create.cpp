// 0055EE40: the world's managers created (the server's and the client's world both call it, and
// it is a virtual slot of their base, vtable entry 0073F194). See docs/notes/combat.md ("Fourth
// round"), the 0055C000-00560000 managers.
//
// The world's paths are taken from the command line globals; every manager is made, referenced
// and attached to the world (its slot 13 with the world, or slot 2); a 0x0C-byte helper goes into
// a counted pointer at +0x5C; then the ISAAC generator is seeded and +0x70 set up.
#include "ei/hook.h"
#include "ei/vcall.h"

#include "lib/crt.h"

#include <cstdint>
#include "originals_library.h"

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

constexpr int kSlotAttach = 0x34 / 4;   // (world)
constexpr int kSlotInit = 8 / 4;        // ()

// MFC CString::operator=(LPCTSTR) on the path globals.
inline void* __fastcall CStringAssign(void* s, void*, const char* t) { return CStringAssignTextOriginal(s, nullptr, t); }
// The managers' constructors (thiscall, no arguments; they return this).
__declspec(naked) void* __fastcall ObjectBaseConstruct(void*) EI_ORIGINAL(0x00402B20)
__declspec(naked) void* __fastcall Manager5125E0(void*) EI_ORIGINAL(0x005125E0)
__declspec(naked) void* __fastcall Manager5AA070(void*) EI_ORIGINAL(0x005AA070)
__declspec(naked) void* __fastcall Manager683FF0(void*) EI_ORIGINAL(0x00683FF0)
__declspec(naked) void* __fastcall Manager688840(void*) EI_ORIGINAL(0x00688840)
__declspec(naked) void* __fastcall Manager6A5AB0(void*) EI_ORIGINAL(0x006A5AB0)
__declspec(naked) void* __fastcall ResourceManagerConstruct(void*) EI_ORIGINAL(0x00674D90)
// 0049F150 IsaacRandom::Init (random.cpp); 006D43E0 the world's +0x70 set up.
__declspec(naked) void __fastcall RandomInit(void*) EI_ORIGINAL(0x0049F150)
__declspec(naked) void __fastcall Setup70(void*) EI_ORIGINAL(0x006D43E0)

using Construct = void* (__fastcall*)(void*);

// operator new, the constructor (null without memory), an optional vtable, the reference.
std::uint8_t* Make(std::size_t size, Construct construct, std::uintptr_t vtable) {
    void* memory = gamecrt::operator_new(size);
    std::uint8_t* object = nullptr;
    if (memory) {
        object = static_cast<std::uint8_t*>(construct(memory));
        if (vtable) {
            object = static_cast<std::uint8_t*>(memory);
            At<std::uintptr_t>(object, 0) = vtable;
        }
    }
    ++At<int>(object, 4);   // the original counts the reference without testing the pointer
    return object;
}

} // namespace

int __fastcall WorldCreateManagers(std::uint8_t* world) {
    At<std::uint8_t>(world, 0x6C) = 0;
    CStringAssign(reinterpret_cast<void*>(0x007AF460), nullptr, *reinterpret_cast<const char* const*>(0x0079BC90));
    CStringAssign(reinterpret_cast<void*>(0x007AF45C), nullptr, *reinterpret_cast<const char* const*>(0x0079BC94));
    CStringAssign(reinterpret_cast<void*>(0x007AF458), nullptr, *reinterpret_cast<const char* const*>(0x0079BC98));
    At<int>(world, 0x64) = 0;
    At<float>(world, 0x24) = 1.0f;
    At<int>(world, 0x68) = 0;
    At<int>(world, 0x28) = 0;
    At<int>(world, 0x30) = 0;
    At<int>(world, 0x2C) = 0;

    std::uint8_t* object = Make(0xB0, ObjectBaseConstruct, 0x0073F328);
    At<std::uint8_t*>(world, 0x38) = object;
    ei::VCall<void>(object, kSlotAttach, world);
    object = Make(0xBC, Manager5125E0, 0);
    At<std::uint8_t*>(world, 0x40) = object;
    ei::VCall<void>(object, kSlotAttach, world);
    object = Make(0xD0, Manager5AA070, 0);
    At<std::uint8_t*>(world, 0x3C) = object;
    ei::VCall<void>(object, kSlotAttach, world);
    object = Make(0xB0, ObjectBaseConstruct, 0x0073F2C4);
    At<std::uint8_t*>(world, 0x48) = object;
    ei::VCall<void>(object, kSlotAttach, world);
    object = Make(0x24, Manager683FF0, 0);
    At<std::uint8_t*>(world, 0x50) = object;
    ei::VCall<void>(object, kSlotInit);
    object = Make(0xD4, Manager688840, 0);
    At<std::uint8_t*>(world, 0x4C) = object;
    ei::VCall<void>(object, kSlotAttach, world);
    object = Make(0x34, Manager6A5AB0, 0);
    At<std::uint8_t*>(world, 0x58) = object;
    ei::VCall<void>(object, kSlotInit);
    object = Make(0xBC, ResourceManagerConstruct, 0x0073F25C);
    At<std::uint8_t*>(world, 0x54) = object;
    ei::VCall<void>(object, kSlotAttach, world);

    // A 0x0C-byte object (vtable 0073F240) into the counted pointer at +0x5C: a 16-bit count at +4,
    // flags at +6 (bit 0 held) and +7 (bit 7 being released).
    auto* helper = static_cast<std::uint8_t*>(gamecrt::operator_new(0xC));
    At<int>(helper, 4) = 0;   // written before the test, as the original does
    if (helper) {
        At<int>(helper, 8) = 0;
        At<std::uintptr_t>(helper, 0) = 0x0073F240;
    }
    std::uint8_t*& held = At<std::uint8_t*>(world, 0x5C);
    if (helper != held) {
        if (std::uint8_t* old = held) {
            if (At<std::int16_t>(old, 4) != 1) {
                At<std::uint8_t>(old, 7) |= 0x80;
                ++At<std::int16_t>(old, 4);
                ei::VCall<void>(old, 8 / 4);
                --At<std::int16_t>(old, 4);
            }
            std::uint8_t* current = held;
            --At<std::int16_t>(current, 4);
            if (At<std::int16_t>(current, 4) == 0 && current) ei::VCall<void>(current, 0, 1);
        }
        if (!helper || (At<std::uint32_t>(helper, 4) & 0x80000000u)) {
            held = nullptr;
        } else {
            held = (At<std::uint8_t>(helper, 6) & 1) ? ei::VCall<std::uint8_t*>(helper, 0xC / 4) : helper;
            ++At<std::int16_t>(held, 4);
            At<std::uint8_t>(held, 6) |= 1;
        }
    }
    RandomInit(reinterpret_cast<void*>(0x007A9D40));
    Setup70(world + 0x70);
    return 1;
}
EI_REPLACE(0x0055EE40, WorldCreateManagers);
