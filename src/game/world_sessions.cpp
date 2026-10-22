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
#include "lib/stlp_list_refs.h"
#include "lib/compiled_vtable.h"

#include <cstdint>

using netlink::DriverNode;
using netlink::DriverSession;

namespace {

// list<RefPtr>::insert(position, first, last) of the session's two lists (00560430 drivers: each
// element shared, 0047A450; 00560460 holders: each counted, 0047A480).
template <typename Policy>
void InsertRange(DriverNode* position, DriverNode* first, DriverNode* last) {
    stlp::list<RefPtr>::InsertRange<Policy>(stlp::AsRefNode(position), stlp::AsRefNode(first), stlp::AsRefNode(last));
}

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

// ---- The player sources' compiled tables (C19) ----------------------------------------------------

extern "C" int __cdecl _purecall();

namespace {

// The base's (0073F38C): slots 2, 3, 4 and 6 pure.
class CPlayerSourceClass final : public ei::CompiledTag<CPlayerSourceClass> {
public:
    virtual void* DeleteScalar(unsigned flags) { return Object()->DeletingDestructBase(flags); }   // 0: 0055F2F0
    virtual void Serialize(ChunkArchive*) {}                                    // 1: 00412580 (empty)
    virtual int OnShutdown() { return _purecall(); }                            // 2: pure
    virtual CRefObject* Clone() {                                               // 3: pure
        _purecall();
        return nullptr;
    }
    virtual bool Query() { return _purecall() != 0; }                           // 4: pure
    virtual void Pump() {}                                                      // 5: 0055F190 (empty)
    virtual void TakeNew(void*) { _purecall(); }                                // 6: pure

private:
    CPlayerSource* Object() { return reinterpret_cast<CPlayerSource*>(this); }
};

// The server world's counted helper's (0073F240).
class CWorldHandleClass final : public ei::CompiledTag<CWorldHandleClass> {
public:
    virtual void* DeleteScalar(unsigned flags) { return Object()->DeletingDestruct(flags); }   // 0: 0055F260
    virtual void Serialize(ChunkArchive*) {}                                    // 1: 00412580 (empty)
    virtual int OnShutdown() { return Object()->CWorldHandle::OnShutdown(); }  // 2: 0055F1E0
    virtual CRefObject* Clone() { return Object()->CWorldHandle::Clone(); }    // 3: 0055F1A0
    virtual bool Query() { return false; }                                      // 4: 0055F250
    virtual void Pump() {}                                                      // 5: 0055F190 (empty)
    virtual void TakeNew(void* joined) { Object()->TakeNewImpl(joined); }      // 6: 00478880

private:
    CWorldHandle* Object() { return reinterpret_cast<CWorldHandle*>(this); }
};

// The client world's player source's (0073F628).
class CClientHandleClass final : public ei::CompiledTag<CClientHandleClass> {
public:
    virtual void* DeleteScalar(unsigned flags) {                                // 0: 00562750
        return reinterpret_cast<CRefObject*>(this)->ValueDeletingDestruct(flags);
    }
    virtual void Serialize(ChunkArchive*) {}                                    // 1: 00412580 (empty)
    virtual int OnShutdown() { return Object()->CClientHandle::OnShutdown(); } // 2: 005626D0
    virtual CRefObject* Clone() { return Object()->CClientHandle::Clone(); }   // 3: 005626B0
    virtual bool Query() { return false; }                                      // 4: 005626F0
    virtual void Pump() {}                                                      // 5: 0055F190 (empty)
    virtual void TakeNew(void* joined) { Object()->TakeNewImpl(joined); }      // 6: 00562700

private:
    CClientHandle* Object() { return reinterpret_cast<CClientHandle*>(this); }
};

} // namespace

void* CPlayerSource::Vtable() { return CPlayerSourceClass::Vtable(); }
void* CWorldHandle::Vtable() { return CWorldHandleClass::Vtable(); }
void* CClientHandle::Vtable() { return CClientHandleClass::Vtable(); }

// ---- CPlayerSource (0073F38C) ---------------------------------------------------------------------

void CPlayerSource::DestructBase() { ei::SetVtable(this, CRefCounted::RootVtable()); }   // 0055F2E0
EI_REWRITTEN(0x0055F2E0, CPlayerSource::DestructBase);

CPlayerSource* CPlayerSource::DeletingDestructBase(unsigned flags) {   // 0055F2F0
    DestructBase();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x0055F2F0, CPlayerSource::DeletingDestructBase);

// ---- CWorldHandle (0073F240) ----------------------------------------------------------------------

// A new helper with the same pending link (counted). The original writes the count before testing
// the allocation; ours tests first.
CWorldHandle* CWorldHandle::Clone() {   // 0055F1A0
    auto* copy = gamecrt::operator_new_as<CWorldHandle>();
    if (!copy) return nullptr;
    copy->refs_ = 0;
    copy->flags6_ = 0;
    copy->flags7_ = 0;
    ei::SetVtable(copy, CPlayerSource::Vtable());
    copy->pendingLink_ = pendingLink_;
    if (copy->pendingLink_) ++copy->pendingLink_->refs_;
    ei::SetVtable(copy, Vtable());
    return copy;
}

// Destroyed and built again in place (the count and flags kept): the link released, none pending.
int CWorldHandle::OnShutdown() {   // 0055F1E0
    Destruct();
    pendingLink_ = nullptr;
    ei::SetVtable(this, Vtable());
    return 0;
}

void CWorldHandle::Destruct() {   // 0055F280
    ei::SetVtable(this, Vtable());
    ReleaseCounted(pendingLink_);
    ei::SetVtable(this, CRefCounted::RootVtable());
}
EI_REWRITTEN(0x0055F280, CWorldHandle::Destruct);

CWorldHandle* CWorldHandle::DeletingDestruct(unsigned flags) {   // 0055F260
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x0055F260, CWorldHandle::DeletingDestruct);

namespace {
int __fastcall HandleShutdownHook(CWorldHandle* self) { return self->CWorldHandle::OnShutdown(); }
CWorldHandle* __fastcall HandleCloneHook(CWorldHandle* self) { return self->CWorldHandle::Clone(); }
} // namespace
EI_REWRITTEN(0x0055F1E0, HandleShutdownHook);
EI_REWRITTEN(0x0055F1A0, HandleCloneHook);

// ---- CClientHandle (0073F628) ---------------------------------------------------------------------

// The original clears the count before testing the allocation; ours tests first.
CClientHandle* CClientHandle::Clone() {   // 005626B0
    auto* copy = gamecrt::operator_new_as<CClientHandle>();
    if (!copy) return nullptr;
    copy->refs_ = 0;
    copy->flags6_ = 0;
    copy->flags7_ = 0;
    ei::SetVtable(copy, Vtable());
    return copy;
}

int CClientHandle::OnShutdown() {   // 005626D0
    ei::SetVtable(this, CRefCounted::RootVtable());
    ei::SetVtable(this, Vtable());
    return 0;
}

void CClientHandle::TakeNewImpl(void* joined) {   // 00562700
    auto* links = static_cast<mfc::CList<RefPtr>*>(joined);
    for (auto* node = links->head; node; node = node->next) ReleaseCounted(node->data.ptr);
    links->RemoveAll();
}

namespace {
int __fastcall ClientHandleShutdownHook(CClientHandle* self) { return self->CClientHandle::OnShutdown(); }
CClientHandle* __fastcall ClientHandleCloneHook(CClientHandle* self) { return self->CClientHandle::Clone(); }
} // namespace
EI_REWRITTEN(0x005626D0, ClientHandleShutdownHook);
EI_REWRITTEN(0x005626B0, ClientHandleCloneHook);
EI_REWRITTEN(0x00562700, CClientHandle::TakeNewImpl);

// ---- netlink::DriverSession (0073F3A8) ------------------------------------------------------------

namespace netlink {

DriverSession* DriverSession::ConstructAt(void* memory) {
    auto* session = static_cast<DriverSession*>(RefValueConstructBase(memory));
    ei::SetVtable(session, CPlayerSource::Vtable());
    session->drivers_ = NewSentinel();
    session->holders_ = NewSentinel();
    ei::SetVtable(session, Vtable());
    return session;
}

// A copy: every driver shared (a copy-on-share driver is cloned), every holder counted. The original
// writes the count before testing the allocation; ours tests first.
DriverSession* DriverSession::Clone() {   // 0055F550
    auto* copy = gamecrt::operator_new_as<DriverSession>();
    if (!copy) return nullptr;
    copy->refs_ = 0;
    copy->flags6_ = 0;
    copy->flags7_ = 0;
    ei::SetVtable(copy, CPlayerSource::Vtable());
    copy->drivers_ = NewSentinel();
    InsertRange<stlp::SharedRef>(copy->drivers_->next, drivers_->next, drivers_);
    copy->holders_ = NewSentinel();
    InsertRange<stlp::CountedRef>(copy->holders_->next, holders_->next, holders_);
    ei::SetVtable(copy, Vtable());
    return copy;
}

// Destroyed and built again in place (the count and flags kept): both lists empty, new sentinels.
int DriverSession::OnShutdown() {   // 0055F6B0
    ei::SetVtable(this, Vtable());
    ClearHolders(holders_);
    stlp::deallocate(holders_, sizeof(DriverNode));
    ClearDrivers(drivers_);
    stlp::deallocate(drivers_, sizeof(DriverNode));
    ei::SetVtable(this, CRefCounted::RootVtable());
    ei::SetVtable(this, CPlayerSource::Vtable());
    drivers_ = NewSentinel();
    holders_ = NewSentinel();
    ei::SetVtable(this, Vtable());
    return 0;
}

void DriverSession::Destruct() {   // 0055F7B0
    ei::SetVtable(this, Vtable());
    ClearHolders(holders_);
    stlp::deallocate(holders_, sizeof(DriverNode));
    ClearDrivers(drivers_);
    stlp::deallocate(drivers_, sizeof(DriverNode));
    ei::SetVtable(this, CRefCounted::RootVtable());
}
EI_REWRITTEN(0x0055F7B0, DriverSession::Destruct);

DriverSession* DriverSession::DeletingDestruct(unsigned flags) {   // 0055F790
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x0055F790, DriverSession::DeletingDestruct);

namespace {
int __fastcall SessionShutdownHook(DriverSession* self) { return self->DriverSession::OnShutdown(); }
DriverSession* __fastcall SessionCloneHook(DriverSession* self) { return self->DriverSession::Clone(); }
} // namespace
EI_REWRITTEN(0x0055F6B0, SessionShutdownHook);
EI_REWRITTEN(0x0055F550, SessionCloneHook);

} // namespace netlink

// ---- CWorld::OpenSession (0055F410) ---------------------------------------------------------------

void CWorld::OpenSession() {
    DriverSession* memory = gamecrt::operator_new_as<DriverSession>();
    DriverSession* session = nullptr;
    if (memory) {
        // The count and both flag bytes as one dword, cleared before the constructor.
        memory->refs_ = 0;
        memory->flags6_ = 0;
        memory->flags7_ = 0;
        session = DriverSession::ConstructAt(memory);
    }
    netSession_.ShareAssign(session);
}
EI_REWRITTEN(0x0055F410, CWorld::OpenSession);

// ---- CWorld::CloseSession (0055F890) --------------------------------------------------------------

void CWorld::CloseSession() {
    RefPtr& session = netSession_;
    if (!session.ptr) return;
    session.Destroy();
    session.ptr = nullptr;
}
EI_REWRITTEN(0x0055F890, CWorld::CloseSession);
