// WorldServer: the party arriving in a zone (Redeploy) and the quest items it brings back. See world.h and
// docs/notes/world.md.
#include "game/items/item_manager.h"
#include "game/script/script.h"
#include "game/world/world.h"

#include "game/map/chunk_stream.h"
#include "engine/core/core_maths.h"
#include "game/items/item.h"
#include "game/items/item_db.h"
#include "game/objects/map_object.h"
#include "game/map/map_database.h"
#include "game/players/party.h"
#include "game/players/player_messages.h"
#include "game/players/player.h"
#include "game/units/unit.h"

#include "lib/crt.h"
#include "lib/containers.h"
#include <bit>
#include <cstdint>
#include <cstring>
#include <string>
#include "lib/maths.h"
#include "lib/text_case.h"

#include <vector>
#include "game/app/singletons.h"

namespace {

constexpr float kFacingOffset = /*007AFED8*/ 1.5707964f;   // pi / 2

constexpr float kNoStartZ = -1313.0f;   // the state's last z: no start position

bool NamesZone(const itemdb::QuestItemRecord* info, std::string_view zone) {
    bool found = false;
    for (unsigned i = 0; i < static_cast<unsigned>(info->zones.size()); ++i)
        if (text::CompareIgnoringCase(info->zones[i], zone) == 0)
            found = true;
    return found;
}

Item* ItemAt(const std::vector<Item*>& items, int i) { return items[i]; }

// Appends to the list: the list holds a reference.
void AddCountedTail(std::vector<EngineObject*>* list, EngineObject* object) {
    if (object) object->AddRef();
    list->push_back(object);
}


} // namespace

std::vector<Item*> WorldServer::TakeZoneQuestItems(std::vector<Item*>* items) {
    std::vector<Item*> taken;
    // The quest ids this zone's objects ask for, each once.
    std::vector<int> wanted{};
    for (auto node = objects.map.begin(); node != objects.map.end() && node->second; ++node) {
        MapObject* object = node->second;
        if (!object->IsUsable()) continue;
        int objectId = object->KeyId();
        if (!lib::Contains(wanted, objectId)) wanted.push_back(objectId);
    }

    // Quest items move from the array to the list: in single player only those whose
    // description names this zone or whose id an object asks for; otherwise all of them.
    for (int i = 0; i < std::ssize(*items); ++i) {
        if (ItemAt(*items, i)->GetClassId() != ClassId::ItemQuest) continue;
        Item* item = ItemAt(*items, i);
        itemdb::QuestItemRecord* info = itemdb::RecordAs<itemdb::QuestItemRecord>(item->GetPrototype());
        if (zoneMode == ZoneMode::Battle && !IsMultiplayerClient()) {
            bool here = info && NamesZone(info, zoneName);
            if (!here && !lib::Contains(wanted, static_cast<int>(item->AsQuest()->questItemId))) continue;
        }
        taken.push_back(ItemAt(*items, i));
        items->erase(items->begin() + i);
        --i;
    }
    return taken;
}

void WorldServer::RestorePlayerItems(Player* player) {
    std::vector<Item*> taken;
    if (zoneMode != ZoneMode::Edge) {
        std::vector<Item*>* items = &player->GetParty()->items;
        if (IsMultiplayerClient()) {
            // A client takes everything back.
            for (int i = 0; i < std::ssize(*items); ++i) taken.push_back(ItemAt(*items, i));
            items->clear();
        } else {
            taken = TakeZoneQuestItems(items);
        }
        if (player) {
            // Each item is given back as a fresh copy (the world's copy virtual) keeping its count; the old
            // one is released.
            for (Item* item : taken) {
                Item* fresh = DuplicateItem(item);
                fresh->count = item->count;
                std::vector<Item*> one{fresh};
                player->GiveItems(one, 0);
                item->Release();
            }
            taken.clear();
        }
    }
    taken.clear();
}

void WorldServer::Redeploy(Player* player, std::vector<UnitPlacement>* placements, char runScripts) {
    if (!player) return;
    RestorePlayerItems(player);
    if (!IsSinglePlayerMode(zoneMode) || zoneLoaded == 0) return;

    Party* party = player->GetParty();
    std::vector<PartyUnitRecord*> records{};
    for (int i = 0; i < std::ssize(party->members); ++i) {
        records.push_back(&party->members[i]);
    }
    // A lost game sends the party back to this zone's first exit.
    if (gameState == GameState::Network) {
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
    const MapRecord* zone = g_mapDatabase.FindMap(party->zone);
    int entrance = static_cast<int>(party->entrance);
    if (zone && entrance >= 0 && entrance < std::ssize(zone->exits)) {
        exit = &zone->exits[entrance];
        start.x = (exit->deploy[2] + exit->deploy[0]) * 0.5f;
        start.y = (exit->deploy[3] + exit->deploy[1]) * 0.5f;
        start.z = 0.0f;
        startValue = std::bit_cast<std::uint32_t>(exit->view);
    }
    float angle = lib::Atan2(start.y - centreY, start.x - centreX) - kFacingOffset;
    if (exit && exit->hasDeployAngle) angle = exit->deployAngle;
    Vector3 up{0.0f, 0.0f, 1.0f};
    rotation = Quaternion::AxisAngle(up, angle);

    CreateUnits(player, &records, start, rotation, placements);
    if (placements) placements->clear();
    PlayerServer* state = player->Server();
    if (state->lastPosition.z != kNoStartZ) {
        state->lastPosition = start;
        state->SendCameraPlacement(start, startValue, static_cast<std::uint32_t>(zoneMode));
    }
    player->lastTick = tick;
    if (zoneMode == ZoneMode::Base) UpdateMercBriefings();
    if (runScripts) {
        scriptsActive = 1;
        for (int i = 0; i < 5; ++i) MainScript()->Tick();
    }
}

// ---- CopyItem ---------------------------------------------------------------------------------

Item* WorldServer::CopyItem(Item* source) {
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

Item* WorldServer::CreateItemNamed(std::string_view name) { return itemManager->CreateItem(name); }

Item* WorldServer::CreateSpellNamed(std::string_view name) { return itemManager->CreateSpellItem(name, GetSpellManager()); }

void WorldServer::AddCounted(EngineObject* item) {
    if (!item || lib::Contains(counted, item)) return;
    AddCountedTail(&counted, item);
}

void WorldServer::UseItemUp(Unit* unit, Item* item) {
    if (unit->player) {
        if (ItemLink* link = item->serverLink.Get()) {
            Party* party = unit->player->GetParty();
            if (party->Find(unit->id)) {
                Item* source = party->ItemFactory()->FindObjectAs<Item>(link->value);
                if (source) --source->count;
            }
        }
    }
    --item->count;
}
