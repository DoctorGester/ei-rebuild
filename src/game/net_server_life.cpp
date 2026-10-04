// The game server's destructor (00417BC0) and scalar deleting destructor (00417A10, slot 0). The
// members are torn down in reverse order through their STLport destructors and the counted
// pointers' releases, then the session base. See net_classes.h and docs/notes/unmapped_410_470.md
// ("Seventh round").
#include "net_classes.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "lib/crt.h"

namespace net {

namespace {

// STLport destructors, clears and sentinel frees of the member containers (library).
__declspec(naked) void __fastcall StringDestruct(StringRep* /*s*/) EI_ORIGINAL(0x00425040)
__declspec(naked) void __fastcall List25C4Destruct(void* /*list*/) EI_ORIGINAL(0x00421CD0)
__declspec(naked) void __fastcall List25C0Destruct(void* /*list*/) EI_ORIGINAL(0x004238B0)
__declspec(naked) void __fastcall List25BCDestruct(void* /*list*/) EI_ORIGINAL(0x004235F0)
__declspec(naked) void __fastcall List25B8Destruct(void* /*list*/) EI_ORIGINAL(0x00423370)
__declspec(naked) void __fastcall List25B4Clear(void* /*list*/) EI_ORIGINAL(0x004272E0)
__declspec(naked) void __fastcall List25B4Free(void* /*list*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x00425AB0)
__declspec(naked) void __fastcall List25B0Clear(void* /*list*/) EI_ORIGINAL(0x00427100)
__declspec(naked) void __fastcall List25B0Free(void* /*list*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x00422390)
__declspec(naked) void __fastcall List25ACClear(void* /*list*/) EI_ORIGINAL(0x004272A0)
__declspec(naked) void __fastcall List25ACFree(void* /*list*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x00425A40)
__declspec(naked) void __fastcall List25A4Clear(void* /*list*/) EI_ORIGINAL(0x00427260)
__declspec(naked) void __fastcall List25A4Free(void* /*list*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x004258F0)
__declspec(naked) void __fastcall List259CClear(void* /*list*/) EI_ORIGINAL(0x00427200)
__declspec(naked) void __fastcall List259CFree(void* /*list*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x004257A0)
__declspec(naked) void __fastcall List2598Clear(void* /*list*/) EI_ORIGINAL(0x004271C0)
__declspec(naked) void __fastcall List2598Free(void* /*list*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x004256D0)
__declspec(naked) void __fastcall List2594Clear(void* /*list*/) EI_ORIGINAL(0x00427180)
__declspec(naked) void __fastcall List2594Free(void* /*list*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x00422790)
__declspec(naked) void __fastcall List2590Clear(void* /*list*/) EI_ORIGINAL(0x00427140)
__declspec(naked) void __fastcall List2590Free(void* /*list*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x00425470)
__declspec(naked) void __fastcall List2568Destruct(void* /*list*/) EI_ORIGINAL(0x00421D20)

// A list destroyed as the destructor inlines it: cleared, then its sentinel node freed.
template <typename Clear, typename Free>
void DestroyList(void*& list, Clear clear, Free free) {
    clear(&list);
    free(&list, nullptr, list, 1);
}

// RefPtr::Destroy as the destructor inlines it (the release is not null-checked after the
// shutdown).
void DestroyInline(std::uint32_t& slot) {
    auto*& object = reinterpret_cast<CRefObject*&>(slot);
    if (!object) return;
    if (object->refs_ != 1) {
        object->flags7_ |= CRefObject::kDetached;
        object->Shutdown();
    }
    object->Release();
}

} // namespace

// 00417BC0: the destructor body. The server's own vtable is not restored first (the original does
// not either).
void ServerSession::Destruct() {
    StringDestruct(&string_25D4);
    StringDestruct(&string_25C8);
    List25C4Destruct(&list_25C4);
    List25C0Destruct(&list_25C0);
    List25BCDestruct(&list_25BC);
    List25B8Destruct(&list_25B8);
    DestroyList(list_25B4, List25B4Clear, List25B4Free);
    DestroyList(list_25B0, List25B0Clear, List25B0Free);
    DestroyList(list_25AC, List25ACClear, List25ACFree);
    DestroyInline(unknown_25A8);
    DestroyList(list_25A4, List25A4Clear, List25A4Free);
    DestroyInline(unknown_25A0);
    DestroyList(list_259C, List259CClear, List259CFree);
    DestroyList(list_2598, List2598Clear, List2598Free);
    DestroyList(list_2594, List2594Clear, List2594Free);
    DestroyList(list_2590, List2590Clear, List2590Free);
    List2568Destruct(&list_2568);
    base.Destruct();
}

namespace {
void __fastcall ServerDestructHook(ServerSession* self) { self->Destruct(); }
} // namespace
EI_REPLACE(0x00417BC0, ServerDestructHook);

// 00417A10 (slot 0): the destructor body, then the memory freed when bit 0 of `flags` is set.
ServerSession* __fastcall ServerDeletingDestructor(ServerSession* self, void*, unsigned flags) {
    self->Destruct();
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x00417A10, ServerDeletingDestructor);

} // namespace net
