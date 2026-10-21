// Scalar deleting destructors in 00400000-00440000 that only call their class's destructor body
// and free the memory when bit 0 of the flags is set (30 bytes each; the message classes of
// net_classes.h, counted pointer holders, session containers). The body is our rewrite, named by
// its member or function (the original's address in the comment). Two exception cleanup helpers
// are here too.
#include "base_manager.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "game_lists.h"
#include "net_classes.h"
#include "net_messages.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstdint>
#include <cstring>

namespace destructors400 {

// The destructor body on `self`: a member of T, or a __fastcall function taking the object.
template <class T, class R>
void RunBody(R (T::*body)(), void* self) { (static_cast<T*>(self)->*body)(); }
template <class T, class R>
void RunBody(R(__fastcall* body)(T*), void* self) { body(static_cast<T*>(self)); }

// Registered as DeletingDestructor<Body>::Delete::Delete: `this` is the object (RET 4); returns it.
template <auto Body>
struct DeletingDestructor {
    void* Delete(unsigned flags) {
        void* self = this;
        RunBody(Body, self);
        if (flags & 1) gamecrt::operator_delete(self);
        return self;
    }
};

// 004054F0 / 00405530: exception cleanup steps (reached by a jump from the unwind funclets at
// 0070E323 .. 0070E3B3): the dword at +8 of ECX cleared.
void __fastcall CleanupClear8(void* object) {
    std::uint32_t zero = 0;
    std::memcpy(static_cast<std::uint8_t*>(object) + 8, &zero, 4);
}

} // namespace destructors400

using destructors400::CleanupClear8;
using destructors400::DeletingDestructor;
using namespace net;

EI_REPLACE_METHOD(0x004128C0, DeletingDestructor<&ServerEntry::Destruct>::Delete);          // 004128E0
EI_REPLACE_METHOD(0x00412970, DeletingDestructor<RefObjectDestructBase>::Delete);           // 00412960
EI_REPLACE_METHOD(0x004132D0, DeletingDestructor<&ServerBase::DestructBase>::Delete);       // 004132F0
EI_REPLACE_METHOD(0x00413410, DeletingDestructor<RefObjectDestructBase>::Delete);           // 00413430: Message (0073BC10)
EI_REPLACE_METHOD(0x004137E0, DeletingDestructor<&LoginMessage::Destruct>::Delete);         // 00413800: 0073BBF8
EI_REPLACE_METHOD(0x00413C20, DeletingDestructor<&SayMessage::Destruct>::Delete);           // 00413C40: 0073BC28
EI_REPLACE_METHOD(0x00414040, DeletingDestructor<&CommandMessage::Destruct>::Delete);       // 00414060: 0073BC40
EI_REPLACE_METHOD(0x00414250, DeletingDestructor<&FileRequestMessage::Destruct>::Delete);   // 00414270: 0073BC58
EI_REPLACE_METHOD(0x004148C0, DeletingDestructor<&PlayerMessage::Destruct>::Delete);        // 004148E0: 0073BC70 player
EI_REPLACE_METHOD(0x00414CF0, DeletingDestructor<&LineMessage::Destruct>::Delete);          // 00414D10: 0073BC88 chat line
EI_REPLACE_METHOD(0x00415280, DeletingDestructor<&HostMessage::Destruct>::Delete);          // 004152A0: 0073BCA0
EI_REPLACE_METHOD(0x004154B0, DeletingDestructor<&BindingMessage::Destruct>::Delete);       // 004154D0: 0073BCB8 binding
EI_REPLACE_METHOD(0x00415910, DeletingDestructor<&ZoneMessage::Destruct>::Delete);          // 00415930: 0073BCD0 queued map message
EI_REPLACE_METHOD(0x00415CE0, DeletingDestructor<&VariableMessage::Destruct>::Delete);      // 00415D00: 0073BCE8 script variable
EI_REPLACE_METHOD(0x004160F0, DeletingDestructor<&MapMessage::Destruct>::Delete);           // 00416110: 0073BD00 map message
EI_REPLACE_METHOD(0x00416620, DeletingDestructor<&FileMessage::Destruct>::Delete);          // 00416640: 0073BD18 file
EI_REPLACE_METHOD(0x004168C0, DeletingDestructor<&FileChunkMessage::Destruct>::Delete);     // 004168E0: 0073BD30 file chunk
EI_REPLACE_METHOD(0x00416D50, DeletingDestructor<&AwardMessage::Destruct>::Delete);         // 00416D70: 0073BD48 award
EI_REPLACE_METHOD(0x00416FA0, DeletingDestructor<&TradeMessage::Destruct>::Delete);         // 00416FC0: 0073BD60 trade
EI_REPLACE_METHOD(0x00417600, DeletingDestructor<&PartyMemberMessage::Destruct>::Delete);   // 00417620: 0073BD78 party member
EI_REPLACE_METHOD(0x004178D0, DeletingDestructor<&PartyMessage::Destruct>::Delete);         // 004178F0
EI_REPLACE_METHOD(0x0041C450, DeletingDestructor<&ClientBase::DestructBase>::Delete);       // 0041C530
// Counted pointer holders (bodies 0041DDA0 and 00421640); the world range has a byte-identical copy
// of each (A-W12).
EI_REWRITTEN(0x004292A0, DeletingDestructor<RefPtrRelease>::Delete);
EI_REWRITTEN(0x004292C0, DeletingDestructor<&RefPtr::Destroy>::Delete);
EI_REWRITTEN(0x0059DD90, DeletingDestructor<RefPtrRelease>::Delete);
EI_REWRITTEN(0x005DA270, DeletingDestructor<&RefPtr::Destroy>::Delete);
EI_REWRITTEN(0x0042A5D0, DeletingDestructor<&RefPtr::Destroy>::Delete);
EI_REWRITTEN(0x0042A5F0, DeletingDestructor<&RefPtr::Destroy>::Delete);
EI_REWRITTEN(0x0042B880, DeletingDestructor<&RefPtr::Destroy>::Delete);
EI_REPLACE_METHOD(0x0042D6D0, DeletingDestructor<misc400::ManagerDestruct>::Delete);        // 0042D6F0
EI_REWRITTEN(0x0043DCA0, DeletingDestructor<&RefPtr::Destroy>::Delete);

EI_REWRITTEN(0x004054F0, CleanupClear8);
EI_REWRITTEN(0x00405530, CleanupClear8);
EI_REWRITTEN(0x00689810, CleanupClear8);
EI_REWRITTEN(0x00689880, CleanupClear8);
EI_REWRITTEN(0x0059AF10, CleanupClear8);
EI_REWRITTEN(0x0059AFE0, CleanupClear8);
