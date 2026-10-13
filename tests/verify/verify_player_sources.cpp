// Checks for player_sources.cpp and object_id_map.cpp (A-E1). Main thread (the world's update and
// its map close).
//  - CWorldHandle::TakeNew: side by side. The original runs on a copy of the handle (the copy's
//    own reference to the pending link added) with an empty list of the same kind; ours on the real
//    ones. Both handles must end without a link, the real list's new tail must be the shadow's
//    elements, and the link's count must be what it was (the list's reference replaces the
//    handle's). Reached when a server world takes its local player (a new game), by reading.
//  - DriverSession::TakeNew: side by side the same way, the copy with its own holders list (each
//    holder counted once more). Reached only by a network server (no scenario).
//  - ObjectIdMap::ReleaseAll: ours only (it releases, and an object can go with its last
//    reference); the map must end empty with maxId 0. Reached when a world closes its map and when
//    a player leaves or forgets the objects sent to it.
#include "verify.h"

#include "../../src/game/net_link.h"
#include "../../src/game/world.h"
#include "../../src/lib/stlp.h"

#include <cstring>

namespace {
void* s_unused;

using LinkList = mfc::CTypedPtrList<CRefObject>;

LinkList EmptyLike(const LinkList* list) {
    LinkList shadow{};
    shadow.vtable = list->vtable;
    shadow.blockSize = list->blockSize;
    return shadow;
}

// The shadow's references dropped and its blocks freed.
void Drop(LinkList* shadow) {
    for (auto* node = shadow->head; node; node = node->next)
        if (node->data) node->data->Release();
    shadow->RemoveAll();
}

// Whether `list` is its first `countBefore` elements followed by the shadow's, in order.
bool SameTail(const LinkList* shadow, const LinkList* list, int countBefore) {
    if (list->count != countBefore + shadow->count) return false;
    auto* node = list->head;
    for (int i = 0; i < countBefore && node; ++i) node = node->next;
    for (auto* expected = shadow->head; expected; expected = expected->next) {
        if (!node || node->data != expected->data) return false;
        node = node->next;
    }
    return node == nullptr;
}

// 00478880: PUSH ECX (1), MOV EAX,ECX (2), PUSH EBP (1), MOV [ESP+4],EAX (4).
void(__fastcall* s_handleTakeNew)(CWorldHandle*, void*, void*);
void __fastcall HandleTakeNewVerify(CWorldHandle* self, void*, void* joined) {
    auto* list = static_cast<LinkList*>(joined);
    CRefObject* link = self->pendingLink_;
    int countBefore = list->count;
    int refsBefore = link ? link->refs_ : 0;
    alignas(CWorldHandle) unsigned char storage[sizeof(CWorldHandle)];
    std::memcpy(storage, static_cast<const void*>(self), sizeof(CWorldHandle));
    auto* copy = reinterpret_cast<CWorldHandle*>(storage);
    if (link) ++link->refs_;
    LinkList shadow = EmptyLike(list);
    s_handleTakeNew(copy, nullptr, &shadow);
    bool originalCleared = copy->pendingLink_ == nullptr;
    self->CWorldHandle::TakeNew(joined);
    bool same = originalCleared && self->pendingLink_ == nullptr && shadow.count == (link ? 1 : 0) &&
                SameTail(&shadow, list, countBefore);
    Drop(&shadow);
    if (link) same = same && link->refs_ == refsBefore;
    ei::verify::Check("CWorldHandle::TakeNew", true, same, "link %p, %d in the list", static_cast<void*>(link),
                      list->count);
}

// 0047A230: SUB ESP,8 (3), PUSH EBX (1), MOV EBX,ECX (2).
void(__fastcall* s_sessionTakeNew)(netlink::DriverSession*, void*, void*);
void __fastcall SessionTakeNewVerify(netlink::DriverSession* self, void*, void* joined) {
    using netlink::DriverNode;
    auto* list = static_cast<LinkList*>(joined);
    int countBefore = list->count;
    alignas(netlink::DriverSession) unsigned char storage[sizeof(netlink::DriverSession)];
    std::memcpy(storage, static_cast<const void*>(self), sizeof(netlink::DriverSession));
    auto* copy = reinterpret_cast<netlink::DriverSession*>(storage);
    auto* sentinel = static_cast<DriverNode*>(stlp::allocate(sizeof(DriverNode)));
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
    int holders = 0;
    for (DriverNode* node = self->holders_->next; node != self->holders_; node = node->next) {
        auto* added = static_cast<DriverNode*>(stlp::allocate(sizeof(DriverNode)));
        added->value.ptr = node->value.ptr;
        if (added->value.ptr) ++added->value.ptr->refs_;
        added->next = sentinel;
        added->prev = sentinel->prev;
        sentinel->prev->next = added;
        sentinel->prev = added;
        ++holders;
    }
    copy->holders_ = sentinel;
    LinkList shadow = EmptyLike(list);
    s_sessionTakeNew(copy, nullptr, &shadow);
    bool originalEmptied = sentinel->next == sentinel && sentinel->prev == sentinel;
    stlp::deallocate(sentinel, sizeof(DriverNode));
    self->DriverSession::TakeNew(joined);
    bool same = originalEmptied && self->holders_->next == self->holders_ && self->holders_->prev == self->holders_ &&
                SameTail(&shadow, list, countBefore);
    int appended = shadow.count;
    Drop(&shadow);
    ei::verify::Check("DriverSession::TakeNew", true, same, "%d holders, %d appended", holders, appended);
}

void __fastcall ReleaseAllVerify(ObjectIdMap* self) {
    unsigned objects = static_cast<unsigned>(self->map.Size());
    self->ReleaseAll();
    bool empty = self->map.Size() == 0 && self->map.First() == nullptr && self->maxId == 0;
    ei::verify::Check("ObjectIdMap::ReleaseAll", true, empty, "%u objects", objects);
}
} // namespace

EI_VERIFY(0x00478880, HandleTakeNewVerify, 8, s_handleTakeNew);
EI_VERIFY(0x0047A230, SessionTakeNewVerify, 6, s_sessionTakeNew);
EI_VERIFY(0x0047B670, ReleaseAllVerify, 0, s_unused);
