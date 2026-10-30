// WorldServer: the quest variables the server keeps for the scripts (the mercenaries' briefing
// variables "b.mercN.nK_N"). See world.h and docs/notes/world.md.
#include "combat.h"
#include "console.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "game_strings.h"
#include "item.h"
#include "item_manager.h"
#include "map_object.h"
#include "object_tables.h"
#include "party.h"
#include "player_manager.h"
#include "player_messages.h"
#include "quest_records.h"
#include "script.h"
#include "text_split.h"
#include "ui_briefing.h"
#include "unit.h"
#include "unit_ai.h"
#include "unit_db.h"
#include "world.h"
#include "world_data.h"

#include "lib/nan_audit.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include "ei/rebirth.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int kMercSlots = 10;

float VariableGet(NamedFloatTable* table, std::string_view name) { return table->Get(name); }

// The briefing variables of one mercenary slot (declared so that they are destroyed in the
// original's order: n11, amerc, n10, n2, n8, n9, n3, n1).
struct Briefing {
    std::string n1, n3, n9, n8, n2, n10, hired, n11;
};

CParty* PartyOf(CPlayer* player) { return player->Party(); }
PartyUnitRecord& MercRecord(CPlayer* player, int index) { return player->Party()->members[index]; }

constexpr int kKindNoRangeTest = 3;

// The item named `name` (the game builds two unused temporary lists around the creation).
Item* CreateNamedItem(std::string_view name) { return g_serverWorld.itemManager->CreateItem(name); }

void GiveBriefingRewards(WorldServer* world, CPlayer* player, QuestBriefingRecord* record) {
    float experience = record->exp;
    EI_NAN_AUDIT(experience);
    if (experience != 0.0f) {
        world->AwardExperience(player, experience);
    }
    PartyOf(player)->money += static_cast<int>(static_cast<std::int64_t>(record->money));   // _ftol: 0 for a NaN

    std::vector<Item*> items;
    for (const std::string& name : record->lists[QuestBriefingRecord::kGiveItems]) {
        Item* item = CreateNamedItem(name);
        if (item) items.push_back(item);
    }
    if (!items.empty()) player->GiveItems(&items, 0);

    for (const std::string& name : record->lists[QuestBriefingRecord::kRemoveItems]) {
        Item* item = CreateNamedItem(name);
        if (!item) continue;
        item->AddRef();
        if (item->GetClassId() == ClassId::ItemQuest)
            player->TakeLoot(static_cast<int>(AsQuest(item)->questItemId));
        item->Release();
    }

    PlayerServer* state = player->Server();
    for (const std::string& name : record->lists[QuestBriefingRecord::kGiveQuests])
        state->SetVariableMax(name, 1.0f);
    for (const std::string& name : record->lists[QuestBriefingRecord::kCompleteQuests])
        state->SetVariable(name, std::bit_cast<std::uint32_t>(2.0f));
    for (const std::string& name : record->lists[QuestBriefingRecord::kAvailableZones])
        state->SetVariableMax(name, 1.0f);
    for (const std::string& name : record->lists[QuestBriefingRecord::kCompleteZones])
        state->SetVariable(name, std::bit_cast<std::uint32_t>(2.0f));

    items.clear();
}

// One slot's names, declared so that they are destroyed in the game's order: amerc, n10, n2, n8, n9, n3,
// n1, merc.
struct MercNames {
    std::string merc, n1, n3, n9, n8, n2, n10, hired;
};

constexpr float kMinimumWeight = 0.01f;

// An element of the list of shares: the unit's controller and its experience (nodes of 0x10 bytes).
struct Share {
    UnitServer* controller;
    float share;
};

// The weight of a unit in the shared experience: sqrt(pending + gained experience) of its controller's statistics.
float Weight(UnitServer* controller) {
    return std::sqrt(controller->stats.pendingExperience + controller->stats.experience);
}

// Without a player: the single heroes of every player share `amount` in proportion to their weights.
void ShareAmongSoloPlayers(WorldServer* world, float amount) {
    if (world->PlayerList()->objects.size() == 0) return;
    std::vector<Share> list{};
    auto soloUnit = [&](CPlayer* p) -> CUnit* {
        if (!p->Server() || std::ssize(p->Party()->members) != 1) return nullptr;
        return world->FindUnit(static_cast<int>(MercRecord(p, 0).unitId));
    };

    float total = 0.0f;
    PlayerIterator it;
    for (CPlayer* p = it.First(world->PlayerList()); p; p = it.Next()) {
        CUnit* unit = soloUnit(p);
        if (!unit) continue;
        UnitServer* controller = unit->ServerController();
        if (!controller) continue;
        list.push_back({controller, 0.0f});
        total = Weight(controller) + total;
        float stored = total;   // stored into the float total
        total = stored;
    }
    float minimum;
    minimum = kMinimumWeight;
    float divisor = total >= minimum ? total : minimum;   // branch-free select

    for (Share& entry : list) {
        float share = Weight(entry.controller) * amount / divisor;
        float stored = share;
        entry.share = stored;
        entry.controller->AddExperience(stored);
    }
    auto cursor = list.begin();
    for (CPlayer* p = it.First(world->PlayerList()); p; p = it.Next()) {
        CUnit* unit = soloUnit(p);
        if (!unit || !unit->ServerController()) continue;
        MercRecord(p, 0).stats.AddScaledExperience((cursor++)->share);
    }
    for (CPlayer* p = it.First(world->PlayerList()); p; p = it.Next()) {
        PlayerServer* state = p->Server();
        if (!state) continue;
        for (auto& node : list) state->SendPair18(node.controller->unit->id, std::bit_cast<std::uint32_t>(node.share));
    }
    if (it.manager) {
        --it.manager->refCount;
        it.manager = nullptr;
    }
    list.clear();
}

// With a player: its living heroes share `amount` equally.
void ShareAmongHeroes(WorldServer* world, CPlayer* player, float amount) {
    std::vector<int> heroes{};
    for (auto node = player->units.begin(); node != player->units.end() && node->second; ++node) {
        CUnit* unit = node->second;
        if (unit->IsDead() != 0 || unit->MapType() != MapObjectType::Hero) continue;
        int id = unit->id;
        int index = std::ssize(heroes);
        heroes.resize(index + 1);
        heroes[index] = id;
    }
    int count = std::ssize(heroes);
    if (count >= 1) {
        float share = amount / static_cast<float>(count);
        float stored = share;
        share = stored;
        for (int i = 0; i < std::ssize(heroes); ++i) {
            CUnit* unit = world->FindUnit(heroes[i]);
            unit->ServerController()->AddExperience(share);
            for (int j = 0; j < std::ssize(player->Party()->members); ++j) {
                PartyUnitRecord& record = MercRecord(player, j);
                if (static_cast<int>(record.unitId) == heroes[i]) record.stats.AddScaledExperience(share);
            }
        }
        player->Server()->SendValue15(std::bit_cast<std::uint32_t>(share));
    }
}

constexpr int kQuickSlots = 4;
constexpr int kWornSlots = 7;
constexpr int kBeltSlots = 4;
constexpr int kSpellSlots = 8;

// The party's copy of an item (CParty's copy virtual; the record entry's source).
Item* PartyCopy(CParty* party, Item* item) { return party->CopyItem(item); }

// The record entry for `item` from the party records, with a reference held across `add`.
template <auto Add>
Item* AddToRecord(CParty* party, PartyUnitRecord* record, Item* item) {
    Item* entry = PartyCopy(party, item);
    if (entry) entry->AddRef();
    Item* added = (record->*Add)(entry);
    if (entry) entry->Release();
    return added;
}

// The record's four lists emptied (armour, weapons, spells, quick items: the first entry each time).
void ClearRecordItems(PartyUnitRecord* record) {
    while (!record->armor.empty()) record->RemoveArmor(record->armor[0]);
    while (!record->weapons.empty()) record->RemoveWeapon(record->weapons[0]);
    while (!record->spells.empty()) record->RemoveSpell(record->spells[0]);
    while (!record->quickItems.empty()) record->RemoveQuickItem(record->quickItems[0]);
}

void SyncRecord(CParty* party, PartyUnitRecord* record, CUnit* unit) {
    UnitServer* controller = unit->ServerController();
    ClearRecordItems(record);

    for (int slot = 0; slot < kQuickSlots; ++slot) {
        Item* item = unit->GetWeapon(slot);
        if (!item) continue;
        EngineObject* entry = AddToRecord<&PartyUnitRecord::AddWeapon>(party, record, item);
        ItemLink* ref = new ItemLink();
        ref->value = static_cast<std::uint32_t>(entry->id);
        item->link.Assign(ref);
    }
    for (int slot = 0; slot < kWornSlots; ++slot) {
        Item* item = unit->WornItem(slot);
        if (!item) continue;
        EngineObject* entry = AddToRecord<&PartyUnitRecord::AddArmor>(party, record, item);
        ItemLink* ref = new ItemLink();
        ref->value = static_cast<std::uint32_t>(entry->id);
        item->link.Assign(ref);
    }
    for (int slot = 0; slot < kBeltSlots; ++slot) {
        auto* item = unit->GetQuickItem(slot);
        if (!item || static_cast<int>(item->count) <= 0) continue;
        Item* entry = PartyCopy(party, item);
        entry->count = item->count;     // before the null test
        if (entry) entry->AddRef();
        EngineObject* added = (EngineObject*)record->AddQuickItem(entry);
        if (entry) entry->Release();
        ItemLink* link = new ItemLink();
        link->value = static_cast<std::uint32_t>(added->id);
        item->link.Assign(link);
    }
    for (int slot = 0; slot < kSpellSlots; ++slot) {
        Item* spell = unit->GetSpell(slot);
        if (!spell) continue;
        AddToRecord<&PartyUnitRecord::AddSpell>(party, record, spell);
    }
    controller->CopyStats(&record->stats);
}

constexpr int kKindNetwork = 4;   // kKindNoRangeTest (3): above

// As SyncPartyRecords does, without the items' records.
void StoreRecord(WorldServer* world, CParty* party, PartyUnitRecord* record, CUnit* unit) {
    UnitServer* controller = unit->ServerController();
    ClearRecordItems(record);
    for (int slot = 0; slot < kQuickSlots; ++slot)
        if (Item* item = unit->GetWeapon(slot)) AddToRecord<&PartyUnitRecord::AddWeapon>(party, record, item);
    for (int slot = 0; slot < kWornSlots; ++slot)
        if (Item* item = unit->WornItem(slot)) AddToRecord<&PartyUnitRecord::AddArmor>(party, record, item);
    for (int slot = 0; slot < kBeltSlots; ++slot) {
        auto* item = unit->GetQuickItem(slot);
        if (!item || static_cast<int>(item->count) <= 0) continue;
        Item* entry = PartyCopy(party, item);
        std::uint32_t amount = item->count;
        std::uint32_t maximum;
        maximum = std::bit_cast<std::uint32_t>(entry->chargeMax);    // before the null test
        entry->count = amount;
        entry->charge = std::bit_cast<float>(maximum);
        if (entry) entry->AddRef();
        record->AddQuickItem(entry);
        if (entry) entry->Release();
    }
    for (int slot = 0; slot < kSpellSlots; ++slot)
        if (Item* spell = unit->GetSpell(slot)) AddToRecord<&PartyUnitRecord::AddSpell>(party, record, spell);
    controller->CopyStats(&record->stats);
    if (world->sessionKind == 1) {
        record->moveMode2 = unit->moveMode2;
        record->weaponIndex = unit->activeWeapon;
    }
    record->fightAtWill = unit->fightAtWill;
    record->stats.RestoreSlots();
}

} // namespace

void WorldServer::UpdateMercBriefings() {
    PlayerIterator it;
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        PlayerServer* state = player->Server();
        int otherMercs = CountLivingMercs(player->playerId);
        auto set = [&](const std::string& name, float value) { state->SetVariable(name, std::bit_cast<std::uint32_t>(value)); };

        for (int slot = 1; slot <= kMercSlots; ++slot) {
            std::string merc = str::Format(worlddata::MercNameFormat(), slot);
            unsigned key = TextKeyFromName(merc);
            bool briefedHere = text::CompareIgnoringCase(zoneName,
                                               worlddata::MercZoneName(slot - 1)) == 0;
            Briefing b;
            b.n8 = str::Format(worlddata::MercVariableN8Format(), merc, slot);
            b.n1 = str::Format(worlddata::MercVariableN1Format(), merc, slot);
            b.n2 = str::Format(worlddata::MercVariableN2Format(), merc, slot);
            b.n3 = str::Format(worlddata::MercVariableN3Format(), merc, slot);
            b.n9 = str::Format(worlddata::MercVariableN9Format(), merc, slot);
            b.n10 = str::Format(worlddata::MercVariableN10Format(), merc, slot);
            b.n11 = str::Format(worlddata::MercVariableN11Format(), merc, slot);
            b.hired = str::Format(worlddata::MercHiredFormat(), slot);
            set(b.n11, 0.0f);

            bool inParty = false;
            for (int i = 0; i < std::ssize(player->Party()->members); ++i) {
                if (MercRecord(player, i).unitId == key) {
                    inParty = true;
                    break;
                }
            }
            NamedFloatTable* variables = &player->Party()->variables;
            float left = VariableGet(variables, b.n10);
            EI_NAN_AUDIT(left);
            if (inParty) {
                if (left != 0.0f) {
                    set(b.n8, 0.0f);
                    set(b.n1, 0.0f);
                    set(b.n2, 0.0f);
                    set(b.n3, 0.0f);
                    set(b.n9, 0.0f);
                } else {
                    set(b.n8, 0.0f);
                    set(b.n1, 0.0f);
                    set(b.n3, 0.0f);
                    set(b.n9, 0.0f);
                    set(b.n2, briefedHere ? 1.0f : 0.0f);
                }
            } else if (left != 0.0f) {
                set(b.n8, 0.0f);
                set(b.n1, 0.0f);
                set(b.n2, 0.0f);
                set(b.n3, 0.0f);
                set(b.n9, 0.0f);
                set(b.n10, 0.0f);
                set(b.hired, 0.0f);
            } else {
                float hiredValue = VariableGet(variables, b.hired);
                float hired = hiredValue;
                float hiredStored = hired;
                float briefed = VariableGet(variables, b.n8);
                EI_NAN_AUDIT(hiredValue, briefed);
                if (hiredValue != 0.0f) {
                    if (briefed != 1.0f) {
                        if (otherMercs < 2) {
                            bool firstTime = hiredStored == 1.0f;
                            set(b.n1, firstTime ? 1.0f : 0.0f);
                            set(b.n3, firstTime ? 0.0f : 1.0f);
                            set(b.n8, 0.0f);
                            set(b.n2, 0.0f);
                            set(b.n9, 0.0f);
                        } else {
                            set(b.n1, 0.0f);
                            set(b.n3, 0.0f);
                            set(b.n8, 0.0f);
                            set(b.n2, 0.0f);
                            set(b.n9, 1.0f);
                        }
                    } else {
                        set(b.n1, 0.0f);
                        set(b.n2, 0.0f);
                        set(b.n3, 0.0f);
                        set(b.n9, 0.0f);
                        set(b.n10, 0.0f);
                    }
                } else if (briefed != 1.0f) {
                    set(b.n11, 1.0f);
                }
            }
        }
    }
}

void WorldServer::CompleteBriefing(int playerId, std::string_view name) {
    if (IsMultiplayerClient()) {
        std::vector<std::string> parts = text::Split(name, '.');
        if (parts.size() == 3) g_gameFlow.RequestBriefing(parts[2]);
        // EI_Plugin's Briefings (the array's destruction moved to the end): the client queues the
        // completion script as well. The plugin formats it with the caller's EBX as the name; the client's
        // only caller holds the name there.
        if (ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginBriefings)) QueueBriefingScript(playerId, name);
        return;
    }
    std::vector<std::string> parts = text::Split(name, '.');
    QuestBriefingRecord* record = nullptr;
    if (parts.size() == 3) {
        std::string key(parts[2]);
        record = worlddata::QuestBriefings()[key];
    }
    CPlayer* player = nullptr;
    {
        PlayerIterator it;
        for (CPlayer* p = it.First(PlayerList()); p; p = it.Next()) {
            if (p->playerId == playerId) {
                player = p;
                break;
            }
        }
    }
    if (player) {
        ApplyMercBriefing(player, name);
        if (record) GiveBriefingRewards(this, player, record);
    }

    QueueBriefingScript(playerId, name);
}

void WorldServer::QueueBriefingScript(int playerId, std::string_view name) {
    std::string script = str::Format(worlddata::BriefingCompleteScript(), playerId, std::string(name), playerId, std::string(name));
    MainScript()->Load(&script, 1);
    if (sessionKind == kKindNoRangeTest)
        for (int i = 0; i < 5; ++i) ScriptTick(MainScript());
}

void WorldServer::ApplyMercBriefing(CPlayer* player, std::string_view name) {
    PlayerServer* state = player->Server();
    CParty* party = PartyOf(player);
    auto is = [name](const std::string& variable) {
        return text::CompareIgnoringCase(variable,
                               name) == 0;
    };
    for (int slot = 1; slot <= kMercSlots; ++slot) {
        MercNames v;
        v.merc = str::Format(worlddata::MercNameFormat(), slot);
        unsigned key = TextKeyFromName(v.merc);
        v.n8 = str::Format(worlddata::MercVariableN8Format(), v.merc, slot);
        v.n1 = str::Format(worlddata::MercVariableN1Format(), v.merc, slot);
        v.n2 = str::Format(worlddata::MercVariableN2Format(), v.merc, slot);
        v.n3 = str::Format(worlddata::MercVariableN3Format(), v.merc, slot);
        v.n9 = str::Format(worlddata::MercVariableN9Format(), v.merc, slot);
        v.n10 = str::Format(worlddata::MercVariableN10Format(), v.merc, slot);
        v.hired = str::Format(worlddata::MercHiredFormat(), slot);
        CUnit* unit = FindUnit(key);

        if (is(v.n8)) state->SetVariable(v.n8, std::bit_cast<std::uint32_t>(0.0f));
        // n10: the mercenary leaves the party and the world; n2: the party.
        if (is(v.n10)) {
            party->RemoveMember(key);
            RemoveObject(key);
        }
        if (is(v.n2)) party->RemoveMember(key);
        // n1 / n3: hired (amerc_N = 2) and added to the party with its unit's prototype.
        if (is(v.n1) || is(v.n3)) {
            state->SetVariable(v.hired, std::bit_cast<std::uint32_t>(2.0f));
            const std::string_view prototype = unit ? std::string_view(unit->GetPrototype()->name) : std::string_view(worlddata::HumanHeroText());
            party->AddMember(v.merc, prototype, game::kEmptyText);
        }
    }
    UpdateMercBriefings();
}

void WorldServer::AwardExperience(CPlayer* player, float amount) {
    EI_NAN_AUDIT(amount);
    if (amount == 0.0f) return;
    if (!player) ShareAmongSoloPlayers(this, amount);
    else ShareAmongHeroes(this, player, amount);
}

void WorldServer::SyncPartyRecords(CPlayer* player) {
    CParty* party = PartyOf(player);
    for (Item* item : player->items) party->AddItem(item, 1, 1);
    std::vector<PartyUnitRecord*> list{};
    for (int i = 0; i < std::ssize(player->Party()->members); ++i) lib::SetAtGrow(list, std::ssize(list), &MercRecord(player, i));
    for (int i = 0; i < std::ssize(list); ++i) {
        auto* record = list[i];
        CUnit* unit = FindUnit(static_cast<int>(record->unitId));
        if (!unit || unit->IsDead() != 0) continue;
        SyncRecord(party, record, unit);
    }
}

void WorldServer::StoreParty(std::vector<PartyUnitRecord*>* list, CParty* party, std::vector<UnitPlacement>* placements) {
    if (sessionKind == 1) {
        // Each record remembers whom its unit follows (-1: nobody).
        for (int i = 0; i < std::ssize(*list); ++i) {
            auto* record = (*list)[i];
            CUnit* unit = FindUnit(static_cast<int>(record->unitId));
            EngineObject* leader = unit->ServerController()->brain.LeaderOrFollowed();
            record->leaderId = leader ? leader->id : -1;
        }
    }
    for (int i = 0; i < std::ssize(*list); ++i) {
        auto* record = (*list)[i];
        CUnit* unit = FindUnit(static_cast<int>(record->unitId));
        if (!unit) continue;
        if (placements) {
            UnitPlacement entry;
            entry.id = unit->id;
            std::memcpy(entry.position, &unit->localPosition.x, sizeof(entry.position));
            std::memcpy(entry.rotation, &unit->localRotation, sizeof(entry.rotation));
            entry.moveMode = static_cast<std::uint32_t>(unit->moveMode2);
            entry.weapon = unit->activeWeapon;
            placements->push_back(entry);
        }
        if (!unit->IsDead() || IsMultiplayerClient()) StoreRecord(this, party, record, unit);
        if (!unit->IsDead()) RemoveObject(unit->id);
        else unit->SetPlayer(nullptr);
    }
}

void WorldServer::CollectParty(CPlayer* player, std::vector<UnitPlacement>* placements) {
    if (!player) return;
    if (sessionKind == 1 || sessionKind == kKindNoRangeTest) UpdatePlayerTimes(0.0f);
    if (sessionKind != kKindNetwork) {
        StorePlayerItems(player, PartyOf(player));
        player->Server()->SendPartyUpdate();
    }
    if (!(sessionKind == 1 || sessionKind == kKindNoRangeTest) || zoneLoaded == 0) return;

    std::vector<PartyUnitRecord*> list{};
    // Records whose unit is gone (or dead, outside a network client game) are dropped.
    for (int i = 0; i < std::ssize(player->Party()->members); ++i) {
        CUnit* unit = FindUnit(static_cast<int>(MercRecord(player, i).unitId));
        if (unit && (!unit->IsDead() || IsMultiplayerClient())) continue;
        player->Party()->members.erase(player->Party()->members.begin() + i);
        --i;
    }
    for (int i = 0; i < std::ssize(player->Party()->members); ++i) {
        int index = std::ssize(list);
        list.resize(index + 1);
        list[index] = &MercRecord(player, i);
    }
    StoreParty(&list, PartyOf(player), placements);
}

void WorldServer::StorePlayerItems(CPlayer* player, CParty* party) {
    if (!player) return;
    for (Item* item : player->items) {
        party->AddItem(item, 1, 1);
        item->Release();
    }
    player->items.clear();
}

void WorldServer::ClearPlayerItems(CPlayer* player) {
    CParty* party = player->Party();
    while (!party->items.empty()) party->RemoveItem(party->items[0], 1);
}

void WorldServer::TellParties(EngineObject* unit, AckKind kind) {
    PlayerIterator it;
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        PlayerServer* server = player->Server();
        if (static_cast<std::uint8_t>(server->IsPartyUnit(unit))) server->SendObject0B(unit, kind);
    }
}

void WorldServer::TellPartiesGone(EngineObject* unit) {
    PlayerIterator it;
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        PlayerServer* server = player->Server();
        if (server->partyUnits.Contains(unit)) server->SendObject43(unit);
    }
}
