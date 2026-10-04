// The game client's destructor (0041CA10) and scalar deleting destructor (0041C8B0, slot 0). The
// members are torn down in reverse order through their STLport destructors and the counted
// pointers' releases, then the session base. See net_classes.h and docs/notes/unmapped_410_470.md
// ("Seventh round").
#include "net_classes.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

namespace net {

namespace {

// The two parties (00661050 class, outside this range).
__declspec(naked) void __fastcall PartyDestruct(void* /*party*/) EI_ORIGINAL(0x006611D0)
// STLport destructors, clears and sentinel frees of the member containers (library).
__declspec(naked) void __fastcall List2614Destruct(void* /*list*/) EI_ORIGINAL(0x00424A40)
__declspec(naked) void __fastcall List2610Destruct(void* /*list*/) EI_ORIGINAL(0x00423370)
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
__declspec(naked) void __fastcall List25BCClear(void* /*list*/) EI_ORIGINAL(0x00421CD0)
__declspec(naked) void __fastcall Vector25ACDestruct(void* /*vector*/) EI_ORIGINAL(0x0041F7F0)
__declspec(naked) void __fastcall List25A8Destruct(void* /*list*/) EI_ORIGINAL(0x00421A50)
__declspec(naked) void __fastcall GrowingBufferDestruct(void* /*buffer*/) EI_ORIGINAL(0x004DA910)

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
    if (object->refs_ == 0 && object) ei::VCall<void>(object, 0, 1);
}

} // namespace

// 0041CA10: the destructor body. The client's own vtable is not restored first (the original does
// not either).
void ClientSession::Destruct() {
    PartyDestruct(&records[1]);
    PartyDestruct(&records[0]);
    if (unknown_2618) static_cast<CRefObject*>(unknown_2618)->Release();
    List2614Destruct(&list_2614);
    List2610Destruct(&list_2610);
    reinterpret_cast<RefPtr*>(&unknown_260C)->Destroy();
    DestroyList(list_2608, List2608Clear, List2608Free);
    DestroyList(list_2604, List2604Clear, List2604Free);
    if (auto* local = static_cast<CRefObject*>(player)) {
        if (local->refs_ != 1) {
            local->flags7_ |= CRefObject::kDetached;
            local->Shutdown();
        }
        static_cast<CRefObject*>(player)->Release();
    }
    DestroyList(list_25FC, List25FCClear, List25FCFree);
    DestroyList(list_25F8, List25F8Clear, List25F8Free);
    ReleaseInline(ref_25F4);
    ReleaseInline(ref_25F0);
    ReleaseInline(ref_25EC);
    DestroyList(list_25E8, List25E8Clear, List25E8Free);
    DestroyList(list_25E4, List25E4Clear, List25E4Free);
    DestroyList(list_25E0, List25E0Clear, List25E0Free);
    DestroyList(list_25DC, List25DCClear, List25DCFree);
    List25D8Clear(&list_25D8);
    List25D8FreeNode(list_25D8, 1);
    List25BCClear(&list_25BC);
    Vector25ACDestruct(vector_25AC);
    List25A8Destruct(&list_25A8);
    GrowingBufferDestruct(buffer);
    base.Destruct();
}

namespace {
void __fastcall ClientDestructHook(ClientSession* self) { self->Destruct(); }
} // namespace
EI_REPLACE(0x0041CA10, ClientDestructHook);

// 0041C8B0 (slot 0): the destructor body, then the memory freed when bit 0 of `flags` is set.
ClientSession* __fastcall ClientDeletingDestructor(ClientSession* self, void*, unsigned flags) {
    self->Destruct();
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x0041C8B0, ClientDeletingDestructor);

} // namespace net
