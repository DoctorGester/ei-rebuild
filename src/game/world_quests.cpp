// CWorldServer: the quest variables the server keeps for the scripts (the mercenaries' briefing
// variables "b.mercN.nK_N"). See world.h and docs/notes/world.md.
#include "core_refcount.h"
#include "core_streams.h"
#include "object_tables.h"
#include "script.h"
#include "unit_ai.h"
#include "world.h"
#include "world_data.h"
#include "game_strings.h"

#include "combat.h"
#include "console.h"
#include "item.h"
#include "map_object.h"
#include "party.h"
#include "player_messages.h"
#include "quest_records.h"
#include "ui_briefing.h"
#include "unit.h"
#include "unit_db.h"

#include "lib/crt.h"
#include "text_split.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/x87.h"
#include "lib/mfc_list.h"
#include "lib/nan_audit.h"
#include "game_data.h"
#include "lib/mfc_slots.h"
#include "lib/str.h"

#include <cstring>
#include <string>

namespace {

constexpr int kMercSlots = 10;

// The text key (object id) of a name (object_tables.cpp).
double VariableGet(NamedFloatTable* table, const char* name) { return table->Get(name); }

// The briefing variables of one mercenary slot (declared so that they are destroyed in the
// original's order: n11, amerc, n10, n2, n8, n9, n3, n1).
struct Briefing {
    mfc::CString n1, n3, n9, n8, n2, n10, hired, n11;
};

// The player's party (its records, items and variables).
CParty* PartyOf(CPlayer* player) { return player->Party(); }
PartyUnitRecord& MercRecord(CPlayer* player, int index) { return player->Party()->members_.data[index]; }

} // namespace

void CWorldServer::UpdateMercBriefings() {
    PlayerIterator it;
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        CPlayerServer* state = player->Server();
        int otherMercs = CountLivingMercs(player->playerId_);
        auto set = [&](const mfc::CString& name, float value) { state->SetVariable(name, std::bit_cast<std::uint32_t>(value)); };

        for (int slot = 1; slot <= kMercSlots; ++slot) {
            std::string merc;
            merc = str::Format(worlddata::MercNameFormat(), slot);
            unsigned key = TextKeyFromName(merc.c_str());
            bool briefedHere = gamecrt::mbsicmp(zoneName_,
                                               worlddata::MercZoneName(slot - 1)) == 0;
            Briefing b;
            b.n8.Format(worlddata::MercVariableN8Format(), merc.c_str(), slot);
            b.n1.Format(worlddata::MercVariableN1Format(), merc.c_str(), slot);
            b.n2.Format(worlddata::MercVariableN2Format(), merc.c_str(), slot);
            b.n3.Format(worlddata::MercVariableN3Format(), merc.c_str(), slot);
            b.n9.Format(worlddata::MercVariableN9Format(), merc.c_str(), slot);
            b.n10.Format(worlddata::MercVariableN10Format(), merc.c_str(), slot);
            b.n11.Format(worlddata::MercVariableN11Format(), merc.c_str(), slot);
            b.hired.Format(worlddata::MercHiredFormat(), slot);
            set(b.n11, 0.0f);

            bool inParty = false;
            for (int i = 0; i < player->Party()->members_.size; ++i) {
                if (MercRecord(player, i).unitId == key) {
                    inParty = true;
                    break;
                }
            }
            NamedFloatTable* variables = &player->Party()->variables_;
            double left = VariableGet(variables, b.n10);
            EI_NAN_AUDIT(left);
            if (inParty) {
                if (left != worlddata::Zero()) {
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
            } else if (left != worlddata::Zero()) {
                set(b.n8, 0.0f);
                set(b.n1, 0.0f);
                set(b.n2, 0.0f);
                set(b.n3, 0.0f);
                set(b.n9, 0.0f);
                set(b.n10, 0.0f);
                set(b.hired, 0.0f);
            } else {
                double hiredValue = VariableGet(variables, b.hired);
                float hired = static_cast<float>(hiredValue);   // FST: stored as float, compared unrounded
                float hiredStored = hired;
                double briefed = VariableGet(variables, b.n8);
                EI_NAN_AUDIT(hiredValue, briefed);
                if (hiredValue != worlddata::Zero()) {
                    if (briefed != worlddata::One()) {
                        if (otherMercs < 2) {
                            bool firstTime = hiredStored == worlddata::One();
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
                } else if (briefed != worlddata::One()) {
                    set(b.n11, 1.0f);
                }
            }
        }
    }
}

namespace {

constexpr int kKindNoRangeTest = 3;
constexpr int kClassQuestItem = 0x14;

// The item manager creates an item by name. Patched by Rebirth. A pointer list built and destroyed around
// each item creation, unused.

// The item named `name`, with the game's unused temporary lists around the creation (built in the order
// `firstAt`, then the other).
CItem* CreateNamedItem(const char* name, bool fromSecond) {
    mfc::CTypedPtrList<void> lists[2];
    mfc::CTypedPtrList<void>* first = fromSecond ? &lists[0] : &lists[1];
    mfc::CTypedPtrList<void>* second = fromSecond ? &lists[1] : &lists[0];
    mfc::impl::ConstructListHeader<mfc::impl::TempListClass>(first, 10);
    mfc::impl::ConstructListHeader<mfc::impl::TempListClass>(second, 10);
    CItem* item = WorldItemManager()->CreateItem(name);
    mfc::impl::DestructList<mfc::impl::TempListClass>(*second);
    mfc::impl::DestructList<mfc::impl::TempListClass>(*first);
    return item;
}

void GiveBriefingRewards(CWorldServer* world, CPlayer* player, QuestBriefingRecord* record) {
    float experience = record->exp;
    EI_NAN_AUDIT(experience);
    if (experience != 0.0f) {
        world->AwardExperience(player, experience);
    }
    PartyOf(player)->Money() += static_cast<int>(gamecrt::Ftol(record->money));

    mfc::CTypedPtrList<CItem> items;
    items.vtable = mfc::impl::ItemListClass::Vtable();
    items.head = nullptr;
    items.tail = nullptr;
    items.count = 0;
    items.free = nullptr;
    items.blocks = nullptr;
    items.blockSize = 10;
    for (const mfc::CString* name = record->lists[QuestBriefingRecord::kGiveItems].begin();
         name != record->lists[QuestBriefingRecord::kGiveItems].end(); ++name) {
        CItem* item = CreateNamedItem(*name, true);
        if (!item) continue;
        auto* node = items.NewNode(items.tail, nullptr);
        node->data = item;
        if (items.tail) items.tail->next = node;
        else items.head = node;
        items.tail = node;
    }
    if (items.count) player->GiveItems(&items, 0);

    for (const mfc::CString* name = record->lists[QuestBriefingRecord::kRemoveItems].begin();
         name != record->lists[QuestBriefingRecord::kRemoveItems].end(); ++name) {
        CItem* item = CreateNamedItem(*name, false);
        if (!item) continue;
        ++item->refCount_;
        if (item->GetClassId() == kClassQuestItem)
            static_cast<CPlayer*>(player)->TakeLoot(static_cast<int>(AsQuest(item)->questItemId_));
        item->Release();
    }

    CPlayerServer* state = player->Server();
    for (const mfc::CString* name = record->lists[QuestBriefingRecord::kGiveQuests].begin();
         name != record->lists[QuestBriefingRecord::kGiveQuests].end(); ++name)
        state->SetVariableMax(*name, 1.0f);
    for (const mfc::CString* name = record->lists[QuestBriefingRecord::kCompleteQuests].begin();
         name != record->lists[QuestBriefingRecord::kCompleteQuests].end(); ++name)
        state->SetVariable(*name, std::bit_cast<std::uint32_t>(2.0f));
    for (const mfc::CString* name = record->lists[QuestBriefingRecord::kAvailableZones].begin();
         name != record->lists[QuestBriefingRecord::kAvailableZones].end(); ++name)
        state->SetVariableMax(*name, 1.0f);
    for (const mfc::CString* name = record->lists[QuestBriefingRecord::kCompleteZones].begin();
         name != record->lists[QuestBriefingRecord::kCompleteZones].end(); ++name)
        state->SetVariable(*name, std::bit_cast<std::uint32_t>(2.0f));

    items.vtable = mfc::impl::ItemListClass::Vtable();
    items.RemoveAll();
}

} // namespace

void CWorldServer::CompleteBriefing(int playerId, const char* name) {
    if (IsMultiplayerClient()) {
        console::CStringArray parts;
        text::Split(name, &parts, '.');
        if (parts.GetSize() == 3) TheGameFlow()->RequestBriefing(parts[2]);
        // EI_Plugin's Briefings (the array's destruction moved to the end): the client queues the
        // completion script as well. The plugin formats it with the caller's EBX as the name; the client's
        // only caller holds the name there.
        if (ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginBriefings)) QueueBriefingScript(playerId, name);
        return;
    }
    console::CStringArray parts;
    text::Split(name, &parts, '.');
    QuestBriefingRecord* record = nullptr;
    if (parts.GetSize() == 3) {
        stlp::string key(parts[2]);
        record = stlp::NamedSlot<QuestBriefingRecord>(worlddata::QuestBriefings(), key);
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

void CWorldServer::QueueBriefingScript(int playerId, const char* name) {
    mfc::CString script;
    script.Format(worlddata::BriefingCompleteScript(), playerId, name, playerId, name);
    MainScript()->Load(&script, 1);
    if (sessionKind_ == kKindNoRangeTest)
        for (int i = 0; i < 5; ++i) ScriptTick(MainScript());
}

namespace {

// CWorld: the unit object with a text key. Patched by Rebirth. CWorldServer: removes the object with an id.

// One slot's names, declared so that they are destroyed in the game's order: amerc, n10, n2, n8, n9, n3,
// n1, merc.
struct MercNames {
    mfc::CString merc, n1, n3, n9, n8, n2, n10, hired;
};

} // namespace

void CWorldServer::ApplyMercBriefing(CPlayer* player, const char* name) {
    CPlayerServer* state = player->Server();
    CParty* party = PartyOf(player);
    auto is = [name](const mfc::CString& variable) {
        return gamecrt::mbsicmp(variable.GetString(),
                               name) == 0;
    };
    for (int slot = 1; slot <= kMercSlots; ++slot) {
        MercNames v;
        v.merc.Format(worlddata::MercNameFormat(), slot);
        unsigned key = TextKeyFromName(v.merc);
        v.n8.Format(worlddata::MercVariableN8Format(), v.merc.GetString(), slot);
        v.n1.Format(worlddata::MercVariableN1Format(), v.merc.GetString(), slot);
        v.n2.Format(worlddata::MercVariableN2Format(), v.merc.GetString(), slot);
        v.n3.Format(worlddata::MercVariableN3Format(), v.merc.GetString(), slot);
        v.n9.Format(worlddata::MercVariableN9Format(), v.merc.GetString(), slot);
        v.n10.Format(worlddata::MercVariableN10Format(), v.merc.GetString(), slot);
        v.hired.Format(worlddata::MercHiredFormat(), slot);
        CUnit* unit = static_cast<CWorld*>(this)->FindUnit(key);

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
            const char* prototype = unit ? unit->GetPrototype()->name.GetString() : worlddata::HumanHeroText();
            party->AddMember(v.merc, prototype, game::EmptyText());
        }
    }
    UpdateMercBriefings();
}

namespace {

constexpr std::uint32_t kMinimumWeight = 0x3C23D70A;      // 0.01f

// An element of the list of shares: the unit's controller and its experience (nodes of 0x10 bytes).
struct Share {
    CUnitServer* controller;
    float share;
};
using ShareList = mfc::CList<Share>;
using ShareNode = ShareList::Node;
static_assert(sizeof(ShareNode) == 0x10);

using IdArray = mfc::CArray<int>;

// The unit controller, a party record's statistics and the player state (which tells its client) gain experience.

// The weight of a unit in the shared experience: sqrt(pending + gained experience) of its controller's statistics.
double Weight(CUnitServer* controller) {
    return std::sqrt(static_cast<double>(controller->stats.pendingExperience) + controller->stats.experience);
}

// Without a player: the single heroes of every player share `amount` in proportion to their
// weights.
void ShareAmongSoloPlayers(CWorldServer* world, float amount) {
    if (world->PlayerList()->objects_.Size() == 0) return;
    ShareList list{mfc::impl::ExperienceListClass::Vtable(), nullptr, nullptr, 0, nullptr, nullptr, 10};
    auto soloUnit = [&](CPlayer* p) -> CUnit* {
        if (!p->Server() || p->Party()->members_.size != 1) return nullptr;
        return static_cast<CWorld*>(world)->FindUnit(static_cast<int>(MercRecord(p, 0).unitId));
    };

    float total = 0.0f;
    PlayerIterator it;
    for (CPlayer* p = it.First(world->PlayerList()); p; p = it.Next()) {
        CUnit* unit = soloUnit(p);
        if (!unit) continue;
        CUnitServer* controller = unit->ServerController();
        if (!controller) continue;
        ShareNode* node = list.NewNode(list.tail, nullptr);
        node->data.controller = controller;
        std::uint32_t zero = 0;
        std::memcpy(&node->data.share, &zero, 4);
        if (list.tail) list.tail->next = node;
        else list.head = node;
        list.tail = node;
        total = static_cast<float>(Weight(node->data.controller) + total);
        float stored = total;   // FSTP into the float total
        total = stored;
    }
    float minimum;
    std::memcpy(&minimum, &kMinimumWeight, 4);
    float divisor = total >= minimum ? total : minimum;   // branch-free select

    for (ShareNode* node = list.head; node;) {
        ShareNode* next = node->next;
        float share = static_cast<float>(Weight(node->data.controller) * amount / divisor);
        float stored = share;
        node->data.share = stored;
        node->data.controller->AddExperience(stored);
        node = next;
    }
    ShareNode* cursor = list.head;
    for (CPlayer* p = it.First(world->PlayerList()); p; p = it.Next()) {
        CUnit* unit = soloUnit(p);
        if (!unit || !unit->ServerController()) continue;
        ShareNode* node = cursor;
        cursor = node->next;
        MercRecord(p, 0).stats.AddScaledExperience(node->data.share);
    }
    for (CPlayer* p = it.First(world->PlayerList()); p; p = it.Next()) {
        CPlayerServer* state = p->Server();
        if (!state) continue;
        for (ShareNode* node = list.head; node; node = node->next)
            state->SendPair18(node->data.controller->unit->id_, std::bit_cast<std::uint32_t>(node->data.share));
    }
    if (it.manager) {
        --it.manager->refCount_;
        it.manager = nullptr;
    }
    list.vtable = mfc::impl::ExperienceListClass::Vtable();
    list.RemoveAll();
}

// With a player: its living heroes share `amount` equally.
void ShareAmongHeroes(CWorldServer* world, CPlayer* player, float amount) {
    IdArray heroes{mfc::impl::IntArrayClass::Vtable(), nullptr, 0, 0, 0};
    for (auto* node = player->units_.First(); node && node->value; node = player->units_.After(node)) {
        CEngineObject* unit = node->value;
        if (ObjectSlots(unit)->IsDead() != 0 || unit->type_ != kUnitTypeHero) continue;
        int id = unit->id_;
        int index = heroes.size;
        heroes.SetSize(index + 1, -1);
        heroes.data[index] = id;
    }
    int count = heroes.size;
    if (count >= 1) {
        float share = static_cast<float>(static_cast<double>(amount) / count);
        float stored = share;
        share = stored;
        for (int i = 0; i < heroes.size; ++i) {
            CUnit* unit = static_cast<CWorld*>(world)->FindUnit(heroes.data[i]);
            unit->ServerController()->AddExperience(share);
            for (int j = 0; j < player->Party()->members_.size; ++j) {
                PartyUnitRecord& record = MercRecord(player, j);
                if (static_cast<int>(record.unitId) == heroes.data[i]) record.stats.AddScaledExperience(share);
            }
        }
        player->Server()->SendValue15(std::bit_cast<std::uint32_t>(share));
    }
    heroes.vtable = mfc::impl::IntArrayClass::Vtable();
    if (heroes.data) gamecrt::operator_delete(heroes.data);
}

} // namespace

void CWorldServer::AwardExperience(CPlayer* player, float amount) {
    EI_NAN_AUDIT(amount);
    if (amount == 0.0f) return;
    if (!player) ShareAmongSoloPlayers(this, amount);
    else ShareAmongHeroes(this, player, amount);
}

namespace {

constexpr int kQuickSlots = 4;
constexpr int kWornSlots = 7;
constexpr int kBeltSlots = 4;
constexpr int kSpellSlots = 8;

// The unit record's lists (weapons ("quick"), quick items ("belt"), armour ("worn"), spells): remove an entry;
// add an entry (they return the record entry).

// The party's copy of an item (CParty's copy virtual; the record entry's source).
CItem* PartyCopy(CParty* party, CItem* item) { return party->CopyItem(item); }

// The record entry for `item` from the party records, with a reference held across `add`.
template <auto Add>
CItem* AddToRecord(CParty* party, PartyUnitRecord* record, CItem* item) {
    CItem* entry = PartyCopy(party, item);
    if (entry) ++entry->refCount_;
    CItem* added = (record->*Add)(entry);
    if (entry) entry->Release();
    return added;
}


// The record's four lists emptied (armour, weapons, spells, quick items: the first entry each time).
void ClearRecordItems(PartyUnitRecord* record) {
    while (record->armorCount) record->RemoveArmor(record->armor[0]);
    while (record->weaponCount) record->RemoveWeapon(record->weapons[0]);
    while (record->spellCount) record->RemoveSpell(record->spells[0]);
    while (record->quickItemCount) record->RemoveQuickItem(record->quickItems[0]);
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
        reinterpret_cast<RefPtr*>(&item->link_)->Assign(reinterpret_cast<CRefObject*>(ref));
    }
    for (int slot = 0; slot < kWornSlots; ++slot) {
        CItem* item = unit->WornItem(slot);
        if (!item) continue;
        CEngineObject* entry = AddToRecord<&PartyUnitRecord::AddArmor>(party, record, item);
        CItemLink* ref = new CItemLink();
        ref->value = static_cast<std::uint32_t>(entry->id_);
        reinterpret_cast<RefPtr*>(&item->link_)->Assign(reinterpret_cast<CRefObject*>(ref));
    }
    for (int slot = 0; slot < kBeltSlots; ++slot) {
        auto* item = unit->GetQuickItem(slot);
        if (!item || static_cast<int>(item->count_) <= 0) continue;
        CItem* entry = PartyCopy(party, item);
        entry->count_ = item->count_;   // before the null test
        if (entry) ++entry->refCount_;
        CEngineObject* added = (CEngineObject*)record->AddQuickItem(entry);
        if (entry) entry->Release();
        CItemLink* link = new CItemLink();
        link->value = static_cast<std::uint32_t>(added->id_);
        CItemLink* old = item->ServerLink();
        item->link_ = link;
        if (link) ++link->refs_;
        if (old) old->Release();
    }
    for (int slot = 0; slot < kSpellSlots; ++slot) {
        CItem* spell = unit->GetSpell(slot);
        if (!spell) continue;
        AddToRecord<&PartyUnitRecord::AddSpell>(party, record, spell);
    }
    controller->CopyStats(&record->stats);
}

} // namespace

void CWorldServer::SyncPartyRecords(CPlayer* player) {
    CParty* party = PartyOf(player);
    for (auto* node = player->items_.head; node;) {
        auto* next = node->next;
        party->AddItem(node->data, 1, 1);
        node = next;
    }
    RecordPtrArray list{mfc::impl::PartyUnitArrayClass::Vtable(), nullptr, 0, 0, 0};
    for (int i = 0; i < player->Party()->members_.size; ++i) list.SetAtGrow(list.size, &MercRecord(player, i));
    for (int i = 0; i < list.size; ++i) {
        auto* record = list.data[i];
        CUnit* unit = static_cast<CWorld*>(this)->FindUnit(static_cast<int>(record->unitId));
        if (!unit || ObjectSlots(unit)->IsDead() != 0) continue;
        SyncRecord(party, record, unit);
    }
    list.vtable = mfc::impl::PartyUnitArrayClass::Vtable();
    if (list.data) gamecrt::operator_delete(list.data);
}

namespace {

constexpr int kKindNetwork = 4;   // kKindNoRangeTest (3): above

using PlacementArray = mfc::CArray<UnitPlacement>;

// One record rebuilt from its living unit (as SyncPartyRecords does, without the items'
// records), plus the unit's +0x268 / +0x26C (single player) and +0x438.
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
        std::memcpy(&maximum, &entry->chargeMax_, sizeof(maximum));   // before the null test
        entry->count_ = amount;
        std::memcpy(&entry->charge_, &maximum, sizeof(maximum));
        if (entry) ++entry->refCount_;
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

void CWorldServer::StoreParty(RecordPtrArray* list, CParty* party, PlacementArray* placements) {
    if (sessionKind_ == 1) {
        // Each record remembers whom its unit follows (-1: nobody).
        for (int i = 0; i < list->size; ++i) {
            auto* record = list->data[i];
            CUnit* unit = static_cast<CWorld*>(this)->FindUnit(static_cast<int>(record->unitId));
            CEngineObject* leader = unit->ServerController()->brain.LeaderOrFollowed();
            record->leaderId = leader ? leader->id_ : -1;
        }
    }
    for (int i = 0; i < list->size; ++i) {
        auto* record = list->data[i];
        CUnit* unit = static_cast<CWorld*>(this)->FindUnit(static_cast<int>(record->unitId));
        if (!unit) continue;
        if (placements) {
            UnitPlacement entry;
            entry.id = unit->id_;
            std::memcpy(entry.position, &unit->localPosition_.x, sizeof(entry.position));
            std::memcpy(entry.rotation, &unit->localRotation_, sizeof(entry.rotation));
            entry.value20 = static_cast<std::uint32_t>(unit->moveMode2);
            entry.value24 = unit->activeWeapon;
            mfc::impl::ArrayAs<mfc::impl::Placements>(*placements)
                .SetAtGrow(placements->size, reinterpret_cast<const mfc::impl::Placement40&>(entry));
        }
        if (!ObjectSlots(unit)->IsDead() || IsMultiplayerClient()) StoreRecord(this, party, record, unit);
        if (!ObjectSlots(unit)->IsDead()) RemoveObject(unit->id_);
        else unit->SetPlayer(nullptr);
    }
}

void CWorldServer::CollectParty(CPlayer* player, PlacementArray* placements) {
    if (!player) return;
    if (sessionKind_ == 1 || sessionKind_ == kKindNoRangeTest) UpdatePlayerTimes(0.0f);
    if (sessionKind_ != kKindNetwork) {
        StorePlayerItems(player, PartyOf(player));
        player->Server()->SendPartyUpdate();
    }
    if (!(sessionKind_ == 1 || sessionKind_ == kKindNoRangeTest) || zoneLoaded_ == 0) return;

    RecordPtrArray list{mfc::impl::PartyUnitArrayClass::Vtable(), nullptr, 0, 0, 0};
    // Records whose unit is gone (or dead, outside a network client game) are dropped.
    for (int i = 0; i < player->Party()->members_.size; ++i) {
        CUnit* unit = static_cast<CWorld*>(this)->FindUnit(static_cast<int>(MercRecord(player, i).unitId));
        if (unit && (!ObjectSlots(unit)->IsDead() || IsMultiplayerClient())) continue;
        int following = player->Party()->members_.size - i - 1;
        MercRecord(player, i).Destruct();
        if (following)
            std::memmove(&MercRecord(player, i), &MercRecord(player, i + 1), following * sizeof(PartyUnitRecord));
        --player->Party()->members_.size;
        --i;
    }
    for (int i = 0; i < player->Party()->members_.size; ++i) {
        int index = list.size;
        list.SetSize(index + 1, -1);
        list.data[index] = &MercRecord(player, i);
    }
    StoreParty(&list, PartyOf(player), placements);
    list.vtable = mfc::impl::PartyUnitArrayClass::Vtable();
    if (list.data) gamecrt::operator_delete(list.data);
}

void CWorldServer::StorePlayerItems(CPlayer* player, CParty* party) {
    if (!player) return;
    for (auto* node = player->items_.head; node;) {
        CItem* item = node->data;
        node = node->next;
        party->AddItem(item, 1, 1);
        item->Release();
    }
    player->items_.RemoveAll();
}

void CWorldServer::ClearPlayerItems(CPlayer* player) {
    CParty* party = player->Party();
    while (party->items_.size != 0) party->RemoveItem(party->items_.data[0], 1);
}
EI_REWRITTEN(0x00591070, CWorldServer::ClearPlayerItems);

void CWorldServer::TellParties(CEngineObject* unit, std::uint32_t value) {
    PlayerIterator it;
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        CPlayerServer* server = player->Server();
        if (static_cast<std::uint8_t>(server->IsPartyUnit(unit))) server->SendObject0B(unit, value);
    }
}

void CWorldServer::TellPartiesGone(CEngineObject* unit) {
    PlayerIterator it;
    for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        CPlayerServer* server = player->Server();
        if (server->partyUnits_.Contains(unit)) server->SendObject43(unit);
    }
}

EI_REWRITTEN(0x00590160, CWorldServer::StorePlayerItems);
EI_REWRITTEN(0x0058D340, CWorldServer::TellParties);
EI_REWRITTEN(0x00593AF0, CWorldServer::TellPartiesGone);
EI_REWRITTEN(0x00591800, CWorldServer::UpdateMercBriefings);
EI_REWRITTEN(0x0058FD90, CWorldServer::StoreParty);
EI_REWRITTEN(0x00590A40, CWorldServer::CollectParty);
EI_REWRITTEN(0x00590C70, CWorldServer::SyncPartyRecords);
EI_REWRITTEN(0x005911C0, CWorldServer::AwardExperience);
EI_REWRITTEN(0x00592090, CWorldServer::CompleteBriefing);
EI_REWRITTEN(0x00591DE0, CWorldServer::ApplyMercBriefing);
