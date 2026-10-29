// The game server's map change. A literal transcription: the helpers it calls are game
// code outside this file (or STLport strings) and are reached through their original addresses.
// See net_classes.h and docs/notes/network.md.
#include "core_refcount.h"
#include "game_flow_data.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "net_messages.h"
#include "quest_records.h"
#include "ui.h"
#include "world.h"
#include "world_data.h"

#include "lib/crt.h"

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
