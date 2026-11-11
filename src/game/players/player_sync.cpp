// PlayerServer: what the server sends a player every world update (WorldServer::SyncPlayer,
// world_sync.cpp): the map and timed objects that came into view, changed or left, and the flush
// of the collected messages to the player's link. See player_messages.h and docs/notes/objects.md.

#include "game/items/item_manager.h"
#include "game/players/player_messages.h"
#include "game/players/player.h"
#include "game/console/console.h"

#include "engine/core/base_manager.h"
#include "game/combat/combat.h"
#include "game/objects/map_object.h"
#include "game/net/net_link.h"
#include "game/players/party.h"
#include "engine/core/random.h"
#include "engine/data/resource_manager.h"
#include "game/script/script.h"
#include "game/units/unit.h"
#include "game/ai/unit_ai.h"
#include "game/world/world.h"
#include "game/effects/world_effect.h"

#include "lib/crt.h"

#include "game/app/singletons.h"

#include <cstring>
#include <vector>

namespace {

constexpr std::uint32_t kFlagDeltaPending = 2;   // flags: the object's delta is still to be written
constexpr std::uint32_t kRefreshMinimum = 900;   // updates until the party is sent again: 900..1800
constexpr std::uint32_t kRefreshSpread = 901;


// The receive errors' formats ("Serv pkt read error prevID=%x curID=%x", ...), read from the exe.

constexpr std::uint32_t kNoPartyCopy = 0xFFFFFFFF;   // PlayerServer::possessedId

// 2.0: a unit level at most this asks the client to refresh the party.
constexpr float kLowLevel = 2.0f;

// The texts of the traffic log (read from the exe).

void WriteDword(MemoryStream* out, std::uint32_t value) { out->WriteValue(value); }

// Appends all of `from` (sought to 0) to `out`, the pending bits of `out` stored first.
void WriteStateStream(MemoryStream* out, MemoryStream* from) {
    out->EndBits();
    from->Seek(0);
    std::uint8_t* data = from->data.data();
    out->Write(data, from->Size());
}

// The highest id kept, the object stored under its id and counted.
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

// SaveListedObjects on a typed list.
template <typename T>
void SaveListed(const std::vector<T*>& list, ByteStream* stream) {
    for (T* object : list) SaveListedObject(object, stream);
    SaveListedObjectsEnd(stream);
}

} // namespace

void PlayerServer::ItemsGiven(const std::vector<Item*>& items, int flag) {
    if (state != State::Joined) return;
    ItemsAndSpells gathered;
    for (Item* item : items) gathered.Add(item);
    MemoryStream states;
    SaveListed(gathered.spells, &states);
    SaveListed(gathered.items, &states);
    WriteType(&out, ServerMessage::NewItemsAndSpells);
    out.WriteStream(&states);
    WriteType(&out, ServerMessage::ItemsReceived);
    WriteDword(&out, static_cast<std::uint32_t>(player->GetParty()->money));
    out.WriteValue(static_cast<std::uint8_t>(flag));
    for (Item* item : items) WriteDword(&out, static_cast<std::uint32_t>(item->id));
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
        console::EndLine(console::g_netLogOut << "server to client, size = " << size << " squeezed to " << packedSize);
    }
    int sent = link->Send(&packed) ? 1 : 0;
    out.Resize(0);
    return sent;
}

void PlayerServer::SendObjectsAdded(const std::vector<MapObject*>& objects) {
    for (MapObject* object : objects) {
        bool isUnit = object->GetClassId() == ClassId::Unit;
        std::uint32_t header[4] = {static_cast<std::uint32_t>(object->id), static_cast<std::uint32_t>(object->type),
                                   object->prototypeId, object->flags};
        WriteType(&out, ServerMessage::ObjectAdded);
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
            out.WriteString("");
        }
        WriteDword(&out, prototype);
        if (object->flags & kFlagDeltaPending) object->WriteChangedState();
        WriteStateStream(&out, &object->netState.header[0]);
        Player* currentPlayer = player;
        Keep(currentPlayer->knownObjects, currentPlayer->maxObjectId, object);
    }
}

void PlayerServer::SendObjectsChanged(const std::vector<MapObject*>& objects) {
    int stamp = WorldUpdateCount(player);
    for (MapObject* object : objects) {
        if (object->changeStamp != stamp) continue;
        if (object->flags & kFlagDeltaPending) object->WriteChangedState();
        WriteType(&out, ServerMessage::ObjectChanged);
        WriteDword(&out, static_cast<std::uint32_t>(object->id));
        WriteStateStream(&out, &object->netState.header[1]);
    }
}

void PlayerServer::SendObjectsRemoved(const std::vector<MapObject*>& objects) {
    for (MapObject* object : objects) {
        int objectId = object->id;
        Forget(player->knownObjects, objectId);
        object->Release();
        WriteType(&out, ServerMessage::ObjectRemoved);
        WriteDword(&out, static_cast<std::uint32_t>(objectId));
    }
}

void PlayerServer::SendTimedAdded(const std::vector<Effect*>& effects) {
    for (Effect* effect : effects) {
        if (effect->flags & kFlagDeltaPending) effect->SendDelta();
        std::uint32_t header[2];
        header[1] = static_cast<std::uint32_t>(effect->GetClassId());
        header[0] = static_cast<std::uint32_t>(effect->id);
        WriteType(&out, ServerMessage::TimedAdded);
        out.Write(header, sizeof(header));
        WriteStateStream(&out, &effect->state.out[0]);
        Player* currentPlayer = player;
        Keep(currentPlayer->knownTimed, currentPlayer->maxTimedId, effect);
    }
}

void PlayerServer::SendTimedChanged(const std::vector<Effect*>& effects) {
    int stamp = WorldUpdateCount(player);
    for (Effect* effect : effects) {
        if (effect->changeStamp != stamp) continue;
        if (effect->flags & kFlagDeltaPending) effect->SendDelta();
        WriteType(&out, ServerMessage::TimedChanged);
        WriteDword(&out, static_cast<std::uint32_t>(effect->id));
        WriteStateStream(&out, &effect->state.out[1]);
    }
}

void PlayerServer::SendTimedRemoved(const std::vector<Effect*>& effects) {
    for (Effect* effect : effects) {
        WriteType(&out, ServerMessage::TimedRemoved);
        WriteDword(&out, static_cast<std::uint32_t>(effect->id));
        Forget(player->knownTimed, effect->id);
        effect->Release();
    }
}

// The original calls ItemUsed's message writer of this object for each item.
void PlayerServer::SendItems() {
    for (unsigned objectId : player->recharging) {
        ResourceManager* items = player->GetParty()->ItemFactory();
        if (Item* item = items->FindObjectAs<Item>(objectId)) ItemUsed(item);
    }
}

void PlayerServer::SendPartyState() {
    danger = PartyDanger::Safe;
    Player* currentPlayer = player;
    const float low = kLowLevel;
    for (auto node = currentPlayer->units.begin(); node != currentPlayer->units.end() && node->second; ++node) {
        const float* levels = node->second->exposure.data();
        if (levels[0] <= low || levels[1] <= low || levels[2] <= low || levels[3] <= low || levels[4] <= low)
            danger = PartyDanger::EnemyNear;
    }

    int count = static_cast<int>(partyUnits.size());
    WriteType(&out, ServerMessage::PartyState);
    out.WriteValue(static_cast<std::uint8_t>(currentPlayer->ServerWorld()->paused != 0 ? 1 : 0));
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

    // The objects shared with the hero of a multiplayer client (its party's first record).
    ScriptGroup shared;
    if (IsMultiplayerClient() && !currentPlayer->GetParty()->members.empty())
        g_serverWorld.CollectSharedObjects(static_cast<int>(currentPlayer->GetParty()->members[0].unitId), &shared);
    WriteDword(&out, static_cast<std::uint32_t>(shared.size()));
    for (const ScriptObjectRef& member : shared) WriteDword(&out, static_cast<std::uint32_t>(member.AsUnit()->id));
}

void PlayerServer::ForgetObject(EngineObject* object) {
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
        WriteType(&out, ServerMessage::ObjectRemoved);
        WriteDword(&out, static_cast<std::uint32_t>(object->id));
    }
    possessedGroup.Remove(object);
    partyUnits.Remove(object);
}

bool PlayerServer::IsPartyUnit(EngineObject* object) { return partyUnits.Contains(object) != 0; }

void PlayerServer::SendParty() {
    MemoryStream party;
    player->GetParty()->SaveParty(&party, true);
    WriteType(&out, ServerMessage::Party);
    out.WriteSized(&party);
    refreshTimer = static_cast<int>(g_random.Next() % kRefreshSpread + kRefreshMinimum);
}

int PlayerServer::Receive() {
    if (state == State::Leaving) return 0;
    netlink::NetLink* link = player->link;
    if (link == nullptr) return 0;
    while (link->HasIncoming()) {
        MemoryStream packet;
        if (!link->Receive(&packet)) return 0;
        packet.DecompressTo(&decompressor, &in);
        in.Seek(0);
        in.ok = true;
        std::uint8_t messageType = 0xFF;
        std::uint8_t previous = 0xFF;
        for (;;) {
            if (!in.ok) return PlayerMessageError("Serv pkt read error prevID=%x curID=%x", previous, messageType);
            previous = messageType;
            if (in.Read(&messageType, 1) == 0) break;
            if (state == State::NotLoggedIn && messageType != static_cast<std::uint8_t>(ClientMessage::Login) &&
                messageType != static_cast<std::uint8_t>(ClientMessage::NewParty))
                break;
            bool handled;
            switch (static_cast<ClientMessage>(messageType)) {
            case ClientMessage::Login: handled = OnLogin(); break;
            case ClientMessage::Leave: handled = OnLeave(); break;
            case ClientMessage::RefreshParty: handled = OnRefreshParty(); break;
            case ClientMessage::TakeExit: handled = OnTakeExit(); break;
            case ClientMessage::GameStateRequest: handled = OnGameStateRequest(); break;
            case ClientMessage::Joined: handled = OnJoined(); break;
            case ClientMessage::LeaveToZone: handled = OnLeaveToZone(); break;
            case ClientMessage::BriefingDone: handled = OnBriefingDone(); break;
            case ClientMessage::NewParty: handled = OnNewParty(); break;
            case ClientMessage::Resume: handled = OnResume(); break;
            case ClientMessage::Pause: handled = OnPause(); break;
            case ClientMessage::Walk: handled = OnWalk(); break;
            case ClientMessage::Attack: handled = OnAttack(); break;
            case ClientMessage::CastAtPoint: handled = OnCastAtPoint(); break;
            case ClientMessage::CastAtObject: handled = OnCastAtObject(); break;
            case ClientMessage::WantedWeapon: handled = OnWantedWeapon(); break;
            case ClientMessage::MoveStyle: handled = OnMoveStyle(); break;
            case ClientMessage::UseOrder: handled = OnUseOrder(); break;
            case ClientMessage::UnitLookup: handled = OnUnitLookup(); break;
            case ClientMessage::Follow: handled = OnFollow(); break;
            case ClientMessage::GoTo: handled = OnGoTo(); break;
            case ClientMessage::FightAtWill: handled = OnFightAtWill(); break;
            case ClientMessage::LastPosition: handled = OnLastPosition(); break;
            case ClientMessage::PartyUpdate: handled = OnPartyUpdate(); break;
            case ClientMessage::AddCompanion: handled = OnAddCompanion(); break;
            case ClientMessage::RemoveCompanion: handled = OnRemoveCompanion(); break;
            default: return PlayerMessageError("Serv packet has no handler prevID=%x curID=%x", previous, messageType);
            }
            if (!handled) return PlayerMessageError("Serv error during parsing command prevID=%x curID=%x", previous, messageType);
            if (state == State::Leaving) return 0;
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
            party.InsertAll(brain->memory.visible);
            known.InsertAll(brain->memory.seenCorpses);
        }
        partyUnits.Union(party, known);
        for (auto node = units.begin(); node != units.end() && node->second; ++node)
            known.CopyToList(&node->second->ServerController()->brain.memory.seenCorpses);
    } else {
        partyUnits = party;
        possessedId = kNoPartyCopy;
    }
}
