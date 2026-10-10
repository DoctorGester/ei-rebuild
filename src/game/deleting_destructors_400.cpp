// Scalar deleting destructors in 00400000-00440000 that only call their class's destructor body
// and free the memory when bit 0 of the flags is set (30 bytes each; the message classes of
// net_classes.h, counted pointer holders, session containers). The body is reached through its
// original address (rewritten or not). Two exception cleanup helpers are here too.
#include "ei/hook.h"
#include "lib/crt.h"

#include <cstdint>
#include <cstring>

namespace destructors400 {

template <std::uintptr_t Body>
void* __fastcall DeletingDestructor(void* self, void*, unsigned flags) {
    reinterpret_cast<void(__fastcall*)(void*)>(Body)(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}

// 004054F0 / 00405530: exception cleanup steps (reached by a jump from the unwind funclets at
// 0070E323 .. 0070E3B3): the dword at +8 of ECX cleared.
void __fastcall CleanupClear8(void* object) {
    std::uint32_t zero = 0;
    std::memcpy(static_cast<std::uint8_t*>(object) + 8, &zero, 4);
}

} // namespace destructors400

using destructors400::CleanupClear8;
using destructors400::DeletingDestructor;

EI_REPLACE(0x004128C0, DeletingDestructor<0x004128E0>);
EI_REPLACE(0x00412970, DeletingDestructor<0x00412960>);
EI_REPLACE(0x004132D0, DeletingDestructor<0x004132F0>);
EI_REPLACE(0x00413410, DeletingDestructor<0x00413430>);   // Message (0073BC10)
EI_REPLACE(0x004137E0, DeletingDestructor<0x00413800>);   // 0073BBF8
EI_REPLACE(0x00413C20, DeletingDestructor<0x00413C40>);   // 0073BC28
EI_REPLACE(0x00414040, DeletingDestructor<0x00414060>);   // 0073BC40
EI_REPLACE(0x00414250, DeletingDestructor<0x00414270>);   // 0073BC58
EI_REPLACE(0x004148C0, DeletingDestructor<0x004148E0>);   // 0073BC70 player
EI_REPLACE(0x00414CF0, DeletingDestructor<0x00414D10>);   // 0073BC88 chat line
EI_REPLACE(0x00415280, DeletingDestructor<0x004152A0>);   // 0073BCA0
EI_REPLACE(0x004154B0, DeletingDestructor<0x004154D0>);   // 0073BCB8 binding
EI_REPLACE(0x00415910, DeletingDestructor<0x00415930>);   // 0073BCD0 queued map message
EI_REPLACE(0x00415CE0, DeletingDestructor<0x00415D00>);   // 0073BCE8 script variable
EI_REPLACE(0x004160F0, DeletingDestructor<0x00416110>);   // 0073BD00 map message
EI_REPLACE(0x00416620, DeletingDestructor<0x00416640>);   // 0073BD18 file
EI_REPLACE(0x004168C0, DeletingDestructor<0x004168E0>);   // 0073BD30 file chunk
EI_REPLACE(0x00416D50, DeletingDestructor<0x00416D70>);   // 0073BD48 award
EI_REPLACE(0x00416FA0, DeletingDestructor<0x00416FC0>);   // 0073BD60 trade
EI_REPLACE(0x00417600, DeletingDestructor<0x00417620>);   // 0073BD78 party member
EI_REPLACE(0x004178D0, DeletingDestructor<0x004178F0>);
EI_REPLACE(0x0041C450, DeletingDestructor<0x0041C530>);
// Counted pointer holders; the world range has a byte-identical copy of each (A-W12).
constexpr auto* CountedHolderDelete = &DeletingDestructor<0x0041DDA0>;
constexpr auto* CountedHolderDelete2 = &DeletingDestructor<0x00421640>;
EI_REPLACE(0x004292A0, *CountedHolderDelete);
EI_REPLACE(0x004292C0, *CountedHolderDelete2);
EI_REPLACE(0x0059DD90, *CountedHolderDelete);
EI_REPLACE(0x005DA270, *CountedHolderDelete2);
EI_REPLACE(0x0042A5D0, DeletingDestructor<0x0042A610>);
EI_REPLACE(0x0042A5F0, DeletingDestructor<0x00421600>);
EI_REPLACE(0x0042B880, DeletingDestructor<0x00419D00>);
EI_REPLACE(0x0042D6D0, DeletingDestructor<0x0042D6F0>);
EI_REPLACE(0x0043DCA0, DeletingDestructor<0x00438B30>);
EI_REPLACE(0x0043DCC0, DeletingDestructor<0x00438A50>);
EI_REPLACE(0x0043DCE0, DeletingDestructor<0x00438A70>);
EI_REPLACE(0x0043FD00, DeletingDestructor<0x00438A30>);
EI_REPLACE(0x0043FD20, DeletingDestructor<0x00438AE0>);

EI_REPLACE(0x004054F0, CleanupClear8);
EI_REPLACE(0x00405530, CleanupClear8);
