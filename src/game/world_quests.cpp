// CWorldServer: the quest variables the server keeps for the scripts (the mercenaries' briefing
// variables "b.mercN.nK_N"). See world.h and docs/notes/world.md (seventh round).
#include "world.h"

#include "console.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"

#include <cstring>

namespace {

const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

// The ten mercenary slots and the zone where each is briefed (pointers at 007907AC).
const char* MercZone(int slot) { return reinterpret_cast<const char* const*>(0x007907AC)[slot]; }
constexpr int kMercSlots = 10;

constexpr std::uintptr_t kMercName = 0x007909E4;    // "merc%i"
constexpr std::uintptr_t kVarN8 = 0x007909D8;       // "b.%s.n8_%i"
constexpr std::uintptr_t kVarN1 = 0x007909CC;       // "b.%s.n1_%i"
constexpr std::uintptr_t kVarN2 = 0x007909C0;       // "b.%s.n2_%i"
constexpr std::uintptr_t kVarN3 = 0x007909B4;       // "b.%s.n3_%i"
constexpr std::uintptr_t kVarN9 = 0x007909A8;       // "b.%s.n9_%i"
constexpr std::uintptr_t kVarN10 = 0x0079099C;      // "b.%s.n10_%i"
constexpr std::uintptr_t kVarN11 = 0x00790990;      // "b.%s.n11_%i"
constexpr std::uintptr_t kVarHired = 0x00790984;    // "amerc_%i"

float Zero() { return *reinterpret_cast<const volatile float*>(0x0073B858); }
float One() { return *reinterpret_cast<const volatile float*>(0x0073B84C); }

// 00461360. The text key (object id) of a name (object_tables.cpp), ECX = name.
__declspec(naked) unsigned __fastcall TextKey(const char*) EI_ORIGINAL(0x00461360)
// 00460AF0 (RET 4). Named float table Get; the value is left in ST0.
__declspec(naked) double __fastcall VariableGet(void* /*table*/, void*, const char*) EI_ORIGINAL(0x00460AF0)
// 0066EE10 (RET 8). CPlayerServer::SetVariable (player_messages.cpp).
__declspec(naked) void __fastcall StateSetVariable(void*, void*, const char*, float) EI_ORIGINAL(0x0066EE10)

// C3 of FCOMP: equal or unordered (the original's TEST AH,0x40).
bool EqualOrUnordered(double a, float b) { return !(a < b) && !(a > b); }

// The briefing variables of one mercenary slot (declared so that they are destroyed in the
// original's order: n11, amerc, n10, n2, n8, n9, n3, n1).
struct Briefing {
    mfc::CString n1, n3, n9, n8, n2, n10, hired, n11;
};

} // namespace

void CWorldServer::UpdateMercBriefings() {
    PlayerIterator it;
    for (WorldPlayer* player = it.First(ei::VCall<CEngineObject*>(this, 20)); player; player = it.Next()) {
        void* state = *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(player) + 0x20);
        int otherMercs = CountLivingMercs(player->id);
        auto set = [&](const mfc::CString& name, float value) { StateSetVariable(state, nullptr, name, value); };

        for (int slot = 1; slot <= kMercSlots; ++slot) {
            mfc::CString merc;
            merc.Format(Text(kMercName), slot);
            unsigned key = TextKey(merc);
            bool briefedHere = gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(zoneName_),
                                                reinterpret_cast<const unsigned char*>(MercZone(slot - 1))) == 0;
            Briefing b;
            b.n8.Format(Text(kVarN8), static_cast<const char*>(merc), slot);
            b.n1.Format(Text(kVarN1), static_cast<const char*>(merc), slot);
            b.n2.Format(Text(kVarN2), static_cast<const char*>(merc), slot);
            b.n3.Format(Text(kVarN3), static_cast<const char*>(merc), slot);
            b.n9.Format(Text(kVarN9), static_cast<const char*>(merc), slot);
            b.n10.Format(Text(kVarN10), static_cast<const char*>(merc), slot);
            b.n11.Format(Text(kVarN11), static_cast<const char*>(merc), slot);
            b.hired.Format(Text(kVarHired), slot);
            set(b.n11, 0.0f);

            bool inParty = false;
            for (int i = 0; i < player->mercCount; ++i) {
                if (*reinterpret_cast<unsigned*>(player->mercs + i * 0x784 + 0x1C) == key) {
                    inParty = true;
                    break;
                }
            }
            void* variables = player->variables;
            if (inParty) {
                if (!EqualOrUnordered(VariableGet(variables, nullptr, b.n10), Zero())) {
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
            } else if (!EqualOrUnordered(VariableGet(variables, nullptr, b.n10), Zero())) {
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
                if (!EqualOrUnordered(hiredValue, Zero())) {
                    if (!EqualOrUnordered(VariableGet(variables, nullptr, b.n8), One())) {
                        if (otherMercs < 2) {
                            bool firstTime = EqualOrUnordered(hiredStored, One());
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
                } else if (!EqualOrUnordered(VariableGet(variables, nullptr, b.n8), One())) {
                    set(b.n11, 1.0f);
                }
            }
        }
    }
}

// ---- CompleteBriefing -------------------------------------------------------------------------

namespace {

constexpr std::uintptr_t kBriefingScript = 0x007909EC;
// "WorldScript( GSSetVar( %i,"%s",2 ) #OnBriefingComplete( %i, "%s" ) )"
constexpr std::uintptr_t kPtrListVtable = 0x0073BE80;
constexpr int kKindNoRangeTest = 3;
constexpr int kClassQuestItem = 0x14;

// The briefing records by name (007C87D4: hash_map<string, record*>). A record: +0x0C experience
// (float), +0x10 gold (float), then vectors of names (begin, end): +0x14 items to give, +0x20
// quest items to take, +0x2C / +0x44 variables raised to 1, +0x38 / +0x50 variables set to 2.
struct NameVector {
    const char** begin;
    const char** end;
    const char** capacity;
};

struct RawString {
    std::uint8_t bytes[12];
};
__declspec(naked) void __fastcall RawStringInit(RawString*, void*, const char*, void* /*allocator*/) EI_ORIGINAL(0x0040C5A0)
__declspec(naked) void __fastcall RawStringDestroy(RawString*, void*) EI_ORIGINAL(0x0040C720)
__declspec(naked) std::uint8_t** __fastcall BriefingMapAt(void*, void*, const RawString*) EI_ORIGINAL(0x004297A0)
// 004DBE40. Splits `text` (ECX) at `separator` into `out` (EDX).
__declspec(naked) void __fastcall Split(const char*, console::CStringArray*, char) EI_ORIGINAL(0x004DBE40)
// 0057FB60 (RET 4). The client asks the server to complete briefing `name` (ECX = 007AFE20).
__declspec(naked) void __fastcall RequestBriefing(void*, void*, const char*) EI_ORIGINAL(0x0057FB60)
// 00591DE0 (RET 8). CWorldServer: the player's part of completing a briefing.
__declspec(naked) void __fastcall BriefingNotify(CWorldServer*, void*, WorldPlayer*, const char*) EI_ORIGINAL(0x00591DE0)
// 005911C0 (RET 8). CWorldServer: experience for `player` (float bits).
__declspec(naked) void __fastcall AwardExperience(CWorldServer*, void*, WorldPlayer*, std::uint32_t) EI_ORIGINAL(0x005911C0)
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
__declspec(naked) CEngineObject* __fastcall CreateItemByName(void*, void*, const char*) EI_ORIGINAL(0x006AC970)
// 00598EC0 (RET 4) / 00405130. A pointer list (vtable 0073B8D4) built and destroyed around each
// item creation, unused.
__declspec(naked) void __fastcall TempListConstruct(void*, void*, int /*blockSize*/) EI_ORIGINAL(0x00598EC0)
__declspec(naked) void __fastcall TempListDestruct(void*, void*) EI_ORIGINAL(0x00405130)
__declspec(naked) mfc::CTypedPtrList<CEngineObject>::Node* __fastcall PtrListNewNode(
    mfc::CTypedPtrList<CEngineObject>*, void*, mfc::CTypedPtrList<CEngineObject>::Node*,
    mfc::CTypedPtrList<CEngineObject>::Node*) EI_ORIGINAL(0x004621A0)
__declspec(naked) void __fastcall PlayerGiveItems(WorldPlayer*, void*, mfc::CTypedPtrList<CEngineObject>*, int)
    EI_ORIGINAL(0x00665470)
__declspec(naked) void __fastcall PlayerEraseQuestItem(WorldPlayer*, void*, int) EI_ORIGINAL(0x00665780)
__declspec(naked) void __fastcall StateSetVariableMax(void*, void*, const char*, float) EI_ORIGINAL(0x0066EE80)
// 004D23A0 (RET 8) / 004BF000. The first command queue: add a line, run once.
__declspec(naked) void __fastcall CommandQueueAdd(void*, void*, const mfc::CString*, int) EI_ORIGINAL(0x004D23A0)
__declspec(naked) void __fastcall CommandQueueRun(void*, void*) EI_ORIGINAL(0x004BF000)

void* ItemManager() { return *reinterpret_cast<void**>(0x007AFFB4); }

// The item named `name`, with the original's unused temporary lists around the creation (built
// in the order `firstAt`, then the other).
CEngineObject* CreateNamedItem(const char* name, bool fromSecond) {
    alignas(4) std::uint8_t lists[2][0x1C];
    void* first = fromSecond ? lists[0] : lists[1];
    void* second = fromSecond ? lists[1] : lists[0];
    TempListConstruct(first, nullptr, 10);
    TempListConstruct(second, nullptr, 10);
    CEngineObject* item = CreateItemByName(ItemManager(), nullptr, name);
    TempListDestruct(second, nullptr);
    TempListDestruct(first, nullptr);
    return item;
}

// The rewards of a briefing record for `player`.
void GiveBriefingRewards(CWorldServer* world, WorldPlayer* player, std::uint8_t* record) {
    float experience = *reinterpret_cast<float*>(record + 0x0C);
    if (experience < 0.0f || experience > 0.0f) {
        std::uint32_t bits;
        std::memcpy(&bits, &experience, 4);
        AwardExperience(world, nullptr, player, bits);
    }
    *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(player) + 0xC0) +=
        GameFtol(*reinterpret_cast<float*>(record + 0x10));

    mfc::CTypedPtrList<CEngineObject> items;
    items.vtable = reinterpret_cast<void*>(kPtrListVtable);
    items.head = nullptr;
    items.tail = nullptr;
    items.count = 0;
    items.free = nullptr;
    items.blocks = nullptr;
    items.blockSize = 10;
    auto& give = *reinterpret_cast<NameVector*>(record + 0x14);
    for (const char** name = give.begin; name != give.end; ++name) {
        CEngineObject* item = CreateNamedItem(*name, true);
        if (!item) continue;
        auto* node = PtrListNewNode(&items, nullptr, items.tail, nullptr);
        node->data = item;
        if (items.tail) items.tail->next = node;
        else items.head = node;
        items.tail = node;
    }
    if (items.count) PlayerGiveItems(player, nullptr, &items, 0);

    auto& take = *reinterpret_cast<NameVector*>(record + 0x20);
    for (const char** name = take.begin; name != take.end; ++name) {
        CEngineObject* item = CreateNamedItem(*name, false);
        if (!item) continue;
        ++item->refCount_;
        if (item->GetClassId() == kClassQuestItem)
            PlayerEraseQuestItem(player, nullptr, *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(item) + 0x4C));
        item->Release();
    }

    void* state = *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(player) + 0x20);
    for (const char** name = reinterpret_cast<NameVector*>(record + 0x2C)->begin;
         name != reinterpret_cast<NameVector*>(record + 0x2C)->end; ++name)
        StateSetVariableMax(state, nullptr, *name, 1.0f);
    for (const char** name = reinterpret_cast<NameVector*>(record + 0x38)->begin;
         name != reinterpret_cast<NameVector*>(record + 0x38)->end; ++name)
        StateSetVariable(state, nullptr, *name, 2.0f);
    for (const char** name = reinterpret_cast<NameVector*>(record + 0x44)->begin;
         name != reinterpret_cast<NameVector*>(record + 0x44)->end; ++name)
        StateSetVariableMax(state, nullptr, *name, 1.0f);
    for (const char** name = reinterpret_cast<NameVector*>(record + 0x50)->begin;
         name != reinterpret_cast<NameVector*>(record + 0x50)->end; ++name)
        StateSetVariable(state, nullptr, *name, 2.0f);

    // The list's inlined destructor (its nodes are dropped, the items stay with the player).
    items.vtable = reinterpret_cast<void*>(kPtrListVtable);
    items.RemoveAll();
}

} // namespace

void CWorldServer::CompleteBriefing(int playerId, const char* name) {
    if (IsMultiplayerClient()) {
        console::CStringArray parts;
        Split(name, &parts, '.');
        if (parts.GetSize() == 3) RequestBriefing(reinterpret_cast<void*>(0x007AFE20), nullptr, parts[2]);
        return;
    }
    console::CStringArray parts;
    Split(name, &parts, '.');
    std::uint8_t* record = nullptr;
    if (parts.GetSize() == 3) {
        RawString key;
        char allocator;
        RawStringInit(&key, nullptr, parts[2], &allocator);
        record = *BriefingMapAt(reinterpret_cast<void*>(0x007C87D4), nullptr, &key);
        RawStringDestroy(&key, nullptr);
    }
    WorldPlayer* player = nullptr;
    {
        PlayerIterator it;
        for (WorldPlayer* p = it.First(ei::VCall<CEngineObject*>(this, 20)); p; p = it.Next()) {
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
    script.Format(reinterpret_cast<const char*>(kBriefingScript), playerId, name, playerId, name);
    CommandQueueAdd(commandsA_, nullptr, &script, 1);
    if (unknown12D64_ == kKindNoRangeTest)
        for (int i = 0; i < 5; ++i) CommandQueueRun(commandsA_, nullptr);
}

// ---- AwardExperience --------------------------------------------------------------------------

// The shares keep the original's operation order: no reassociation here.
#pragma float_control(precise, on, push)

namespace {

constexpr std::uintptr_t kShareListVtable = 0x0073FAB0;   // CList<{controller, share}>
constexpr std::uintptr_t kIdArrayVtable = 0x0073C3D0;     // CArray<int>
constexpr int kTypeHero = 0x32;
constexpr std::uint32_t kMinimumWeight = 0x3C23D70A;      // 0.01f

// A node of the list of shares (0x10 bytes): the unit's controller and its experience.
struct ShareNode {
    ShareNode* next;
    ShareNode* prev;
    std::uint8_t* controller;
    float share;
};
struct ShareList {
    void* vtable;
    ShareNode* head;
    ShareNode* tail;
    int count;
    void* free;
    void* blocks;
    int blockSize;
};

struct IdArray {
    void* vtable;
    int* data;
    int size;
    int capacity;
    int growBy;
};

__declspec(naked) ShareNode* __fastcall ShareListNewNode(ShareList*, void*, ShareNode* /*prev*/, ShareNode* /*next*/)
    EI_ORIGINAL(0x0059D5F0)
__declspec(naked) void __fastcall ShareListRemoveAll(ShareList*, void*) EI_ORIGINAL(0x0059D5C0)
__declspec(naked) void __fastcall IdArraySetSize(IdArray*, void*, int, int) EI_ORIGINAL(0x0059C660)
__declspec(naked) CEngineObject* __fastcall WorldFindUnit(CWorldServer*, void*, int) EI_ORIGINAL(0x0055FB00)
// 00553380 (RET 4). The unit controller gains experience.
__declspec(naked) void __fastcall ControllerGainExperience(void*, void*, float) EI_ORIGINAL(0x00553380)
// 00523960 (RET 4). A party record's statistics (+0x20) gain experience.
__declspec(naked) void __fastcall RecordGainExperience(void*, void*, float) EI_ORIGINAL(0x00523960)
// 0066F2C0 (RET 8) / 0066F280 (RET 4). The player state tells its client about experience.
__declspec(naked) void __fastcall StateUnitExperience(void*, void*, int /*unit id*/, float) EI_ORIGINAL(0x0066F2C0)
__declspec(naked) void __fastcall StatePartyExperience(void*, void*, float) EI_ORIGINAL(0x0066F280)

constexpr int kSlotIsDead = 55;

// The weight of a unit in the shared experience: sqrt(+0x8E0 + +0x1E4) of its controller.
double Weight(std::uint8_t* controller) {
    return x87::fsqrt(static_cast<double>(*reinterpret_cast<float*>(controller + 0x8E0)) +
                      *reinterpret_cast<float*>(controller + 0x1E4));
}

// Without a player: the single heroes of every player share `amount` in proportion to their
// weights.
void ShareAmongSoloPlayers(CWorldServer* world, float amount) {
    if (*reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(ei::VCall<CEngineObject*>(world, 20)) + 0x2C) == 0)
        return;
    ShareList list{reinterpret_cast<void*>(kShareListVtable), nullptr, nullptr, 0, nullptr, nullptr, 10};
    auto soloUnit = [&](WorldPlayer* p) -> CEngineObject* {
        if (!*reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(p) + 0x20) || p->mercCount != 1) return nullptr;
        return WorldFindUnit(world, nullptr, *reinterpret_cast<int*>(p->mercs + 0x1C));
    };

    float total = 0.0f;
    PlayerIterator it;
    for (WorldPlayer* p = it.First(ei::VCall<CEngineObject*>(world, 20)); p; p = it.Next()) {
        CEngineObject* unit = soloUnit(p);
        if (!unit) continue;
        auto* controller = *reinterpret_cast<std::uint8_t**>(reinterpret_cast<std::uint8_t*>(unit) + 0x240);
        if (!controller) continue;
        ShareNode* node = ShareListNewNode(&list, nullptr, list.tail, nullptr);
        node->controller = controller;
        std::uint32_t zero = 0;
        std::memcpy(&node->share, &zero, 4);
        if (list.tail) list.tail->next = node;
        else list.head = node;
        list.tail = node;
        total = static_cast<float>(Weight(node->controller) + total);
        volatile float stored = total;   // FSTP into the float total
        total = stored;
    }
    float minimum;
    std::memcpy(&minimum, &kMinimumWeight, 4);
    float divisor = !(total >= minimum) ? minimum : total;   // the original's branch-free select

    for (ShareNode* node = list.head; node;) {
        ShareNode* next = node->next;
        float share = static_cast<float>(Weight(node->controller) * amount / divisor);
        volatile float stored = share;
        node->share = stored;
        ControllerGainExperience(node->controller, nullptr, stored);
        node = next;
    }
    ShareNode* cursor = list.head;
    for (WorldPlayer* p = it.First(ei::VCall<CEngineObject*>(world, 20)); p; p = it.Next()) {
        CEngineObject* unit = soloUnit(p);
        if (!unit || !*reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(unit) + 0x240)) continue;
        ShareNode* node = cursor;
        cursor = node->next;
        RecordGainExperience(p->mercs + 0x20, nullptr, node->share);
    }
    for (WorldPlayer* p = it.First(ei::VCall<CEngineObject*>(world, 20)); p; p = it.Next()) {
        void* state = *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(p) + 0x20);
        if (!state) continue;
        for (ShareNode* node = list.head; node; node = node->next) {
            auto* unit = *reinterpret_cast<CEngineObject**>(node->controller + 0x1C);
            StateUnitExperience(state, nullptr, unit->id_, node->share);
        }
    }
    if (it.list) {
        --it.list->refCount_;
        it.list = nullptr;
    }
    list.vtable = reinterpret_cast<void*>(kShareListVtable);
    ShareListRemoveAll(&list, nullptr);
}

// With a player: its living heroes share `amount` equally.
void ShareAmongHeroes(CWorldServer* world, WorldPlayer* player, float amount) {
    IdArray heroes{reinterpret_cast<void*>(kIdArrayVtable), nullptr, 0, 0, 0};
    auto& units = *reinterpret_cast<stlp::hash_map<unsigned, CEngineObject*, stlp::IntHash>*>(
        reinterpret_cast<std::uint8_t*>(player) + 0x40);
    for (auto* node = units.First(); node && node->value; node = units.After(node)) {
        CEngineObject* unit = node->value;
        if (ei::VCall<int>(unit, kSlotIsDead) != 0 || unit->type_ != kTypeHero) continue;
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
            ControllerGainExperience(*reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(unit) + 0x240), nullptr,
                                     share);
            for (int j = 0; j < player->mercCount; ++j) {
                std::uint8_t* record = player->mercs + j * 0x784;
                if (*reinterpret_cast<int*>(record + 0x1C) == heroes.data[i])
                    RecordGainExperience(record + 0x20, nullptr, share);
            }
        }
        StatePartyExperience(*reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(player) + 0x20), nullptr, share);
    }
    heroes.vtable = reinterpret_cast<void*>(kIdArrayVtable);
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

constexpr std::uintptr_t kRecordArrayVtable = 0x0073FAA4;   // CArray<record*>
constexpr std::uintptr_t kItemRecordVtable = 0x0073FA88;
constexpr int kQuickSlots = 4;
constexpr int kWornSlots = 7;
constexpr int kBeltSlots = 4;
constexpr int kSpellSlots = 8;
constexpr int kSlotRecordFromItem = 24;   // the party records (WorldPlayer + 0x70): slot 24 (+0x60)

struct RecordArray {
    void* vtable;
    std::uint8_t** data;
    int size;
    int capacity;
    int growBy;
};

__declspec(naked) void __fastcall PartyAddItem(void* /*records*/, void*, void* /*item*/, int, int) EI_ORIGINAL(0x00661DB0)
__declspec(naked) void __fastcall RecordArraySetAtGrow(RecordArray*, void*, int, std::uint8_t*) EI_ORIGINAL(0x0059D2E0)
// The unit record's lists: remove the first entry of quick items (00660E10), items (00660A50),
// spells (00660FF0), worn items (00660C30); add an entry (006608D0, 00660C90, 00660AB0,
// 00660E70; they return the record entry).
__declspec(naked) void __fastcall RecordRemoveQuick(std::uint8_t*, void*, void*) EI_ORIGINAL(0x00660E10)
__declspec(naked) void __fastcall RecordRemoveItem(std::uint8_t*, void*, void*) EI_ORIGINAL(0x00660A50)
__declspec(naked) void __fastcall RecordRemoveSpell(std::uint8_t*, void*, void*) EI_ORIGINAL(0x00660FF0)
__declspec(naked) void __fastcall RecordRemoveWorn(std::uint8_t*, void*, void*) EI_ORIGINAL(0x00660C30)
__declspec(naked) std::uint8_t* __fastcall RecordAddQuick(std::uint8_t*, void*, void*) EI_ORIGINAL(0x006608D0)
__declspec(naked) std::uint8_t* __fastcall RecordAddWorn(std::uint8_t*, void*, void*) EI_ORIGINAL(0x00660C90)
__declspec(naked) std::uint8_t* __fastcall RecordAddBelt(std::uint8_t*, void*, void*) EI_ORIGINAL(0x00660AB0)
__declspec(naked) std::uint8_t* __fastcall RecordAddSpell(std::uint8_t*, void*, void*) EI_ORIGINAL(0x00660E70)
// The unit's slots: quick (005318B0), worn (00530830), belt (00531920), spells (005319A0).
__declspec(naked) CEngineObject* __fastcall UnitQuickItem(CEngineObject*, void*, int) EI_ORIGINAL(0x005318B0)
__declspec(naked) CEngineObject* __fastcall UnitWornItem(CEngineObject*, void*, int) EI_ORIGINAL(0x00530830)
__declspec(naked) CEngineObject* __fastcall UnitBeltItem(CEngineObject*, void*, int) EI_ORIGINAL(0x00531920)
__declspec(naked) CEngineObject* __fastcall UnitSpell(CEngineObject*, void*, int) EI_ORIGINAL(0x005319A0)
// 00553360 (RET 4). The controller copies its statistics into a record's (+0x20).
__declspec(naked) void __fastcall ControllerStoreStats(void*, void*, void*) EI_ORIGINAL(0x00553360)
__declspec(naked) void* __fastcall ItemRecordConstruct(void*, void*) EI_ORIGINAL(0x0058FBB0)
__declspec(naked) void* __fastcall RefValueConstruct(void*, void*) EI_ORIGINAL(0x00419720)
__declspec(naked) void __fastcall RefPtrAssign(void*, void*, void*) EI_ORIGINAL(0x00419CD0)
__declspec(naked) void __fastcall RefRelease(void*, void*) EI_ORIGINAL(0x0040A3A0)

// The record entry for `item` from the party records, with a reference held across `add`.
template <typename Add>
std::uint8_t* AddToRecord(void* records, std::uint8_t* record, CEngineObject* item, Add add) {
    auto* entry = ei::VCall<CEngineObject*>(records, kSlotRecordFromItem, item);
    if (entry) ++entry->refCount_;
    std::uint8_t* added = add(record, nullptr, entry);
    if (entry) entry->Release();
    return added;
}

// A fresh 12-byte item record (+0x08 = the record entry's +0x0C) for the item's +0x1C.
void* NewItemRecord(bool refValueBase) {
    void* memory = gamecrt::operator_new(0xC);
    *reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(memory) + 4) = 0;   // before the null test
    if (!memory) return nullptr;
    if (!refValueBase) return ItemRecordConstruct(memory, nullptr);
    RefValueConstruct(memory, nullptr);
    *static_cast<void**>(memory) = reinterpret_cast<void*>(kItemRecordVtable);
    return memory;
}

void SyncRecord(std::uint8_t* records, std::uint8_t* record, CEngineObject* unit) {
    void* controller = *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(unit) + 0x240);
    auto count = [&](int offset) { return *reinterpret_cast<int*>(record + offset); };
    auto first = [&](int offset) { return **reinterpret_cast<void***>(record + offset); };
    while (count(0x754)) RecordRemoveQuick(record, nullptr, first(0x750));
    while (count(0x734)) RecordRemoveItem(record, nullptr, first(0x730));
    while (count(0x764)) RecordRemoveSpell(record, nullptr, first(0x760));
    while (count(0x744)) RecordRemoveWorn(record, nullptr, first(0x740));

    for (int slot = 0; slot < kQuickSlots; ++slot) {
        CEngineObject* item = UnitQuickItem(unit, nullptr, slot);
        if (!item) continue;
        std::uint8_t* entry = AddToRecord(records, record, item, RecordAddQuick);
        void* ref = NewItemRecord(false);
        *reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(ref) + 8) = *reinterpret_cast<std::uint32_t*>(entry + 0xC);
        RefPtrAssign(reinterpret_cast<std::uint8_t*>(item) + 0x1C, nullptr, ref);
    }
    for (int slot = 0; slot < kWornSlots; ++slot) {
        CEngineObject* item = UnitWornItem(unit, nullptr, slot);
        if (!item) continue;
        std::uint8_t* entry = AddToRecord(records, record, item, RecordAddWorn);
        void* ref = NewItemRecord(true);
        *reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(ref) + 8) = *reinterpret_cast<std::uint32_t*>(entry + 0xC);
        RefPtrAssign(reinterpret_cast<std::uint8_t*>(item) + 0x1C, nullptr, ref);
    }
    for (int slot = 0; slot < kBeltSlots; ++slot) {
        CEngineObject* item = UnitBeltItem(unit, nullptr, slot);
        if (!item || *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(item) + 0x48) <= 0) continue;
        auto* entry = ei::VCall<CEngineObject*>(records, kSlotRecordFromItem, item);
        *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(entry) + 0x48) =
            *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(item) + 0x48);   // before the null test
        if (entry) ++entry->refCount_;
        std::uint8_t* added = RecordAddBelt(record, nullptr, entry);
        if (entry) entry->Release();
        void* ref = NewItemRecord(true);
        *reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(ref) + 8) = *reinterpret_cast<std::uint32_t*>(added + 0xC);
        // The counted pointer's assignment, inlined (16-bit count at +4).
        void*& slotRef = *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(item) + 0x1C);
        void* old = slotRef;
        slotRef = ref;
        if (ref) ++*reinterpret_cast<std::int16_t*>(static_cast<std::uint8_t*>(ref) + 4);
        if (old) RefRelease(old, nullptr);
    }
    for (int slot = 0; slot < kSpellSlots; ++slot) {
        CEngineObject* spell = UnitSpell(unit, nullptr, slot);
        if (!spell) continue;
        AddToRecord(records, record, spell, RecordAddSpell);
    }
    ControllerStoreStats(controller, nullptr, record + 0x20);
}

} // namespace

void CWorldServer::SyncPartyRecords(WorldPlayer* player) {
    std::uint8_t* records = reinterpret_cast<std::uint8_t*>(player) + 0x70;
    for (auto* node = *reinterpret_cast<mfc::CTypedPtrList<void>::Node**>(reinterpret_cast<std::uint8_t*>(player) + 0xE4);
         node;) {
        auto* next = node->next;
        PartyAddItem(records, nullptr, node->data, 1, 1);
        node = next;
    }
    RecordArray list{reinterpret_cast<void*>(kRecordArrayVtable), nullptr, 0, 0, 0};
    for (int i = 0; i < player->mercCount; ++i) RecordArraySetAtGrow(&list, nullptr, list.size, player->mercs + i * 0x784);
    for (int i = 0; i < list.size; ++i) {
        std::uint8_t* record = list.data[i];
        CEngineObject* unit = WorldFindUnit(this, nullptr, *reinterpret_cast<int*>(record + 0x1C));
        if (!unit || ei::VCall<int>(unit, kSlotIsDead) != 0) continue;
        SyncRecord(records, record, unit);
    }
    list.vtable = reinterpret_cast<void*>(kRecordArrayVtable);
    if (list.data) gamecrt::operator_delete(list.data);
}

// ---- StoreParty / CollectParty ----------------------------------------------------------------

namespace {

constexpr int kKindNetwork = 4;   // kKindNoRangeTest (3): above
constexpr std::size_t kUnitRecordSize = 0x784;

// The placement array a zone change fills (CArray<Placement>, SetAtGrow 0059C510).
struct PlacementRecord {
    int id;
    std::uint32_t position[3];
    std::uint32_t rotation[4];
    std::uint32_t value268;
    std::uint32_t value26C;
};
static_assert(sizeof(PlacementRecord) == 0x28);

__declspec(naked) void __fastcall PlacementsSetAtGrow(void*, void*, int, const PlacementRecord*) EI_ORIGINAL(0x0059C510)
__declspec(naked) void* __fastcall BrainFindFollowMode(void*, void*) EI_ORIGINAL(0x005D5E90)
__declspec(naked) void __fastcall RecordStatsFinish(void*, void*) EI_ORIGINAL(0x00523C80)
__declspec(naked) void __fastcall UnitSetController(CEngineObject*, void*, void*) EI_ORIGINAL(0x005300D0)
__declspec(naked) void __fastcall ServerRemoveObject(CWorldServer*, void*, int) EI_ORIGINAL(0x005935C0)
__declspec(naked) void __fastcall StorePartyAt(CWorldServer*, void*, RecordArray*, void*, void*) EI_ORIGINAL(0x0058FD90)
__declspec(naked) void __fastcall UpdatePlayerTimesAt(CWorldServer*, void*, float) EI_ORIGINAL(0x0058D280)
// 00590160 (RET 8). The player's items into its party records before leaving.
__declspec(naked) void __fastcall StorePlayerItems(CWorldServer*, void*, WorldPlayer*, void* /*records*/) EI_ORIGINAL(0x00590160)
// 0066F0C0. The player state before a zone change.
__declspec(naked) void __fastcall StateBeforeZoneChange(void*, void*) EI_ORIGINAL(0x0066F0C0)
// 0065F740. The unit record's destructor body.
__declspec(naked) void __fastcall UnitRecordDestruct(void*, void*) EI_ORIGINAL(0x0065F740)
__declspec(naked) void __fastcall RecordArraySetSize(RecordArray*, void*, int, int) EI_ORIGINAL(0x005719B0)
__declspec(naked) void* __cdecl GameMemmove(void*, const void*, std::size_t) EI_ORIGINAL(0x006E79A0)

// One record rebuilt from its living unit (as SyncPartyRecords does, without the items'
// records), plus the unit's +0x268 / +0x26C (single player) and +0x438.
void StoreRecord(CWorldServer* world, void* records, std::uint8_t* record, CEngineObject* unit) {
    void* controller = *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(unit) + 0x240);
    auto count = [&](int offset) { return *reinterpret_cast<int*>(record + offset); };
    auto first = [&](int offset) { return **reinterpret_cast<void***>(record + offset); };
    while (count(0x754)) RecordRemoveQuick(record, nullptr, first(0x750));
    while (count(0x734)) RecordRemoveItem(record, nullptr, first(0x730));
    while (count(0x764)) RecordRemoveSpell(record, nullptr, first(0x760));
    while (count(0x744)) RecordRemoveWorn(record, nullptr, first(0x740));
    for (int slot = 0; slot < kQuickSlots; ++slot)
        if (CEngineObject* item = UnitQuickItem(unit, nullptr, slot)) AddToRecord(records, record, item, RecordAddQuick);
    for (int slot = 0; slot < kWornSlots; ++slot)
        if (CEngineObject* item = UnitWornItem(unit, nullptr, slot)) AddToRecord(records, record, item, RecordAddWorn);
    for (int slot = 0; slot < kBeltSlots; ++slot) {
        CEngineObject* item = UnitBeltItem(unit, nullptr, slot);
        if (!item || *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(item) + 0x48) <= 0) continue;
        auto* entry = ei::VCall<std::uint8_t*>(records, kSlotRecordFromItem, item);
        int amount = *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(item) + 0x48);
        std::uint32_t value2C = *reinterpret_cast<std::uint32_t*>(entry + 0x2C);   // before the null test
        *reinterpret_cast<int*>(entry + 0x48) = amount;
        *reinterpret_cast<std::uint32_t*>(entry + 0x28) = value2C;
        if (entry) ++reinterpret_cast<CEngineObject*>(entry)->refCount_;
        RecordAddBelt(record, nullptr, entry);
        if (entry) reinterpret_cast<CEngineObject*>(entry)->Release();
    }
    for (int slot = 0; slot < kSpellSlots; ++slot)
        if (CEngineObject* spell = UnitSpell(unit, nullptr, slot)) AddToRecord(records, record, spell, RecordAddSpell);
    ControllerStoreStats(controller, nullptr, record + 0x20);
    if (world->unknown12D64_ == 1) {
        *reinterpret_cast<std::uint32_t*>(record + 0x770) = *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(unit) + 0x268);
        *reinterpret_cast<std::uint32_t*>(record + 0x774) = *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(unit) + 0x26C);
    }
    *reinterpret_cast<std::uint32_t*>(record + 0x780) = *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(unit) + 0x438);
    RecordStatsFinish(record + 0x20, nullptr);
}

} // namespace

void CWorldServer::StoreParty(void* recordsArg, void* partyRecords, void* placements) {
    auto* list = static_cast<RecordArray*>(recordsArg);
    if (unknown12D64_ == 1) {
        // Each record remembers whom its unit follows (-1: nobody).
        for (int i = 0; i < list->size; ++i) {
            std::uint8_t* record = list->data[i];
            CEngineObject* unit = WorldFindUnit(this, nullptr, *reinterpret_cast<int*>(record + 0x1C));
            void* brain = *reinterpret_cast<std::uint8_t**>(reinterpret_cast<std::uint8_t*>(unit) + 0x240) + 0x980;
            auto* follow = static_cast<std::uint8_t*>(BrainFindFollowMode(brain, nullptr));
            *reinterpret_cast<int*>(record + 0x77C) = follow ? *reinterpret_cast<int*>(follow + 0xC) : -1;
        }
    }
    for (int i = 0; i < list->size; ++i) {
        std::uint8_t* record = list->data[i];
        CEngineObject* unit = WorldFindUnit(this, nullptr, *reinterpret_cast<int*>(record + 0x1C));
        if (!unit) continue;
        if (placements) {
            auto* raw = reinterpret_cast<std::uint8_t*>(unit);
            PlacementRecord entry;
            entry.id = unit->id_;
            std::memcpy(entry.position, raw + 0x1C, 12);
            std::memcpy(entry.rotation, raw + 0x34, 16);
            entry.value268 = *reinterpret_cast<std::uint32_t*>(raw + 0x268);
            entry.value26C = *reinterpret_cast<std::uint32_t*>(raw + 0x26C);
            PlacementsSetAtGrow(placements, nullptr, *reinterpret_cast<int*>(static_cast<std::uint8_t*>(placements) + 8),
                                &entry);
        }
        if (!ei::VCall<int>(unit, kSlotIsDead) || IsMultiplayerClient()) StoreRecord(this, partyRecords, record, unit);
        if (!ei::VCall<int>(unit, kSlotIsDead)) ServerRemoveObject(this, nullptr, unit->id_);
        else UnitSetController(unit, nullptr, nullptr);
    }
}

void CWorldServer::CollectParty(WorldPlayer* player, void* placements) {
    if (!player) return;
    if (unknown12D64_ == 1 || unknown12D64_ == kKindNoRangeTest) UpdatePlayerTimesAt(this, nullptr, 0.0f);
    if (unknown12D64_ != kKindNetwork) {
        StorePlayerItems(this, nullptr, player, reinterpret_cast<std::uint8_t*>(player) + 0x70);
        StateBeforeZoneChange(*reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(player) + 0x20), nullptr);
    }
    if (!(unknown12D64_ == 1 || unknown12D64_ == kKindNoRangeTest) || unknown15C_ == 0) return;

    RecordArray list{reinterpret_cast<void*>(kRecordArrayVtable), nullptr, 0, 0, 0};
    // Records whose unit is gone (or dead, outside a network client game) are dropped (the
    // inlined CArray::RemoveAt).
    for (int i = 0; i < player->mercCount; ++i) {
        CEngineObject* unit = WorldFindUnit(this, nullptr, *reinterpret_cast<int*>(player->mercs + i * kUnitRecordSize + 0x1C));
        if (unit && (!ei::VCall<int>(unit, kSlotIsDead) || IsMultiplayerClient())) continue;
        int following = player->mercCount - i - 1;
        UnitRecordDestruct(player->mercs + i * kUnitRecordSize, nullptr);
        if (following)
            GameMemmove(player->mercs + i * kUnitRecordSize, player->mercs + (i + 1) * kUnitRecordSize,
                        following * kUnitRecordSize);
        --player->mercCount;
        --i;
    }
    for (int i = 0; i < player->mercCount; ++i) {
        int index = list.size;
        RecordArraySetSize(&list, nullptr, index + 1, -1);
        list.data[index] = player->mercs + i * kUnitRecordSize;
    }
    StorePartyAt(this, nullptr, &list, reinterpret_cast<std::uint8_t*>(player) + 0x70, placements);
    list.vtable = reinterpret_cast<void*>(kRecordArrayVtable);
    if (list.data) gamecrt::operator_delete(list.data);
}

EI_REPLACE_METHOD(0x00591800, CWorldServer::UpdateMercBriefings);
EI_REPLACE_METHOD(0x0058FD90, CWorldServer::StoreParty);
EI_REPLACE_METHOD(0x00590A40, CWorldServer::CollectParty);
EI_REPLACE_METHOD(0x00590C70, CWorldServer::SyncPartyRecords);
EI_REPLACE_METHOD(0x005911C0, CWorldServer::AwardExperience);
EI_REPLACE_METHOD(0x00592090, CWorldServer::CompleteBriefing);
