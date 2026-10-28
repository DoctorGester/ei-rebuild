// CPlayerServer: what the server sends a player every world update (CWorldServer::SyncPlayer,
// world_sync.cpp): the map and timed objects that came into view, changed or left, and the flush
// of the collected messages to the player's link. See player_messages.h and docs/notes/objects.md.

#include "player_messages.h"
#include "console.h"
#include "game_strings.h"
#include "lib/static_object.h"

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
#include <list>

namespace {

constexpr std::uint8_t kMessageObjectAdded = 0x40;
constexpr std::uint8_t kMessageObjectChanged = 0x41;
constexpr std::uint8_t kMessageObjectRemoved = 0x42;
constexpr std::uint8_t kMessageTimedAdded = 0x50;
constexpr std::uint8_t kMessageTimedChanged = 0x51;
constexpr std::uint8_t kMessageTimedRemoved = 0x52;
constexpr std::uint8_t kMessagePartyState = 0x60;
constexpr std::uint8_t kMessageParty = 0x80;
constexpr std::uint32_t kFlagDeltaPending = 2;   // flags_: the object's delta is still to be written
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

constexpr std::uint32_t kNoPartyCopy = 0xFFFFFFFF;   // CPlayerServer::possessedId_

// 2.0: a unit level at most this asks the client to refresh the party.
float LowLevel() { return 2.0f; }

// The texts of the traffic log (read from the exe).
const char* TextServerToClient() { return "server to client, size = "; }
const char* TextSqueezedTo() { return " squeezed to "; }

void WriteByte(CMemoryStream* out, std::uint8_t value) { out->Write(&value, 1); }
void WriteDword(CMemoryStream* out, std::uint32_t value) { out->Write(&value, 4); }

// Appends all of `from` (sought to 0) to `out`, the pending bits of `out` stored first.
void WriteStateStream(CMemoryStream* out, CMemoryStream* from) {
    out->EndBits();
    from->Seek(0);
    std::uint8_t* data = from->data_;
    out->Write(data, from->Size());
}

// The insert of hash_map::operator[] inlined by the senders: the highest id kept, the object
// stored under its id and counted.
template <typename T>
void Keep(lib::HashMap<int, T*>& map, int& maxId, T* object) {
    int id = object->id_;
    if (id > maxId) maxId = id;
    map[id] = object;
    ++object->refCount_;
}

// The erase the removers inline: the node of `id`, if any, dropped.
template <typename Map>
void Forget(Map& map, int id) {
    map.erase(id);
}

int WorldUpdateCount(CPlayer* player) { return player->ServerWorld()->updateCount_; }

// SendItemsGiven.
constexpr std::uint8_t kMessageItemStates = 0x05;
constexpr std::uint8_t kMessageItemsGiven = 0x06;
constexpr int kStateJoined = 1;
// SaveListedObjects on a typed list.
template <typename T>
void SaveListed(const std::list<T*>* list, CStream* stream) {
    for (T* object : *list) SaveListedObject(object, stream);
    SaveListedObjectsEnd(stream);
}

// SyncPlayer's arrays (world_sync.cpp) hold one class per pass: the map objects, then the timed
// objects (CEffect). The next element as that class; the loops read the data pointer once.
template <typename T>
T* NextAs(CEngineObject* const*& next) {
    return static_cast<T*>(*next++);
}

} // namespace

void CPlayerServer::SendItemsGiven(const std::list<CItem*>* items, int flag) {
    if (state_ != kStateJoined) return;
    std::list<CSpell*> spells{};
    std::list<CItem*> gathered{};
    for (auto node = items->begin(); node != items->end(); ++node)
        GatherItem(&gathered, &spells, *node);
    CMemoryStream states;
    SaveListed(&spells, &states);
    SaveListed(&gathered, &states);
    WriteByte(&out_, kMessageItemStates);
    out_.WriteStream(&states);
    WriteByte(&out_, kMessageItemsGiven);
    WriteDword(&out_, static_cast<std::uint32_t>(player_->Party()->money_));
    WriteByte(&out_, static_cast<std::uint8_t>(flag));
    for (auto node = items->begin(); node != items->end(); ++node)
        WriteDword(&out_, static_cast<std::uint32_t>((*node)->id_));
    WriteDword(&out_, 0xFFFFFFFFu);
    gathered.clear();   // the lists' inlined destructors
    spells.clear();
}

int CPlayerServer::Flush() {
    netlink::NetLink* link = player_->link_;
    if (link == nullptr) return 0;
    CMemoryStream packed;
    out_.CompressTo(&compressor_, &packed);
    if (player_->logTraffic_ != 0) {
        std::uint32_t packedSize = packed.size_;
        std::uint32_t size = out_.Size();
        console::EndLine(console::g_netLogOut << TextServerToClient() << size << TextSqueezedTo() << packedSize);
    }
    int sent = link->Send(&packed) ? 1 : 0;
    out_.Resize(0);
    return sent;
}

int CPlayerServer::SendObjectsAdded(const std::vector<CEngineObject*>* objects) {
    CEngineObject* const* next = objects->data();
    for (int i = 0; i < std::ssize(*objects); ++i) {
        auto* object = NextAs<CMapObject>(next);
        bool isUnit = object->GetClassId() == world::kClassUnit;
        std::uint32_t header[4] = {static_cast<std::uint32_t>(object->id_), static_cast<std::uint32_t>(object->type_),
                                   object->prototypeId_, object->flags_};
        WriteByte(&out_, kMessageObjectAdded);
        out_.Write(header, sizeof(header));
        Vector3 scale;
        std::memcpy(&scale, &object->scale, sizeof(scale));
        out_.Write(&scale, sizeof(scale));
        out_.WriteString(object->name2);
        out_.WriteString(object->name);
        auto* server = object->ServerCompanion();
        out_.WriteString(server->parentIname);
        out_.WriteString(server->skinTexture);
        std::uint32_t count = static_cast<std::uint32_t>(server->strings.size());
        out_.Write(&count, 2);
        for (const std::string& text : server->strings) out_.WriteString(text);
        std::uint32_t prototype = 0;
        if (isUnit) {
            out_.WriteString(object->GetName());
            prototype = static_cast<std::uint32_t>(UnitOrNull(object)->secondPrototype);
        } else {
            out_.WriteString(game::kEmptyText);
        }
        WriteDword(&out_, prototype);
        if (object->flags_ & kFlagDeltaPending) object->WriteChangedState();
        WriteStateStream(&out_, &object->netState_.header_[0]);
        CPlayer* player = player_;
        Keep(player->knownObjects_, player->maxObjectId_, object);
    }
    return 1;
}

int CPlayerServer::SendObjectsChanged(const std::vector<CEngineObject*>* objects) {
    CEngineObject* const* next = objects->data();
    int stamp = WorldUpdateCount(player_);
    for (int i = 0; i < std::ssize(*objects); ++i) {
        auto* object = NextAs<CMapObject>(next);
        if (object->changeStamp != stamp) continue;
        if (object->flags_ & kFlagDeltaPending) object->WriteChangedState();
        WriteByte(&out_, kMessageObjectChanged);
        WriteDword(&out_, static_cast<std::uint32_t>(object->id_));
        WriteStateStream(&out_, &object->netState_.header_[1]);
    }
    return 1;
}

int CPlayerServer::SendObjectsRemoved(const std::vector<CEngineObject*>* objects) {
    CEngineObject* const* next = objects->data();
    for (int i = 0; i < std::ssize(*objects); ++i) {
        CEngineObject* object = *next++;
        int id = object->id_;
        Forget(player_->knownObjects_, id);
        object->Release();
        WriteByte(&out_, kMessageObjectRemoved);
        WriteDword(&out_, static_cast<std::uint32_t>(id));
    }
    return 1;
}

int CPlayerServer::SendTimedAdded(const std::vector<CEngineObject*>* objects) {
    CEngineObject* const* next = objects->data();
    for (int i = 0; i < std::ssize(*objects); ++i) {
        auto* effect = NextAs<CEffect>(next);
        if (effect->flags_ & kFlagDeltaPending) effect->SendDelta();
        std::uint32_t header[2];
        header[1] = static_cast<std::uint32_t>(effect->GetClassId());
        header[0] = static_cast<std::uint32_t>(effect->id_);
        WriteByte(&out_, kMessageTimedAdded);
        out_.Write(header, sizeof(header));
        WriteStateStream(&out_, &effect->state_.out[0]);
        CPlayer* player = player_;
        Keep(player->knownTimed_, player->maxTimedId_, effect);
    }
    return 1;
}

int CPlayerServer::SendTimedChanged(const std::vector<CEngineObject*>* objects) {
    CEngineObject* const* next = objects->data();
    int stamp = WorldUpdateCount(player_);
    for (int i = 0; i < std::ssize(*objects); ++i) {
        auto* effect = NextAs<CEffect>(next);
        if (effect->changeStamp_ != stamp) continue;
        if (effect->flags_ & kFlagDeltaPending) effect->SendDelta();
        WriteByte(&out_, kMessageTimedChanged);
        WriteDword(&out_, static_cast<std::uint32_t>(effect->id_));
        WriteStateStream(&out_, &effect->state_.out[1]);
    }
    return 1;
}

int CPlayerServer::SendTimedRemoved(const std::vector<CEngineObject*>* objects) {
    CEngineObject* const* next = objects->data();
    for (int i = 0; i < std::ssize(*objects); ++i) {
        CEngineObject* object = *next++;
        WriteByte(&out_, kMessageTimedRemoved);
        WriteDword(&out_, static_cast<std::uint32_t>(object->id_));
        Forget(player_->knownTimed_, object->id_);
        object->Release();
    }
    return 1;
}

// The original calls SendObject14 of this object for each item.
void CPlayerServer::SendItems() {
    auto node = player_->recharging_.begin();
    while (node != player_->recharging_.end()) {
        auto current = node;
        ++node;
        unsigned id = *current;
        CResourceManager* items = player_->Party()->ItemFactory();
        if (CEngineObject* item = items->FindObject(id)) SendObject14(item);
    }
}

int CPlayerServer::SendPartyState() {
    refreshPending_ = 0;
    CPlayer* player = player_;
    const float low = LowLevel();
    for (auto node = player->units_.begin(); node != player->units_.end() && node->second; ++node) {
        const float* levels = node->second->exposure;
        EI_NAN_AUDIT(levels[0], levels[1], levels[2], levels[3], levels[4], low);
        if (levels[0] <= low || levels[1] <= low || levels[2] <= low || levels[3] <= low || levels[4] <= low)
            refreshPending_ = 1;
    }

    int count = static_cast<int>(partyUnits_.size());
    WriteByte(&out_, kMessagePartyState);
    WriteByte(&out_, player->ServerWorld()->paused_ != 0 ? 1 : 0);
    WriteDword(&out_, possessedId_);
    WriteDword(&out_, static_cast<std::uint32_t>(count));
    std::uint32_t playerBit = 1u << (player->playerId_ & 31);
    for (int i = 0; i < count; ++i) {
        auto* unit = partyUnits_[static_cast<std::size_t>(i)].AsUnit();
        if (refreshPending_ != 2 && !unit->IsDead()) {
            if (unit->ServerController()->IsFighting()) refreshPending_ = 2;
            // A neighbour hostile to this player whose aggression is aimed at the unit.
            for (auto node = unit->ServerController()->brain.memory.nearby.begin(); node != unit->ServerController()->brain.memory.nearby.end();) {
                auto* other = *node;
                ++node;
                if ((playerBit & other->relationsB & other->relationsA) == 0) continue;
                if (other->ServerController()->brain.AggressionEnemy() == unit) refreshPending_ = 2;
            }
        }
        WriteDword(&out_, static_cast<std::uint32_t>(unit->id_));
    }
    out_.Write(&refreshPending_, 4);

    // The objects shared with the hero of a multiplayer client that plays one.
    ScriptGroup shared;
    if (IsMultiplayerClient() && std::ssize(player->Party()->members_) == 1)
        g_serverWorld->CollectSharedObjects(static_cast<int>(player->Party()->members_[0].unitId), &shared);
    WriteDword(&out_, static_cast<std::uint32_t>(shared.size()));
    for (const ScriptObjectRef& member : shared) WriteDword(&out_, static_cast<std::uint32_t>(member.AsUnit()->id_));
    return 1;
}

int CPlayerServer::ForgetObject(CEngineObject* object) {
    CPlayer::MapObjectMap& known = player_->knownObjects_;
    if (known.contains(object->id_)) {
        Forget(known, object->id_);
        object->Release();
        for (int i = 0; i < std::ssize(sentObjects_); ++i) {
            if (sentObjects_[i] != object) continue;
            sentObjects_.erase(sentObjects_.begin() + i);
            object->Release();
            --i;
        }
        WriteByte(&out_, kMessageObjectRemoved);
        WriteDword(&out_, static_cast<std::uint32_t>(object->id_));
    }
    possessedGroup_.Remove(object);
    partyUnits_.Remove(object);
    return 1;
}

BOOL CPlayerServer::IsPartyUnit(CEngineObject* object) { return partyUnits_.Contains(object) != 0; }

void CPlayerServer::SendParty() {
    CMemoryStream party;
    player_->Party()->SaveParty(&party, 1);
    WriteByte(&out_, kMessageParty);
    out_.WriteSized(&party);
    refreshTimer_ = static_cast<int>(g_random.Next() % kRefreshSpread + kRefreshMinimum);
}

int CPlayerServer::ReceiveMessages() {
    if (state_ == kStateLeaving) return 0;
    netlink::NetLink* link = player_->link_;
    if (link == nullptr) return 0;
    CMemoryStream& in = *g_receivedMessages;
    while (link->HasIncoming()) {
        CMemoryStream packet;
        if (!link->Receive(&packet)) return 0;
        packet.DecompressTo(&decompressor_, &in);
        in.Seek(0);
        in.ok_ = 1;
        std::uint8_t type = 0xFF;
        std::uint8_t previous = 0xFF;
        for (;;) {
            if (!in.ok_) return PlayerMessageError(TextReadPastEnd(), previous, type);
            previous = type;
            if (in.Read(&type, 1) == 0) break;
            if (state_ == 0 && type != kMessageJoin && type != kMessageJoin8) break;
            if (static_cast<int>(type) >= std::ssize(handlers_)) return PlayerMessageError(TextUnknownType(), previous, type);
            auto handler = handlers_[type];
            if (handler == nullptr) return PlayerMessageError(TextNoHandler(), previous, type);
            if (!(this->*handler)()) return PlayerMessageError(TextHandlerFailed(), previous, type);
            if (state_ == kStateLeaving) return 0;
        }
    }
    return 1;
}

void CPlayerServer::UpdatePartyUnits() {
    ScriptGroup known;
    ScriptGroup party = possessedGroup_;
    CPlayer::UnitMap& units = player_->units_;
    if (possessedId_ == kNoPartyCopy) {
        for (auto node = units.begin(); node != units.end() && node->second; ++node) {
            CUnit* unit = node->second;
            party.Insert(unit);
            AIBrain* brain = &unit->ServerController()->brain;
            party.InsertAll(&brain->memory.visible);
            known.InsertAll(&brain->memory.seenCorpses);
        }
        partyUnits_.Union(party, known);
        for (auto node = units.begin(); node != units.end() && node->second; ++node)
            known.CopyToList(&node->second->ServerController()->brain.memory.seenCorpses);
    } else {
        partyUnits_ = party;
        possessedId_ = kNoPartyCopy;
    }
}
