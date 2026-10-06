// CPlayerServer: what the server sends a player every world update (CWorldServer::SyncPlayer,
// world_sync.cpp): the map and timed objects that came into view, changed or left, and the flush
// of the collected messages to the player's link. See player_messages.h and docs/notes/objects.md.

#include "player_messages.h"

#include "base_manager.h"
#include "map_object.h"
#include "net_link.h"
#include "originals_library.h"
#include "originals_units.h"
#include "party.h"
#include "resource_manager.h"
#include "unit.h"
#include "world.h"
#include "world_effect.h"

#include "ei/hook.h"

#include <cstring>

namespace {

constexpr std::uint8_t kMessageObjectAdded = 0x40;
constexpr std::uint8_t kMessageObjectChanged = 0x41;
constexpr std::uint8_t kMessageObjectRemoved = 0x42;
constexpr std::uint8_t kMessageTimedAdded = 0x50;
constexpr std::uint8_t kMessageTimedChanged = 0x51;
constexpr std::uint8_t kMessageTimedRemoved = 0x52;
constexpr std::uint32_t kFlagDeltaPending = 2;   // flags_: the object's delta is still to be written

// The game's cout, the flush manipulator and the texts of the traffic log (read from the exe).
void* GameCout() { return reinterpret_cast<void*>(0x007C56B0); }
void* FlushManipulator() { return reinterpret_cast<void*>(0x00435000); }
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

// A memory stream on the stack with the network queue's vtable, as the original builds and
// destroys it inline.
struct QueueStream {
    alignas(4) unsigned char bytes[sizeof(CMemoryStream)];
    QueueStream() { Get()->ConstructQueueStream(); }
    ~QueueStream() { Get()->Destruct(); }
    CMemoryStream* Get() { return reinterpret_cast<CMemoryStream*>(bytes); }
};

} // namespace

int CPlayerServer::Flush() {
    netlink::NetLink* link = Owner()->link_;
    if (link == nullptr) return 0;
    QueueStream packed;
    out_.CompressTo(&compressor_, packed.Get());
    if (Owner()->logTraffic_ != 0) {
        std::uint32_t packedSize = packed.Get()->size_;
        std::uint32_t size = out_.Size();
        void* log = OstreamPutString(GameCout(), nullptr, TextServerToClient());
        log = OstreamPutUnsignedOriginal(log, nullptr, size);
        log = OstreamPutString(log, nullptr, TextSqueezedTo());
        log = OstreamPutUnsignedOriginal(log, nullptr, packedSize);
        log = OstreamPutChar(log, nullptr, '\n');
        OstreamManipulate(log, nullptr, FlushManipulator());
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
                                   object->unknown_18, object->flags_};
        WriteByte(&out_, kMessageObjectAdded);
        out_.Write(header, sizeof(header));
        float scale[3];
        std::memcpy(scale, object->scale, sizeof(scale));
        out_.Write(scale, sizeof(scale));
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
        if (object->flags_ & kFlagDeltaPending) MapObjectSendDeltaOriginal(object);
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
        if (object->flags_ & kFlagDeltaPending) MapObjectSendDeltaOriginal(object);
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

EI_REPLACE_METHOD(0x0066D100, CPlayerServer::Flush);
EI_REPLACE_METHOD(0x0066D7F0, CPlayerServer::SendObjectsAdded);
EI_REPLACE_METHOD(0x0066DAD0, CPlayerServer::SendObjectsRemoved);
EI_REPLACE_METHOD(0x0066DBA0, CPlayerServer::SendObjectsChanged);
EI_REPLACE_METHOD(0x0066DDE0, CPlayerServer::SendTimedAdded);
EI_REPLACE_METHOD(0x0066DF40, CPlayerServer::SendTimedRemoved);
EI_REPLACE_METHOD(0x0066E020, CPlayerServer::SendTimedChanged);
EI_REPLACE_METHOD(0x0066F080, CPlayerServer::SendItems);
