// WorldServer: the quest variables the server keeps for the scripts (the mercenaries' briefing
// variables "b.mercN.nK_N"). See world.h and docs/notes/world.md.
#include "combat.h"
#include "console.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "game_flow.h"
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

#include "lib/containers.h"
#include "lib/float_select.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include "lib/rebirth.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int kMercSlots = 10;

// The names of one mercenary slot's variables: the scripts' "b.<merc>.nK_<slot>" (K in the comments) and "amerc_<slot>".
struct MercVariables {
    std::string merc;
    std::string firstHire;    // n1: hired for the first time
    std::string rehire;       // n3: hired again
    std::string partyFull;    // n9: hired, but two other mercenaries are alive
    std::string briefed;      // n8
    std::string dismiss;      // n2: leaves the party
    std::string leave;        // n10: leaves the party and the world
    std::string offer;        // n11: not hired and not briefed (1), else 0
    std::string hired;        // amerc_<slot>: 1 hired, 2 hired and in the party
};

MercVariables MercVariablesOf(int slot) {
    MercVariables v;
    v.merc = str::Format(worlddata::MercNameFormat(), slot);
    v.briefed = str::Format(worlddata::MercVariableN8Format(), v.merc, slot);
    v.firstHire = str::Format(worlddata::MercVariableN1Format(), v.merc, slot);
    v.dismiss = str::Format(worlddata::MercVariableN2Format(), v.merc, slot);
    v.rehire = str::Format(worlddata::MercVariableN3Format(), v.merc, slot);
    v.partyFull = str::Format(worlddata::MercVariableN9Format(), v.merc, slot);
    v.leave = str::Format(worlddata::MercVariableN10Format(), v.merc, slot);
    v.offer = str::Format(worlddata::MercVariableN11Format(), v.merc, slot);
    v.hired = str::Format(worlddata::MercHiredFormat(), slot);
    return v;
}

Party* PartyOf(Player* player) { return player->GetParty(); }
PartyUnitRecord& MercRecord(Player* player, int index) { return player->GetParty()->members[index]; }


// Unreached in the shipped game: CompleteBriefing returns first in a network game and no single-player game holds a
// briefing record.
void GiveBriefingRewards(WorldServer* world, Player* player, QuestBriefingRecord* record) {
    float experience = record->exp;
    if (experience != 0.0f) {
        world->AwardExperience(player, experience);
    }
    PartyOf(player)->money += static_cast<int>(static_cast<std::int64_t>(record->money));   // _ftol: 0 for a NaN

    std::vector<Item*> items;
    for (const std::string& name : record->lists[QuestBriefingRecord::kGiveItems]) {
        Item* item = world->CreateItemNamed(name);
        if (item) items.push_back(item);
    }
    if (!items.empty()) player->GiveItems(&items, 0);

    for (const std::string& name : record->lists[QuestBriefingRecord::kRemoveItems]) {
        Item* item = world->CreateItemNamed(name);
        if (!item) continue;
        item->AddRef();
        if (item->GetClassId() == ClassId::ItemQuest)
            player->TakeLoot(static_cast<int>(item->AsQuest()->questItemId));
        item->Release();
    }

    PlayerServer* state = player->Server();
    for (const std::string& name : record->lists[QuestBriefingRecord::kGiveQuests])
        state->SetVariableMax(name, 1.0f);
    for (const std::string& name : record->lists[QuestBriefingRecord::kCompleteQuests])
        state->SetVariable(name, 2.0f);
    for (const std::string& name : record->lists[QuestBriefingRecord::kAvailableZones])
        state->SetVariableMax(name, 1.0f);
    for (const std::string& name : record->lists[QuestBriefingRecord::kCompleteZones])
        state->SetVariable(name, 2.0f);

    items.clear();
}

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
    auto soloUnit = [&](Player* p) -> Unit* {
        if (!p->Server() || std::ssize(p->GetParty()->members) != 1) return nullptr;
        return world->FindUnit(static_cast<int>(MercRecord(p, 0).unitId));
    };

    float total = 0.0f;
    PlayerIterator it;
    for (Player* p = it.First(world->PlayerList()); p; p = it.Next()) {
        Unit* unit = soloUnit(p);
        if (!unit) continue;
        UnitServer* controller = unit->ServerController();
        if (!controller) continue;
        list.push_back({controller, 0.0f});
        total = Weight(controller) + total;
    }
    float minimum;
    minimum = kMinimumWeight;
    float divisor = MaxKeepB(total, minimum);   // branch-free select

    for (Share& entry : list) {
        float share = Weight(entry.controller) * amount / divisor;
        entry.share = share;
        entry.controller->AddExperience(share);
    }
    auto cursor = list.begin();
    for (Player* p = it.First(world->PlayerList()); p; p = it.Next()) {
        Unit* unit = soloUnit(p);
        if (!unit || !unit->ServerController()) continue;
        MercRecord(p, 0).stats.AddScaledExperience((cursor++)->share);
    }
    for (Player* p = it.First(world->PlayerList()); p; p = it.Next()) {
        PlayerServer* state = p->Server();
        if (!state) continue;
        for (auto& node : list) state->SendExperience(node.controller->unit->id, std::bit_cast<std::uint32_t>(node.share));
    }
}

// With a player: its living heroes share `amount` equally.
void ShareAmongHeroes(WorldServer* world, Player* player, float amount) {
    std::vector<int> heroes{};
    for (auto node = player->units.begin(); node != player->units.end() && node->second; ++node) {
        Unit* unit = node->second;
        if (unit->IsDead() != 0 || unit->MapType() != MapObjectType::Hero) continue;
        int id = unit->id;
        heroes.push_back(id);
    }
    int count = lib::Count(heroes);
    if (count >= 1) {
        float share = amount / static_cast<float>(count);
        for (int i = 0; i < std::ssize(heroes); ++i) {
            Unit* unit = world->FindUnit(heroes[i]);
            unit->ServerController()->AddExperience(share);
            for (int j = 0; j < std::ssize(player->GetParty()->members); ++j) {
                PartyUnitRecord& record = MercRecord(player, j);
                if (static_cast<int>(record.unitId) == heroes[i]) record.stats.AddScaledExperience(share);
            }
        }
        player->Server()->SendExperienceAll(std::bit_cast<std::uint32_t>(share));
    }
}

constexpr int kQuickSlots = 4;
constexpr int kWornSlots = 7;
constexpr int kBeltSlots = 4;
constexpr int kSpellSlots = 8;

// The party's copy of an item (Party's copy virtual; the record entry's source).
Item* PartyCopy(Party* party, Item* item) { return party->CopyItem(item); }

// The record entry for `item` from the party records, with a reference held across `add`.
template <auto Add>
Item* AddToRecord(Party* party, PartyUnitRecord* record, Item* item) {
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

void SyncRecord(Party* party, PartyUnitRecord* record, Unit* unit) {
    UnitServer* controller = unit->ServerController();
    ClearRecordItems(record);

    for (int slot = 0; slot < kQuickSlots; ++slot) {
        Item* item = unit->GetWeapon(slot);
        if (!item) continue;
        EngineObject* entry = AddToRecord<&PartyUnitRecord::AddWeapon>(party, record, item);
        ItemLink* ref = new ItemLink();
        ref->value = static_cast<std::uint32_t>(entry->id);
        item->serverLink.Assign(ref);
    }
    for (int slot = 0; slot < kWornSlots; ++slot) {
        Item* item = unit->WornItem(slot);
        if (!item) continue;
        EngineObject* entry = AddToRecord<&PartyUnitRecord::AddArmor>(party, record, item);
        ItemLink* ref = new ItemLink();
        ref->value = static_cast<std::uint32_t>(entry->id);
        item->serverLink.Assign(ref);
    }
    for (int slot = 0; slot < kBeltSlots; ++slot) {
        auto* item = unit->GetQuickItem(slot);
        if (!item || static_cast<int>(item->count) <= 0) continue;
        Item* entry = PartyCopy(party, item);
        if (!entry) continue;
        entry->count = item->count;
        entry->AddRef();
        EngineObject* added = (EngineObject*)record->AddQuickItem(entry);
        entry->Release();
        ItemLink* link = new ItemLink();
        link->value = static_cast<std::uint32_t>(added->id);
        item->serverLink.Assign(link);
    }
    for (int slot = 0; slot < kSpellSlots; ++slot) {
        Item* spell = unit->GetSpell(slot);
        if (!spell) continue;
        AddToRecord<&PartyUnitRecord::AddSpell>(party, record, spell);
    }
    controller->CopyStats(&record->stats);
}


// As SyncPartyRecords does, without the items' records.
void StoreRecord(WorldServer* world, Party* party, PartyUnitRecord* record, Unit* unit) {
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
        if (!entry) continue;
        entry->count = item->count;
        entry->charge = entry->chargeMax;
        entry->AddRef();
        record->AddQuickItem(entry);
        entry->Release();
    }
    for (int slot = 0; slot < kSpellSlots; ++slot)
        if (Item* spell = unit->GetSpell(slot)) AddToRecord<&PartyUnitRecord::AddSpell>(party, record, spell);
    controller->CopyStats(&record->stats);
    if (world->zoneMode == ZoneMode::Battle) {
        record->moveMode2 = unit->moveMode2;
        record->weaponIndex = unit->activeWeapon;
    }
    record->fightAtWill = unit->fightAtWill;
    record->stats.RestoreSlots();
}

} // namespace

void WorldServer::UpdateMercBriefings() {
    PlayerIterator it;
    for (Player* player = it.First(PlayerList()); player; player = it.Next()) {
        PlayerServer* state = player->Server();
        int otherMercs = CountLivingMercs(player->playerId);
        auto set = [&](const std::string& name, float value) { state->SetVariable(name, value); };

        for (int slot = 1; slot <= kMercSlots; ++slot) {
            const MercVariables b = MercVariablesOf(slot);
            unsigned key = TextKeyFromName(b.merc);
            bool briefedHere = text::CompareIgnoringCase(zoneName,
                                               worlddata::MercZoneName(slot - 1)) == 0;
            set(b.offer, 0.0f);

            bool inParty = false;
            for (int i = 0; i < std::ssize(player->GetParty()->members); ++i) {
                if (MercRecord(player, i).unitId == key) {
                    inParty = true;
                    break;
                }
            }
            NamedFloatTable* variables = &player->GetParty()->variables;
            float left = variables->Get(b.leave);
            if (inParty) {
                if (left != 0.0f) {
                    set(b.briefed, 0.0f);
                    set(b.firstHire, 0.0f);
                    set(b.dismiss, 0.0f);
                    set(b.rehire, 0.0f);
                    set(b.partyFull, 0.0f);
                } else {
                    set(b.briefed, 0.0f);
                    set(b.firstHire, 0.0f);
                    set(b.rehire, 0.0f);
                    set(b.partyFull, 0.0f);
                    set(b.dismiss, briefedHere ? 1.0f : 0.0f);
                }
            } else if (left != 0.0f) {
                set(b.briefed, 0.0f);
                set(b.firstHire, 0.0f);
                set(b.dismiss, 0.0f);
                set(b.rehire, 0.0f);
                set(b.partyFull, 0.0f);
                set(b.leave, 0.0f);
                set(b.hired, 0.0f);
            } else {
                float hired = variables->Get(b.hired);
                float briefed = variables->Get(b.briefed);
                if (hired != 0.0f) {
                    if (briefed != 1.0f) {
                        if (otherMercs < 2) {
                            bool firstTime = hired == 1.0f;
                            set(b.firstHire, firstTime ? 1.0f : 0.0f);
                            set(b.rehire, firstTime ? 0.0f : 1.0f);
                            set(b.briefed, 0.0f);
                            set(b.dismiss, 0.0f);
                            set(b.partyFull, 0.0f);
                        } else {
                            set(b.firstHire, 0.0f);
                            set(b.rehire, 0.0f);
                            set(b.briefed, 0.0f);
                            set(b.dismiss, 0.0f);
                            set(b.partyFull, 1.0f);
                        }
                    } else {
                        set(b.firstHire, 0.0f);
                        set(b.dismiss, 0.0f);
                        set(b.rehire, 0.0f);
                        set(b.partyFull, 0.0f);
                        set(b.leave, 0.0f);
                    }
                } else if (briefed != 1.0f) {
                    set(b.offer, 1.0f);
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
    Player* player = GetPlayer(playerId);
    if (player) {
        ApplyMercBriefing(player, name);
        if (record) GiveBriefingRewards(this, player, record);
    }

    QueueBriefingScript(playerId, name);
}

void WorldServer::QueueBriefingScript(int playerId, std::string_view name) {
    std::string script = str::Format(worlddata::BriefingCompleteScript(), playerId, std::string(name), playerId, std::string(name));
    MainScript()->Load(&script, 1);
    if (zoneMode == ZoneMode::Base)
        for (int i = 0; i < 5; ++i) MainScript()->Tick();
}

void WorldServer::ApplyMercBriefing(Player* player, std::string_view name) {
    PlayerServer* state = player->Server();
    Party* party = PartyOf(player);
    auto is = [name](const std::string& variable) {
        return text::CompareIgnoringCase(variable,
                               name) == 0;
    };
    for (int slot = 1; slot <= kMercSlots; ++slot) {
        const MercVariables v = MercVariablesOf(slot);
        unsigned key = TextKeyFromName(v.merc);
        Unit* unit = FindUnit(key);

        if (is(v.briefed)) state->SetVariable(v.briefed, 0.0f);
        if (is(v.leave)) {
            party->RemoveMember(key);
            RemoveObject(key);
        }
        if (is(v.dismiss)) party->RemoveMember(key);
        // Hired (amerc_N = 2) and added to the party with its unit's prototype.
        if (is(v.firstHire) || is(v.rehire)) {
            state->SetVariable(v.hired, 2.0f);
            const std::string_view prototype = unit ? std::string_view(unit->GetPrototype()->name) : std::string_view(worlddata::HumanHeroText());
            party->AddMember(v.merc, prototype, game::kEmptyText);
        }
    }
    UpdateMercBriefings();
}

void WorldServer::AwardExperience(Player* player, float amount) {
    if (amount == 0.0f) return;
    if (!player) ShareAmongSoloPlayers(this, amount);
    else ShareAmongHeroes(this, player, amount);
}

void WorldServer::SyncPartyRecords(Player* player) {
    Party* party = PartyOf(player);
    for (Item* item : player->items) party->AddItem(item, 1, 1);
    std::vector<PartyUnitRecord*> list{};
    for (int i = 0; i < lib::Count(player->GetParty()->members); ++i) list.push_back(&MercRecord(player, i));
    for (int i = 0; i < std::ssize(list); ++i) {
        auto* record = list[i];
        Unit* unit = FindUnit(static_cast<int>(record->unitId));
        if (!unit || unit->IsDead() != 0) continue;
        SyncRecord(party, record, unit);
    }
}

void WorldServer::StoreParty(std::vector<PartyUnitRecord*>* list, Party* party, std::vector<UnitPlacement>* placements) {
    if (zoneMode == ZoneMode::Battle) {
        // Each record remembers whom its unit follows (-1: nobody).
        for (int i = 0; i < std::ssize(*list); ++i) {
            auto* record = (*list)[i];
            Unit* unit = FindUnit(static_cast<int>(record->unitId));
            EngineObject* leader = unit->ServerController()->brain.LeaderOrFollowed();
            record->leaderId = leader ? leader->id : -1;
        }
    }
    for (int i = 0; i < std::ssize(*list); ++i) {
        auto* record = (*list)[i];
        Unit* unit = FindUnit(static_cast<int>(record->unitId));
        if (!unit) continue;
        if (placements) {
            UnitPlacement entry;
            entry.id = unit->id;
            entry.position = unit->localPosition;
            entry.rotation = unit->localRotation;
            entry.moveMode = static_cast<std::uint32_t>(unit->moveMode2);
            entry.weapon = unit->activeWeapon;
            placements->push_back(entry);
        }
        if (!unit->IsDead() || IsMultiplayerClient()) StoreRecord(this, party, record, unit);
        if (!unit->IsDead()) RemoveObject(unit->id);
        else unit->SetPlayer(nullptr);
    }
}

void WorldServer::CollectParty(Player* player, std::vector<UnitPlacement>* placements) {
    if (!player) return;
    if (IsSinglePlayerMode(zoneMode)) UpdatePlayerTimes(0.0f);
    if (zoneMode != ZoneMode::Edge) {
        StorePlayerItems(player, PartyOf(player));
        player->Server()->SendPartyUpdate();
    }
    if (!IsSinglePlayerMode(zoneMode) || zoneLoaded == 0) return;

    std::vector<PartyUnitRecord*> list{};
    // Records whose unit is gone (or dead, outside a network client game) are dropped.
    for (int i = 0; i < std::ssize(player->GetParty()->members); ++i) {
        Unit* unit = FindUnit(static_cast<int>(MercRecord(player, i).unitId));
        if (unit && (!unit->IsDead() || IsMultiplayerClient())) continue;
        player->GetParty()->members.erase(player->GetParty()->members.begin() + i);
        --i;
    }
    for (int i = 0; i < std::ssize(player->GetParty()->members); ++i) {
        list.push_back(&MercRecord(player, i));
    }
    StoreParty(&list, PartyOf(player), placements);
}

void WorldServer::StorePlayerItems(Player* player, Party* party) {
    if (!player) return;
    for (Item* item : player->items) {
        party->AddItem(item, 1, 1);
        item->Release();
    }
    player->items.clear();
}

void WorldServer::ClearPlayerItems(Player* player) {
    Party* party = player->GetParty();
    while (!party->items.empty()) party->RemoveItem(party->items[0], 1);
}

void WorldServer::TellParties(EngineObject* unit, AckKind kind) {
    PlayerIterator it;
    for (Player* player = it.First(PlayerList()); player; player = it.Next()) {
        PlayerServer* server = player->Server();
        if (static_cast<std::uint8_t>(server->IsPartyUnit(unit))) server->SendUnitSpeech(unit, kind);
    }
}

void WorldServer::TellPartiesGone(EngineObject* unit) {
    PlayerIterator it;
    for (Player* player = it.First(PlayerList()); player; player = it.Next()) {
        PlayerServer* server = player->Server();
        if (server->partyUnits.Contains(unit)) server->SendUnitDied(unit);
    }
}
