// The game client's destructor (0041CA10), scalar deleting destructor (0041C8B0, slot 0) and its
// base layer's destructor body (0041C530). The members are torn down in reverse order through
// their STLport destructors and the counted pointers' releases, then the session base. See
// net_classes.h and docs/notes/network.md.
#include "net_classes.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "originals_library.h"
#include "originals_gameplay.h"

namespace net {

namespace {

// The two parties (00661050 class, outside this range).
void __fastcall PartyDestruct(void* a0) { PartyRecordsDestructOriginal(a0); }
// STLport destructors, clears and sentinel frees of the member containers (library).
__declspec(naked) void __fastcall List2614Destruct(void* /*list*/) EI_ORIGINAL(0x00424A40)
__declspec(naked) void __fastcall List2608Clear(void* /*list*/) EI_ORIGINAL(0x004275A0)
__declspec(naked) void __fastcall List2608Free(void* /*list*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x00426330)
__declspec(naked) void __fastcall List2604Clear(void* /*list*/) EI_ORIGINAL(0x00427560)
__declspec(naked) void __fastcall List2604Free(void* /*list*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x00426260)
__declspec(naked) void __fastcall List25FCClear(void* /*list*/) EI_ORIGINAL(0x00427500)
__declspec(naked) void __fastcall List25FCFree(void* /*list*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x004261F0)
__declspec(naked) void __fastcall List25F8Clear(void* /*list*/) EI_ORIGINAL(0x004274C0)
__declspec(naked) void __fastcall List25F8Free(void* /*list*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x00426120)
__declspec(naked) void __fastcall List25E8Clear(void* /*list*/) EI_ORIGINAL(0x00427480)
__declspec(naked) void __fastcall List25E8Free(void* /*list*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x00426050)
__declspec(naked) void __fastcall List25E4Clear(void* /*list*/) EI_ORIGINAL(0x00427420)
__declspec(naked) void __fastcall List25E4Free(void* /*list*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x00425F00)
__declspec(naked) void __fastcall List25E0Clear(void* /*list*/) EI_ORIGINAL(0x004273E0)
__declspec(naked) void __fastcall List25E0Free(void* /*list*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x00425E30)
__declspec(naked) void __fastcall List25DCClear(void* /*list*/) EI_ORIGINAL(0x004273A0)
__declspec(naked) void __fastcall List25DCFree(void* /*list*/, void*, void* /*node*/, int /*n*/) EI_ORIGINAL(0x00425D60)
__declspec(naked) void __fastcall List25D8Clear(void* /*list*/) EI_ORIGINAL(0x00427360)
__declspec(naked) void __fastcall List25D8FreeNode(void* /*node*/, int /*n*/) EI_ORIGINAL(0x00426960)
__declspec(naked) void __fastcall Vector25ACDestruct(void* /*vector*/) EI_ORIGINAL(0x0041F7F0)
__declspec(naked) void __fastcall List25A8Destruct(void* /*list*/) EI_ORIGINAL(0x00421A50)

// A list destroyed as the destructor inlines it: cleared, then its sentinel node freed.
template <typename Clear, typename Free>
void DestroyList(void*& list, Clear clear, Free free) {
    clear(&list);
    free(&list, nullptr, list, 1);
}

// The release a counted pointer's destructor inlines: one reference less, deleted at zero.
void ReleaseInline(void* slot) {
    auto* object = static_cast<CRefObject*>(slot);
    if (!object) return;
    --object->refs_;
    if (object->refs_ == 0 && object) delete object;
}

} // namespace

// 0041CA10: the destructor body. The client's own vtable is not restored first (the original does
// not either).
void ClientSession::Destruct() {
    PartyDestruct(&parties_[1]);
    PartyDestruct(&parties_[0]);
    if (trade_) static_cast<CRefObject*>(trade_)->Release();
    List2614Destruct(&tradeRecords_);
    List2610Destruct(&offers_);
    party_.Destroy();
    DestroyList(awards_, List2608Clear, List2608Free);
    DestroyList(timers_, List2604Clear, List2604Free);
    if (auto* local = player_.ptr) {
        if (local->refs_ != 1) {
            local->flags7_ |= CRefObject::kDetached;
            local->Shutdown();
        }
        player_.ptr->Release();
    }
    DestroyList(variables_, List25FCClear, List25FCFree);
    DestroyList(zones_, List25F8Clear, List25F8Free);
    ReleaseInline(map_);
    ReleaseInline(binding_);
    ReleaseInline(info_);
    DestroyList(fileRequests_, List25E8Clear, List25E8Free);
    DestroyList(chunks_, List25E4Clear, List25E4Free);
    DestroyList(files_, List25E0Clear, List25E0Free);
    DestroyList(lines_, List25DCClear, List25DCFree);
    List25D8Clear(&players_);
    List25D8FreeNode(players_, 1);
    List25BCClear(&waiting_);
    Vector25ACDestruct(&queries_);
    List25A8Destruct(&servers_);
    GrowingBufferDestruct(&joinData_);
    SessionBase::Destruct();
}
EI_REPLACE_METHOD(0x0041CA10, ClientSession::Destruct);

// 0041C530: the destructor body of the client's base layer (vtable 0073BE30; reached from its
// scalar deleting destructor 0041C450 and the constructor's unwind funclets): the list +0x25BC,
// the vector of buffers +0x25AC, the list +0x25A8, the buffer +0x2578, then the session base. The
// original inlines the three container destructors (with the same element destructors and node
// frees as the out-of-line ones called here, which ClientSession::Destruct uses too).
void ClientBase::DestructBase() {
    List25BCClear(&waiting_);
    Vector25ACDestruct(&queries_);
    List25A8Destruct(&servers_);
    GrowingBufferDestruct(&joinData_);
    SessionBase::Destruct();
}
EI_REPLACE_METHOD(0x0041C530, ClientBase::DestructBase);

// 0041C8B0 (slot 0): the destructor body, then the memory freed when bit 0 of `flags` is set.
ClientSession* ClientSession::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0041C8B0, ClientSession::DeletingDestruct);

} // namespace net
