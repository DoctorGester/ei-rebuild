// A world player (CPlayer, vtable 00743104; 00664BF0-00665D20): constructor and destructor,
// Create / Destroy, Clear, the link and connection, the given items and the units. See player_messages.h
// and docs/notes/objects.md ("Players").
#include "player_messages.h"

#include "item.h"
#include "net_link.h"
#include "originals_engine.h"
#include "originals_world.h"
#include "party.h"
#include "unit.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "game_data.h"

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
    partyId_ = 0;
    InitList(objects_, kRechargeListVtable);
    ei::SetVtable(this, kVtableAddress);
    connection_ = nullptr;
    logTraffic_ = 0;
    Party()->Create();
    objects_.RemoveAll();
    Clear();
    Party()->Destroy();
    Party()->Create();
    dirty_ = 0;
    playerId_ = -1;
    godMode_ = 0;
    if (link_ != nullptr) link_->Disconnect();
    // (The original lets RefPtr::Assign(null) release the link, which is null here.)
    link_ = nullptr;
    SetConnection(nullptr);
    lastTick_ = -1;
    sessionKind100_ = 0;
    lastScreen_ = 0;
    return this;
}
EI_REPLACE_METHOD(0x00664BF0, CPlayer::Construct);

void CPlayer::DestructBody() {
    ei::SetVtable(this, kVtableAddress);
    DestroyPlayer();
    Party()->Destroy();
    objects_.vtable = ei::GamePointer<void>(kRechargeListVtable);
    objects_.RemoveAll();
    items_.vtable = ei::GamePointer<void>(kItemListVtable);
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

// 00664B70 (__fastcall, RET 4). The message dispatchers' fatal error: `format` ("... prevID=%x
// curID=%x") with the previous and the current message type (their low bytes; the callers set
// only DL) raises FatalError (00480080, patched by Rebirth: through its address); its result.
int __fastcall PlayerMessageError(const char* format, std::uint32_t previous, std::uint32_t type) {
    mfc::CString text;
    text.Format(format, previous & 0xFF, type & 0xFF);
    return game::FatalError(text);
}
EI_REPLACE(0x00664B70, PlayerMessageError);

// 00664DE0 / 00664DF0: slots 11 and 12.
const char* __fastcall PlayerClassName(CPlayer*) { return "CPlayer"; }
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
    dirty_ = 0;
    playerId_ = -1;
    godMode_ = 0;
    if (link_ != nullptr) link_->Disconnect();
    netlink::NetLink* link = link_;
    link_ = nullptr;
    if (link != nullptr) link->Release();
    if (connection_ != nullptr) {
        connection_->player_ = nullptr;
        connection_->Release();
    }
    lastTick_ = -1;
    connection_ = nullptr;
    sessionKind100_ = 0;
    lastScreen_ = 0;
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

namespace {

// The link released the way the player's code inlines it: the field cleared first.
void DropLink(netlink::NetLink*& link) {
    netlink::NetLink* old = link;
    link = nullptr;
    if (old != nullptr) old->Release();
}

// The data word every item class keeps at +0x4C (a quest item's id).
std::uint32_t ItemWord4C(CEngineObject* item) { return static_cast<CItemQuest*>(item)->questItemId_; }

} // namespace

void CPlayer::Connect(netlink::NetLink* link) {
    if (link_ != nullptr) connection_->Leave();
    Clear();
    if (link_ != nullptr) link_->Disconnect();
    DropLink(link_);
    link_ = link;
    if (link != nullptr) ++link->refs_;
    connection_->ResetState();
}
EI_REPLACE_METHOD(0x00665990, CPlayer::Connect);

void CPlayer::Disconnect() {
    if (link_ != nullptr) connection_->Leave();
    Clear();
    if (link_ != nullptr) link_->Disconnect();
    DropLink(link_);
}
EI_REPLACE_METHOD(0x00665A10, CPlayer::Disconnect);

void CPlayer::ForgetKnownObjects() {
    ObjectMapClearOriginal(&knownTimed_);
    ObjectMapClearOriginal(&knownObjects_);
    if (connection_ != nullptr) connection_->ForgetSentObjects();
    dirty_ = -1;
}
EI_REPLACE_METHOD(0x006651D0, CPlayer::ForgetKnownObjects);

BOOL CPlayer::IsLeader(const CEngineObject* unit) {
    const PartyMemberArray& members = Party()->members_;
    if (members.size == 0 || unit == nullptr) return 0;
    return static_cast<std::uint32_t>(unit->id_) == members.data[0].unitId;
}
EI_REPLACE_METHOD(0x00665440, CPlayer::IsLeader);

void CPlayer::GiveItems(const mfc::CTypedPtrList<CEngineObject>* items, int flag) {
    connection_->ItemsGiven(items, flag);
    for (auto* node = items->head; node != nullptr;) {
        CEngineObject* item = node->data;
        node = node->next;
        ++item->refCount_;
        items_.AddTail(item);
    }
}
EI_REPLACE_METHOD(0x00665470, CPlayer::GiveItems);

void CPlayer::TakeAllItems() {
    connection_->ItemsTaken();
    while (items_.count != 0) items_.RemoveHead()->Release();
    items_.RemoveAll();
}
EI_REPLACE_METHOD(0x00665550, CPlayer::TakeAllItems);

BOOL CPlayer::HasLoot(int kind) {
    for (auto* node = items_.head; node != nullptr; node = node->next)
        if (node->data->type_ == kItemQuest && ItemWord4C(node->data) == static_cast<std::uint32_t>(kind)) return 1;
    for (ObjectMap::Node* node = units_.First(); node != nullptr && node->value != nullptr; node = units_.After(node)) {
        auto* unit = static_cast<CUnit*>(node->value);
        for (int i = 0; i < unit->carriedCount; ++i)
            if (ItemWord4C(unit->carried[i]) == static_cast<std::uint32_t>(kind)) return 1;
    }
    return 0;
}
EI_REPLACE_METHOD(0x00665640, CPlayer::HasLoot);

namespace {

// The first given item `match` accepts: the connection's slot 19, its node removed, the item
// released. 1, or 0 when there is none.
template <typename Match>
BOOL TakeFirst(CPlayer* player, Match match) {
    auto* node = player->items_.head;
    while (node != nullptr && !match(node->data)) node = node->next;
    if (node == nullptr) return 0;
    CEngineObject* item = node->data;
    player->connection_->ItemTaken(item);
    player->items_.RemoveAt(node);
    item->Release();
    return 1;
}

} // namespace

BOOL CPlayer::TakeLoot(int kind) {
    return TakeFirst(this, [kind](CEngineObject* item) {
        return item->type_ == kItemQuest && ItemWord4C(item) == static_cast<std::uint32_t>(kind);
    });
}
EI_REPLACE_METHOD(0x00665780, CPlayer::TakeLoot);

BOOL CPlayer::TakeItem(int id) {
    return TakeFirst(this, [id](CEngineObject* item) { return item->id_ == id; });
}
EI_REPLACE_METHOD(0x00665870, CPlayer::TakeItem);

void CPlayer::AddUnit(CUnit* unit) {
    if (unit == nullptr) return;
    const int id = unit->id_;
    if (units_.Find(id) != nullptr) return;
    ++unit->refCount_;
    if (maxUnitId_ < id) maxUnitId_ = id;
    units_[id] = unit;
    connection_->UnitChanged(unit);
}
EI_REPLACE_METHOD(0x00665A70, CPlayer::AddUnit);

void CPlayer::RemoveUnit(CUnit* unit) {
    if (unit == nullptr) return;
    ObjectMap::Node* node = units_.Find(unit->id_);
    if (node == nullptr) return;
    units_.Erase(node);
    unit->Release();
    connection_->UnitChanged(unit);
}
EI_REPLACE_METHOD(0x00665B50, CPlayer::RemoveUnit);

void CPlayer::ItemUsed(CEngineObject* item) {
    void* id = reinterpret_cast<void*>(static_cast<std::intptr_t>(item->id_));
    if (objects_.Find(id) == nullptr) objects_.AddTail(id);
    connection_->ItemUsed(item);
}
EI_REPLACE_METHOD(0x00665BE0, CPlayer::ItemUsed);

CParty* CPlayer::AssignParty(CParty* source) {
    objects_.RemoveAll();
    return Party()->Assign(source);
}
EI_REPLACE_METHOD(0x00665CC0, CPlayer::AssignParty);

void CPlayer::ReleaseTamedUnits() {
    // The walk goes on from the node of a unit that has just left (and been erased), as the
    // original's does.
    for (ObjectMap::Node* node = units_.First(); node != nullptr && node->value != nullptr; node = units_.After(node)) {
        auto* unit = static_cast<CUnit*>(node->value);
        if (unit->Controller()->TamingStage() != 0) unit->SetPlayer(nullptr);
    }
}
EI_REPLACE_METHOD(0x00665D20, CPlayer::ReleaseTamedUnits);
