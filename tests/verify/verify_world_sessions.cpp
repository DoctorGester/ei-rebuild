// Checks for world_sessions.cpp and the managers' small slots in world_managers.cpp (A-U8).
// - Class names / ids (0055F310 / 20, 0055F360 / 70, 0055F3C0 / D0): pure, against the original.
// - Clones (CWorldHandle 0055F1A0, DriverSession 0055F550): both clone; the copies compared (the
//   helper's link; the session's lists element by element: holders the same object, drivers the
//   same object or, for a copy-on-share driver, clones of the same class), the original's copy
//   deleted afterwards (ours' deleting destructor; its references go with it).
// - Shutdowns, destructors, the base's destructor, OpenSession: ours only, then the state checked.
// Reached: the helper at every world setup / teardown (new game, exit: DestroyManagers), the
// managers' destructors at teardown; the session only with a network game (SetGameResult 2 / 3, a
// client connecting; net_lobby.txt may reach it).
#include "verify.h"

#include "../../src/game/item.h"
#include "../../src/game/net_link.h"
#include "../../src/game/player_manager.h"
#include "../../src/game/world.h"

#include <cstdint>
#include <cstring>

using netlink::DriverNode;
using netlink::DriverSession;

// world_managers.cpp
const char* __fastcall PlayerManagerClassName(CPlayerManager*);
int __fastcall PlayerManagerClassId(CPlayerManager*);
void __fastcall PlayerManagerDestruct(CPlayerManager* self);
CPlayerManager* __fastcall PlayerManagerDelete(CPlayerManager* self, void*, unsigned flags);
const char* __fastcall EffectManagerClassName(CBaseManager*);
int __fastcall EffectManagerClassId(CBaseManager*);
void __fastcall EffectManagerDestruct(CBaseManager* self);
CBaseManager* __fastcall EffectManagerDelete(CBaseManager* self, void*, unsigned flags);
BOOL __fastcall EffectManagerCreate(CBaseManager* self);
const char* __fastcall ItemManagerClassName(CItemManager*);
int __fastcall ItemManagerClassId(CItemManager*);
void __fastcall ItemManagerDestruct(CItemManager* self);
CItemManager* __fastcall ItemManagerDelete(CItemManager* self, void*, unsigned flags);

namespace {

void* s_unused;

std::uintptr_t VtableOf(const void* object) {
    std::uintptr_t v;
    std::memcpy(&v, object, sizeof(v));
    return v;
}

// ---- Class names and ids: MOV EAX,imm32 (5) ------------------------------------------------------

template <typename Self> using NameFn = const char*(__fastcall*)(Self*);
template <typename Self> using IdFn = int(__fastcall*)(Self*);

NameFn<CPlayerManager> s_playerName;
NameFn<CBaseManager> s_effectName;
NameFn<CItemManager> s_itemName;
IdFn<CPlayerManager> s_playerId;
IdFn<CBaseManager> s_effectId;
IdFn<CItemManager> s_itemId;

const char* __fastcall PlayerNameVerify(CPlayerManager* self) {
    const char* e = s_playerName(self);
    ei::verify::Check("PlayerManagerClassName", e, PlayerManagerClassName(self), "");
    return e;
}
int __fastcall PlayerIdVerify(CPlayerManager* self) {
    int e = s_playerId(self);
    ei::verify::Check("PlayerManagerClassId", e, PlayerManagerClassId(self), "");
    return e;
}
const char* __fastcall EffectNameVerify(CBaseManager* self) {
    const char* e = s_effectName(self);
    ei::verify::Check("EffectManagerClassName", e, EffectManagerClassName(self), "");
    return e;
}
int __fastcall EffectIdVerify(CBaseManager* self) {
    int e = s_effectId(self);
    ei::verify::Check("EffectManagerClassId", e, EffectManagerClassId(self), "");
    return e;
}
const char* __fastcall ItemNameVerify(CItemManager* self) {
    const char* e = s_itemName(self);
    ei::verify::Check("ItemManagerClassName", e, ItemManagerClassName(self), "");
    return e;
}
int __fastcall ItemIdVerify(CItemManager* self) {
    int e = s_itemId(self);
    ei::verify::Check("ItemManagerClassId", e, ItemManagerClassId(self), "");
    return e;
}

// ---- The managers' destructors: ours only; the body leaves CEngineObject's root vtable ---------

void __fastcall PlayerDestructVerify(CPlayerManager* self) {
    PlayerManagerDestruct(self);
    ei::verify::Check("PlayerManagerDestruct", CEngineObject::kVtableAddress, VtableOf(self), "");
}
void* __fastcall PlayerDeleteVerify(CPlayerManager* self, void*, unsigned flags) {
    void* result = PlayerManagerDelete(self, nullptr, flags);
    ei::verify::Check("PlayerManagerDelete", static_cast<void*>(self), result, "");
    return result;
}
void __fastcall EffectDestructVerify(CBaseManager* self) {
    EffectManagerDestruct(self);
    ei::verify::Check("EffectManagerDestruct", CEngineObject::kVtableAddress, VtableOf(self), "");
}
void* __fastcall EffectDeleteVerify(CBaseManager* self, void*, unsigned flags) {
    void* result = EffectManagerDelete(self, nullptr, flags);
    ei::verify::Check("EffectManagerDelete", static_cast<void*>(self), result, "");
    return result;
}
// 0067E150 (A-H24): ours only (Create empties the tables): 1, the tables empty.
BOOL __fastcall EffectCreateVerify(CBaseManager* self) {
    BOOL result = EffectManagerCreate(self);
    ei::verify::Check("EffectManagerCreate", true, result == 1 && self->objects_.Size() == 0 && self->objectList_.count == 0,
                      "%p", static_cast<void*>(self));
    return result;
}
void __fastcall ItemDestructVerify(CItemManager* self) {
    ItemManagerDestruct(self);
    ei::verify::Check("ItemManagerDestruct", CEngineObject::kVtableAddress, VtableOf(self), "");
}
void* __fastcall ItemDeleteVerify(CItemManager* self, void*, unsigned flags) {
    void* result = ItemManagerDelete(self, nullptr, flags);
    ei::verify::Check("ItemManagerDelete", static_cast<void*>(self), result, "");
    return result;
}

// ---- The player sources' base ---------------------------------------------------------------------

void __fastcall SourceDestructVerify(CPlayerSource* self) {
    self->DestructBase();
    ei::verify::Check("PlayerSourceDestructBase", kRefObjectRootVtable, VtableOf(self), "");
}
void* __fastcall SourceDeleteVerify(CPlayerSource* self, void*, unsigned flags) {
    void* result = self->DeletingDestructBase(flags);
    ei::verify::Check("PlayerSourceDeleteBase", static_cast<void*>(self), result, "");
    return result;
}

// ---- CWorldHandle ---------------------------------------------------------------------------------

// 0055F1A0: PUSH ESI (1), MOV ESI,ECX (2), PUSH 0xC (2).
CWorldHandle*(__fastcall* s_handleClone)(CWorldHandle*);
CWorldHandle* __fastcall HandleCloneVerify(CWorldHandle* self) {
    CWorldHandle* expected = s_handleClone(self);
    CWorldHandle* actual = self->CWorldHandle::Clone();
    if (expected && actual)
        ei::verify::Check("CWorldHandle::Clone", true,
                          std::memcmp(expected, actual, sizeof(CWorldHandle)) == 0, "");
    if (expected) expected->DeletingDestruct(1);
    return actual;
}

int __fastcall HandleShutdownVerify(CWorldHandle* self) {
    std::int16_t refs = self->refs_;
    int result = self->CWorldHandle::OnShutdown();
    ei::verify::Check("CWorldHandle::OnShutdown", true,
                      VtableOf(self) == CWorldHandle::kVtableAddress && self->refs_ == refs && !self->pendingLink_, "");
    return result;
}

// The client's handle (A-W14). Clone 005626B0 starts with PUSH 8; CALL (no 5 clean bytes): ours
// only, a new handle with no references. OnShutdown 005626D0 (TEST ECX,ECX; MOV [ECX],imm32: 8
// bytes): the original on a scratch copy. TakeNew 00562700: ours only, the list empty afterwards.
// The client's Segment asks the handle every tick only through slots 4 / 5; slot 6 and these two
// have no known caller in a single-player run.
CClientHandle* __fastcall ClientHandleCloneVerify(CClientHandle* self) {
    CClientHandle* copy = self->CClientHandle::Clone();
    bool ok = copy && copy != self && VtableOf(copy) == CClientHandle::kVtableAddress && copy->refs_ == 0 &&
              copy->flags6_ == 0 && copy->flags7_ == 0;
    ei::verify::Check("CClientHandle::Clone", true, ok, "handle %p", self);
    return copy;
}
int(__fastcall* s_clientHandleShutdown)(CClientHandle*);
int __fastcall ClientHandleShutdownVerify(CClientHandle* self) {
    std::uint8_t scratch[sizeof(CClientHandle)];
    std::memcpy(scratch, self, sizeof(scratch));
    s_clientHandleShutdown(reinterpret_cast<CClientHandle*>(scratch));
    int result = self->CClientHandle::OnShutdown();
    ei::verify::Check("CClientHandle::OnShutdown", true, std::memcmp(scratch, self, sizeof(scratch)) == 0, "");
    return result;
}
void __fastcall ClientHandleTakeNewVerify(CClientHandle* self, void*, void* joined) {
    auto* links = static_cast<mfc::CList<RefPtr>*>(joined);
    int before = links->count;
    self->CClientHandle::TakeNew(joined);
    bool ok = links->head == nullptr && links->tail == nullptr && links->count == 0 && links->blocks == nullptr;
    ei::verify::Check("CClientHandle::TakeNew", true, ok, "%d links", before);
}

void __fastcall HandleDestructVerify(CWorldHandle* self) {
    self->Destruct();
    ei::verify::Check("CWorldHandle::Destruct", kRefObjectRootVtable, VtableOf(self), "");
}
void* __fastcall HandleDeleteVerify(CWorldHandle* self, void*, unsigned flags) {
    void* result = self->DeletingDestruct(flags);
    ei::verify::Check("CWorldHandle::DeletingDestruct", static_cast<void*>(self), result, "");
    return result;
}

// ---- DriverSession --------------------------------------------------------------------------------

bool Empty(const DriverNode* sentinel) { return sentinel->next == sentinel && sentinel->prev == sentinel; }

bool SameElements(const DriverNode* a, const DriverNode* b, bool cloned) {
    const DriverNode* p = a->next;
    const DriverNode* q = b->next;
    for (; p != a && q != b; p = p->next, q = q->next) {
        const CRefObject* x = p->value.ptr;
        const CRefObject* y = q->value.ptr;
        if (x == y) continue;
        if (!cloned || !x || !y || VtableOf(x) != VtableOf(y)) return false;
    }
    return p == a && q == b;
}

// 0055F550: PUSH -1 (2), PUSH imm32 (5).
DriverSession*(__fastcall* s_sessionClone)(DriverSession*);
DriverSession* __fastcall SessionCloneVerify(DriverSession* self) {
    DriverSession* expected = s_sessionClone(self);
    DriverSession* actual = self->DriverSession::Clone();
    if (expected && actual) {
        bool ok = std::memcmp(expected, actual, 8) == 0 && SameElements(expected->drivers_, actual->drivers_, true) &&
                  SameElements(expected->holders_, actual->holders_, false);
        ei::verify::Check("DriverSession::Clone", true, ok, "");
    }
    if (expected) expected->DeletingDestruct(1);
    return actual;
}

int __fastcall SessionShutdownVerify(DriverSession* self) {
    std::int16_t refs = self->refs_;
    int result = self->DriverSession::OnShutdown();
    ei::verify::Check("DriverSession::OnShutdown", true,
                      VtableOf(self) == DriverSession::kVtableAddress && self->refs_ == refs && Empty(self->drivers_) &&
                          Empty(self->holders_),
                      "");
    return result;
}

void __fastcall SessionDestructVerify(DriverSession* self) {
    self->Destruct();
    ei::verify::Check("DriverSession::Destruct", kRefObjectRootVtable, VtableOf(self), "");
}
void* __fastcall SessionDeleteVerify(DriverSession* self, void*, unsigned flags) {
    void* result = self->DeletingDestruct(flags);
    ei::verify::Check("DriverSession::DeletingDestruct", static_cast<void*>(self), result, "");
    return result;
}

// ---- CWorld::OpenSession: ours only; a fresh, empty session shared into +0x60 -------------------

void __fastcall OpenSessionVerify(CWorld* self) {
    self->OpenSession();
    auto* session = static_cast<DriverSession*>(self->unknown60_);
    bool ok = session && VtableOf(session) == DriverSession::kVtableAddress && session->refs_ >= 1 &&
              (session->flags6_ & CRefObject::kCopyOnShare) && Empty(session->drivers_) && Empty(session->holders_);
    ei::verify::Check("CWorld::OpenSession", true, ok, "");
}

} // namespace

EI_VERIFY(0x0055F310, PlayerNameVerify, 5, s_playerName);
EI_VERIFY(0x0055F320, PlayerIdVerify, 5, s_playerId);
EI_VERIFY(0x0055F360, EffectNameVerify, 5, s_effectName);
EI_VERIFY(0x0055F370, EffectIdVerify, 5, s_effectId);
EI_VERIFY(0x0055F3C0, ItemNameVerify, 5, s_itemName);
EI_VERIFY(0x0055F3D0, ItemIdVerify, 5, s_itemId);
EI_VERIFY(0x0055F350, PlayerDestructVerify, 0, s_unused);
EI_VERIFY(0x0055F330, PlayerDeleteVerify, 0, s_unused);
EI_VERIFY(0x0055F3A0, EffectDestructVerify, 0, s_unused);
EI_VERIFY(0x0055F380, EffectDeleteVerify, 0, s_unused);
EI_VERIFY(0x0067E150, EffectCreateVerify, 0, s_unused);
EI_VERIFY(0x0055F400, ItemDestructVerify, 0, s_unused);
EI_VERIFY(0x0055F3E0, ItemDeleteVerify, 0, s_unused);
EI_VERIFY(0x0055F2E0, SourceDestructVerify, 0, s_unused);
EI_VERIFY(0x0055F2F0, SourceDeleteVerify, 0, s_unused);
EI_VERIFY(0x0055F1A0, HandleCloneVerify, 5, s_handleClone);
EI_VERIFY(0x0055F1E0, HandleShutdownVerify, 0, s_unused);
EI_VERIFY(0x0055F280, HandleDestructVerify, 0, s_unused);
EI_VERIFY(0x005626B0, ClientHandleCloneVerify, 0, s_unused);
EI_VERIFY(0x005626D0, ClientHandleShutdownVerify, 8, s_clientHandleShutdown);
EI_VERIFY(0x00562700, ClientHandleTakeNewVerify, 0, s_unused);
EI_VERIFY(0x0055F260, HandleDeleteVerify, 0, s_unused);
EI_VERIFY(0x0055F550, SessionCloneVerify, 7, s_sessionClone);
EI_VERIFY(0x0055F6B0, SessionShutdownVerify, 0, s_unused);
EI_VERIFY(0x0055F7B0, SessionDestructVerify, 0, s_unused);
EI_VERIFY(0x0055F790, SessionDeleteVerify, 0, s_unused);
EI_VERIFY(0x0055F410, OpenSessionVerify, 0, s_unused);
