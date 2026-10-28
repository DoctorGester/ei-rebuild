// The game server's map change. A literal transcription: the helpers it calls are game
// code outside this file (or STLport strings) and are reached through their original addresses.
// See net_classes.h and docs/notes/network.md.
#include "net_messages.h"
#include "quest_records.h"
#include "game_strings.h"
#include "core_refcount.h"

#include "lib/crt.h"
#include "ui.h"
#include "world.h"
#include "world_data.h"
#include "game_flow_data.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstring>

namespace net {

namespace {

// The world (0x7AFE20) and the text table (0x7C7D50), the map registry (0x7C87D4). The game flow at 0x7AFE20
// (world_briefing.cpp, world_game.cpp): loading a zone's quests. vector<string> (library): the block free.
void NamesFree(std::string* a0, std::size_t a1) { stlp::deallocate(a0, a1 * sizeof(std::string)); }



} // namespace

// Empties queuedMaps_ and variables_, queues a map message (name + suffix) on the first, replaces map_ by a fresh
// one and copies the name into it and into mapName_, loads the map into the world and announces its title. Then,
// when the map registry knows "name + suffix", sets a script variable to 1 for each of the map's names (inline,
// through FindOrAddVariable) and for two paths built from the map name (SetVariable), and queues the host's item
// awards (without the final award).
void ServerSession::SetMap(const std::string* name) {
    queuedMaps_.Clear();
    variables_.Clear();

    {
        std::string messageName = *name + game::ZoneSuffix2Text();
        auto* message = NewCounted<ZoneMessage>();
        message->text_ = messageName;
        RefPtr ref;
        queuedMaps_.PushBack(*RefPtrFromRaw(&ref, message));
        RefPtrClear(&ref);
    }

    auto* mapMessage = NewCounted<MapMessage>();
    RefPtr* holder = &map_;
    if (mapMessage != holder->ptr) {
        holder->Destroy();
        holder->Share(mapMessage);
    }
    MapMessage* current = map_.Get();
    if (name != &current->text_) current->text_ = *name;
    if (name != &mapName_) mapName_ = *name;

    GameFlow* world = TheGameFlow();
    world->RegisterQuests(name->data());
    world->SetNetworkZone(name->data());
    {
        mfc::CString title(mfc::CString::kUnset), text(mfc::CString::kUnset);   // constructed by the calls
        const char* mapTitle = *SessionMapNameInto(&title);
        Announce(*TheInterface()->MessageText(&text, 5, mapTitle), nullptr);
    }

    QuestBriefingRecord* entry;
    {
        std::string registryName = *name + game::ZoneSuffix1Text();
        entry = stlp::NamedSlot<QuestBriefingRecord>(worlddata::QuestBriefings(), registryName);
    }
    if (!entry) return;

    const stlp::vector<std::string>& quests = entry->lists[2];   // the quests given
    for (const std::string* it = quests.begin(); it != quests.end(); ++it) {
        std::string resourceName(*it);
        RefNode* added;
        FindOrAddVariable(&added, &resourceName);
        auto* record = variables_.At(added);
        const std::uint32_t one = 0x3F800000;   // 1.0f
        std::memcpy(&record->value_, &one, 4);
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
    stlp::vector<std::string> zones;    // empty
    PickZones(&zones, kind_, mapName_.data());
    for (std::string* zone = zones.begin(); zone != zones.end(); ++zone) {
        {
            std::string key = *zone + game::ZoneSuffix1Text();
            auto* message = NewCounted<ZoneMessage>();
            message->text_ = key;
            RefPtr ref;
            queuedMaps_.PushBack(*RefPtrFromRaw(&ref, message));
            RefPtrClear(&ref);
        }
        LoadZoneQuests(zone->data());
    }
    for (std::string* zone = zones.begin(); zone != zones.end(); ++zone) std::destroy_at(zone);
    NamesFree(zones.begin(), static_cast<std::size_t>(zones.EndOfStorage() - zones.begin()));
}

// When the held map message names `name`: drops it, empties the queued messages
// And queues a map message "name" + "_3". Returns whether it did.
bool ServerSession::FinishMap(const std::string* name) {
    auto* holder = &map_;
    MapMessage* map = holder->Get();
    if (!IsLive(map)) return false;
    const std::string* held = &map->text_;
    if (!(*held == *name)) return false;

    if (CRefObject* object = holder->ptr) {
        if (object->refs_ != 1) {
            object->flags7_ |= CRefObject::kDetached;
            object->Shutdown();
        }
        holder->ptr->Release();
        holder->Share(nullptr);
    }
    queuedMaps_.Clear();

    std::string messageName = *name + game::ZoneSuffix3Text();
    auto* message = NewCounted<ZoneMessage>();
    message->text_ = messageName;
    RefPtr ref;
    queuedMaps_.PushBack(*RefPtrFromRaw(&ref, message));
    RefPtrClear(&ref);
    return true;
}

} // namespace net
