// A world player (CPlayer, vtable 00743104; 00664BF0-00665D20): constructor and destructor,
// Create / Destroy, Clear and the connection. See player_messages.h and docs/notes/items_spells.md
// ("Players").
#include "player_messages.h"

#include "net_link.h"
#include "originals_world.h"
#include "party.h"
#include "unit.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "ei/vcall.h"

namespace {

// The vtable of the recharge list (an MFC CList instantiation; Serialize 00666050).
constexpr std::uintptr_t kRechargeListVtable = std::uintptr_t(0x00743138);
// The given items' list is a CPtrList (vtable 0073BE80, the pointer lists the units fill).
constexpr std::uintptr_t kItemListVtable = std::uintptr_t(0x0073BE80);

template <typename T>
void InitList(mfc::CList<T>& list, std::uintptr_t vtable) {
    list.vtable = reinterpret_cast<void*>(vtable);
    list.tail = nullptr;
    list.count = 0;
    list.head = nullptr;
    list.free = nullptr;
    list.blocks = nullptr;
    list.blockSize = 10;
}

} // namespace

CPlayer* CPlayer::Construct() {
    ClearFields();
    ei::SetVtable(this, CEngineObject::kVtableAddress);
    link_ = nullptr;
    knownObjects_.InitBuckets(100);
    maxObjectId_ = 0;
    units_.InitBuckets(100);
    maxUnitId_ = 0;
    knownTimed_.InitBuckets(100);
    maxTimedId_ = 0;
    Party()->Construct();
    InitList(items_, kItemListVtable);
    unknownFC_ = 0;
    InitList(objects_, kRechargeListVtable);
    ei::SetVtable(this, kVtableAddress);
    connection_ = nullptr;
    logTraffic_ = 0;
    Party()->Create();
    objects_.RemoveAll();
    Clear();
    Party()->Destroy();
    Party()->Create();
    unknown_130 = 0;
    playerId_ = -1;
    unknown_108 = 0;
    if (link_ != nullptr) link_->Disconnect();
    // (The original lets RefPtr::Assign(null) release the link, which is null here.)
    link_ = nullptr;
    SetConnection(nullptr);
    unknown_110 = -1;
    sessionKind100_ = 0;
    unknown_104 = 0;
    return this;
}
EI_REPLACE_METHOD(0x00664BF0, CPlayer::Construct);

void CPlayer::DestructBody() {
    ei::SetVtable(this, kVtableAddress);
    DestroyPlayer();
    Party()->Destroy();
    objects_.vtable = reinterpret_cast<void*>(kRechargeListVtable);
    objects_.RemoveAll();
    items_.vtable = reinterpret_cast<void*>(kItemListVtable);
    items_.RemoveAll();
    Party()->DestructBody();
    knownTimed_.Destroy();
    units_.Destroy();
    knownObjects_.Destroy();
    if (link_ != nullptr) link_->Release();
    DestructBase();
}
EI_REPLACE_METHOD(0x00664E20, CPlayer::DestructBody);

CPlayer* CPlayer::DeletingDestruct(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00664E00, CPlayer::DeletingDestruct);

// 00664DE0 / 00664DF0: slots 11 and 12.
const char* __fastcall PlayerClassName(CPlayer*) { return reinterpret_cast<const char*>(0x00794CA4); }
EI_REPLACE(0x00664DE0, PlayerClassName);
int __fastcall PlayerClassId(CPlayer*) { return 0xF; }
EI_REPLACE(0x00664DF0, PlayerClassId);

BOOL CPlayer::CreatePlayer() {
    DestroyPlayer();
    return 1;
}
EI_REPLACE_METHOD(0x00665010, CPlayer::CreatePlayer);

void CPlayer::DestroyPlayer() {
    objects_.RemoveAll();
    Clear();
    Party()->Destroy();
    Party()->Create();
    unknown_130 = 0;
    playerId_ = -1;
    unknown_108 = 0;
    if (link_ != nullptr) link_->Disconnect();
    netlink::NetLink* link = link_;
    link_ = nullptr;
    if (link != nullptr) link->Release();
    if (connection_ != nullptr) {
        connection_->player_ = nullptr;
        connection_->Release();
    }
    unknown_110 = -1;
    connection_ = nullptr;
    sessionKind100_ = 0;
    unknown_104 = 0;
}
EI_REPLACE_METHOD(0x006650E0, CPlayer::DestroyPlayer);
EI_REPLACE_METHOD(0x00665380, CPlayer::DestroyPlayer);

void CPlayer::Clear() {
    for (auto* node = items_.head; node != nullptr;) {
        CEngineObject* item = node->data;
        node = node->next;
        item->Release();
    }
    items_.RemoveAll();
    ObjectMapClearOriginal(&knownTimed_);
    ObjectMapClearOriginal(&knownObjects_);
    // Each unit leaves (its SetPlayer erases it here) until the map is empty or its first entry
    // holds no unit.
    for (ObjectMap::Node* first = units_.First(); first != nullptr && first->value != nullptr; first = units_.First())
        static_cast<CUnit*>(first->value)->SetPlayer(nullptr);
    units_.Clear();
    maxUnitId_ = 0;
    objects_.RemoveAll();
    if (connection_ != nullptr) connection_->ForgetSentObjects();
}
EI_REPLACE_METHOD(0x00665200, CPlayer::Clear);

void CPlayer::SetConnection(CPlayerConnection* connection) {
    if (connection_ != nullptr) {
        connection_->player_ = nullptr;
        connection_->Release();
    }
    connection_ = connection;
    if (connection != nullptr) {
        connection->player_ = this;
        ++connection_->refCount_;
    }
}
EI_REPLACE_METHOD(0x00665950, CPlayer::SetConnection);

void CPlayer::SetPlayerId(int id) { playerId_ = id; }
EI_REPLACE_METHOD(0x00665A60, CPlayer::SetPlayerId);
