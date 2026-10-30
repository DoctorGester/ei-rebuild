// The game server's map change. A literal transcription: the helpers it calls are game
// code outside this file (or STLport strings) and are reached through their original addresses.
// See net_classes.h and docs/notes/network.md.
#include "core_refcount.h"

#include "game_flow_data.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "net_coder.h"
#include "net_connection.h"
#include "net_messages.h"
#include "quest_records.h"
#include "ui.h"
#include "world.h"
#include "world_data.h"
#include "lib/crt.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace net {

// Empties queuedMaps and variables, queues a map message (name + suffix) on the first, replaces map by a fresh
// one and copies the name into it and into mapName, loads the map into the world and announces its title. Then,
// when the map registry knows "name + suffix", sets a script variable to 1 for each of the map's names (inline,
// through FindOrAddVariable) and for two paths built from the map name (SetVariable), and queues the host's item
// awards (without the final award).
void ServerSession::SetMap(const std::string* newName) {
    queuedMaps.clear();
    variables.clear();

    {
        std::string messageName = *newName + game::ZoneSuffix2Text();
        auto* message = NewCounted<ZoneMessage>();
        message->text = messageName;
        queuedMaps.emplace_back(message);
    }

    auto* mapMessage = NewCounted<MapMessage>();
    ShareAssignOwned(map, mapMessage);
    MapMessage* current = map.Get();
    if (newName != &current->text) current->text = *newName;
    if (newName != &mapName) mapName = *newName;

    GameFlow* world = &g_gameFlow;
    world->RegisterQuests(newName->data());
    world->SetNetworkZone(newName->data());
    Announce(g_interface.MessageText(5, SessionMapName().c_str()), nullptr);

    QuestBriefingRecord* entry;
    {
        std::string registryName = *newName + game::ZoneSuffix1Text();
        entry = worlddata::QuestBriefings()[registryName];
    }
    if (!entry) return;

    const std::vector<std::string>& quests = entry->lists[2];   // the quests given
    for (const auto& it : quests) {
        std::string resourceName(it);
        auto* record = FindOrAddVariable(&resourceName)->Get();
        record->value = 1.0f;
        record->serial = g_variableSerial;
        ++g_variableSerial;
        ++record->version;
    }

    // "<prefix><name>.<name>" and the same with the path suffix; the temporaries freed in reverse.
    {
        std::string prefix(flowdata::kQuestVariablePrefix);
        std::string a = prefix + *newName;
        std::string b = a + flowdata::kDot;
        std::string path = b + *newName;
        SetVariable(&path, 1.0f);
    }
    {
        std::string prefix(flowdata::kQuestVariablePrefix);
        std::string a = prefix + *newName;
        std::string b = a + flowdata::kDot;
        std::string c = b + *newName;
        std::string path = c + game::PathSuffixText();
        SetVariable(&path, 1.0f);
    }

    AwardHost(entry, false);
}
// Unloads the flow's quest texts, then queues a map message ("zone" + "_1") on queuedMaps for each zone
// the flow picks for the server's kind after the current map, loading each zone's quests.
void ServerSession::QueueMapList() {
    UnloadQuestTexts();
    for (auto& zone : PickZones(kind, mapName.data())) {
        {
            std::string key = zone + game::ZoneSuffix1Text();
            auto* message = NewCounted<ZoneMessage>();
            message->text = key;
            queuedMaps.emplace_back(message);
        }
        LoadZoneQuests(zone.data());
    }
}

// When the held map message names `name`: drops it, empties the queued messages
// And queues a map message "name" + "_3". Returns whether it did.
bool ServerSession::FinishMap(const std::string* mapFile) {
    auto* holder = &map;
    MapMessage* newMap = holder->Get();
    if (!IsLive(newMap)) return false;
    const std::string* held = &newMap->text;
    if (!(*held == *mapFile)) return false;

    DestroyOwned(*holder);
    queuedMaps.clear();

    std::string messageName = *mapFile + game::ZoneSuffix3Text();
    auto* message = NewCounted<ZoneMessage>();
    message->text = messageName;
    queuedMaps.emplace_back(message);
    return true;
}

} // namespace net

// --- Net server files --------------------------------------------------------------------------------

// The game server's shared files (called by the world for the saved zone data): a file
// message split into 64-byte chunk messages, queued on files. See net_classes.h and docs/notes/network.md.

namespace net {

namespace {

constexpr std::int32_t kChunkSize = static_cast<std::int32_t>(FileChunkMessage::kDataSize);

} // namespace

// Shares `data` (read from its start to its highest position) under `name` unless a file of that name is
// listed already: a file message (chunk count, size, name) with a chunk message (the file, its index, up to
// 64 bytes) per 64 bytes. The vector is first filled with one shared chunk (a copy per element when shared,
// as SharedRef copies are), whose elements are then filled in turn.
void ServerSession::ShareFile(const std::string* fileName, GameBuffer* data) {
    for (auto node = files.begin(); node != files.end(); ++node)
        if (node->Get()->name == *fileName) return;

    auto* file = NewCounted<FileMessage>();

    BufferUpdateHigh(data);
    std::int32_t size = data->PositionOf(data->high);
    std::int32_t chunkCount = (size + kChunkSize - 1) / kChunkSize;
    if (fileName != &file->name) file->name = *fileName;
    file->size = size;
    file->chunkCount = static_cast<std::uint16_t>(chunkCount);

    // Filled with shares of one chunk: each element a clone of it (it is marked copy-on-share by the first share).
    auto& chunks = file->chunks;
    chunks.resize(static_cast<std::size_t>(chunkCount), SharedRef<RefObject>(NewCounted<FileChunkMessage>()));

    data->Seek(0);
    std::int32_t remaining = size;
    for (std::int32_t i = 0; i < chunkCount; ++i) {
        auto* piece = static_cast<FileChunkMessage*>(chunks[i].Get());
        piece->file.Assign(file);
        piece->index = static_cast<std::uint16_t>(i);
        unsigned n = static_cast<unsigned>(remaining > kChunkSize ? kChunkSize : remaining);
        std::uint8_t* to = piece->data;
        if (data->cur + n > data->high) {
            data->ReadPastEnd(to, n);
        } else if (data->cur + n > data->end) {
            data->ReadMore(to, n);
        } else {
            std::memcpy(to, data->cur, n);
            data->cur += n;
        }
        remaining -= kChunkSize;
    }

    files.emplace_back(file);
}

} // namespace net

// --- Net server commands -----------------------------------------------------------------------------

// The game server's command messages (CommandMessage) and the file requests a client
// hands in, with the host check they use. See net_messages.h and docs/notes/network.md.

namespace net {

namespace {

enum CommandKind : std::uint32_t {
    kPreviewMap = 0,       // the host shows a map
    kZoneCommand = 1,      // the host picks a queued map message: "<zone>_1" / "_2" / "_3"
    kKickByName = 2,
    kBanByName = 3,
    kKickBySerial = 4,
    kBanBySerial = 5,
};

std::size_t StringNpos() { return 0xFFFFFFFFu; }

bool Is(const std::string& s, const char* t) {
    std::size_t length = std::strlen(t);
    return s.size() == length && std::memcmp(s.data(), t, length) == 0;
}

} // namespace

// Whether `connection` is the host's (a binding of it with a live host player).
bool ServerSession::IsHostConnection(const Connection* connection) {
    for (auto& node : bindings) {
        auto* binding = node.Get();
        if (binding->ConnectionRef().Get() != connection) continue;
        auto* player = binding->PlayerInfo();
        if (IsLive(player) && player->host) return true;
    }
    return false;
}

// File requests: the request keeps its sender's connection and is listed on fileRequests (dropped again on
// removal).
void ServerOnObjectAdded(ServerSession* self, FileRequestMessage* message) {
    message->ConnectionRef().Assign(g_currentConnection);
    self->fileRequests.emplace_back(message);
}

// Commands, from the connection being read. Kinds 0-2 need the host's
// connection, 3-5 the same through IsHostConnection:
//   0: the world prepares map `text`;
//   1: when a queued map message is named `text`, its suffix picks SetMap (_1),
//      LeaveZone (_2) or CompleteZone (_3) with the name before it;
//   2 / 3: every guest whose name starts with `text` is kicked / banned;
//   4 / 5: the client with serial atoi(text) is kicked / banned.
void ServerOnCommand(ServerSession* self, CommandMessage* message) {
    std::string* text = &message->text;
    switch (message->kind) {
    case kPreviewMap:
        if (self->IsHostConnection(g_currentConnection)) g_gameFlow.SetNetworkZone(text->data());
        return;
    case kZoneCommand: {
        if (!self->IsHostConnection(g_currentConnection)) return;
        auto node = self->queuedMaps.begin();
        for (; node != self->queuedMaps.end(); ++node) {
            auto* entry = node->Get();
            if (entry->text == *text) break;
        }
        if (node == self->queuedMaps.end()) return;
        const std::size_t cut = text->size() - 2;
        std::string suffix = text->substr(cut, StringNpos());
        std::string name = text->substr(0, text->size() - 2);
        if (Is(suffix, game::ZoneSuffix1Text())) self->SetMap(&name);
        if (Is(suffix, game::ZoneSuffix2Text())) self->LeaveZone();
        if (Is(suffix, game::ZoneSuffix3Text())) self->CompleteZone(&name);
        return;
    }
    case kKickByName:
        if (!self->IsHostConnection(g_currentConnection)) return;
        for (;;) {
            Connection* connection = self->FindGuestConnection(text);
            if (!IsLive(connection)) return;
            self->Kick(connection);
        }
    case kBanByName:
        if (!self->IsHostConnection(g_currentConnection)) return;
        for (;;) {
            Connection* connection = self->FindGuestConnection(text);
            if (!IsLive(connection)) return;
            self->Ban(connection);
        }
    case kKickBySerial:
        if (self->IsHostConnection(g_currentConnection)) self->Kick(self->FindConnectionBySerial(std::atoi(text->data())));
        return;
    case kBanBySerial:
        if (self->IsHostConnection(g_currentConnection)) self->Ban(self->FindConnectionBySerial(std::atoi(text->data())));
        return;
    default:
        return;
    }
}

} // namespace net
