// The game server's destructor (00417BC0), scalar deleting destructor (00417A10, slot 0) and its
// base layer's destructor body (004132F0). The members are torn down in reverse order through
// their STLport destructors and the counted pointers' releases, then the session base. See
// net_classes.h and docs/notes/network.md.
#include "net_classes.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_library.h"

namespace net {

namespace {

// STLport destructors, clears and sentinel frees of the member containers (library).
__declspec(naked) void __fastcall List25C0Destruct(void* /*list*/) EI_ORIGINAL(0x004238B0)
__declspec(naked) void __fastcall List25BCDestruct(void* /*list*/) EI_ORIGINAL(0x004235F0)
__declspec(naked) void __fastcall List25B4Clear(void* /*list*/) EI_ORIGINAL(0x004272E0)
__declspec(naked) void __fastcall List25B4Free(void* /*list*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x00425AB0)
__declspec(naked) void __fastcall List25ACClear(void* /*list*/) EI_ORIGINAL(0x004272A0)
__declspec(naked) void __fastcall List25A4Clear(void* /*list*/) EI_ORIGINAL(0x00427260)
__declspec(naked) void __fastcall List25A4Free(void* /*list*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x004258F0)
__declspec(naked) void __fastcall List259CClear(void* /*list*/) EI_ORIGINAL(0x00427200)
__declspec(naked) void __fastcall List259CFree(void* /*list*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x004257A0)
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
void DestroyInline(RefPtr& slot) {
    CRefObject*& object = slot.ptr;
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
    StringBufferFree(&mapName_);
    StringBufferFree(&name_);
    List25BCClear(&banned_);
    List25C0Destruct(&finishedTrades_);
    List25BCDestruct(&trades_);
    List2610Destruct(&offers_);
    DestroyList(awards_, List25B4Clear, List25B4Free);
    DestroyList(variables_, List25B0Clear, List25B0FreeNode);
    DestroyList(queuedMaps_, List25ACClear, List25ACFree);
    DestroyInline(map_);
    DestroyList(bindings_, List25A4Clear, List25A4Free);
    DestroyInline(hostInfo_);
    DestroyList(fileRequests_, List259CClear, List259CFree);
    DestroyList(files_, List2598Clear, List2598Free);
    DestroyList(lines_, List2594Clear, List2594Free);
    DestroyList(players_, List2590Clear, List2590Free);
    List2568Destruct(&joins_);
    SessionBase::Destruct();
}
EI_REPLACE_METHOD(0x00417BC0, ServerSession::Destruct);

// 004132F0: the destructor body of the server's base layer (reached from its scalar deleting
// destructor 004132D0 and the constructor's unwind funclets): the pending joins, then the session
// base. The same steps end ServerSession::Destruct.
void ServerBase::DestructBase() {
    List2568Destruct(&joins_);
    SessionBase::Destruct();
}
EI_REPLACE_METHOD(0x004132F0, ServerBase::DestructBase);

// 00417A10 (slot 0): the destructor body, then the memory freed when bit 0 of `flags` is set.
ServerSession* ServerSession::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00417A10, ServerSession::DeletingDestruct);

} // namespace net
