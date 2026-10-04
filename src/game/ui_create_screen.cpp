// CInterface3D::CreateScreen (005F5BA0): the screens by id. See ui.h and docs/notes/ui.md.
//
// The original inlines the constructors of screens 1, 2, 3 and 7 (the container base 00600B20,
// their members' constructors and vtables) and calls the others' constructors; ours does the same
// through the original addresses.
#include "ui.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstdint>
#include <cstring>

namespace {

void SetDword(void* p, int offset, std::uint32_t value) { std::memcpy(static_cast<std::uint8_t*>(p) + offset, &value, 4); }
std::uint32_t EmptyString() { return *reinterpret_cast<const std::uint32_t*>(0x00796E48); }   // afxEmptyString

__declspec(naked) void __fastcall ConstructContainer(void*, void*, CInterface3D*) EI_ORIGINAL(0x00600B20)
__declspec(naked) void __fastcall ConstructIslandMap(void*, void*, CInterface3D*) EI_ORIGINAL(0x005F67A0)      // id 4
__declspec(naked) void __fastcall ConstructScreen5(void*, void*, CInterface3D*, int) EI_ORIGINAL(0x005F6240)    // id 5
__declspec(naked) void __fastcall ConstructScreen8(void*, void*, CInterface3D*, int, const void*) EI_ORIGINAL(0x005F6D50)
__declspec(naked) void __fastcall ConstructScreen9(void*, void*, CInterface3D*) EI_ORIGINAL(0x005F6F30)

// Member constructors (thiscall on the member).
__declspec(naked) void __fastcall Member5C1760(void*) EI_ORIGINAL(0x005C1760)
__declspec(naked) void __fastcall Member5F8F30(void*) EI_ORIGINAL(0x005F8F30)
__declspec(naked) void __fastcall Member5F6080(void*) EI_ORIGINAL(0x005F6080)
__declspec(naked) void __fastcall Member5F5F90(void*) EI_ORIGINAL(0x005F5F90)
__declspec(naked) void __fastcall StringArrayConstruct(void*) EI_ORIGINAL(0x006FE925)   // CStringArray
__declspec(naked) void __fastcall Member5FAF80(void*) EI_ORIGINAL(0x005FAF80)
// STLport containers taking an allocator (by address; empty).
__declspec(naked) void __fastcall Member5F90E0(void*, void*, const void*) EI_ORIGINAL(0x005F90E0)
__declspec(naked) void __fastcall Member5F9300(void*, void*, const void*) EI_ORIGINAL(0x005F9300)
__declspec(naked) void __fastcall Member5F8F10(void*, void*, const void*) EI_ORIGINAL(0x005F8F10)
__declspec(naked) void __fastcall Member5F9540(void*, void*, const void*) EI_ORIGINAL(0x005F9540)

struct InterfaceAt {
    void Activate(CUiWindow* window);   // 005F71C0
};
__declspec(naked) void InterfaceAt::Activate(CUiWindow*) EI_ORIGINAL(0x005F71C0)

std::uint8_t* At(void* p, int offset) { return static_cast<std::uint8_t*>(p) + offset; }

} // namespace

void CInterface3D::CreateScreen(int id) {
    void* screen = nullptr;
    char allocators[4] = {};
    switch (id) {
    case 0:
        return;
    case 1: {   // the in-game screen (ui_camp.h)
        screen = gamecrt::operator_new(0x230);
        if (!screen) break;
        ConstructContainer(screen, nullptr, this);
        SetDword(screen, 0x168, 0x007402AC);
        SetDword(screen, 0x16C, 0);
        SetDword(screen, 0x178, 0);
        SetDword(screen, 0x174, 0);
        SetDword(screen, 0x170, 0);
        Member5C1760(At(screen, 0x1DC));
        Member5F8F30(At(screen, 0x1F0));
        Member5F6080(At(screen, 0x204));
        Member5F5F90(At(screen, 0x220));
        SetDword(screen, 0x00, 0x00740764);
        SetDword(screen, 0x94, 0x00740730);
        break;
    }
    case 2:
        screen = gamecrt::operator_new(0x17C);
        if (!screen) break;
        ConstructContainer(screen, nullptr, this);
        SetDword(screen, 0x00, 0x00740688);
        SetDword(screen, 0x94, 0x00740654);
        break;
    case 3: {   // the base / briefing screen
        screen = gamecrt::operator_new(0x2A4);
        if (!screen) break;
        ConstructContainer(screen, nullptr, this);
        StringArrayConstruct(At(screen, 0x170));
        SetDword(screen, 0x184, EmptyString());
        Member5FAF80(At(screen, 0x190));
        Member5F90E0(At(screen, 0x240), nullptr, &allocators[0]);
        SetDword(screen, 0x248, EmptyString());
        Member5F8F30(At(screen, 0x26C));
        Member5F9300(At(screen, 0x278), nullptr, &allocators[1]);
        Member5F8F10(At(screen, 0x284), nullptr, &allocators[2]);
        Member5F9540(At(screen, 0x294), nullptr, &allocators[3]);
        SetDword(screen, 0x00, 0x007405AC);
        SetDword(screen, 0x94, 0x00740578);
        break;
    }
    case 4:
        screen = gamecrt::operator_new(0x294);
        if (screen) ConstructIslandMap(screen, nullptr, this);
        break;
    case 5:
        screen = gamecrt::operator_new(0x260);
        if (screen) ConstructScreen5(screen, nullptr, this, 0);
        break;
    case 7:
        screen = gamecrt::operator_new(0x168);
        if (!screen) break;
        ConstructContainer(screen, nullptr, this);
        SetDword(screen, 0x00, 0x00740840);
        SetDword(screen, 0x94, 0x0074080C);
        break;
    case 8:
        screen = gamecrt::operator_new(0x1FC);
        if (screen) ConstructScreen8(screen, nullptr, this, 0, reinterpret_cast<const void*>(0x00793084));
        break;
    case 9:
        screen = gamecrt::operator_new(0x1B8);
        if (screen) ConstructScreen9(screen, nullptr, this);
        break;
    default:   // 6 and above 9: the id itself
        screen = reinterpret_cast<void*>(static_cast<std::uintptr_t>(static_cast<unsigned>(id)));
        break;
    }
    reinterpret_cast<InterfaceAt*>(this)->Activate(static_cast<CUiWindow*>(screen));
}

namespace {
void __fastcall InterfaceCreateScreen(CInterface3D* self, void*, int id) { self->CreateScreen(id); }
} // namespace
EI_REPLACE(0x005F5BA0, InterfaceCreateScreen);
