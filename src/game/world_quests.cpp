// CWorldServer: the quest variables the server keeps for the scripts (the mercenaries' briefing
// variables "b.mercN.nK_N"). See world.h and docs/notes/world.md.
#include "combat.h"
#include "console.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "game_data.h"
#include "game_strings.h"
#include "item.h"
#include "map_object.h"
#include "object_tables.h"
#include "party.h"
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

#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include "ei/rebirth.h"
#include "ei/x87.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <list>
#include <string>
#include <vector>

namespace {

constexpr int kMercSlots = 10;

double VariableGet(NamedFloatTable* table, std::string_view name) { return table->Get(name); }

// The briefing variables of one mercenary slot (declared so that they are destroyed in the
// original's order: n11, amerc, n10, n2, n8, n9, n3, n1).
struct Briefing {
    std::string n1, n3, n9, n8, n2, n10, hired, n11;
};

CParty* PartyOf(CPlayer* player) { return player->Party(); }
PartyUnitRecord& MercRecord(CPlayer* player, int index) { return player->Party()->members_[index]; }

constexpr int kKindNoRangeTest = 3;

// The item named `name` (the game builds two unused temporary lists around the creation).
CItem* CreateNamedItem(std::string_view name) { return g_serverWorld->itemManager_->CreateItem(name); }

void GiveBriefingRewards(CWorldServer* world, CPlayer* player, QuestBriefingRecord* record) {
    float experience = record->exp;
    EI_NAN_AUDIT(experience);
    if (experience != 0.0f) {
        world->AwardExperience(player, experience);
    }
    PartyOf(player)->money_ += static_cast<int>(libcrt::Ftol(record->money));

    std::list<CItem*> items;
    for (const std::string& name : record->lists[QuestBriefingRecord::kGiveItems]) {
        CItem* item = CreateNamedItem(name);
        if (!item) continue;
        auto node = items.emplace(items.end());
        *node = item;
    }
    if (!items.empty()) player->GiveItems(&items, 0);

    for (const std::string& name : record->lists[QuestBriefingRecord::kRemoveItems]) {
        CItem* item = CreateNamedItem(name);
        if (!item) continue;
        item->AddRef();
        if (item->GetClassId() == ClassId::ItemQuest)
            player->TakeLoot(static_cast<int>(AsQuest(item)->questItemId_));
        item->Release();
    }

    CPlayerServer* state = player->Server();
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
    CUnitServer* controller;
    float share;
};

// The weight of a unit in the shared experience: sqrt(pending + gained experience) of its controller's statistics.
double Weight(CUnitServer* controller) {
    return std::sqrt(static_cast<double>(controller->stats.pendingExperience) + controller->stats.experience);
}

// Without a player: the single heroes of every player share `amount` in proportion to their weights.
void ShareAmongSoloPlayers(CWorldServer* world, float amount) {
    if (world->PlayerList()->objects_.size() == 0) return;
    std::list<Share> list{};
    auto soloUnit = [&](CPlayer* p) -> CUnit* {
        if (!p->Server() || std::ssize(p->Party()->members_) != 1) return nullptr;
        return world->FindUnit(static_cast<int>(MercRecord(p, 0).unitId));
    };

    float total = 0.0f;
    PlayerIterator it;
    for (CPlayer* p = it.First(world->PlayerList()); p; p = it.Next()) {
        CUnit* unit = soloUnit(p);
        if (!unit) continue;
        CUnitServer* controller = unit->ServerController();
        if (!controller) continue;
        auto node = list.emplace(list.end());
        (*node).controller = controller;
        (*node).share = 0.0f;
        total = static_cast<float>(Weight((*node).controller) + total);
        float stored = total;   // stored into the float total
        total = stored;
    }
    float minimum;
    minimum = kMinimumWeight;
    float divisor = total >= minimum ? total : minimum;   // branch-free select

    for (auto node = list.begin(); node != list.end();) {
        auto next = std::next(node);
        float share = static_cast<float>(Weight(node->controller) * amount / divisor);
        float stored = share;
        node->share = stored;
        node->controller->AddExperience(stored);
        node = next;
    }
    auto cursor = list.begin();
    for (CPlayer* p = it.First(world->PlayerList()); p; p = it.Next()) {
        CUnit* unit = soloUnit(p);
        if (!unit || !unit->ServerController()) continue;
        auto node = cursor++;
        MercRecord(p, 0).stats.AddScaledExperience((*node).share);
    }
    for (CPlayer* p = it.First(world->PlayerList()); p; p = it.Next()) {
        CPlayerServer* state = p->Server();
        if (!state) continue;
        for (auto& node : list) state->SendPair18(node.controller->unit->id_, std::bit_cast<std::uint32_t>(node.share));
    }
    if (it.manager) {
        --it.manager->refCount_;
        it.manager = nullptr;
    }
    list.clear();
}

// With a player: its living heroes share `amount` equally.
void ShareAmongHeroes(CWorldServer* world, CPlayer* player, float amount) {
    std::vector<int> heroes{};
    for (auto node = player->units_.begin(); node != player->units_.end() && node->second; ++node) {
        CUnit* unit = node->second;
        if (unit->IsDead() != 0 || unit->MapType() != MapObjectType::Hero) continue;
        int id = unit->id_;
        int index = std::ssize(heroes);
        heroes.resize(index + 1);
        heroes[index] = id;
    }
    int count = std::ssize(heroes);
    if (count >= 1) {
        float share = static_cast<float>(static_cast<double>(amount) / count);
        float stored = share;
        share = stored;
        for (int i = 0; i < std::ssize(heroes); ++i) {
            CUnit* unit = world->FindUnit(heroes[i]);
            unit->ServerController()->AddExperience(share);
            for (int j = 0; j < std::ssize(player->Party()->members_); ++j) {
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
CItem* PartyCopy(CParty* party, CItem* item) { return party->CopyItem(item); }

// The record entry for `item` from the party records, with a reference held across `add`.
template <auto Add>
CItem* AddToRecord(CParty* party, PartyUnitRecord* record, CItem* item) {
    CItem* entry = PartyCopy(party, item);
    if (entry) entry->AddRef();
    CItem* added = (record->*Add)(entry);
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
    CUnitServer* controller = unit->ServerController();
    ClearRecordItems(record);

    for (int slot = 0; slot < kQuickSlots; ++slot) {
        CItem* item = unit->GetWeapon(slot);
        if (!item) continue;
        CEngineObject* entry = AddToRecord<&PartyUnitRecord::AddWeapon>(party, record, item);
        CItemLink* ref = new CItemLink();
        ref->value = static_cast<std::uint32_t>(entry->id_);
        item->SetLink(ref);
    }
    for (int slot = 0; slot < kWornSlots; ++slot) {
        CItem* item = unit->WornItem(slot);
        if (!item) continue;
        CEngineObject* entry = AddToRecord<&PartyUnitRecord::AddArmor>(party, record, item);
        CItemLink* ref = new CItemLink();
        ref->value = static_cast<std::uint32_t>(entry->id_);
        item->SetLink(ref);
    }
    for (int slot = 0; slot < kBeltSlots; ++slot) {
        auto* item = unit->GetQuickItem(slot);
        if (!item || static_cast<int>(item->count_) <= 0) continue;
        CItem* entry = PartyCopy(party, item);
        entry->count_ = item->count_;   // before the null test
        if (entry) entry->AddRef();
        CEngineObject* added = (CEngineObject*)record->AddQuickItem(entry);
        if (entry) entry->Release();
        CItemLink* link = new CItemLink();
        link->value = static_cast<std::uint32_t>(added->id_);
        CItemLink* old = item->ServerLink();
        item->link_ = link;
        if (link) link->AddRef();
        if (old) old->Release();
    }
    for (int slot = 0; slot < kSpellSlots; ++slot) {
        CItem* spell = unit->GetSpell(slot);
        if (!spell) continue;
        AddToRecord<&PartyUnitRecord::AddSpell>(party, record, spell);
    }
    controller->CopyStats(&record->stats);
}

constexpr int kKindNetwork = 4;   // kKindNoRangeTest (3): above

// As SyncPartyRecords does, without the items' records.
void StoreRecord(CWorldServer* world, CParty* party, PartyUnitRecord* record, CUnit* unit) {
    CUnitServer* controller = unit->ServerController();
    ClearRecordItems(record);
    for (int slot = 0; slot < kQuickSlots; ++slot)
        if (CItem* item = unit->GetWeapon(slot)) AddToRecord<&PartyUnitRecord::AddWeapon>(party, record, item);
    for (int slot = 0; slot < kWornSlots; ++slot)
        if (CItem* item = unit->WornItem(slot)) AddToRecord<&PartyUnitRecord::AddArmor>(party, record, item);
    for (int slot = 0; slot < kBeltSlots; ++slot) {
        auto* item = unit->GetQuickItem(slot);
        if (!item || static_cast<int>(item->count_) <= 0) continue;
        CItem* entry = PartyCopy(party, item);
        std::uint32_t amount = item->count_;
        std::uint32_t maximum;
        maximum = std::bit_cast<std::uint32_t>(entry->chargeMax_);   // before the null test
        entry->count_ = amount;
        entry->charge_ = std::bit_cast<float>(maximum);
        if (entry) entry->AddRef();
        record->AddQuickItem(entry);
        if (entry) entry->Release();
    }
    for (int slot = 0; slot < kSpellSlots; ++slot)
        if (CItem* spell = unit->GetSpell(slot)) AddToRecord<&PartyUnitRecord::AddSpell>(party, record, spell);
    controller->CopyStats(&record->stats);
    if (world->sessionKind_ == 1) {
        record->moveMode2 = unit->moveMode2;
        record->weaponIndex = unit->activeWeapon;
    }
    record->fightAtWill = unit->fightAtWill;
    record->stats.RestoreSlots();
}

} // namespace

void CWorldServer::UpdateMercBriefings() {
    PlayerIterator it;
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        CPlayerServer* state = player->Server();
        int otherMercs = CountLivingMercs(player->playerId_);
        auto set = [&](const std::string& name, float value) { state->SetVariable(name, std::bit_cast<std::uint32_t>(value)); };

        for (int slot = 1; slot <= kMercSlots; ++slot) {
            std::string merc;
            merc = str::Format(worlddata::MercNameFormat(), slot);
            unsigned key = TextKeyFromName(merc);
            bool briefedHere = text::CompareIgnoringCase(zoneName_,
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
            for (int i = 0; i < std::ssize(player->Party()->members_); ++i) {
                if (MercRecord(player, i).unitId == key) {
                    inParty = true;
                    break;
                }
            }
            NamedFloatTable* variables = &player->Party()->variables_;
            double left = VariableGet(variables, b.n10);
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
                double hiredValue = VariableGet(variables, b.hired);
                float hired = static_cast<float>(hiredValue);   // stored as float, compared unrounded
                float hiredStored = hired;
                double briefed = VariableGet(variables, b.n8);
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

void CWorldServer::CompleteBriefing(int playerId, std::string_view name) {
    if (IsMultiplayerClient()) {
        std::vector<std::string> parts;
        text::Split(name, &parts, '.');
        if (parts.size() == 3) g_gameFlow.RequestBriefing(parts[2]);
        // EI_Plugin's Briefings (the array's destruction moved to the end): the client queues the
        // completion script as well. The plugin formats it with the caller's EBX as the name; the client's
        // only caller holds the name there.
        if (ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginBriefings)) QueueBriefingScript(playerId, name);
        return;
    }
    std::vector<std::string> parts;
    text::Split(name, &parts, '.');
    QuestBriefingRecord* record = nullptr;
    if (parts.size() == 3) {
        std::string key(parts[2]);
        record = worlddata::QuestBriefings()[key];
    }
    CPlayer* player = nullptr;
    {
        PlayerIterator it;
        for (CPlayer* p = it.First(PlayerList()); p; p = it.Next()) {
            if (p->playerId_ == playerId) {
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

void CWorldServer::QueueBriefingScript(int playerId, std::string_view name) {
    std::string script;
    script = str::Format(worlddata::BriefingCompleteScript(), playerId, std::string(name), playerId, std::string(name));
    MainScript()->Load(&script, 1);
    if (sessionKind_ == kKindNoRangeTest)
        for (int i = 0; i < 5; ++i) ScriptTick(MainScript());
}

void CWorldServer::ApplyMercBriefing(CPlayer* player, std::string_view name) {
    CPlayerServer* state = player->Server();
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

void CWorldServer::AwardExperience(CPlayer* player, float amount) {
    EI_NAN_AUDIT(amount);
    if (amount == 0.0f) return;
    if (!player) ShareAmongSoloPlayers(this, amount);
    else ShareAmongHeroes(this, player, amount);
}

void CWorldServer::SyncPartyRecords(CPlayer* player) {
    CParty* party = PartyOf(player);
    for (auto node = player->items_.begin(); node != player->items_.end();) {
        auto next = std::next(node);
        party->AddItem(*node, 1, 1);
        node = next;
    }
    std::vector<PartyUnitRecord*> list{};
    for (int i = 0; i < std::ssize(player->Party()->members_); ++i) lib::SetAtGrow(list, std::ssize(list), &MercRecord(player, i));
    for (int i = 0; i < std::ssize(list); ++i) {
        auto* record = list[i];
        CUnit* unit = FindUnit(static_cast<int>(record->unitId));
        if (!unit || unit->IsDead() != 0) continue;
        SyncRecord(party, record, unit);
    }
}

void CWorldServer::StoreParty(std::vector<PartyUnitRecord*>* list, CParty* party, std::vector<UnitPlacement>* placements) {
    if (sessionKind_ == 1) {
        // Each record remembers whom its unit follows (-1: nobody).
        for (int i = 0; i < std::ssize(*list); ++i) {
            auto* record = (*list)[i];
            CUnit* unit = FindUnit(static_cast<int>(record->unitId));
            CEngineObject* leader = unit->ServerController()->brain.LeaderOrFollowed();
            record->leaderId = leader ? leader->id_ : -1;
        }
    }
    for (int i = 0; i < std::ssize(*list); ++i) {
        auto* record = (*list)[i];
        CUnit* unit = FindUnit(static_cast<int>(record->unitId));
        if (!unit) continue;
        if (placements) {
            UnitPlacement entry;
            entry.id = unit->id_;
            std::memcpy(entry.position, &unit->localPosition_.x, sizeof(entry.position));
            std::memcpy(entry.rotation, &unit->localRotation_, sizeof(entry.rotation));
            entry.moveMode = static_cast<std::uint32_t>(unit->moveMode2);
            entry.weapon = unit->activeWeapon;
            placements->push_back(entry);
        }
        if (!unit->IsDead() || IsMultiplayerClient()) StoreRecord(this, party, record, unit);
        if (!unit->IsDead()) RemoveObject(unit->id_);
        else unit->SetPlayer(nullptr);
    }
}

void CWorldServer::CollectParty(CPlayer* player, std::vector<UnitPlacement>* placements) {
    if (!player) return;
    if (sessionKind_ == 1 || sessionKind_ == kKindNoRangeTest) UpdatePlayerTimes(0.0f);
    if (sessionKind_ != kKindNetwork) {
        StorePlayerItems(player, PartyOf(player));
        player->Server()->SendPartyUpdate();
    }
    if (!(sessionKind_ == 1 || sessionKind_ == kKindNoRangeTest) || zoneLoaded_ == 0) return;

    std::vector<PartyUnitRecord*> list{};
    // Records whose unit is gone (or dead, outside a network client game) are dropped.
    for (int i = 0; i < std::ssize(player->Party()->members_); ++i) {
        CUnit* unit = FindUnit(static_cast<int>(MercRecord(player, i).unitId));
        if (unit && (!unit->IsDead() || IsMultiplayerClient())) continue;
        player->Party()->members_.erase(player->Party()->members_.begin() + i);
        --i;
    }
    for (int i = 0; i < std::ssize(player->Party()->members_); ++i) {
        int index = std::ssize(list);
        list.resize(index + 1);
        list[index] = &MercRecord(player, i);
    }
    StoreParty(&list, PartyOf(player), placements);
}

void CWorldServer::StorePlayerItems(CPlayer* player, CParty* party) {
    if (!player) return;
    for (auto node = player->items_.begin(); node != player->items_.end();) {
        CItem* item = *node;
        ++node;
        party->AddItem(item, 1, 1);
        item->Release();
    }
    player->items_.clear();
}

void CWorldServer::ClearPlayerItems(CPlayer* player) {
    CParty* party = player->Party();
    while (!party->items_.empty()) party->RemoveItem(party->items_[0], 1);
}

void CWorldServer::TellParties(CEngineObject* unit, AckKind kind) {
    PlayerIterator it;
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        CPlayerServer* server = player->Server();
        if (static_cast<std::uint8_t>(server->IsPartyUnit(unit))) server->SendObject0B(unit, kind);
    }
}

void CWorldServer::TellPartiesGone(CEngineObject* unit) {
    PlayerIterator it;
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        CPlayerServer* server = player->Server();
        if (server->partyUnits_.Contains(unit)) server->SendObject43(unit);
    }
}
