// PlayerServer: what the server sends a player every world update (WorldServer::SyncPlayer,
// world_sync.cpp): the map and timed objects that came into view, changed or left, and the flush
// of the collected messages to the player's link. See player_messages.h and docs/notes/objects.md.

#include "item_manager.h"
#include "player_messages.h"
#include "console.h"
#include "game_strings.h"

#include "base_manager.h"
#include "combat.h"
#include "map_object.h"
#include "net_link.h"
#include "party.h"
#include "random.h"
#include "resource_manager.h"
#include "script.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"
#include "world_effect.h"

#include "lib/crt.h"

#include "game_singletons.h"
#include "lib/nan_audit.h"

#include <cstring>
#include <vector>

namespace {

constexpr std::uint8_t kMessageObjectAdded = 0x40;
constexpr std::uint8_t kMessageObjectChanged = 0x41;
constexpr std::uint8_t kMessageObjectRemoved = 0x42;
constexpr std::uint8_t kMessageTimedAdded = 0x50;
constexpr std::uint8_t kMessageTimedChanged = 0x51;
constexpr std::uint8_t kMessageTimedRemoved = 0x52;
constexpr std::uint8_t kMessagePartyState = 0x60;
constexpr std::uint8_t kMessageParty = 0x80;
constexpr std::uint32_t kFlagDeltaPending = 2;   // flags: the object's delta is still to be written
constexpr std::uint32_t kRefreshMinimum = 900;   // updates until the party is sent again: 900..1800
constexpr std::uint32_t kRefreshSpread = 901;

constexpr std::uint8_t kMessageJoin = 0;    // the two types handled before the player has joined
constexpr std::uint8_t kMessageJoin8 = 8;
constexpr int kStateLeaving = 3;

// The receive errors' formats ("Serv pkt read error prevID=%x curID=%x", ...), read from the exe.
const char* TextReadPastEnd() { return "Serv pkt read error prevID=%x curID=%x"; }
const char* TextNoHandler() { return "Serv packet has no handler prevID=%x curID=%x"; }
const char* TextHandlerFailed() { return "Serv error during parsing command prevID=%x curID=%x"; }
const char* TextUnknownType() { return "Serv wrong packet ID prevID=%x curID=%x"; }

constexpr std::uint32_t kNoPartyCopy = 0xFFFFFFFF;   // PlayerServer::possessedId

// 2.0: a unit level at most this asks the client to refresh the party.
constexpr float kLowLevel = 2.0f;

// The texts of the traffic log (read from the exe).
const char* TextServerToClient() { return "server to client, size = "; }
const char* TextSqueezedTo() { return " squeezed to "; }

void WriteByte(MemoryStream* out, std::uint8_t value) { out->WriteValue(value); }
void WriteDword(MemoryStream* out, std::uint32_t value) { out->WriteValue(value); }

// Appends all of `from` (sought to 0) to `out`, the pending bits of `out` stored first.
void WriteStateStream(MemoryStream* out, MemoryStream* from) {
    out->EndBits();
    from->Seek(0);
    std::uint8_t* data = from->data.data();
    out->Write(data, from->Size());
}

// The insert of hash_map::operator[] inlined by the senders: the highest id kept, the object
// stored under its id and counted.
template <typename T>
void Keep(lib::HashMap<int, T*>& map, int& maxId, T* object) {
    int id = object->id;
    if (id > maxId) maxId = id;
    map[id] = object;
    object->AddRef();
}

// The erase the removers inline: the node of `id`, if any, dropped.
template <typename Map>
void Forget(Map& map, int id) {
    map.erase(id);
}

int WorldUpdateCount(Player* player) { return player->ServerWorld()->updateCount; }

// SendItemsGiven.
constexpr std::uint8_t kMessageItemStates = 0x05;
constexpr std::uint8_t kMessageItemsGiven = 0x06;
constexpr int kStateJoined = 1;
// SaveListedObjects on a typed list.
template <typename T>
void SaveListed(const std::vector<T*>* list, ByteStream* stream) {
    for (T* object : *list) SaveListedObject(object, stream);
    SaveListedObjectsEnd(stream);
}

// SyncPlayer's arrays (world_sync.cpp) hold one class per pass: the map objects, then the timed
// objects (Effect). The next element as that class; the loops read the data pointer once.
template <typename T>
T* NextAs(EngineObject* const*& next) {
    return static_cast<T*>(*next++);
}

} // namespace

void PlayerServer::SendItemsGiven(const std::vector<Item*>* items, int flag) {
    if (state != kStateJoined) return;
    ItemsAndSpells gathered;
    for (Item* item : *items) gathered.Add(item);
    MemoryStream states;
    SaveListed(&gathered.spells, &states);
    SaveListed(&gathered.items, &states);
    WriteByte(&out, kMessageItemStates);
    out.WriteStream(&states);
    WriteByte(&out, kMessageItemsGiven);
    WriteDword(&out, static_cast<std::uint32_t>(player->GetParty()->money));
    WriteByte(&out, static_cast<std::uint8_t>(flag));
    for (Item* item : *items) WriteDword(&out, static_cast<std::uint32_t>(item->id));
    WriteDword(&out, 0xFFFFFFFFu);
    gathered.items.clear();
    gathered.spells.clear();
}

int PlayerServer::Flush() {
    netlink::NetLink* link = player->link;
    if (link == nullptr) return 0;
    MemoryStream packed;
    out.CompressTo(&compressor, &packed);
    if (player->logTraffic != 0) {
        std::uint32_t packedSize = packed.size;
        std::uint32_t size = out.Size();
        console::EndLine(console::g_netLogOut << TextServerToClient() << size << TextSqueezedTo() << packedSize);
    }
    int sent = link->Send(&packed) ? 1 : 0;
    out.Resize(0);
    return sent;
}

int PlayerServer::SendObjectsAdded(const std::vector<EngineObject*>* objects) {
    EngineObject* const* next = objects->data();
    for (int i = 0; i < std::ssize(*objects); ++i) {
        auto* object = NextAs<MapObject>(next);
        bool isUnit = object->GetClassId() == ClassId::Unit;
        std::uint32_t header[4] = {static_cast<std::uint32_t>(object->id), static_cast<std::uint32_t>(object->type),
                                   object->prototypeId, object->flags};
        WriteByte(&out, kMessageObjectAdded);
        out.Write(header, sizeof(header));
        Vector3 scale;
        scale = object->scale;
        out.WriteValue(scale);
        out.WriteString(object->name2);
        out.WriteString(object->name);
        auto* server = object->ServerCompanion();
        out.WriteString(server->parentIname);
        out.WriteString(server->skinTexture);
        std::uint32_t count = static_cast<std::uint32_t>(server->strings.size());
        out.Write(&count, 2);
        for (const std::string& text : server->strings) out.WriteString(text);
        std::uint32_t prototype = 0;
        if (isUnit) {
            out.WriteString(object->GetName());
            prototype = static_cast<std::uint32_t>(UnitOrNull(object)->secondPrototype);
        } else {
            out.WriteString(game::kEmptyText);
        }
        WriteDword(&out, prototype);
        if (object->flags & kFlagDeltaPending) object->WriteChangedState();
        WriteStateStream(&out, &object->netState.header[0]);
        Player* currentPlayer = player;
        Keep(currentPlayer->knownObjects, currentPlayer->maxObjectId, object);
    }
    return 1;
}

int PlayerServer::SendObjectsChanged(const std::vector<EngineObject*>* objects) {
    EngineObject* const* next = objects->data();
    int stamp = WorldUpdateCount(player);
    for (int i = 0; i < std::ssize(*objects); ++i) {
        auto* object = NextAs<MapObject>(next);
        if (object->changeStamp != stamp) continue;
        if (object->flags & kFlagDeltaPending) object->WriteChangedState();
        WriteByte(&out, kMessageObjectChanged);
        WriteDword(&out, static_cast<std::uint32_t>(object->id));
        WriteStateStream(&out, &object->netState.header[1]);
    }
    return 1;
}

int PlayerServer::SendObjectsRemoved(const std::vector<EngineObject*>* objects) {
    EngineObject* const* next = objects->data();
    for (int i = 0; i < std::ssize(*objects); ++i) {
        EngineObject* object = *next++;
        int objectId = object->id;
        Forget(player->knownObjects, objectId);
        object->Release();
        WriteByte(&out, kMessageObjectRemoved);
        WriteDword(&out, static_cast<std::uint32_t>(objectId));
    }
    return 1;
}

int PlayerServer::SendTimedAdded(const std::vector<EngineObject*>* objects) {
    EngineObject* const* next = objects->data();
    for (int i = 0; i < std::ssize(*objects); ++i) {
        auto* effect = NextAs<Effect>(next);
        if (effect->flags & kFlagDeltaPending) effect->SendDelta();
        std::uint32_t header[2];
        header[1] = static_cast<std::uint32_t>(effect->GetClassId());
        header[0] = static_cast<std::uint32_t>(effect->id);
        WriteByte(&out, kMessageTimedAdded);
        out.Write(header, sizeof(header));
        WriteStateStream(&out, &effect->state.out[0]);
        Player* currentPlayer = player;
        Keep(currentPlayer->knownTimed, currentPlayer->maxTimedId, effect);
    }
    return 1;
}

int PlayerServer::SendTimedChanged(const std::vector<EngineObject*>* objects) {
    EngineObject* const* next = objects->data();
    int stamp = WorldUpdateCount(player);
    for (int i = 0; i < std::ssize(*objects); ++i) {
        auto* effect = NextAs<Effect>(next);
        if (effect->changeStamp != stamp) continue;
        if (effect->flags & kFlagDeltaPending) effect->SendDelta();
        WriteByte(&out, kMessageTimedChanged);
        WriteDword(&out, static_cast<std::uint32_t>(effect->id));
        WriteStateStream(&out, &effect->state.out[1]);
    }
    return 1;
}

int PlayerServer::SendTimedRemoved(const std::vector<EngineObject*>* objects) {
    EngineObject* const* next = objects->data();
    for (int i = 0; i < std::ssize(*objects); ++i) {
        EngineObject* object = *next++;
        WriteByte(&out, kMessageTimedRemoved);
        WriteDword(&out, static_cast<std::uint32_t>(object->id));
        Forget(player->knownTimed, object->id);
        object->Release();
    }
    return 1;
}

// The original calls SendObject14 of this object for each item.
void PlayerServer::SendItems() {
    for (unsigned objectId : player->recharging) {
        ResourceManager* items = player->GetParty()->ItemFactory();
        if (EngineObject* item = items->FindObject(objectId)) SendObject14(item);
    }
}

int PlayerServer::SendPartyState() {
    danger = PartyDanger::Safe;
    Player* currentPlayer = player;
    const float low = kLowLevel;
    for (auto node = currentPlayer->units.begin(); node != currentPlayer->units.end() && node->second; ++node) {
        const float* levels = node->second->exposure;
        EI_NAN_AUDIT(levels[0], levels[1], levels[2], levels[3], levels[4], low);
        if (levels[0] <= low || levels[1] <= low || levels[2] <= low || levels[3] <= low || levels[4] <= low)
            danger = PartyDanger::EnemyNear;
    }

    int count = static_cast<int>(partyUnits.size());
    WriteByte(&out, kMessagePartyState);
    WriteByte(&out, currentPlayer->ServerWorld()->paused != 0 ? 1 : 0);
    WriteDword(&out, possessedId);
    WriteDword(&out, static_cast<std::uint32_t>(count));
    std::uint32_t playerBit = 1u << (currentPlayer->playerId & 31);
    for (int i = 0; i < count; ++i) {
        auto* unit = partyUnits[static_cast<std::size_t>(i)].AsUnit();
        if (danger != PartyDanger::InDanger && !unit->IsDead()) {
            if (unit->ServerController()->IsFighting()) danger = PartyDanger::InDanger;
            // A neighbour hostile to this player whose aggression is aimed at the unit.
            for (auto node = unit->ServerController()->brain.memory.nearby.begin(); node != unit->ServerController()->brain.memory.nearby.end();) {
                Unit* other = *node;
                ++node;
                if ((playerBit & other->relationsB & other->relationsA) == 0) continue;
                if (other->ServerController()->brain.AggressionEnemy() == unit) danger = PartyDanger::InDanger;
            }
        }
        WriteDword(&out, static_cast<std::uint32_t>(unit->id));
    }
    out.WriteValue(danger);

    // The objects shared with the hero of a multiplayer client that plays one.
    ScriptGroup shared;
    if (IsMultiplayerClient() && std::ssize(currentPlayer->GetParty()->members) == 1)
        g_serverWorld.CollectSharedObjects(static_cast<int>(currentPlayer->GetParty()->members[0].unitId), &shared);
    WriteDword(&out, static_cast<std::uint32_t>(shared.size()));
    for (const ScriptObjectRef& member : shared) WriteDword(&out, static_cast<std::uint32_t>(member.AsUnit()->id));
    return 1;
}

int PlayerServer::ForgetObject(EngineObject* object) {
    Player::MapObjectMap& known = player->knownObjects;
    if (known.contains(object->id)) {
        Forget(known, object->id);
        object->Release();
        for (int i = 0; i < std::ssize(sentObjects); ++i) {
            if (sentObjects[i] != object) continue;
            sentObjects.erase(sentObjects.begin() + i);
            object->Release();
            --i;
        }
        WriteByte(&out, kMessageObjectRemoved);
        WriteDword(&out, static_cast<std::uint32_t>(object->id));
    }
    possessedGroup.Remove(object);
    partyUnits.Remove(object);
    return 1;
}

bool PlayerServer::IsPartyUnit(EngineObject* object) { return partyUnits.Contains(object) != 0; }

void PlayerServer::SendParty() {
    MemoryStream party;
    player->GetParty()->SaveParty(&party, 1);
    WriteByte(&out, kMessageParty);
    out.WriteSized(&party);
    refreshTimer = static_cast<int>(g_random.Next() % kRefreshSpread + kRefreshMinimum);
}

int PlayerServer::ReceiveMessages() {
    if (state == kStateLeaving) return 0;
    netlink::NetLink* link = player->link;
    if (link == nullptr) return 0;
    MemoryStream& in = g_receivedMessages;
    while (link->HasIncoming()) {
        MemoryStream packet;
        if (!link->Receive(&packet)) return 0;
        packet.DecompressTo(&decompressor, &in);
        in.Seek(0);
        in.ok = 1;
        std::uint8_t messageType = 0xFF;
        std::uint8_t previous = 0xFF;
        for (;;) {
            if (!in.ok) return PlayerMessageError(TextReadPastEnd(), previous, messageType);
            previous = messageType;
            if (in.Read(&messageType, 1) == 0) break;
            if (state == 0 && messageType != kMessageJoin && messageType != kMessageJoin8) break;
            if (static_cast<int>(messageType) >= std::ssize(handlers)) return PlayerMessageError(TextUnknownType(), previous, messageType);
            auto handler = handlers[messageType];
            if (handler == nullptr) return PlayerMessageError(TextNoHandler(), previous, messageType);
            if (!(this->*handler)()) return PlayerMessageError(TextHandlerFailed(), previous, messageType);
            if (state == kStateLeaving) return 0;
        }
    }
    return 1;
}

void PlayerServer::UpdatePartyUnits() {
    ScriptGroup known;
    ScriptGroup party = possessedGroup;
    Player::UnitMap& units = player->units;
    if (possessedId == kNoPartyCopy) {
        for (auto node = units.begin(); node != units.end() && node->second; ++node) {
            Unit* unit = node->second;
            party.Insert(unit);
            AIBrain* brain = &unit->ServerController()->brain;
            party.InsertAll(&brain->memory.visible);
            known.InsertAll(&brain->memory.seenCorpses);
        }
        partyUnits.Union(party, known);
        for (auto node = units.begin(); node != units.end() && node->second; ++node)
            known.CopyToList(&node->second->ServerController()->brain.memory.seenCorpses);
    } else {
        partyUnits = party;
        possessedId = kNoPartyCopy;
    }
}
