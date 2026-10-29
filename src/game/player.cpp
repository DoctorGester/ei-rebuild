// A world player (CPlayer): constructor and destructor,
// Create / Destroy, Clear, the link and connection, the given items and the units. See player_messages.h
// and docs/notes/objects.md ("Players").
#include "item.h"
#include "net_link.h"
#include "party.h"
#include "player_messages.h"
#include "unit.h"
#include "world.h"
#include "world_effect.h"

#include "lib/crt.h"
#include "lib/str.h"

#include <list>
#include <string>

CPlayer::CPlayer() {
    link_ = nullptr;
    maxObjectId_ = 0;
    maxUnitId_ = 0;
    maxTimedId_ = 0;
    partyId_ = 0;
    connection_ = nullptr;
    logTraffic_ = 0;
    Party()->Create();
    recharging_.clear();
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
    sessionKind_ = 0;
    lastScreen_ = 0;
}

CPlayer::~CPlayer() {
    DestroyPlayer();
    Party()->Destroy();
    recharging_.clear();
    items_.clear();
    if (link_ != nullptr) link_->Release();
}

// The message dispatchers' fatal error: `format` ("... prevID=%x
// curID=%x") with the previous and the current message type (their low bytes; the callers set
// only DL) raises FatalError (patched by Rebirth: through its address); its result.
int PlayerMessageError(const char* format, std::uint32_t previous, std::uint32_t type) {
    std::string text;
    text = str::Format(format, previous & 0xFF, type & 0xFF);
    return game::FatalError(text);
}

const char* CPlayer::ClassName() { return "CPlayer"; }
ClassId CPlayer::GetClassId() { return ClassId::Player; }

void CPlayer::CreatePlayer() {
    DestroyPlayer();
}

void CPlayer::DestroyPlayer() {
    recharging_.clear();
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
    sessionKind_ = 0;
    lastScreen_ = 0;
}

void CPlayer::Clear() {
    for (auto node = items_.begin(); node != items_.end();) {
        CEngineObject* item = *node;
        ++node;
        item->Release();
    }
    items_.clear();
    ReleaseEach(knownTimed_);
    maxTimedId_ = 0;
    ReleaseEach(knownObjects_);
    maxObjectId_ = 0;
    // Each unit leaves (its SetPlayer erases it here) until the map is empty or its first entry
    // holds no unit.
    while (!units_.empty() && units_.begin()->second != nullptr) units_.begin()->second->SetPlayer(nullptr);
    units_.clear();
    maxUnitId_ = 0;
    recharging_.clear();
    if (connection_ != nullptr) connection_->ForgetSentObjects();
}

void CPlayer::SetConnection(CPlayerConnection* connection) {
    if (connection_ != nullptr) {
        connection_->player_ = nullptr;
        connection_->Release();
    }
    connection_ = connection;
    if (connection != nullptr) {
        connection->player_ = this;
        connection_->AddRef();
    }
}

void CPlayer::SetPlayerId(int id) { playerId_ = id; }

namespace {

// The link released the way the player's code inlines it: the field cleared first.
void DropLink(netlink::NetLink*& link) {
    netlink::NetLink* old = link;
    link = nullptr;
    if (old != nullptr) old->Release();
}

// The data word every item class keeps (a quest item's id).
std::uint32_t ItemWord4C(CItem* item) { return AsQuest(item)->questItemId_; }

// The first given item `match` accepts: the connection's lookup, its node removed, the item
// released. 1, or 0 when there is none.
template <typename Match>
bool TakeFirst(CPlayer* player, Match match) {
    auto node = player->items_.begin();
    while (node != player->items_.end() && !match(*node)) ++node;
    if (node == player->items_.end()) return false;
    CEngineObject* item = *node;
    player->connection_->ItemTaken(item);
    player->items_.erase(node);
    item->Release();
    return true;
}

} // namespace

void CPlayer::Connect(netlink::NetLink* link) {
    if (link_ != nullptr) connection_->Leave();
    Clear();
    if (link_ != nullptr) link_->Disconnect();
    DropLink(link_);
    link_ = link;
    if (link != nullptr) link->AddRef();
    connection_->ResetState();
}

void CPlayer::Disconnect() {
    if (link_ != nullptr) connection_->Leave();
    Clear();
    if (link_ != nullptr) link_->Disconnect();
    DropLink(link_);
}

void CPlayer::ForgetKnownObjects() {
    ReleaseEach(knownTimed_);
    maxTimedId_ = 0;
    ReleaseEach(knownObjects_);
    maxObjectId_ = 0;
    if (connection_ != nullptr) connection_->ForgetSentObjects();
    dirty_ = kPlayerDirtyAll;
}

bool CPlayer::IsLeader(const CEngineObject* unit) {
    const std::vector<PartyUnitRecord>& members = Party()->members_;
    if (std::ssize(members) == 0 || unit == nullptr) return false;
    return static_cast<std::uint32_t>(unit->id_) == members[0].unitId;
}

void CPlayer::GiveItems(const std::list<CItem*>* items, int flag) {
    connection_->ItemsGiven(items, flag);
    for (auto node = items->begin(); node != items->end();) {
        CItem* item = *node;
        ++node;
        item->AddRef();
        items_.push_back(item);
    }
}

void CPlayer::TakeAllItems() {
    connection_->ItemsTaken();
    while (!items_.empty()) {
        CItem* item = items_.front();
        items_.pop_front();
        item->Release();
    }
    items_.clear();
}

bool CPlayer::HasLoot(int kind) {
    for (auto node = items_.begin(); node != items_.end(); ++node)
        if ((*node)->Kind() == ItemKind::Quest && ItemWord4C(*node) == static_cast<std::uint32_t>(kind)) return true;
    for (auto node = units_.begin(); node != units_.end() && node->second != nullptr; ++node) {
        CUnit* unit = node->second;
        for (CItem* item : unit->carried)
            if (ItemWord4C(item) == static_cast<std::uint32_t>(kind)) return true;
    }
    return false;
}

bool CPlayer::TakeLoot(int kind) {
    return TakeFirst(this, [kind](CItem* item) {
        return item->Kind() == ItemKind::Quest && ItemWord4C(item) == static_cast<std::uint32_t>(kind);
    });
}

bool CPlayer::TakeItem(int id) {
    return TakeFirst(this, [id](CItem* item) { return item->id_ == id; });
}

void CPlayer::AddUnit(CUnit* unit) {
    if (unit == nullptr) return;
    const int id = unit->id_;
    if (units_.contains(id)) return;
    unit->AddRef();
    if (maxUnitId_ < id) maxUnitId_ = id;
    units_[id] = unit;
    connection_->UnitChanged(unit);
}

void CPlayer::RemoveUnit(CUnit* unit) {
    if (unit == nullptr) return;
    auto node = units_.find(unit->id_);
    if (node == units_.end()) return;
    units_.erase(node);
    unit->Release();
    connection_->UnitChanged(unit);
}

void CPlayer::ItemUsed(CEngineObject* item) {
    std::uint32_t id = static_cast<std::uint32_t>(item->id_);
    if (!lib::Contains(recharging_, id)) recharging_.push_back(id);
    connection_->ItemUsed(item);
}

CParty* CPlayer::AssignParty(CParty* source) {
    recharging_.clear();
    return Party()->Assign(source);
}

void CPlayer::ReleaseTamedUnits() {
    // A unit that leaves is erased here, so the walk steps on first. (The game's walk went on from the erased
    // node, reading freed memory.)
    for (auto node = units_.begin(); node != units_.end() && node->second != nullptr;) {
        CUnit* unit = node->second;
        ++node;
        if (unit->Controller()->TamingStage() != 0) unit->SetPlayer(nullptr);
    }
}
