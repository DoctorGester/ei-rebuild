// Checks for deleting_destructors_400.cpp.
//  - The deleting destructors: ours only (they destroy and free the object).
//  - CleanupClear8: side by side on a copy of the 12 bytes (MOV [ECX+8],0: 7 bytes stolen).
#include "verify.h"

#include <cstdint>
#include <cstring>

namespace destructors400 {
template <std::uintptr_t Body>
void* __fastcall DeletingDestructor(void* self, void*, unsigned flags);
void __fastcall CleanupClear8(void* object);
} // namespace destructors400

namespace {
void* s_unused;

template <std::uintptr_t Body>
void* __fastcall DeleteVerify(void* self, void*, unsigned flags) {
    ei::verify::Check("DeletingDestructor", true, true, "");
    return destructors400::DeletingDestructor<Body>(self, nullptr, flags);
}

void(__fastcall* s_clearA)(void*);
void(__fastcall* s_clearB)(void*);
void CheckClear(const char* name, void(__fastcall* original)(void*), void* object) {
    alignas(4) std::uint8_t scratch[12];
    std::memcpy(scratch, object, sizeof(scratch));
    original(scratch);
    destructors400::CleanupClear8(object);
    ei::verify::Check(name, true, std::memcmp(scratch, object, sizeof(scratch)) == 0, "");
}
void __fastcall ClearAVerify(void* object) { CheckClear("CleanupClear8@004054F0", s_clearA, object); }
void __fastcall ClearBVerify(void* object) { CheckClear("CleanupClear8@00405530", s_clearB, object); }
} // namespace

EI_VERIFY(0x004054F0, ClearAVerify, 7, s_clearA);
EI_VERIFY(0x00405530, ClearBVerify, 7, s_clearB);

EI_VERIFY(0x004128C0, DeleteVerify<0x004128E0>, 0, s_unused);
EI_VERIFY(0x00412970, DeleteVerify<0x00412960>, 0, s_unused);
EI_VERIFY(0x004132D0, DeleteVerify<0x004132F0>, 0, s_unused);
EI_VERIFY(0x00413410, DeleteVerify<0x00413430>, 0, s_unused);
EI_VERIFY(0x004137E0, DeleteVerify<0x00413800>, 0, s_unused);
EI_VERIFY(0x00413C20, DeleteVerify<0x00413C40>, 0, s_unused);
EI_VERIFY(0x00414040, DeleteVerify<0x00414060>, 0, s_unused);
EI_VERIFY(0x00414250, DeleteVerify<0x00414270>, 0, s_unused);
EI_VERIFY(0x004148C0, DeleteVerify<0x004148E0>, 0, s_unused);
EI_VERIFY(0x00414CF0, DeleteVerify<0x00414D10>, 0, s_unused);
EI_VERIFY(0x00415280, DeleteVerify<0x004152A0>, 0, s_unused);
EI_VERIFY(0x004154B0, DeleteVerify<0x004154D0>, 0, s_unused);
EI_VERIFY(0x00415910, DeleteVerify<0x00415930>, 0, s_unused);
EI_VERIFY(0x00415CE0, DeleteVerify<0x00415D00>, 0, s_unused);
EI_VERIFY(0x004160F0, DeleteVerify<0x00416110>, 0, s_unused);
EI_VERIFY(0x00416620, DeleteVerify<0x00416640>, 0, s_unused);
EI_VERIFY(0x004168C0, DeleteVerify<0x004168E0>, 0, s_unused);
EI_VERIFY(0x00416D50, DeleteVerify<0x00416D70>, 0, s_unused);
EI_VERIFY(0x00416FA0, DeleteVerify<0x00416FC0>, 0, s_unused);
EI_VERIFY(0x00417600, DeleteVerify<0x00417620>, 0, s_unused);
EI_VERIFY(0x004178D0, DeleteVerify<0x004178F0>, 0, s_unused);
EI_VERIFY(0x0041C450, DeleteVerify<0x0041C530>, 0, s_unused);
EI_VERIFY(0x004292A0, DeleteVerify<0x0041DDA0>, 0, s_unused);
EI_VERIFY(0x004292C0, DeleteVerify<0x00421640>, 0, s_unused);
EI_VERIFY(0x0042A5D0, DeleteVerify<0x0042A610>, 0, s_unused);
EI_VERIFY(0x0042A5F0, DeleteVerify<0x00421600>, 0, s_unused);
EI_VERIFY(0x0042B880, DeleteVerify<0x00419D00>, 0, s_unused);
EI_VERIFY(0x0042D6D0, DeleteVerify<0x0042D6F0>, 0, s_unused);
EI_VERIFY(0x0043DCA0, DeleteVerify<0x00438B30>, 0, s_unused);
EI_VERIFY(0x0043DCC0, DeleteVerify<0x00438A50>, 0, s_unused);
EI_VERIFY(0x0043DCE0, DeleteVerify<0x00438A70>, 0, s_unused);
EI_VERIFY(0x0043FD00, DeleteVerify<0x00438A30>, 0, s_unused);
EI_VERIFY(0x0043FD20, DeleteVerify<0x00438AE0>, 0, s_unused);
