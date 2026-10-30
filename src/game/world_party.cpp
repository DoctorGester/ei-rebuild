// WorldServer: the party arriving in a zone (Redeploy) and the quest items it brings back. See world.h and
// docs/notes/world.md.
#include "item_manager.h"
#include "script.h"
#include "world.h"

#include "chunk_stream.h"
#include "core_maths.h"
#include "item.h"
#include "item_db.h"
#include "map_object.h"
#include "object_tables.h"
#include "party.h"
#include "player_messages.h"
#include "unit.h"

#include "lib/crt.h"
#include "lib/containers.h"
#include <bit>
#include <cstdint>
#include <cstring>
#include <string>
#include "lib/pc24.h"
#include "lib/text_case.h"

#include <vector>
#include "game_singletons.h"

namespace {

// Values read from the exe's memory (rule 9).
constexpr float kFacingOffset = /*007AFED8*/ 1.5707964f;   // pi / 2

constexpr int kKindSinglePlayer = 1;
constexpr int kKindNoRangeTest = 3;
constexpr int kKindNetwork = 4;
constexpr int kGameResultLost = 2;
constexpr std::uint32_t kNoStart = 0xC4A42000;   // -1313.0f as the state's last z: no start position

const MapRecord* FindZone(const std::string& name) {
    return g_mapDatabase.FindMap(name);
}

bool Contains(const std::vector<int>& ids, int id) {
    for (int i = 0; i < std::ssize(ids); ++i)
        if (ids[i] == id) return true;
    return false;
}

bool NamesZone(const itemdb::QuestItemRecord* info, const char* zone) {
    bool found = false;
    for (unsigned i = 0; i < static_cast<unsigned>(info->zones.size()); ++i)
        if (text::CompareIgnoringCase(info->zones[i], zone) == 0)
            found = true;
    return found;
}

Item* ItemAt(const std::vector<Item*>* items, int i) { return (*items)[i]; }

// Appends to the list: the list holds a reference.
void AddCountedTail(std::vector<EngineObject*>* list, EngineObject* object) {
    if (object) object->AddRef();
    list->push_back(object);
}


} // namespace

void WorldServer::TakeZoneQuestItems(std::vector<Item*>* items, std::vector<Item*>* list) {
    // The quest ids this zone's objects ask for, each once.
    std::vector<int> wanted{};
    for (auto node = objects.map.begin(); node != objects.map.end() && node->second; ++node) {
        MapObject* object = node->second;
        if (!object->IsUsable()) continue;
        int objectId = object->KeyId();
        if (!Contains(wanted, objectId)) lib::SetAtGrow(wanted, lib::Count(wanted), objectId);
    }

    // Quest items move from the array to the list: in single player only those whose
    // description names this zone or whose id an object asks for; otherwise all of them.
    for (int i = 0; i < std::ssize(*items); ++i) {
        if (ItemAt(items, i)->GetClassId() != ClassId::ItemQuest) continue;
        Item* item = ItemAt(items, i);
        itemdb::QuestItemRecord* info = itemdb::RecordAs<itemdb::QuestItemRecord>(item->GetPrototype());
        if (sessionKind == kKindSinglePlayer && !IsMultiplayerClient()) {
            bool here = info && NamesZone(info, zoneName.c_str());
            if (!here && !Contains(wanted, static_cast<int>(AsQuest(item)->questItemId))) continue;
        }
        list->push_back(ItemAt(items, i));
        items->erase(items->begin() + i);
        --i;
    }
}

void WorldServer::RestorePlayerItems(Player* player) {
    std::vector<Item*> taken;
    if (sessionKind != kKindNetwork) {
        std::vector<Item*>* items = &player->GetParty()->items;
        if (IsMultiplayerClient()) {
            // A client takes everything back.
            for (int i = 0; i < std::ssize(*items); ++i) taken.push_back(ItemAt(items, i));
            items->clear();
        } else {
            TakeZoneQuestItems(items, &taken);
        }
        if (player) {
            // Each item is given back as a fresh copy (the world's copy virtual) keeping its count; the old
            // one is released.
            for (Item* item : taken) {
                Item* fresh = DuplicateItem(item);
                fresh->count = item->count;
                std::vector<Item*> one{fresh};
                player->GiveItems(&one, 0);
                item->Release();
            }
            taken.clear();
        }
    }
    // The list's inline destructor.
    taken.clear();
}

void WorldServer::Redeploy(Player* player, std::vector<UnitPlacement>* placements, char runScripts) {
    if (!player) return;
    RestorePlayerItems(player);
    if (!(sessionKind == kKindSinglePlayer || sessionKind == kKindNoRangeTest) || zoneLoaded == 0) return;

    Party* party = player->GetParty();
    std::vector<PartyUnitRecord*> records{};
    for (int i = 0; i < std::ssize(party->members); ++i) {
        int index = lib::Count(records);
        records.resize(index + 1);
        records[index] = &party->members[i];
    }
    // A lost game sends the party back to this zone's first exit.
    if (gameResult == kGameResultLost) {
        party->zone = zoneName;
        party->entrance = 0;
    }

    // The party arrives at the centre of its entrance's deploy rectangle (the target zone's exit
    // `entrance`), facing away from the map's centre unless the exit gives a heading.
    float cell = 0.5f;
    float centreX = static_cast<float>(aiMap.cells.width) * cell * 0.5f;
    float centreY = static_cast<float>(aiMap.cells.height) * cell * 0.5f;
    Quaternion rotation;
    rotation = kNewUnitRotation;
    Vector3 start{0.0f, 0.0f, 0.0f};
    std::uint32_t startValue = 0;
    const ZoneExit* exit = nullptr;
    const MapRecord* zone = FindZone(party->zone);
    int entrance = static_cast<int>(party->entrance);
    if (zone && entrance >= 0 && entrance < std::ssize(zone->exits)) {
        exit = &zone->exits[entrance];
        start.x = (exit->deploy[2] + exit->deploy[0]) * 0.5f;
        start.y = (exit->deploy[3] + exit->deploy[1]) * 0.5f;
        start.z = 0.0f;
        startValue = std::bit_cast<std::uint32_t>(exit->view);
    }
    float angle = pc24::Sub(pc24::Atan2(start.y - centreY, start.x - centreX), kFacingOffset);
    if (exit && exit->hasDeployAngle) angle = exit->deployAngle;
    Vector3 up{0.0f, 0.0f, 1.0f};
    rotation.FromAxisAngle(up, angle);

    CreateUnits(player, &records, start, rotation, placements);
    if (placements) placements->clear();
    PlayerServer* state = player->Server();
    std::uint32_t lastZ;
    lastZ = std::bit_cast<std::uint32_t>(state->lastPosition.z);
    if (lastZ != kNoStart) {
        state->lastPosition = start;
        state->SendPosition04(start, startValue, sessionKind);
    }
    player->lastTick = tick;
    if (sessionKind == kKindNoRangeTest) UpdateMercBriefings();
    if (runScripts) {
        scriptsActive = 1;
        for (int i = 0; i < 5; ++i) ScriptTick(MainScript());
    }
}

// ---- CopyItem ---------------------------------------------------------------------------------

Item* WorldServer::CopyItem(Item* source) {
    // The game also builds and frees two empty CPtrLists here, unused.
    std::uint32_t packed = source->prototypeId;
    ItemKind itemKind = source->Kind();
    ItemManager* factory = ItemFactory();
    Item* copy = factory->MakeItem(itemKind, static_cast<int>(packed >> 16), static_cast<int>(packed & 0xFFFF));
    source->CopyTo(copy, GetSpellManager());
    copy->charge = source->charge;
    // An item below its maximum is kept on the counted list (once).
    float current = source->charge;
    float maximum = source->chargeMax;
    if (maximum > current && copy && !lib::Contains(counted, copy)) AddCountedTail(&counted, copy);
    return copy;
}

// ---- Items and spells by name -------------------------------------------------------------------

// As in CopyItem, the game also builds and frees two empty CPtrLists around the call.
Item* WorldServer::CreateItemNamed(const char* name) { return g_serverWorld.itemManager->CreateItem(name); }

Item* WorldServer::CreateSpellNamed(const char* name) {
    ItemManager* manager = g_serverWorld.itemManager;    // read first
    return manager->CreateSpellItem(name, GetSpellManager());
}

void WorldServer::AddCounted(EngineObject* item) {
    if (!item || lib::Contains(counted, item)) return;
    AddCountedTail(&counted, item);
}

void WorldServer::UseItemUp(Unit* unit, Item* item) {
    if (unit->player) {
        if (ItemLink* link = item->ServerLink()) {
            Party* party = unit->player->GetParty();
            if (party->Find(unit->id)) {
                Item* source = FindItem(party->ItemFactory(), link->value);
                if (source) --source->count;
            }
        }
    }
    --item->count;
}
