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

// Empties queuedMaps_ and variables_, queues a map message (name + suffix) on the first, replaces map_ by a fresh
// one and copies the name into it and into mapName_, loads the map into the world and announces its title. Then,
// when the map registry knows "name + suffix", sets a script variable to 1 for each of the map's names (inline,
// through FindOrAddVariable) and for two paths built from the map name (SetVariable), and queues the host's item
// awards (without the final award).
void ServerSession::SetMap(const std::string* name) {
    queuedMaps_.clear();
    variables_.clear();

    {
        std::string messageName = *name + game::ZoneSuffix2Text();
        auto* message = NewCounted<ZoneMessage>();
        message->text_ = messageName;
        queuedMaps_.emplace_back(message);
    }

    auto* mapMessage = NewCounted<MapMessage>();
    ShareAssignOwned(map_, mapMessage);
    MapMessage* current = map_.Get();
    if (name != &current->text_) current->text_ = *name;
    if (name != &mapName_) mapName_ = *name;

    GameFlow* world = &g_gameFlow;
    world->RegisterQuests(name->data());
    world->SetNetworkZone(name->data());
    {
        std::string title;
        const char* mapTitle = (*SessionMapNameInto(&title)).c_str();
        Announce(g_interface.MessageText(5, mapTitle), nullptr);
    }

    QuestBriefingRecord* entry;
    {
        std::string registryName = *name + game::ZoneSuffix1Text();
        entry = worlddata::QuestBriefings()[registryName];
    }
    if (!entry) return;

    const std::vector<std::string>& quests = entry->lists[2];   // the quests given
    for (const auto& it : quests) {
        std::string resourceName(it);
        auto* record = FindOrAddVariable(&resourceName)->Get();
        record->value_ = 1.0f;
        record->serial_ = g_variableSerial;
        ++g_variableSerial;
        ++record->version_;
    }

    // "<prefix><name>.<name>" and the same with the path suffix; the temporaries freed in reverse.
    {
        std::string prefix(flowdata::kQuestVariablePrefix);
        std::string a = prefix + *name;
        std::string b = a + flowdata::kDot;
        std::string path = b + *name;
        SetVariable(&path, 1.0f);
    }
    {
        std::string prefix(flowdata::kQuestVariablePrefix);
        std::string a = prefix + *name;
        std::string b = a + flowdata::kDot;
        std::string c = b + *name;
        std::string path = c + game::PathSuffixText();
        SetVariable(&path, 1.0f);
    }

    AwardHost(entry, false);
}
// Unloads the flow's quest texts, then queues a map message ("zone" + "_1") on queuedMaps_ for each zone
// the flow picks for the server's kind after the current map, loading each zone's quests.
void ServerSession::QueueMapList() {
    UnloadQuestTexts();
    std::vector<std::string> zones;    // empty
    PickZones(&zones, kind_, mapName_.data());
    for (auto& zone : zones) {
        {
            std::string key = zone + game::ZoneSuffix1Text();
            auto* message = NewCounted<ZoneMessage>();
            message->text_ = key;
            queuedMaps_.emplace_back(message);
        }
        LoadZoneQuests(zone.data());
    }
}

// When the held map message names `name`: drops it, empties the queued messages
// And queues a map message "name" + "_3". Returns whether it did.
bool ServerSession::FinishMap(const std::string* name) {
    auto* holder = &map_;
    MapMessage* map = holder->Get();
    if (!IsLive(map)) return false;
    const std::string* held = &map->text_;
    if (!(*held == *name)) return false;

    DestroyOwned(*holder);
    queuedMaps_.clear();

    std::string messageName = *name + game::ZoneSuffix3Text();
    auto* message = NewCounted<ZoneMessage>();
    message->text_ = messageName;
    queuedMaps_.emplace_back(message);
    return true;
}

} // namespace net
