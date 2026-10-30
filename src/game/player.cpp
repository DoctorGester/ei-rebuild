// A world player (Player): constructor and destructor,
// Create / Destroy, Clear, the link and connection, the given items and the units. See player_messages.h
// and docs/notes/objects.md ("Players").
#include "item.h"
#include "item_manager.h"
#include "net_link.h"
#include "party.h"
#include "player_messages.h"
#include "unit.h"
#include "world.h"
#include "world_effect.h"

#include "lib/crt.h"
#include "lib/str.h"

#include <algorithm>
#include <string>
#include <vector>

Player::Player() {
    link = nullptr;
    maxObjectId = 0;
    maxUnitId = 0;
    maxTimedId = 0;
    partyId = 0;
    connection = nullptr;
    logTraffic = 0;
    GetParty()->Create();
    recharging.clear();
    Clear();
    GetParty()->Destroy();
    GetParty()->Create();
    dirty = 0;
    playerId = -1;
    godMode = 0;
    if (link != nullptr) link->Disconnect();
    // (The original lets RefPtr::Assign(null) release the link, which is null here.)
    link = nullptr;
    SetConnection(nullptr);
    lastTick = -1;
    sessionKind = 0;
    lastScreen = 0;
}

Player::~Player() {
    DestroyPlayer();
    GetParty()->Destroy();
    recharging.clear();
    items.clear();
    if (link != nullptr) link->Release();
}

// The message dispatchers' fatal error: `format` ("... prevID=%x
// curID=%x") with the previous and the current message type (their low bytes; the callers set
// only DL) raises FatalError (patched by Rebirth: through its address); its result.
int PlayerMessageError(const char* format, std::uint32_t previous, std::uint32_t type) {
    std::string text;
    text = str::Format(format, previous & 0xFF, type & 0xFF);
    return game::FatalError(text);
}

const char* Player::ClassName() { return "CPlayer"; }
ClassId Player::GetClassId() { return ClassId::Player; }

void Player::CreatePlayer() {
    DestroyPlayer();
}

void Player::DestroyPlayer() {
    recharging.clear();
    Clear();
    GetParty()->Destroy();
    GetParty()->Create();
    dirty = 0;
    playerId = -1;
    godMode = 0;
    if (link != nullptr) link->Disconnect();
    netlink::NetLink* currentLink = link;
    link = nullptr;
    if (currentLink != nullptr) currentLink->Release();
    if (connection != nullptr) {
        connection->player = nullptr;
        connection->Release();
    }
    lastTick = -1;
    connection = nullptr;
    sessionKind = 0;
    lastScreen = 0;
}

void Player::Clear() {
    for (Item* item : items) item->Release();
    items.clear();
    ReleaseEach(knownTimed);
    maxTimedId = 0;
    ReleaseEach(knownObjects);
    maxObjectId = 0;
    // Each unit leaves (its SetPlayer erases it here) until the map is empty or its first entry
    // holds no unit.
    while (!units.empty() && units.begin()->second != nullptr) units.begin()->second->SetPlayer(nullptr);
    units.clear();
    maxUnitId = 0;
    recharging.clear();
    if (connection != nullptr) connection->ForgetSentObjects();
}

void Player::SetConnection(PlayerConnection* newConnection) {
    if (connection != nullptr) {
        connection->player = nullptr;
        connection->Release();
    }
    connection = newConnection;
    if (newConnection != nullptr) {
        newConnection->player = this;
        connection->AddRef();
    }
}

void Player::SetPlayerId(int newId) { playerId = newId; }

namespace {

// The link released the way the player's code inlines it: the field cleared first.
void DropLink(netlink::NetLink*& link) {
    netlink::NetLink* old = link;
    link = nullptr;
    if (old != nullptr) old->Release();
}

// The data word every item class keeps (a quest item's id).
std::uint32_t ItemWord4C(Item* item) { return AsQuest(item)->questItemId; }

// The first given item `match` accepts: the connection's lookup, the item out of the list and
// released. 1, or 0 when there is none.
template <typename Match>
bool TakeFirst(Player* player, Match match) {
    auto node = std::ranges::find_if(player->items, match);
    if (node == player->items.end()) return false;
    EngineObject* item = *node;
    player->connection->ItemTaken(item);
    player->items.erase(node);
    item->Release();
    return true;
}

} // namespace

void Player::Connect(netlink::NetLink* newLink) {
    if (link != nullptr) connection->Leave();
    Clear();
    if (link != nullptr) link->Disconnect();
    DropLink(link);
    link = newLink;
    if (newLink != nullptr) newLink->AddRef();
    connection->ResetState();
}

void Player::Disconnect() {
    if (link != nullptr) connection->Leave();
    Clear();
    if (link != nullptr) link->Disconnect();
    DropLink(link);
}

void Player::ForgetKnownObjects() {
    ReleaseEach(knownTimed);
    maxTimedId = 0;
    ReleaseEach(knownObjects);
    maxObjectId = 0;
    if (connection != nullptr) connection->ForgetSentObjects();
    dirty = kPlayerDirtyAll;
}

bool Player::IsLeader(const EngineObject* unit) {
    const std::vector<PartyUnitRecord>& members = GetParty()->members;
    if (std::ssize(members) == 0 || unit == nullptr) return false;
    return static_cast<std::uint32_t>(unit->id) == members[0].unitId;
}

void Player::GiveItems(const std::vector<Item*>* given, int flag) {
    connection->ItemsGiven(given, flag);
    for (Item* item : *given) {
        item->AddRef();
        items.push_back(item);
    }
}

void Player::TakeAllItems() {
    connection->ItemsTaken();
    for (Item* item : items) item->Release();
    items.clear();
}

bool Player::HasLoot(int kind) {
    for (Item* item : items)
        if (item->Kind() == ItemKind::Quest && ItemWord4C(item) == static_cast<std::uint32_t>(kind)) return true;
    for (auto node = units.begin(); node != units.end() && node->second != nullptr; ++node) {
        Unit* unit = node->second;
        for (Item* item : unit->carried)
            if (ItemWord4C(item) == static_cast<std::uint32_t>(kind)) return true;
    }
    return false;
}

bool Player::TakeLoot(int kind) {
    return TakeFirst(this, [kind](Item* item) {
        return item->Kind() == ItemKind::Quest && ItemWord4C(item) == static_cast<std::uint32_t>(kind);
    });
}

bool Player::TakeItem(int itemId) {
    return TakeFirst(this, [itemId](Item* item) { return item->id == itemId; });
}

void Player::AddUnit(Unit* unit) {
    if (unit == nullptr) return;
    const int unitId = unit->id;
    if (units.contains(unitId)) return;
    unit->AddRef();
    if (maxUnitId < unitId) maxUnitId = unitId;
    units[unitId] = unit;
    connection->UnitChanged(unit);
}

void Player::RemoveUnit(Unit* unit) {
    if (unit == nullptr) return;
    auto node = units.find(unit->id);
    if (node == units.end()) return;
    units.erase(node);
    unit->Release();
    connection->UnitChanged(unit);
}

void Player::ItemUsed(EngineObject* item) {
    std::uint32_t itemId = static_cast<std::uint32_t>(item->id);
    if (!lib::Contains(recharging, itemId)) recharging.push_back(itemId);
    connection->ItemUsed(item);
}

Party* Player::AssignParty(Party* source) {
    recharging.clear();
    return GetParty()->Assign(source);
}

void Player::ReleaseTamedUnits() {
    // A unit that leaves is erased here, so the walk steps on first. (The game's walk went on from the erased
    // node, reading freed memory.)
    for (auto node = units.begin(); node != units.end() && node->second != nullptr;) {
        Unit* unit = node->second;
        ++node;
        if (unit->Controller()->TamingStage() != 0) unit->SetPlayer(nullptr);
    }
}

void Player::Tick() {
    for (std::size_t i = 0; i < recharging.size();) {
        int objectId = static_cast<int>(recharging[i]);
        EngineObject* object = GetParty()->ItemFactory()->FindObject(objectId);
        if (object == nullptr || !object->RunTick()) recharging.erase(recharging.begin() + i);
        else ++i;
    }
    connection->TickDone();
}
