// The worlds' player sources (0055F190-0055F7B0, A-U8; player_source.h): the base's destructor, the
// counted helper at CWorld + 0x5C (CWorldHandle) and the network session at CWorld + 0x60
// (netlink::DriverSession): their clones, shutdowns and destructors, and CWorld::OpenSession, which
// makes a session. The session's lists are STLport list<RefPtr>: drivers owning (RefPtr::Destroy),
// holders plain (Release); their range inserts are the instantiations' own (library code).
#include "world.h"

#include "core_refcount.h"
#include "core_streams.h"
#include "net_link.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/stlp.h"

#include <cstdint>

using netlink::DriverNode;
using netlink::DriverSession;

namespace {

// list<RefPtr>::insert(position, first, last) of the session's two lists (00560430 drivers: each
// element shared, 0047A450; 00560460 holders: each counted, 0047A480). thiscall, RET 0xC.
__declspec(naked) void __fastcall DriversInsertRange(DriverNode** /*list*/, void*, DriverNode* /*position*/,
                                                     DriverNode* /*first*/, DriverNode* /*last*/) EI_ORIGINAL(0x00560430)
__declspec(naked) void __fastcall HoldersInsertRange(DriverNode** /*list*/, void*, DriverNode* /*position*/,
                                                     DriverNode* /*first*/, DriverNode* /*last*/) EI_ORIGINAL(0x00560460)

// An empty list's sentinel from the node pool (the lists' constructors, inlined).
DriverNode* NewSentinel() {
    auto* node = static_cast<DriverNode*>(stlp::allocate(sizeof(DriverNode)));
    node->next = node;
    node->prev = node;
    return node;
}

// The lists' clear (005603B0 drivers, 005603F0 holders): each element destroyed / released, its node
// back to the pool, the sentinel linked to itself.
void ClearDrivers(DriverNode* sentinel) {
    for (DriverNode* node = sentinel->next; node != sentinel;) {
        DriverNode* current = node;
        node = node->next;
        current->value.Destroy();
        stlp::deallocate(current, sizeof(DriverNode));
    }
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
}
void ClearHolders(DriverNode* sentinel) {
    for (DriverNode* node = sentinel->next; node != sentinel;) {
        DriverNode* current = node;
        node = node->next;
        if (CRefObject* object = current->value.ptr) object->Release();
        stlp::deallocate(current, sizeof(DriverNode));
    }
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
}

// A counted pointer's release as the helpers inline it: one reference dropped, deleted at zero.
void ReleaseCounted(CRefObject* object) {
    if (object) object->Release();
}

} // namespace

// ---- CPlayerSource (0073F38C) ---------------------------------------------------------------------

void CPlayerSource::DestructBase() { ei::SetVtable(this, kRefObjectRootVtable); }   // 0055F2E0
EI_REPLACE_METHOD(0x0055F2E0, CPlayerSource::DestructBase);

CPlayerSource* CPlayerSource::DeletingDestructBase(unsigned flags) {   // 0055F2F0
    DestructBase();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0055F2F0, CPlayerSource::DeletingDestructBase);

// ---- CWorldHandle (0073F240) ----------------------------------------------------------------------

// A new helper with the same pending link (counted). The original writes the count before testing
// the allocation; ours tests first.
CWorldHandle* CWorldHandle::Clone() {   // 0055F1A0
    auto* copy = static_cast<CWorldHandle*>(gamecrt::operator_new(sizeof(CWorldHandle)));
    if (!copy) return nullptr;
    copy->refs_ = 0;
    copy->flags6_ = 0;
    copy->flags7_ = 0;
    ei::SetVtable(copy, CPlayerSource::kVtableAddress);
    copy->pendingLink_ = pendingLink_;
    if (copy->pendingLink_) ++copy->pendingLink_->refs_;
    ei::SetVtable(copy, kVtableAddress);
    return copy;
}

// Destroyed and built again in place (the count and flags kept): the link released, none pending.
int CWorldHandle::OnShutdown() {   // 0055F1E0
    Destruct();
    pendingLink_ = nullptr;
    ei::SetVtable(this, kVtableAddress);
    return 0;
}

void CWorldHandle::Destruct() {   // 0055F280
    ei::SetVtable(this, kVtableAddress);
    ReleaseCounted(pendingLink_);
    ei::SetVtable(this, kRefObjectRootVtable);
}
EI_REPLACE_METHOD(0x0055F280, CWorldHandle::Destruct);

CWorldHandle* CWorldHandle::DeletingDestruct(unsigned flags) {   // 0055F260
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0055F260, CWorldHandle::DeletingDestruct);

namespace {
int __fastcall HandleShutdownHook(CWorldHandle* self) { return self->CWorldHandle::OnShutdown(); }
CWorldHandle* __fastcall HandleCloneHook(CWorldHandle* self) { return self->CWorldHandle::Clone(); }
} // namespace
EI_REPLACE(0x0055F1E0, HandleShutdownHook);
EI_REPLACE(0x0055F1A0, HandleCloneHook);

// ---- CClientHandle (0073F628) ---------------------------------------------------------------------

// The original clears the count before testing the allocation; ours tests first.
CClientHandle* CClientHandle::Clone() {   // 005626B0
    auto* copy = static_cast<CClientHandle*>(gamecrt::operator_new(sizeof(CClientHandle)));
    if (!copy) return nullptr;
    copy->refs_ = 0;
    copy->flags6_ = 0;
    copy->flags7_ = 0;
    ei::SetVtable(copy, kVtableAddress);
    return copy;
}

int CClientHandle::OnShutdown() {   // 005626D0
    ei::SetVtable(this, kRefObjectRootVtable);
    ei::SetVtable(this, kVtableAddress);
    return 0;
}

void CClientHandle::TakeNew(void* joined) {   // 00562700
    auto* links = static_cast<mfc::CList<RefPtr>*>(joined);
    for (auto* node = links->head; node; node = node->next) ReleaseCounted(node->data.ptr);
    links->RemoveAll();
}

namespace {
int __fastcall ClientHandleShutdownHook(CClientHandle* self) { return self->CClientHandle::OnShutdown(); }
CClientHandle* __fastcall ClientHandleCloneHook(CClientHandle* self) { return self->CClientHandle::Clone(); }
void __fastcall ClientHandleTakeNewHook(CClientHandle* self, void*, void* joined) { self->CClientHandle::TakeNew(joined); }
} // namespace
EI_REPLACE(0x005626D0, ClientHandleShutdownHook);
EI_REPLACE(0x005626B0, ClientHandleCloneHook);
EI_REPLACE(0x00562700, ClientHandleTakeNewHook);

// ---- netlink::DriverSession (0073F3A8) ------------------------------------------------------------

namespace netlink {

DriverSession* DriverSession::ConstructAt(void* memory) {
    auto* session = static_cast<DriverSession*>(RefValueConstructBase(memory));
    ei::SetVtable(session, CPlayerSource::kVtableAddress);
    session->drivers_ = NewSentinel();
    session->holders_ = NewSentinel();
    ei::SetVtable(session, kVtableAddress);
    return session;
}

// A copy: every driver shared (a copy-on-share driver is cloned), every holder counted. The original
// writes the count before testing the allocation; ours tests first.
DriverSession* DriverSession::Clone() {   // 0055F550
    auto* copy = static_cast<DriverSession*>(gamecrt::operator_new(sizeof(DriverSession)));
    if (!copy) return nullptr;
    copy->refs_ = 0;
    copy->flags6_ = 0;
    copy->flags7_ = 0;
    ei::SetVtable(copy, CPlayerSource::kVtableAddress);
    copy->drivers_ = NewSentinel();
    DriversInsertRange(&copy->drivers_, nullptr, copy->drivers_->next, drivers_->next, drivers_);
    copy->holders_ = NewSentinel();
    HoldersInsertRange(&copy->holders_, nullptr, copy->holders_->next, holders_->next, holders_);
    ei::SetVtable(copy, kVtableAddress);
    return copy;
}

// Destroyed and built again in place (the count and flags kept): both lists empty, new sentinels.
int DriverSession::OnShutdown() {   // 0055F6B0
    ei::SetVtable(this, kVtableAddress);
    ClearHolders(holders_);
    stlp::deallocate(holders_, sizeof(DriverNode));
    ClearDrivers(drivers_);
    stlp::deallocate(drivers_, sizeof(DriverNode));
    ei::SetVtable(this, kRefObjectRootVtable);
    ei::SetVtable(this, CPlayerSource::kVtableAddress);
    drivers_ = NewSentinel();
    holders_ = NewSentinel();
    ei::SetVtable(this, kVtableAddress);
    return 0;
}

void DriverSession::Destruct() {   // 0055F7B0
    ei::SetVtable(this, kVtableAddress);
    ClearHolders(holders_);
    stlp::deallocate(holders_, sizeof(DriverNode));
    ClearDrivers(drivers_);
    stlp::deallocate(drivers_, sizeof(DriverNode));
    ei::SetVtable(this, kRefObjectRootVtable);
}
EI_REPLACE_METHOD(0x0055F7B0, DriverSession::Destruct);

DriverSession* DriverSession::DeletingDestruct(unsigned flags) {   // 0055F790
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0055F790, DriverSession::DeletingDestruct);

namespace {
int __fastcall SessionShutdownHook(DriverSession* self) { return self->DriverSession::OnShutdown(); }
DriverSession* __fastcall SessionCloneHook(DriverSession* self) { return self->DriverSession::Clone(); }
} // namespace
EI_REPLACE(0x0055F6B0, SessionShutdownHook);
EI_REPLACE(0x0055F550, SessionCloneHook);

} // namespace netlink

// ---- CWorld::OpenSession (0055F410) ---------------------------------------------------------------

void CWorld::OpenSession() {
    void* memory = gamecrt::operator_new(sizeof(DriverSession));
    DriverSession* session = nullptr;
    if (memory) {
        // The count and both flag bytes as one dword, cleared before the constructor.
        static_cast<DriverSession*>(memory)->refs_ = 0;
        static_cast<DriverSession*>(memory)->flags6_ = 0;
        static_cast<DriverSession*>(memory)->flags7_ = 0;
        session = DriverSession::ConstructAt(memory);
    }
    reinterpret_cast<RefPtr*>(&netSession_)->ShareAssign(session);
}
EI_REPLACE_METHOD(0x0055F410, CWorld::OpenSession);

// ---- CWorld::CloseSession (0055F890) --------------------------------------------------------------

void CWorld::CloseSession() {
    RefPtr& session = *reinterpret_cast<RefPtr*>(&netSession_);
    if (!session.ptr) return;
    session.Destroy();
    session.ptr = nullptr;
}
EI_REPLACE_METHOD(0x0055F890, CWorld::CloseSession);
