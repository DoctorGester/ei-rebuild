// CWorldServer: the party arriving in a zone (Redeploy) and the quest items it brings back. See world.h and
// docs/notes/world.md.
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
#include <cstdint>
#include <cstring>
#include <string>
#include "lib/text_case.h"

#include <list>
#include <vector>
#include "ei/x87.h"
#include "game_singletons.h"

namespace {

// Values read from the exe's memory (rule 9).
float Half() { return 0.5f; }
float CellSize() { return 1.0f; }        // the map cell size
constexpr float kFacingOffset = /*007AFED8*/ 1.5707964f;   // pi / 2

constexpr int kClassQuestItem = 0x14;
constexpr int kKindSinglePlayer = 1;
constexpr int kKindNoRangeTest = 3;
constexpr int kKindNetwork = 4;
constexpr int kGameResultLost = 2;
constexpr std::uint32_t kNoStart = 0xC4A42000;   // -1313.0f as the state's last z: no start position

const MapRecord* FindZone(const std::string& name) {
    return g_mapDatabase->FindMap(name);
}

// AddTail through the game's out-of-line NewNode.
void AddTail(std::list<CItem*>& list, CItem* object) {
    auto node = list.emplace(list.end());
    *node = object;
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

CItem* ItemAt(const std::vector<CItem*>* items, int i) { return (*items)[i]; }

// Appends to the list: the list holds a reference.
void AddCountedTail(std::list<CEngineObject*>* list, CEngineObject* object) {
    HeldObject<CEngineObject>::Of(&list->emplace_back(nullptr))->Assign(object);
}


} // namespace

void CWorldServer::TakeZoneQuestItems(std::vector<CItem*>* items, std::list<CItem*>* list) {
    // The quest ids this zone's objects ask for, each once.
    std::vector<int> wanted{};
    for (auto node = objects_.map.begin(); node != objects_.map.end() && node->second; ++node) {
        CMapObject* object = node->second;
        if (!object->IsUsable()) continue;
        int id = object->KeyId();
        if (!Contains(wanted, id)) lib::SetAtGrow(wanted, std::ssize(wanted), id);
    }

    // Quest items move from the array to the list: in single player only those whose
    // description names this zone or whose id an object asks for; otherwise all of them.
    for (int i = 0; i < std::ssize(*items); ++i) {
        if (ItemAt(items, i)->GetClassId() != kClassQuestItem) continue;
        CItem* item = ItemAt(items, i);
        itemdb::QuestItemRecord* info = itemdb::RecordAs<itemdb::QuestItemRecord>(item->GetPrototype());
        if (sessionKind_ == kKindSinglePlayer && !IsMultiplayerClient()) {
            bool here = info && NamesZone(info, zoneName_.c_str());
            if (!here && !Contains(wanted, static_cast<int>(AsQuest(item)->questItemId_))) continue;
        }
        AddTail(*list, ItemAt(items, i));
        items->erase(items->begin() + i);
        --i;
    }
}

void CWorldServer::RestorePlayerItems(CPlayer* player) {
    std::list<CItem*> taken;
    if (sessionKind_ != kKindNetwork) {
        std::vector<CItem*>* items = &player->Party()->items_;
        if (IsMultiplayerClient()) {
            // A client takes everything back.
            for (int i = 0; i < std::ssize(*items); ++i) AddTail(taken, ItemAt(items, i));
            items->clear();
        } else {
            TakeZoneQuestItems(items, &taken);
        }
        if (player) {
            // Each item is given back as a fresh copy (the world's copy virtual) keeping its count; the old
            // one is released.
            for (auto node = taken.begin(); node != taken.end();) {
                auto next = std::next(node);
                CItem* item = *node;
                std::list<CItem*> one;
                CItem* fresh = DuplicateItem(item);
                fresh->count_ = item->count_;
                AddTail(one, fresh);
                player->GiveItems(&one, 0);
                item->Release();
                node = next;
            }
            taken.clear();
        }
    }
    // The list's inline destructor.
    taken.clear();
}

void CWorldServer::Redeploy(CPlayer* player, std::vector<UnitPlacement>* placements, char runScripts) {
    if (!player) return;
    RestorePlayerItems(player);
    if (!(sessionKind_ == kKindSinglePlayer || sessionKind_ == kKindNoRangeTest) || zoneLoaded_ == 0) return;

    CParty* party = player->Party();
    std::vector<PartyUnitRecord*> records{};
    for (int i = 0; i < std::ssize(party->members_); ++i) {
        int index = std::ssize(records);
        records.resize(index + 1);
        records[index] = &party->members_[i];
    }
    // A lost game sends the party back to this zone's first exit.
    if (gameResult_ == kGameResultLost) {
        party->zone_ = zoneName_;
        party->entrance_ = 0;
    }

    // The party arrives at the centre of its entrance's deploy rectangle (the target zone's exit
    // `entrance_`), facing away from the map's centre unless the exit gives a heading.
    double cell = static_cast<double>(CellSize()) * Half();
    float centreX = static_cast<float>(aiMap_.cells.width * cell * Half());
    float centreY = static_cast<float>(aiMap_.cells.height * cell * Half());
    Quaternion rotation;
    std::memcpy(&rotation, &kNewUnitRotation, sizeof(rotation));
    Vector3 start{0.0f, 0.0f, 0.0f};
    std::uint32_t startValue = 0;
    const ZoneExit* exit = nullptr;
    const MapRecord* zone = FindZone(party->zone_);
    int entrance = static_cast<int>(party->entrance_);
    if (zone && entrance >= 0 && entrance < std::ssize(zone->exits)) {
        exit = &zone->exits[entrance];
        start.x = static_cast<float>((static_cast<double>(exit->deploy[2]) + exit->deploy[0]) * Half());
        start.y = static_cast<float>((static_cast<double>(exit->deploy[3]) + exit->deploy[1]) * Half());
        start.z = 0.0f;
        std::memcpy(&startValue, &exit->view, 4);
    }
    float angle = static_cast<float>(x87::fpatan(static_cast<double>(start.y) - centreY,
                                                       static_cast<double>(start.x) - centreX) - kFacingOffset);
    if (exit && exit->hasDeployAngle) std::memcpy(&angle, &exit->deployAngle, sizeof(angle));
    Vector3 up{0.0f, 0.0f, 1.0f};
    rotation.FromAxisAngle(up, angle);

    CreateUnits(player, &records, &start, rotation, placements);
    if (placements) placements->clear();
    CPlayerServer* state = player->Server();
    std::uint32_t lastZ;
    std::memcpy(&lastZ, &state->lastPosition_.z, 4);
    if (lastZ != kNoStart) {
        std::memcpy(&state->lastPosition_, &start, sizeof(start));
        state->SendPosition04(&start, startValue, sessionKind_);
    }
    player->lastTick_ = tick_;
    if (sessionKind_ == kKindNoRangeTest) UpdateMercBriefings();
    if (runScripts) {
        scriptsActive_ = 1;
        for (int i = 0; i < 5; ++i) ScriptTick(MainScript());
    }
}

// ---- CopyItem ---------------------------------------------------------------------------------

CItem* CWorldServer::CopyItem(CItem* source) {
    // The game also builds and frees two empty CPtrLists here, unused.
    std::uint32_t packed = source->prototypeId_;
    int type = source->type_;
    CItemManager* factory = ItemFactory();
    CItem* copy = factory->MakeItem(type, static_cast<int>(packed >> 16), static_cast<int>(packed & 0xFFFF), -1);
    source->CopyTo(copy, SpellManager());
    std::memcpy(&copy->charge_, &source->charge_, 4);
    // An item below its maximum is kept on the counted list (once).
    float current = source->charge_;
    float maximum = source->chargeMax_;
    if (maximum > current && copy && !lib::Contains(counted_, copy)) AddCountedTail(&counted_, copy);
    return copy;
}

// ---- Items and spells by name -------------------------------------------------------------------

// As in CopyItem, the game also builds and frees two empty CPtrLists around the call.
CItem* CWorldServer::CreateItemNamed(const char* name) { return g_serverWorld->itemManager_->CreateItem(name); }

CItem* CWorldServer::CreateSpellNamed(const char* name) {
    CItemManager* manager = g_serverWorld->itemManager_;   // read first
    return manager->CreateSpellItem(name, SpellManager());
}

void CWorldServer::AddCounted(CEngineObject* item) {
    if (!item || lib::Contains(counted_, item)) return;
    AddCountedTail(&counted_, item);
}

void CWorldServer::UseItemUp(CUnit* unit, CItem* item) {
    if (unit->player) {
        if (CItemLink* link = item->ServerLink()) {
            CParty* party = unit->player->Party();
            if (party->Find(unit->id_)) {
                CItem* source = FindItem(party->ItemFactory(), link->value);
                if (source) --source->count_;
            }
        }
    }
    --item->count_;
}
