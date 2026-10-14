// CWorldServer: the quest variables the server keeps for the scripts (the mercenaries' briefing
// variables "b.mercN.nK_N"). See world.h and docs/notes/world.md.
#include "world.h"
#include "world_data.h"
#include "game_strings.h"

#include "combat.h"
#include "console.h"
#include "item.h"
#include "map_object.h"
#include "party.h"
#include "player_messages.h"
#include "ui_briefing.h"
#include "unit.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "originals_gameplay.h"
#include "originals_library.h"
#include "originals_units.h"
#include "originals_ai.h"
#include "originals_world.h"
#include "originals_core.h"
#include "originals_engine.h"
#include "originals_script.h"

#include <cstring>

namespace {

constexpr int kMercSlots = 10;

// 00461360. The text key (object id) of a name (object_tables.cpp), ECX = name.
// 00460AF0 (RET 4). Named float table Get; the value is left in ST0.
double __fastcall VariableGet(void* a0, void*, const char* a2) { return (double)NamedTableGet(a0, nullptr, a2); }

// C3 of FCOMP: equal or unordered (the original's TEST AH,0x40).
bool EqualOrUnordered(double a, float b) { return !(a < b) && !(a > b); }

// The briefing variables of one mercenary slot (declared so that they are destroyed in the
// original's order: n11, amerc, n10, n2, n8, n9, n3, n1).
struct Briefing {
    mfc::CString n1, n3, n9, n8, n2, n10, hired, n11;
};

// The player's party (its records, items and variables; WorldPlayer + 0x70).
CParty* PartyOf(WorldPlayer* player) { return player->Party(); }
PartyUnitRecord& MercRecord(WorldPlayer* player, int index) { return player->Party()->members_.data[index]; }

} // namespace

void CWorldServer::UpdateMercBriefings() {
    PlayerIterator it;
    for (WorldPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        CPlayerServer* state = player->server;
        int otherMercs = CountLivingMercs(player->id);
        auto set = [&](const mfc::CString& name, float value) { PlayerStateSetVariableOriginal(state, nullptr, name, value); };

        for (int slot = 1; slot <= kMercSlots; ++slot) {
            mfc::CString merc;
            merc.Format(worlddata::MercNameFormat(), slot);
            unsigned key = TextKeyOf(merc);
            bool briefedHere = gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(zoneName_),
                                                reinterpret_cast<const unsigned char*>(worlddata::MercZoneName(slot - 1))) == 0;
            Briefing b;
            b.n8.Format(worlddata::MercVariableN8Format(), static_cast<const char*>(merc), slot);
            b.n1.Format(worlddata::MercVariableN1Format(), static_cast<const char*>(merc), slot);
            b.n2.Format(worlddata::MercVariableN2Format(), static_cast<const char*>(merc), slot);
            b.n3.Format(worlddata::MercVariableN3Format(), static_cast<const char*>(merc), slot);
            b.n9.Format(worlddata::MercVariableN9Format(), static_cast<const char*>(merc), slot);
            b.n10.Format(worlddata::MercVariableN10Format(), static_cast<const char*>(merc), slot);
            b.n11.Format(worlddata::MercVariableN11Format(), static_cast<const char*>(merc), slot);
            b.hired.Format(worlddata::MercHiredFormat(), slot);
            set(b.n11, 0.0f);

            bool inParty = false;
            for (int i = 0; i < player->mercCount; ++i) {
                if (MercRecord(player, i).unknown_01C == key) {
                    inParty = true;
                    break;
                }
            }
            void* variables = player->variables;
            if (inParty) {
                if (!EqualOrUnordered(VariableGet(variables, nullptr, b.n10), worlddata::Zero())) {
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
            } else if (!EqualOrUnordered(VariableGet(variables, nullptr, b.n10), worlddata::Zero())) {
                set(b.n8, 0.0f);
                set(b.n1, 0.0f);
                set(b.n2, 0.0f);
                set(b.n3, 0.0f);
                set(b.n9, 0.0f);
                set(b.n10, 0.0f);
                set(b.hired, 0.0f);
            } else {
                double hiredValue = VariableGet(variables, nullptr, b.hired);
                float hired = static_cast<float>(hiredValue);   // FST: stored as float, compared unrounded
                volatile float hiredStored = hired;
                if (!EqualOrUnordered(hiredValue, worlddata::Zero())) {
                    if (!EqualOrUnordered(VariableGet(variables, nullptr, b.n8), worlddata::One())) {
                        if (otherMercs < 2) {
                            bool firstTime = EqualOrUnordered(hiredStored, worlddata::One());
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
                } else if (!EqualOrUnordered(VariableGet(variables, nullptr, b.n8), worlddata::One())) {
                    set(b.n11, 1.0f);
                }
            }
        }
    }
}

// ---- CompleteBriefing -------------------------------------------------------------------------

namespace {

constexpr int kKindNoRangeTest = 3;
constexpr int kClassQuestItem = 0x14;

using NameList = BriefingRecord::NameList;

struct RawString {
    std::uint8_t bytes[12];
};
static RawString* RawStringInit(RawString* s, void*, const char* text, void* alloc) { return static_cast<RawString*>(::StringInitFromChars(s, nullptr, text, alloc)); }
inline BriefingRecord** __fastcall BriefingMapAt(void* a0, void*, const RawString* a2) { return static_cast<BriefingRecord**>(MapRegistryFindOriginal(a0, nullptr, a2)); }
// 004DBE40. Splits `text` (ECX) at `separator` into `out` (EDX).
// 0057FB60 (RET 4). The client asks the server to complete briefing `name` (GameFlow::RequestBriefing).
__declspec(naked) void __fastcall RequestBriefing(GameFlow*, void*, const char*) EI_ORIGINAL(0x0057FB60)
// 00591DE0 (RET 8). CWorldServer: the player's part of completing a briefing.
__declspec(naked) void __fastcall BriefingNotify(CWorldServer*, void*, WorldPlayer*, const char*) EI_ORIGINAL(0x00591DE0)
// 005911C0 (RET 8). CWorldServer: experience for `player` (float bits): WorldAwardExperienceOriginal.
// The game's _ftol (006E853C).
__declspec(naked) int __cdecl GameFtol(double) {
    __asm {
        fld qword ptr [esp + 4]
        mov eax, 0x006E853C
        call eax
        ret
    }
}
// 006AC970 (RET 4). The item manager (007AFFB4) creates an item by name. Patched by Rebirth.
// 00598EC0 (RET 4) / 00405130. A pointer list (vtable 0073B8D4) built and destroyed around each
// item creation, unused.
__declspec(naked) void __fastcall TempListConstruct(mfc::CTypedPtrList<void>*, void*, int /*blockSize*/) EI_ORIGINAL(0x00598EC0)
__declspec(naked) void __fastcall TempListDestruct(mfc::CTypedPtrList<void>*, void*) EI_ORIGINAL(0x00405130)
inline mfc::CTypedPtrList<CEngineObject>::Node* __fastcall PtrListNewNode(mfc::CTypedPtrList<CEngineObject>* l, void*, mfc::CTypedPtrList<CEngineObject>::Node* prev, mfc::CTypedPtrList<CEngineObject>::Node* next) { return static_cast<mfc::CTypedPtrList<CEngineObject>::Node*>(PtrListNewNodeOriginal(l, nullptr, prev, next)); }
void __fastcall PlayerGiveItems(WorldPlayer* a0, void*, mfc::CTypedPtrList<CEngineObject>* a2, int a3) { PlayerGiveItemsOriginal((void*)a0, nullptr, (void*)a2, a3); }
void __fastcall PlayerEraseQuestItem(WorldPlayer* a0, void*, int a2) { PlayerEraseQuestItemOriginal(a0, nullptr, a2); }
// 004D23A0 (RET 8). The first command queue: add a line (ScriptsRun runs it once).
int __fastcall CommandQueueAdd(void* a0, void*, const mfc::CString* a2, int a3) { return ScriptLoadLine(a0, nullptr, a2, a3); }

// The item named `name`, with the original's unused temporary lists around the creation (built
// in the order `firstAt`, then the other).
CEngineObject* CreateNamedItem(const char* name, bool fromSecond) {
    mfc::CTypedPtrList<void> lists[2];
    mfc::CTypedPtrList<void>* first = fromSecond ? &lists[0] : &lists[1];
    mfc::CTypedPtrList<void>* second = fromSecond ? &lists[1] : &lists[0];
    TempListConstruct(first, nullptr, 10);
    TempListConstruct(second, nullptr, 10);
    CEngineObject* item = ItemManagerCreateItemOriginal(WorldItemManager(), nullptr, name);
    TempListDestruct(second, nullptr);
    TempListDestruct(first, nullptr);
    return item;
}

// The rewards of a briefing record for `player`.
void GiveBriefingRewards(CWorldServer* world, WorldPlayer* player, BriefingRecord* record) {
    float experience = record->experience;
    if (experience < 0.0f || experience > 0.0f) {
        std::uint32_t bits;
        std::memcpy(&bits, &experience, 4);
        WorldAwardExperienceOriginal(world, nullptr, player, bits);
    }
    PartyOf(player)->Money() += GameFtol(record->gold);

    mfc::CTypedPtrList<CEngineObject> items;
    items.vtable = reinterpret_cast<void*>(worlddata::kItemListVtable);
    items.head = nullptr;
    items.tail = nullptr;
    items.count = 0;
    items.free = nullptr;
    items.blocks = nullptr;
    items.blockSize = 10;
    for (const mfc::CString* name = record->giveItems.begin(); name != record->giveItems.end(); ++name) {
        CEngineObject* item = CreateNamedItem(*name, true);
        if (!item) continue;
        auto* node = PtrListNewNode(&items, nullptr, items.tail, nullptr);
        node->data = item;
        if (items.tail) items.tail->next = node;
        else items.head = node;
        items.tail = node;
    }
    if (items.count) PlayerGiveItems(player, nullptr, &items, 0);

    for (const mfc::CString* name = record->removeItems.begin(); name != record->removeItems.end(); ++name) {
        CEngineObject* item = CreateNamedItem(*name, false);
        if (!item) continue;
        ++item->refCount_;
        if (item->GetClassId() == kClassQuestItem)
            PlayerEraseQuestItem(player, nullptr, static_cast<int>(static_cast<CItemQuest*>(item)->questItemId_));
        item->Release();
    }

    CPlayerServer* state = player->server;
    for (const mfc::CString* name = record->giveQuests.begin(); name != record->giveQuests.end(); ++name)
        PlayerStateSetVariableMaxOriginal(state, nullptr, *name, 1.0f);
    for (const mfc::CString* name = record->completeQuests.begin(); name != record->completeQuests.end(); ++name)
        PlayerStateSetVariableOriginal(state, nullptr, *name, 2.0f);
    for (const mfc::CString* name = record->availableZones.begin(); name != record->availableZones.end(); ++name)
        PlayerStateSetVariableMaxOriginal(state, nullptr, *name, 1.0f);
    for (const mfc::CString* name = record->completeZones.begin(); name != record->completeZones.end(); ++name)
        PlayerStateSetVariableOriginal(state, nullptr, *name, 2.0f);

    // The list's inlined destructor (its nodes are dropped, the items stay with the player).
    items.vtable = reinterpret_cast<void*>(worlddata::kItemListVtable);
    items.RemoveAll();
}

} // namespace

void CWorldServer::CompleteBriefing(int playerId, const char* name) {
    if (IsMultiplayerClient()) {
        console::CStringArray parts;
        StringSplit(name, &parts, '.');
        if (parts.GetSize() == 3) RequestBriefing(TheGameFlow(), nullptr, parts[2]);
        return;
    }
    console::CStringArray parts;
    StringSplit(name, &parts, '.');
    BriefingRecord* record = nullptr;
    if (parts.GetSize() == 3) {
        RawString key;
        char allocator;
        RawStringInit(&key, nullptr, parts[2], &allocator);
        record = *BriefingMapAt(worlddata::QuestBriefings(), nullptr, &key);
        StlStringDestroy(&key);
    }
    WorldPlayer* player = nullptr;
    {
        PlayerIterator it;
        for (WorldPlayer* p = it.First(PlayerList()); p; p = it.Next()) {
            if (p->id == playerId) {
                player = p;
                break;
            }
        }
    }
    if (player) {
        BriefingNotify(this, nullptr, player, name);
        if (record) GiveBriefingRewards(this, player, record);
    }

    mfc::CString script;
    script.Format(worlddata::BriefingCompleteScript(), playerId, name, playerId, name);
    CommandQueueAdd(commandsA_, nullptr, &script, 1);
    if (unknown12D64_ == kKindNoRangeTest)
        for (int i = 0; i < 5; ++i) ScriptsRun(MainScript());
}

// ---- ApplyMercBriefing ------------------------------------------------------------------------

namespace {

// 0055FB00 (RET 4). CWorld: the unit object with a text key.
// 00530930. The unit's prototype record; its first field is the prototype name.
static const char** UnitPrototype(CEngineObject* object, void*) { return static_cast<const char**>(::UnitPrototypeOf(object)); }
// 006628D0 (RET 0xC) / 006620F0 (RET 4). The player's party records (+0x70): add a member
// (name, prototype, extra), remove the member with a text key.
void __fastcall PartyAddMember(CParty* a0, void*, const char* a2, const char* a3, const char* a4) { PartyAddMemberOriginal(a0, nullptr, a2, a3, a4); }
void __fastcall PartyRemoveMember(CParty* a0, void*, unsigned a2) { PartyRemoveMemberOriginal(a0, nullptr, a2); }
// 005935C0 (RET 4, patched by Rebirth). CWorldServer: removes the object with an id.

// One slot's names, declared so that they are destroyed in the original's order: amerc, n10,
// n2, n8, n9, n3, n1, merc.
struct MercNames {
    mfc::CString merc, n1, n3, n9, n8, n2, n10, hired;
};

} // namespace

void CWorldServer::ApplyMercBriefing(WorldPlayer* player, const char* name) {
    CPlayerServer* state = player->server;
    CParty* party = PartyOf(player);
    auto is = [name](const mfc::CString& variable) {
        return gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(variable)),
                                reinterpret_cast<const unsigned char*>(name)) == 0;
    };
    for (int slot = 1; slot <= kMercSlots; ++slot) {
        MercNames v;
        v.merc.Format(worlddata::MercNameFormat(), slot);
        unsigned key = TextKeyOf(v.merc);
        v.n8.Format(worlddata::MercVariableN8Format(), static_cast<const char*>(v.merc), slot);
        v.n1.Format(worlddata::MercVariableN1Format(), static_cast<const char*>(v.merc), slot);
        v.n2.Format(worlddata::MercVariableN2Format(), static_cast<const char*>(v.merc), slot);
        v.n3.Format(worlddata::MercVariableN3Format(), static_cast<const char*>(v.merc), slot);
        v.n9.Format(worlddata::MercVariableN9Format(), static_cast<const char*>(v.merc), slot);
        v.n10.Format(worlddata::MercVariableN10Format(), static_cast<const char*>(v.merc), slot);
        v.hired.Format(worlddata::MercHiredFormat(), slot);
        CEngineObject* unit = WorldFindUnit(this, nullptr, key);

        if (is(v.n8)) PlayerStateSetVariableOriginal(state, nullptr, v.n8, 0.0f);
        // n10: the mercenary leaves the party and the world; n2: the party.
        if (is(v.n10)) {
            PartyRemoveMember(party, nullptr, key);
            WorldRemoveObject(this, nullptr, key);
        }
        if (is(v.n2)) PartyRemoveMember(party, nullptr, key);
        // n1 / n3: hired (amerc_N = 2) and added to the party with its unit's prototype.
        if (is(v.n1) || is(v.n3)) {
            PlayerStateSetVariableOriginal(state, nullptr, v.hired, 2.0f);
            const char* prototype = unit ? *UnitPrototype(unit, nullptr) : worlddata::HumanHeroText();
            PartyAddMember(party, nullptr, v.merc, prototype, game::EmptyText());
        }
    }
    WorldUpdateMercBriefingsOriginal(this);
}

// ---- AwardExperience --------------------------------------------------------------------------

// The shares keep the original's operation order: no reassociation here.
#pragma float_control(precise, on, push)

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

__declspec(naked) ShareNode* __fastcall ShareListNewNode(ShareList*, void*, ShareNode* /*prev*/, ShareNode* /*next*/)
    EI_ORIGINAL(0x0059D5F0)
__declspec(naked) void __fastcall ShareListRemoveAll(ShareList*, void*) EI_ORIGINAL(0x0059D5C0)
inline void __fastcall IdArraySetSize(IdArray* a, void*, int n, int g) { DwordArraySetSizeOriginal(a, nullptr, n, g); }
// 00553380 (RET 4). The unit controller gains experience.
// 00523960 (RET 4). A party record's statistics (+0x20) gain experience.
// 0066F2C0 (RET 8) / 0066F280 (RET 4). The player state tells its client about experience.
__declspec(naked) void __fastcall StatePartyExperience(CPlayerServer*, void*, float) EI_ORIGINAL(0x0066F280)

// The unit's controller (CUnit + 0x240), a CUnitServer on the server.
CUnitServer* ControllerOf(CEngineObject* unit) { return static_cast<CUnitServer*>(static_cast<CUnit*>(unit)->controller); }

// The weight of a unit in the shared experience: sqrt(pending + gained experience) of its
// controller's statistics (+0x8E0, +0x1E4).
double Weight(CUnitServer* controller) {
    return x87::fsqrt(static_cast<double>(controller->stats.pendingExperience) + controller->stats.experience);
}

// Without a player: the single heroes of every player share `amount` in proportion to their
// weights.
void ShareAmongSoloPlayers(CWorldServer* world, float amount) {
    if (world->PlayerList()->objects_.Size() == 0) return;
    ShareList list{reinterpret_cast<void*>(worlddata::kShareListVtable), nullptr, nullptr, 0, nullptr, nullptr, 10};
    auto soloUnit = [&](WorldPlayer* p) -> CEngineObject* {
        if (!p->server || p->mercCount != 1) return nullptr;
        return WorldFindUnit(world, nullptr, static_cast<int>(MercRecord(p, 0).unknown_01C));
    };

    float total = 0.0f;
    PlayerIterator it;
    for (WorldPlayer* p = it.First(world->PlayerList()); p; p = it.Next()) {
        CEngineObject* unit = soloUnit(p);
        if (!unit) continue;
        CUnitServer* controller = ControllerOf(unit);
        if (!controller) continue;
        ShareNode* node = ShareListNewNode(&list, nullptr, list.tail, nullptr);
        node->data.controller = controller;
        std::uint32_t zero = 0;
        std::memcpy(&node->data.share, &zero, 4);
        if (list.tail) list.tail->next = node;
        else list.head = node;
        list.tail = node;
        total = static_cast<float>(Weight(node->data.controller) + total);
        volatile float stored = total;   // FSTP into the float total
        total = stored;
    }
    float minimum;
    std::memcpy(&minimum, &kMinimumWeight, 4);
    float divisor = !(total >= minimum) ? minimum : total;   // the original's branch-free select

    for (ShareNode* node = list.head; node;) {
        ShareNode* next = node->next;
        float share = static_cast<float>(Weight(node->data.controller) * amount / divisor);
        volatile float stored = share;
        node->data.share = stored;
        DataGainExperience(node->data.controller, nullptr, stored);
        node = next;
    }
    ShareNode* cursor = list.head;
    for (WorldPlayer* p = it.First(world->PlayerList()); p; p = it.Next()) {
        CEngineObject* unit = soloUnit(p);
        if (!unit || !ControllerOf(unit)) continue;
        ShareNode* node = cursor;
        cursor = node->next;
        UnitStatsAddExperience(reinterpret_cast<UnitStats*>(MercRecord(p, 0).stats), nullptr, node->data.share);
    }
    for (WorldPlayer* p = it.First(world->PlayerList()); p; p = it.Next()) {
        CPlayerServer* state = p->server;
        if (!state) continue;
        for (ShareNode* node = list.head; node; node = node->next)
            PlayerSendPair(state, nullptr, node->data.controller->unit->id_, node->data.share);
    }
    if (it.list) {
        --it.list->refCount_;
        it.list = nullptr;
    }
    list.vtable = reinterpret_cast<void*>(worlddata::kShareListVtable);
    ShareListRemoveAll(&list, nullptr);
}

// With a player: its living heroes share `amount` equally.
void ShareAmongHeroes(CWorldServer* world, WorldPlayer* player, float amount) {
    IdArray heroes{mfc::DWordArrayVtable(), nullptr, 0, 0, 0};
    for (auto* node = player->units.map.First(); node && node->value; node = player->units.map.After(node)) {
        CEngineObject* unit = node->value;
        if (ObjectSlots(unit)->IsDead() != 0 || unit->type_ != kUnitTypeHero) continue;
        int id = unit->id_;
        int index = heroes.size;
        IdArraySetSize(&heroes, nullptr, index + 1, -1);
        heroes.data[index] = id;
    }
    int count = heroes.size;
    if (count >= 1) {
        float share = static_cast<float>(static_cast<double>(amount) / count);
        volatile float stored = share;
        share = stored;
        for (int i = 0; i < heroes.size; ++i) {
            CEngineObject* unit = WorldFindUnit(world, nullptr, heroes.data[i]);
            DataGainExperience(ControllerOf(unit), nullptr, share);
            for (int j = 0; j < player->mercCount; ++j) {
                PartyUnitRecord& record = MercRecord(player, j);
                if (static_cast<int>(record.unknown_01C) == heroes.data[i]) UnitStatsAddExperience(reinterpret_cast<UnitStats*>(record.stats), nullptr, share);
            }
        }
        StatePartyExperience(player->server, nullptr, share);
    }
    heroes.vtable = mfc::DWordArrayVtable();
    if (heroes.data) gamecrt::operator_delete(heroes.data);
}

} // namespace

void CWorldServer::AwardExperience(WorldPlayer* player, float amount) {
    if (!(amount < 0.0f || amount > 0.0f)) return;   // C3: zero or NaN
    if (!player) ShareAmongSoloPlayers(this, amount);
    else ShareAmongHeroes(this, player, amount);
}

#pragma float_control(pop)

// ---- SyncPartyRecords -------------------------------------------------------------------------

namespace {

constexpr int kQuickSlots = 4;
constexpr int kWornSlots = 7;
constexpr int kBeltSlots = 4;
constexpr int kSpellSlots = 8;

inline int __fastcall PartyAddItem(CParty* a0, void*, void* a2, int a3, int a4) { return (int)PartyAddItemOriginal((void*)a0, nullptr, a2, a3, a4); }
void __fastcall RecordArraySetAtGrow(RecordPtrArray* a0, void*, int a2, PartyUnitRecord* a3) { RecordArraySetAtGrowOriginal((void*)a0, nullptr, a2, (void*)a3); }
// The unit record's lists (weapons +0x730 (the unit's "quick" slots below), quick items +0x740
// ("belt"), armour +0x750 ("worn"), spells +0x760): remove an entry (00660A50, 00660C30, 00660E10,
// 00660FF0); add an entry (006608D0, 00660AB0, 00660C90, 00660E70; they return the record entry).
void __fastcall RecordRemoveWorn(PartyUnitRecord* a0, void*, void* a2) { RecordRemoveWornEntryOriginal((void*)a0, nullptr, a2); }
void __fastcall RecordRemoveQuick(PartyUnitRecord* a0, void*, void* a2) { RecordRemoveQuickEntryOriginal((void*)a0, nullptr, a2); }
void __fastcall RecordRemoveSpell(PartyUnitRecord* a0, void*, void* a2) { RecordRemoveSpellEntryOriginal((void*)a0, nullptr, a2); }
void __fastcall RecordRemoveBelt(PartyUnitRecord* a0, void*, void* a2) { RecordRemoveBeltEntryOriginal((void*)a0, nullptr, a2); }
CEngineObject* __fastcall RecordAddQuick(PartyUnitRecord* a0, void*, void* a2) { return (CEngineObject*)RecordAddQuickEntryOriginal((void*)a0, nullptr, a2); }
CEngineObject* __fastcall RecordAddWorn(PartyUnitRecord* a0, void*, void* a2) { return (CEngineObject*)RecordAddWornEntryOriginal((void*)a0, nullptr, a2); }
CEngineObject* __fastcall RecordAddBelt(PartyUnitRecord* a0, void*, void* a2) { return (CEngineObject*)RecordAddBeltEntryOriginal((void*)a0, nullptr, a2); }
CEngineObject* __fastcall RecordAddSpell(PartyUnitRecord* a0, void*, void* a2) { return (CEngineObject*)RecordAddSpellEntryOriginal((void*)a0, nullptr, a2); }
// The unit's slots: quick (005318B0), worn (00530830), belt (00531920), spells (005319A0).
CEngineObject* __fastcall UnitWornItem(CEngineObject* a0, void*, int a2) { return UnitWornItemOriginal((const void*)a0, nullptr, a2); }
// 00553360 (RET 4). The controller copies its statistics into a record's (+0x20).
__declspec(naked) void __fastcall ControllerStoreStats(CUnitServer*, void*, void*) EI_ORIGINAL(0x00553360)
void __fastcall RefRelease(void* a0, void*) { RefObjectRelease(a0); }

// The party's copy of an item (CParty slot 24; the record entry's source).
CItem* PartyCopy(CParty* party, CEngineObject* item) { return party->CopyItem(static_cast<CItem*>(item)); }

// The record entry for `item` from the party records, with a reference held across `add`.
template <typename Add>
CEngineObject* AddToRecord(CParty* party, PartyUnitRecord* record, CEngineObject* item, Add add) {
    CItem* entry = PartyCopy(party, item);
    if (entry) ++entry->refCount_;
    CEngineObject* added = add(record, nullptr, entry);
    if (entry) entry->Release();
    return added;
}

// A fresh item record (ItemSourceRef, item.h) for the item's link (+0x1C).
ItemSourceRef* NewItemRecord(bool refValueBase) {
    auto* memory = static_cast<ItemSourceRef*>(gamecrt::operator_new(sizeof(ItemSourceRef)));
    memory->refs = 0;   // before the null test
    if (!memory) return nullptr;
    if (!refValueBase) return static_cast<ItemSourceRef*>(ItemRecordConstruct(memory, nullptr));
    RefValueConstructOriginal(memory);
    memory->vtable = reinterpret_cast<void*>(worlddata::kItemSourceRefVtable);
    return memory;
}

// The record's four lists emptied (armour, weapons, spells, quick items: the first entry each time).
void ClearRecordItems(PartyUnitRecord* record) {
    while (record->armorCount) RecordRemoveWorn(record, nullptr, record->armor[0]);
    while (record->weaponCount) RecordRemoveQuick(record, nullptr, record->weapons[0]);
    while (record->spellCount) RecordRemoveSpell(record, nullptr, record->spells[0]);
    while (record->quickItemCount) RecordRemoveBelt(record, nullptr, record->quickItems[0]);
}

void SyncRecord(CParty* party, PartyUnitRecord* record, CEngineObject* unit) {
    CUnitServer* controller = ControllerOf(unit);
    ClearRecordItems(record);

    for (int slot = 0; slot < kQuickSlots; ++slot) {
        CEngineObject* item = UnitWeapon(static_cast<CUnit*>(unit), nullptr, slot);
        if (!item) continue;
        CEngineObject* entry = AddToRecord(party, record, item, RecordAddQuick);
        ItemSourceRef* ref = NewItemRecord(false);
        ref->itemId = static_cast<std::uint32_t>(entry->id_);
        RefPtrAssign(&static_cast<CItem*>(item)->link_, nullptr, ref);
    }
    for (int slot = 0; slot < kWornSlots; ++slot) {
        CEngineObject* item = UnitWornItem(unit, nullptr, slot);
        if (!item) continue;
        CEngineObject* entry = AddToRecord(party, record, item, RecordAddWorn);
        ItemSourceRef* ref = NewItemRecord(true);
        ref->itemId = static_cast<std::uint32_t>(entry->id_);
        RefPtrAssign(&static_cast<CItem*>(item)->link_, nullptr, ref);
    }
    for (int slot = 0; slot < kBeltSlots; ++slot) {
        auto* item = static_cast<CItem*>(UnitQuickItem(static_cast<CUnit*>(unit), nullptr, slot));
        if (!item || static_cast<int>(item->count_) <= 0) continue;
        CItem* entry = PartyCopy(party, item);
        entry->count_ = item->count_;   // before the null test
        if (entry) ++entry->refCount_;
        CEngineObject* added = RecordAddBelt(record, nullptr, entry);
        if (entry) entry->Release();
        ItemSourceRef* ref = NewItemRecord(true);
        ref->itemId = static_cast<std::uint32_t>(added->id_);
        // The counted pointer's assignment, inlined (16-bit count at +4).
        auto* link = reinterpret_cast<CItemLink*>(ref);
        CItemLink* old = reinterpret_cast<CItemLink*>(static_cast<std::uintptr_t>(item->link_));
        item->link_ = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(link));
        if (link) ++link->references;
        if (old) RefRelease(old, nullptr);
    }
    for (int slot = 0; slot < kSpellSlots; ++slot) {
        CEngineObject* spell = UnitSpell(static_cast<CUnit*>(unit), nullptr, slot);
        if (!spell) continue;
        AddToRecord(party, record, spell, RecordAddSpell);
    }
    ControllerStoreStats(controller, nullptr, record->stats);
}

} // namespace

void CWorldServer::SyncPartyRecords(WorldPlayer* player) {
    CParty* party = PartyOf(player);
    for (auto* node = player->items.head; node;) {
        auto* next = node->next;
        PartyAddItem(party, nullptr, node->data, 1, 1);
        node = next;
    }
    RecordPtrArray list{reinterpret_cast<void*>(worlddata::kRecordArrayVtable), nullptr, 0, 0, 0};
    for (int i = 0; i < player->mercCount; ++i) RecordArraySetAtGrow(&list, nullptr, list.size, &MercRecord(player, i));
    for (int i = 0; i < list.size; ++i) {
        auto* record = reinterpret_cast<PartyUnitRecord*>(list.data[i]);
        CEngineObject* unit = WorldFindUnit(this, nullptr, static_cast<int>(record->unknown_01C));
        if (!unit || ObjectSlots(unit)->IsDead() != 0) continue;
        SyncRecord(party, record, unit);
    }
    list.vtable = reinterpret_cast<void*>(worlddata::kRecordArrayVtable);
    if (list.data) gamecrt::operator_delete(list.data);
}

// ---- StoreParty / CollectParty ----------------------------------------------------------------

namespace {

constexpr int kKindNetwork = 4;   // kKindNoRangeTest (3): above

using PlacementArray = mfc::CArray<UnitPlacement>;

__declspec(naked) void __fastcall PlacementsSetAtGrow(PlacementArray*, void*, int, const UnitPlacement*) EI_ORIGINAL(0x0059C510)
__declspec(naked) void __fastcall RecordStatsFinish(void*, void*) EI_ORIGINAL(0x00523C80)
inline void __fastcall UnitSetController(CEngineObject* a0, void*, void* a2) { UnitSetControllerOriginal((void*)a0, nullptr, a2); }
__declspec(naked) void __fastcall StorePartyAt(CWorldServer*, void*, RecordPtrArray*, CParty*, void*) EI_ORIGINAL(0x0058FD90)
// 00590160 (RET 8). The player's items into its party records before leaving.
__declspec(naked) void __fastcall StorePlayerItemsOriginal(CWorldServer*, void*, WorldPlayer*, CParty* /*records*/) EI_ORIGINAL(0x00590160)
// 0066F0C0. The player state before a zone change.
__declspec(naked) void __fastcall StateBeforeZoneChange(CPlayerServer*, void*) EI_ORIGINAL(0x0066F0C0)
// 0065F740. The unit record's destructor body.
__declspec(naked) void __fastcall UnitRecordDestruct(PartyUnitRecord*, void*) EI_ORIGINAL(0x0065F740)
inline void __fastcall RecordArraySetSize(RecordPtrArray* a, void*, int n, int g) { RecordArraySetSizeOriginal(a, nullptr, n, g); }

// One record rebuilt from its living unit (as SyncPartyRecords does, without the items'
// records), plus the unit's +0x268 / +0x26C (single player) and +0x438.
void StoreRecord(CWorldServer* world, CParty* party, PartyUnitRecord* record, CEngineObject* unitObject) {
    auto* unit = static_cast<CUnit*>(unitObject);
    CUnitServer* controller = ControllerOf(unit);
    ClearRecordItems(record);
    for (int slot = 0; slot < kQuickSlots; ++slot)
        if (CEngineObject* item = UnitWeapon(static_cast<CUnit*>(unit), nullptr, slot)) AddToRecord(party, record, item, RecordAddQuick);
    for (int slot = 0; slot < kWornSlots; ++slot)
        if (CEngineObject* item = UnitWornItem(unit, nullptr, slot)) AddToRecord(party, record, item, RecordAddWorn);
    for (int slot = 0; slot < kBeltSlots; ++slot) {
        auto* item = static_cast<CItem*>(UnitQuickItem(static_cast<CUnit*>(unit), nullptr, slot));
        if (!item || static_cast<int>(item->count_) <= 0) continue;
        CItem* entry = PartyCopy(party, item);
        std::uint32_t amount = item->count_;
        std::uint32_t maximum;
        std::memcpy(&maximum, &entry->chargeMax_, sizeof(maximum));   // before the null test
        entry->count_ = amount;
        std::memcpy(&entry->charge_, &maximum, sizeof(maximum));
        if (entry) ++entry->refCount_;
        RecordAddBelt(record, nullptr, entry);
        if (entry) entry->Release();
    }
    for (int slot = 0; slot < kSpellSlots; ++slot)
        if (CEngineObject* spell = UnitSpell(static_cast<CUnit*>(unit), nullptr, slot)) AddToRecord(party, record, spell, RecordAddSpell);
    ControllerStoreStats(controller, nullptr, record->stats);
    if (world->unknown12D64_ == 1) {
        record->unknown_770 = unit->moveMode2;
        record->weaponIndex = unit->activeWeapon;
    }
    record->unknown_780 = unit->unknown_438;
    RecordStatsFinish(record->stats, nullptr);
}

} // namespace

void CWorldServer::StoreParty(void* recordsArg, void* partyRecords, void* placementsArg) {
    auto* list = static_cast<RecordPtrArray*>(recordsArg);
    auto* party = static_cast<CParty*>(partyRecords);
    auto* placements = static_cast<PlacementArray*>(placementsArg);
    if (unknown12D64_ == 1) {
        // Each record remembers whom its unit follows (-1: nobody).
        for (int i = 0; i < list->size; ++i) {
            auto* record = reinterpret_cast<PartyUnitRecord*>(list->data[i]);
            CEngineObject* unit = WorldFindUnit(this, nullptr, static_cast<int>(record->unknown_01C));
            CEngineObject* leader = BrainLeaderOrFollowedOriginal(ControllerOf(unit)->brain);
            record->unknown_77C = leader ? leader->id_ : -1;
        }
    }
    for (int i = 0; i < list->size; ++i) {
        auto* record = reinterpret_cast<PartyUnitRecord*>(list->data[i]);
        CEngineObject* found = WorldFindUnit(this, nullptr, static_cast<int>(record->unknown_01C));
        if (!found) continue;
        auto* unit = static_cast<CUnit*>(found);
        if (placements) {
            UnitPlacement entry;
            entry.id = unit->id_;
            std::memcpy(entry.position, &unit->position.x, sizeof(entry.position));
            std::memcpy(entry.rotation, &unit->orientation, sizeof(entry.rotation));
            entry.value20 = static_cast<std::uint32_t>(unit->moveMode2);
            entry.value24 = unit->activeWeapon;
            PlacementsSetAtGrow(placements, nullptr, placements->size, &entry);
        }
        if (!ObjectSlots(unit)->IsDead() || IsMultiplayerClient()) StoreRecord(this, party, record, unit);
        if (!ObjectSlots(unit)->IsDead()) WorldRemoveObject(this, nullptr, unit->id_);
        else UnitSetController(unit, nullptr, nullptr);
    }
}

void CWorldServer::CollectParty(WorldPlayer* player, void* placements) {
    if (!player) return;
    if (unknown12D64_ == 1 || unknown12D64_ == kKindNoRangeTest) UpdatePlayerTimesAt(this, nullptr, 0.0f);
    if (unknown12D64_ != kKindNetwork) {
        StorePlayerItemsOriginal(this, nullptr, player, PartyOf(player));
        StateBeforeZoneChange(player->server, nullptr);
    }
    if (!(unknown12D64_ == 1 || unknown12D64_ == kKindNoRangeTest) || unknown15C_ == 0) return;

    RecordPtrArray list{reinterpret_cast<void*>(worlddata::kRecordArrayVtable), nullptr, 0, 0, 0};
    // Records whose unit is gone (or dead, outside a network client game) are dropped (the
    // inlined CArray::RemoveAt).
    for (int i = 0; i < player->mercCount; ++i) {
        CEngineObject* unit = WorldFindUnit(this, nullptr, static_cast<int>(MercRecord(player, i).unknown_01C));
        if (unit && (!ObjectSlots(unit)->IsDead() || IsMultiplayerClient())) continue;
        int following = player->mercCount - i - 1;
        UnitRecordDestruct(&MercRecord(player, i), nullptr);
        if (following)
            GameMemmove(&MercRecord(player, i), &MercRecord(player, i + 1), following * sizeof(PartyUnitRecord));
        --player->mercCount;
        --i;
    }
    for (int i = 0; i < player->mercCount; ++i) {
        int index = list.size;
        RecordArraySetSize(&list, nullptr, index + 1, -1);
        list.data[index] = reinterpret_cast<std::uint8_t*>(&MercRecord(player, i));
    }
    StorePartyAt(this, nullptr, &list, PartyOf(player), placements);
    list.vtable = reinterpret_cast<void*>(worlddata::kRecordArrayVtable);
    if (list.data) gamecrt::operator_delete(list.data);
}

void CWorldServer::StorePlayerItems(WorldPlayer* player, CParty* party) {
    if (!player) return;
    for (auto* node = player->items.head; node;) {
        CEngineObject* item = node->data;
        node = node->next;
        party->AddItem(static_cast<CItem*>(item), 1, 1);
        item->Release();
    }
    player->items.RemoveAll();
}

void CWorldServer::ClearPlayerItems(WorldPlayer* player) {
    CParty* party = player->Party();
    while (party->items_.size != 0) party->RemoveItem(static_cast<CItem*>(party->items_.data[0]), 1);
}
EI_REPLACE_METHOD(0x00591070, CWorldServer::ClearPlayerItems);

void CWorldServer::TellParties(CEngineObject* unit, std::uint32_t value) {
    PlayerIterator it;
    for (WorldPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        CPlayerServer* server = player->server;
        // The original tests AL only.
        if (static_cast<std::uint8_t>(server->IsPartyUnit(unit))) server->SendObject0B(unit, value);
    }
}

void CWorldServer::TellPartiesGone(CEngineObject* unit) {
    PlayerIterator it;
    for (WorldPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
        CPlayerServer* server = player->server;
        if (server->partyUnits_.Contains(unit)) server->SendObject43(unit);
    }
}

EI_REPLACE_METHOD(0x00590160, CWorldServer::StorePlayerItems);
EI_REPLACE_METHOD(0x0058D340, CWorldServer::TellParties);
EI_REPLACE_METHOD(0x00593AF0, CWorldServer::TellPartiesGone);
EI_REPLACE_METHOD(0x00591800, CWorldServer::UpdateMercBriefings);
EI_REPLACE_METHOD(0x0058FD90, CWorldServer::StoreParty);
EI_REPLACE_METHOD(0x00590A40, CWorldServer::CollectParty);
EI_REPLACE_METHOD(0x00590C70, CWorldServer::SyncPartyRecords);
EI_REPLACE_METHOD(0x005911C0, CWorldServer::AwardExperience);
EI_REPLACE_METHOD(0x00592090, CWorldServer::CompleteBriefing);
EI_REPLACE_METHOD(0x00591DE0, CWorldServer::ApplyMercBriefing);
