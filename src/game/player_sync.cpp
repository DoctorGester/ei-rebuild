// CPlayerServer: what the server sends a player every world update (CWorldServer::SyncPlayer,
// world_sync.cpp): the map and timed objects that came into view, changed or left, and the flush
// of the collected messages to the player's link. See player_messages.h and docs/notes/objects.md.

#include "player_messages.h"
#include "lib/crt_iostream.h"

#include "base_manager.h"
#include "combat.h"
#include "game_arrays.h"
#include "map_object.h"
#include "net_link.h"
#include "originals_gameplay.h"
#include "originals_library.h"
#include "originals_script.h"
#include "originals_units.h"
#include "party.h"
#include "random.h"
#include "resource_manager.h"
#include "script.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"
#include "world_effect.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "lib/mfc_array.h"

#include <cstring>

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

// The stream the received packets are decompressed into (007C83D0).
CMemoryStream& ReceivedMessages() { return *reinterpret_cast<CMemoryStream*>(0x007C83D0); }
// The receive errors' formats ("Serv pkt read error prevID=%x curID=%x", ...), read from the exe.
const char* TextReadPastEnd() { return reinterpret_cast<const char*>(0x00794F0C); }
const char* TextNoHandler() { return reinterpret_cast<const char*>(0x00794EDC); }
const char* TextHandlerFailed() { return reinterpret_cast<const char*>(0x00794EA4); }
const char* TextUnknownType() { return reinterpret_cast<const char*>(0x00794E7C); }

constexpr std::uint32_t kNoPartyCopy = 0xFFFFFFFF;   // CPlayerServer::possessedId_

// The brain embedded in a unit's controller (CUnitServer + 0x980).
AIBrain* BrainOf(CUnit* unit) { return &static_cast<CUnitServer*>(unit->controller)->brain; }

// 2.0 (0073CDB8): a unit level at most this asks the client to refresh the party.
float LowLevel() { return *reinterpret_cast<const float*>(0x0073CDB8); }

// The game's cout, the flush manipulator and the texts of the traffic log (read from the exe).
libcrt::Ostream* GameCout() { return reinterpret_cast<libcrt::Ostream*>(0x007C56B0); }
const char* TextServerToClient() { return reinterpret_cast<const char*>(0x00794E50); }
const char* TextSqueezedTo() { return reinterpret_cast<const char*>(0x00794E6C); }
// The empty string the original sends in place of a non-unit's name.
const char* EmptyText() { return reinterpret_cast<const char*>(0x0079B298); }

void WriteByte(BitStream* out, std::uint8_t value) { out->Write(&value, 1); }
void WriteDword(BitStream* out, std::uint32_t value) { out->Write(&value, 4); }
void WriteText(BitStream* out, const char* text) { out->Write(text, static_cast<std::uint32_t>(std::strlen(text) + 1)); }

// Appends all of `from` (sought to 0) to `out`, the pending bits of `out` stored first.
void WriteStateStream(BitStream* out, BitStream* from) {
    out->EndBits();
    from->Seek(0);
    std::uint8_t* data = from->data_;
    out->Write(data, from->Size());
}

// The insert of hash_map::operator[] inlined by the senders: the highest id kept, the object
// stored under its id and counted.
void Keep(CPlayer::ObjectMap& map, int& maxId, CEngineObject* object) {
    int id = object->id_;
    if (id > maxId) maxId = id;
    map[id] = object;
    ++object->refCount_;
}

// The erase the removers inline: the node of `id`, if any, dropped.
void Forget(CPlayer::ObjectMap& map, int id) {
    if (CPlayer::ObjectMap::Node* node = map.Find(id)) map.Erase(node);
}

int WorldUpdateCount(CPlayer* player) { return static_cast<CWorldServer*>(player->ManagerContext())->updateCount_; }

// SendItemsGiven (A-H10).
constexpr std::uint8_t kMessageItemStates = 0x05;
constexpr std::uint8_t kMessageItemsGiven = 0x06;
constexpr int kStateJoined = 1;
constexpr int kItemListBlockSize = 10;
using GivenList = mfc::CTypedPtrList<CEngineObject>;
void* ItemListVtable() { return reinterpret_cast<void*>(0x0073B8D4); }   // the CPtrList built on the stack

} // namespace

void CPlayerServer::SendItemsGiven(const GivenList* items, int flag) {
    if (state_ != kStateJoined) return;
    GivenList spells{ItemListVtable(), nullptr, nullptr, 0, nullptr, nullptr, kItemListBlockSize};
    GivenList gathered{ItemListVtable(), nullptr, nullptr, 0, nullptr, nullptr, kItemListBlockSize};
    for (GivenList::Node* node = items->head; node != nullptr; node = node->next)
        GatherItem(&gathered, reinterpret_cast<mfc::CTypedPtrList<void>*>(&spells), node->data);   // 0069FB60
    alignas(4) unsigned char bytes[sizeof(CMemoryStream)];
    auto* states = reinterpret_cast<CMemoryStream*>(bytes);
    states->Construct();
    SaveListedObjects(&spells, states);
    SaveListedObjects(&gathered, states);
    WriteByte(&out_, kMessageItemStates);
    out_.WriteStream(states);
    WriteByte(&out_, kMessageItemsGiven);
    WriteDword(&out_, static_cast<std::uint32_t>(Owner()->Party()->Money()));
    WriteByte(&out_, static_cast<std::uint8_t>(flag));
    for (GivenList::Node* node = items->head; node != nullptr; node = node->next)
        WriteDword(&out_, static_cast<std::uint32_t>(node->data->id_));
    WriteDword(&out_, 0xFFFFFFFFu);
    states->Destruct();
    gathered.RemoveAll();   // the lists' inlined destructors
    spells.RemoveAll();
}

int CPlayerServer::Flush() {
    netlink::NetLink* link = Owner()->link_;
    if (link == nullptr) return 0;
    QueueStream packed;
    out_.CompressTo(&compressor_, packed.Get());
    if (Owner()->logTraffic_ != 0) {
        std::uint32_t packedSize = packed.Get()->size_;
        std::uint32_t size = out_.Size();
        GameCout()->PutText(TextServerToClient())->PutUnsigned(size)->PutText(TextSqueezedTo())
            ->PutUnsigned(packedSize)->PutChar('\n')->Apply(libcrt::FlushManipulator);
    }
    int sent = link->Send(packed.Get()) ? 1 : 0;
    out_.Resize(0);
    return sent;
}

int CPlayerServer::SendObjectsAdded(const PlayerObjectArray* objects) {
    CEngineObject** next = objects->data;
    for (int i = 0; i < objects->size; ++i) {
        auto* object = static_cast<CMapObject*>(*next++);
        bool isUnit = object->GetClassId() == world::kClassUnit;
        std::uint32_t header[4] = {static_cast<std::uint32_t>(object->id_), static_cast<std::uint32_t>(object->type_),
                                   object->prototypeId_, object->flags_};
        WriteByte(&out_, kMessageObjectAdded);
        out_.Write(header, sizeof(header));
        Vector3 scale;
        std::memcpy(&scale, &object->scale, sizeof(scale));
        out_.Write(&scale, sizeof(scale));
        WriteText(&out_, object->name2);
        WriteText(&out_, object->name);
        auto* server = static_cast<CMapObjectServer*>(object->companion);
        WriteText(&out_, server->name78);
        WriteText(&out_, server->name7C);
        std::uint32_t count = static_cast<std::uint32_t>(server->stringCount);
        out_.Write(&count, 2);
        for (int s = 0; s < server->stringCount; ++s) WriteText(&out_, server->strings[s]);
        std::uint32_t prototype = 0;
        if (isUnit) {
            WriteText(&out_, object->GetName());
            prototype = static_cast<std::uint32_t>(reinterpret_cast<CUnit*>(object)->prototype23C);
        } else {
            WriteText(&out_, EmptyText());
        }
        WriteDword(&out_, prototype);
        if (object->flags_ & kFlagDeltaPending) object->WriteChangedState();
        WriteStateStream(&out_, &object->NetState()->header_[0]);
        CPlayer* player = Owner();
        Keep(player->knownObjects_, player->maxObjectId_, object);
    }
    return 1;
}

int CPlayerServer::SendObjectsChanged(const PlayerObjectArray* objects) {
    CEngineObject** next = objects->data;
    int stamp = WorldUpdateCount(Owner());
    for (int i = 0; i < objects->size; ++i) {
        auto* object = static_cast<CMapObject*>(*next++);
        if (object->changeStamp != stamp) continue;
        if (object->flags_ & kFlagDeltaPending) object->WriteChangedState();
        WriteByte(&out_, kMessageObjectChanged);
        WriteDword(&out_, static_cast<std::uint32_t>(object->id_));
        WriteStateStream(&out_, &object->NetState()->header_[1]);
    }
    return 1;
}

int CPlayerServer::SendObjectsRemoved(const PlayerObjectArray* objects) {
    CEngineObject** next = objects->data;
    for (int i = 0; i < objects->size; ++i) {
        CEngineObject* object = *next++;
        int id = object->id_;
        Forget(Owner()->knownObjects_, id);
        object->Release();
        WriteByte(&out_, kMessageObjectRemoved);
        WriteDword(&out_, static_cast<std::uint32_t>(id));
    }
    return 1;
}

int CPlayerServer::SendTimedAdded(const PlayerObjectArray* objects) {
    CEngineObject** next = objects->data;
    for (int i = 0; i < objects->size; ++i) {
        auto* effect = static_cast<CEffect*>(*next++);
        if (effect->flags_ & kFlagDeltaPending) effect->SendDelta();
        std::uint32_t header[2];
        header[1] = static_cast<std::uint32_t>(effect->GetClassId());
        header[0] = static_cast<std::uint32_t>(effect->id_);
        WriteByte(&out_, kMessageTimedAdded);
        out_.Write(header, sizeof(header));
        WriteStateStream(&out_, &effect->state_.out[0]);
        CPlayer* player = Owner();
        Keep(player->knownTimed_, player->maxTimedId_, effect);
    }
    return 1;
}

int CPlayerServer::SendTimedChanged(const PlayerObjectArray* objects) {
    CEngineObject** next = objects->data;
    int stamp = WorldUpdateCount(Owner());
    for (int i = 0; i < objects->size; ++i) {
        auto* effect = static_cast<CEffect*>(*next++);
        if (effect->changeStamp_ != stamp) continue;
        if (effect->flags_ & kFlagDeltaPending) effect->SendDelta();
        WriteByte(&out_, kMessageTimedChanged);
        WriteDword(&out_, static_cast<std::uint32_t>(effect->id_));
        WriteStateStream(&out_, &effect->state_.out[1]);
    }
    return 1;
}

int CPlayerServer::SendTimedRemoved(const PlayerObjectArray* objects) {
    CEngineObject** next = objects->data;
    for (int i = 0; i < objects->size; ++i) {
        CEngineObject* object = *next++;
        WriteByte(&out_, kMessageTimedRemoved);
        WriteDword(&out_, static_cast<std::uint32_t>(object->id_));
        Forget(Owner()->knownTimed_, object->id_);
        object->Release();
    }
    return 1;
}

// The original calls slot 22 of this object (00743280: SendObject14, rewritten) for each item.
void CPlayerServer::SendItems() {
    auto* node = Owner()->objects_.head;
    while (node != nullptr) {
        auto* current = node;
        node = node->next;
        unsigned id = static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(current->data));
        CResourceManager* items = Owner()->Party()->ItemManager();
        if (CEngineObject* item = items->FindObject(id)) SendObject14(item);
    }
}

int CPlayerServer::SendPartyState() {
    refreshPending_ = 0;
    CPlayer* player = Owner();
    const float low = LowLevel();
    for (CPlayer::ObjectMap::Node* node = player->units_.First(); node && node->value; node = player->units_.After(node)) {
        const float* levels = static_cast<CUnit*>(node->value)->levels4C8;
        if (!(levels[0] > low) || !(levels[1] > low) || !(levels[2] > low) || !(levels[3] > low) || !(levels[4] > low))
            refreshPending_ = 1;
    }

    int count = partyUnits_.size;
    WriteByte(&out_, kMessagePartyState);
    WriteByte(&out_, static_cast<CWorldServer*>(player->ManagerContext())->paused_ != 0 ? 1 : 0);
    WriteDword(&out_, possessedId_);
    WriteDword(&out_, static_cast<std::uint32_t>(count));
    std::uint32_t playerBit = 1u << (player->playerId_ & 31);
    for (int i = 0; i < count; ++i) {
        auto* unit = static_cast<CUnit*>(partyUnits_.data[i].AsUnit());
        if (refreshPending_ != 2 && !ObjectSlots(unit)->IsDead()) {
            if (UnitServerIsFightingOriginal(unit->controller)) refreshPending_ = 2;
            // A neighbour hostile to this player whose aggression is aimed at the unit.
            for (auto* node = BrainOf(unit)->nearby.head; node != nullptr;) {
                auto* other = static_cast<CUnit*>(node->data);
                node = node->next;
                if ((playerBit & other->relationsB & other->relationsA) == 0) continue;
                if (BrainOf(other)->AggressionEnemy() == unit) refreshPending_ = 2;
            }
        }
        WriteDword(&out_, static_cast<std::uint32_t>(unit->id_));
    }
    out_.Write(&refreshPending_, 4);

    // The objects shared with the hero of a multiplayer client that plays one.
    ScriptGroup shared;
    ScriptGroupConstructOriginal(&shared);
    shared.vtable = scriptvtable::kRefs;
    if (IsMultiplayerClient() && player->Party()->members_.size == 1)
        WorldServer()->CollectSharedObjects(static_cast<int>(player->Party()->members_.data[0].unitId), &shared);
    int sharedCount = shared.size;
    WriteDword(&out_, static_cast<std::uint32_t>(sharedCount));
    for (int i = 0; i < sharedCount; ++i) WriteDword(&out_, static_cast<std::uint32_t>(shared.data[i].AsUnit()->id_));
    shared.vtable = scriptvtable::kRefsBase;
    if (shared.data != nullptr) {
        for (int i = 0; i < shared.size; ++i) shared.data[i].Reset();
        gamecrt::operator_delete(shared.data);
    }
    return 1;
}

int CPlayerServer::ForgetObject(CEngineObject* object) {
    CPlayer::ObjectMap& known = Owner()->knownObjects_;
    if (known.Find(object->id_) != nullptr) {
        Forget(known, object->id_);
        object->Release();
        for (int i = 0; i < sentObjects_.size; ++i) {
            CEngineObject** data = sentObjects_.data;
            if (data[i] != object) continue;
            std::memmove(&data[i], &data[i + 1], static_cast<std::size_t>(sentObjects_.size - i - 1) * 4);
            --sentObjects_.size;
            object->Release();
            --i;
        }
        WriteByte(&out_, kMessageObjectRemoved);
        WriteDword(&out_, static_cast<std::uint32_t>(object->id_));
    }
    group50F8_.Remove(object);
    partyUnits_.Remove(object);
    return 1;
}

BOOL CPlayerServer::IsPartyUnit(CEngineObject* object) { return partyUnits_.Contains(object) != 0; }

void CPlayerServer::SendParty() {
    alignas(4) unsigned char bytes[sizeof(CMemoryStream)];
    auto* party = reinterpret_cast<CMemoryStream*>(bytes);
    party->Construct();
    Owner()->Party()->SaveParty(party, 1);
    WriteByte(&out_, kMessageParty);
    out_.WriteSized(party);
    refreshTimer_ = static_cast<int>(IsaacRandom::Global().Next() % kRefreshSpread + kRefreshMinimum);
    party->Destruct();
}

int CPlayerServer::ReceiveMessages() {
    if (state_ == kStateLeaving) return 0;
    netlink::NetLink* link = Owner()->link_;
    if (link == nullptr) return 0;
    CMemoryStream& in = ReceivedMessages();
    while (link->HasIncoming()) {
        QueueStream packet;
        if (!link->Receive(packet.Get())) return 0;
        packet.Get()->DecompressTo(&decompressor_, &in);
        in.Seek(0);
        in.ok_ = 1;
        std::uint8_t type = 0xFF;
        std::uint8_t previous = 0xFF;
        for (;;) {
            if (!in.ok_) return PlayerMessageErrorOriginal(TextReadPastEnd(), previous, type);
            previous = type;
            if (in.Read(&type, 1) == 0) break;
            if (state_ == 0 && type != kMessageJoin && type != kMessageJoin8) break;
            if (static_cast<int>(type) >= handlers_.size) return PlayerMessageErrorOriginal(TextUnknownType(), previous, type);
            auto handler = handlers_[type];
            if (handler == nullptr) return PlayerMessageErrorOriginal(TextNoHandler(), previous, type);
            if (!handler(this, nullptr)) return PlayerMessageErrorOriginal(TextHandlerFailed(), previous, type);
            if (state_ == kStateLeaving) return 0;
        }
    }
    return 1;
}

void CPlayerServer::UpdatePartyUnits() {
    ScriptGroup known;
    ScriptGroupConstructOriginal(&known);
    known.vtable = scriptvtable::kRefs;
    ScriptGroup party;
    ScriptGroupConstructOriginal(&party);
    party.vtable = scriptvtable::kRefs;
    gamearrays::ObjectRefCopy(reinterpret_cast<gamearrays::ObjectRefs::Array*>(&party), nullptr,
                              reinterpret_cast<const gamearrays::ObjectRefs::Array*>(&group50F8_));
    CPlayer::ObjectMap& units = Owner()->units_;
    if (possessedId_ == kNoPartyCopy) {
        for (CPlayer::ObjectMap::Node* node = units.First(); node && node->value; node = units.After(node)) {
            auto* unit = static_cast<CUnit*>(node->value);
            party.Insert(unit);
            AIBrain* brain = BrainOf(unit);
            party.InsertAll(&brain->visible);
            known.InsertAll(&brain->seenCorpses);
        }
        partyUnits_.Union(party, known);
        for (CPlayer::ObjectMap::Node* node = units.First(); node && node->value; node = units.After(node))
            known.CopyToList(&BrainOf(static_cast<CUnit*>(node->value))->seenCorpses);
    } else {
        PtrArraySetSize(&partyUnits_, nullptr, party.size, -1);
        for (int i = 0; i < party.size; ++i) {
            ScriptObjectRef copy;
            gamearrays::ObjectRefGetAt(reinterpret_cast<const gamearrays::ObjectRefs::Array*>(&party), nullptr, &copy, i);
            partyUnits_.data[i].Reset();
            partyUnits_.data[i].Construct(copy);
            copy.Reset();
        }
        possessedId_ = kNoPartyCopy;
    }
    party.vtable = scriptvtable::kRefsBase;
    if (party.data != nullptr) {
        gamearrays::ObjectRefs::DestructElements(party.data, party.size);
        gamecrt::operator_delete(party.data);
    }
    known.vtable = scriptvtable::kRefsBase;
    if (known.data != nullptr) {
        for (int i = 0; i < known.size; ++i) ScriptRefDestroy(&known.data[i].object, nullptr, 0);
        gamecrt::operator_delete(known.data);
    }
}

EI_REPLACE_METHOD(0x0066E130, CPlayerServer::UpdatePartyUnits);
EI_REPLACE_METHOD(0x0066D270, CPlayerServer::ReceiveMessages);
EI_REPLACE_METHOD(0x0066D100, CPlayerServer::Flush);
EI_REPLACE_METHOD(0x0066E430, CPlayerServer::SendPartyState);
EI_REPLACE_METHOD(0x0066DC90, CPlayerServer::ForgetObject);
EI_REPLACE_METHOD(0x0066E110, CPlayerServer::IsPartyUnit);
EI_REPLACE_METHOD(0x0066EF80, CPlayerServer::SendParty);
EI_REPLACE_METHOD(0x0066D7F0, CPlayerServer::SendObjectsAdded);
EI_REPLACE_METHOD(0x0066DAD0, CPlayerServer::SendObjectsRemoved);
EI_REPLACE_METHOD(0x0066DBA0, CPlayerServer::SendObjectsChanged);
EI_REPLACE_METHOD(0x0066DDE0, CPlayerServer::SendTimedAdded);
EI_REPLACE_METHOD(0x0066DF40, CPlayerServer::SendTimedRemoved);
EI_REPLACE_METHOD(0x0066E020, CPlayerServer::SendTimedChanged);
EI_REPLACE_METHOD(0x0066F080, CPlayerServer::SendItems);
EI_REPLACE_METHOD(0x0066E7D0, CPlayerServer::SendItemsGiven);
