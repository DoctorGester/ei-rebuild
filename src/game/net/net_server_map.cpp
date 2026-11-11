// The game server's map change. A literal transcription: the helpers it calls are game
// code outside this file (or STLport strings) and are reached through their original addresses.
// See net/net_session.h and docs/notes/network.md.
#include "engine/core/core_refcount.h"

#include "game/app/game_flow.h"
#include "game/app/singletons.h"
#include "engine/core/coder.h"
#include "game/net/net_connection.h"
#include "game/net/net_messages.h"
#include "game/world/quest_records.h"
#include "game/ui/ui.h"
#include "lib/crt.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace net {

namespace {

constexpr std::int32_t kChunkSize = static_cast<std::int32_t>(FileChunkMessage::kDataSize);

bool Is(const std::string& s, const char* t) {
    std::size_t length = std::strlen(t);
    return s.size() == length && std::memcmp(s.data(), t, length) == 0;
}

// A map message carrying a zone's name.
ZoneMessage* NewZoneMessage(std::string text) {
    auto* message = new ZoneMessage();
    message->text = std::move(text);
    return message;
}

} // namespace

// Empties queuedMaps and variables, queues a map message (name + suffix) on the first, replaces map by a fresh
// one and copies the name into it and into mapName, loads the map into the world and announces its title. Then,
// when the map registry knows "name + suffix", sets a script variable to 1 for each of the map's names
// (FindOrAddVariable) and for two paths built from the map name (SetVariable), and queues the host's item
// awards (without the final award).
void ServerSession::SetMap(const std::string& newName) {
    queuedMaps.clear();
    variables.clear();

    queuedMaps.emplace_back(NewZoneMessage(newName + kZoneLeaveSuffix));

    auto* mapMessage = new MapMessage();
    ShareAssignOwned(map, mapMessage);
    MapMessage* current = map.Get();
    current->text = newName;
    mapName = newName;

    GameFlow* world = &g_gameFlow;
    world->RegisterQuests(newName.data());
    world->SetNetworkZone(newName.data());
    Announce(g_interface.MessageText(5, SessionMapName()), nullptr);

    QuestBriefingRecord* entry = g_questDatabase.briefings.index[newName + kZoneEnterSuffix];
    if (!entry) return;

    const std::vector<std::string>& quests = entry->lists[2];   // the quests given
    for (const auto& it : quests) {
        std::string resourceName(it);
        auto* record = FindOrAddVariable(resourceName)->Get();
        record->value = 1.0f;
        record->serial = g_variableSerial;
        ++g_variableSerial;
        ++record->version;
    }

    // "<prefix><name>.<name>" and the same with the path suffix; the temporaries freed in reverse.
    {
        std::string prefix("q.");
        std::string a = prefix + newName;
        std::string b = a + ".";
        std::string path = b + newName;
        SetVariable(path, 1.0f);
    }
    {
        std::string prefix("q.");
        std::string a = prefix + newName;
        std::string b = a + ".";
        std::string c = b + newName;
        std::string path = c + ".1";
        SetVariable(path, 1.0f);
    }

    AwardHost(entry, false);
}
// Unloads the flow's quest texts, then queues a map message ("zone" + "_1") on queuedMaps for each zone
// the flow picks for the server's kind after the current map, loading each zone's quests.
void ServerSession::QueueMapList() {
    UnloadQuestTexts();
    for (auto& zone : PickZones(kind, mapName.data())) {
        queuedMaps.emplace_back(NewZoneMessage(zone + kZoneEnterSuffix));
        LoadZoneQuests(zone.data());
    }
}

// When the held map message names `name`: drops it, empties the queued messages
// And queues a map message "name" + "_3". Returns whether it did.
bool ServerSession::FinishMap(const std::string& mapFile) {
    auto* holder = &map;
    MapMessage* newMap = holder->Get();
    if (!IsLive(newMap)) return false;
    if (newMap->text != mapFile) return false;

    DestroyOwned(*holder);
    queuedMaps.clear();

    queuedMaps.emplace_back(NewZoneMessage(mapFile + kZoneCompleteSuffix));
    return true;
}

// --- Net server files --------------------------------------------------------------------------------

// The game server's shared files (called by the world for the saved zone data): a file
// message split into 64-byte chunk messages, queued on files. See net/net_session.h and docs/notes/network.md.

// Shares `data` (read from its start to its highest position) under `name` unless a file of that name is
// listed already: a file message (chunk count, size, name) with a chunk message (the file, its index, up to
// 64 bytes) per 64 bytes. The vector is first filled with one shared chunk (a copy per element when shared,
// as SharedRef copies are), whose elements are then filled in turn.
void ServerSession::ShareFile(const std::string& fileName, GameBuffer* data) {
    for (auto node = files.begin(); node != files.end(); ++node)
        if (node->Get()->name == fileName) return;

    auto* file = new FileMessage();

    std::int32_t size = data->Size();
    std::int32_t chunkCount = (size + kChunkSize - 1) / kChunkSize;
    file->name = fileName;
    file->size = size;
    file->chunkCount = static_cast<std::uint16_t>(chunkCount);

    // Filled with shares of one chunk: each element a clone of it (it is marked copy-on-share by the first share).
    auto& chunks = file->chunks;
    chunks.resize(static_cast<std::size_t>(chunkCount), SharedRef<FileChunkMessage>(new FileChunkMessage()));

    data->Seek(0);
    std::int32_t remaining = size;
    for (std::int32_t i = 0; i < chunkCount; ++i) {
        FileChunkMessage* piece = chunks[i].Get();
        piece->file.Assign(file);
        piece->index = static_cast<std::uint16_t>(i);
        unsigned n = static_cast<unsigned>(remaining > kChunkSize ? kChunkSize : remaining);
        data->Read(piece->data, n);
        remaining -= kChunkSize;
    }

    files.emplace_back(file);
}

// --- Net server commands -----------------------------------------------------------------------------

// The game server's command messages (CommandMessage) and the file requests a client
// hands in, with the host check they use. See net_messages.h and docs/notes/network.md.

// Whether `connection` is the host's (a binding of it with a live host player).
bool ServerSession::IsHostConnection(const Connection* connection) {
    auto* binding = BindingOf(connection);
    if (!binding) return false;
    auto* player = binding->player.Get();
    return IsLive(player) && player->host;
}

// Commands from `from`; each needs the host's connection (IsHostConnection):
//   PreviewMap: the world prepares map `text`;
//   Zone: when a queued map message is named `text`, its suffix picks SetMap (_1),
//      LeaveZone (_2) or CompleteZone (_3) with the name before it;
//   KickByName / BanByName: every guest whose name starts with `text` is kicked / banned;
//   KickBySerial / BanBySerial: the client with serial atoi(text) is kicked / banned.
void ServerSession::OnCommand(Connection* from, CommandMessage* message) {
    std::string* text = &message->text;
    switch (message->kind) {
    case CommandKind::PreviewMap:
        if (IsHostConnection(from)) g_gameFlow.SetNetworkZone(text->data());
        return;
    case CommandKind::Zone: {
        if (!IsHostConnection(from)) return;
        auto node = queuedMaps.begin();
        for (; node != queuedMaps.end(); ++node) {
            auto* entry = node->Get();
            if (entry->text == *text) break;
        }
        if (node == queuedMaps.end()) return;
        const std::size_t cut = text->size() - 2;
        std::string suffix = text->substr(cut);
        std::string zone = text->substr(0, text->size() - 2);
        if (Is(suffix, kZoneEnterSuffix)) SetMap(zone);
        if (Is(suffix, kZoneLeaveSuffix)) LeaveZone();
        if (Is(suffix, kZoneCompleteSuffix)) CompleteZone(zone);
        return;
    }
    case CommandKind::KickByName:
        if (!IsHostConnection(from)) return;
        for (;;) {
            Connection* connection = FindGuestConnection(*text);
            if (!IsLive(connection)) return;
            Kick(connection);
        }
    case CommandKind::BanByName:
        if (!IsHostConnection(from)) return;
        for (;;) {
            Connection* connection = FindGuestConnection(*text);
            if (!IsLive(connection)) return;
            Ban(connection);
        }
    case CommandKind::KickBySerial:
        if (IsHostConnection(from)) Kick(FindConnectionBySerial(std::atoi(text->data())));
        return;
    case CommandKind::BanBySerial:
        if (IsHostConnection(from)) Ban(FindConnectionBySerial(std::atoi(text->data())));
        return;
    default:
        return;
    }
}

} // namespace net
