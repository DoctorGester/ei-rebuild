// Checks for the global constructors ported under S3b that only the static initialisers call
// (src/game/database_construct.cpp, logic_settings.cpp, party_file.cpp, world_construct.cpp). The
// originals run before the hooks are installed, so no wrapper ever sees them; what they built is
// still untouched at the entry of WinMain (the databases load later). Each check below runs once at
// that point (EI_STARTUP_CHECK), builds the same object with our constructor into storage of its
// own, compares it with the original's global and destroys ours. Compares against the original;
// every game start reaches them, no scenario is needed. The spell and item constructors also run
// when a network game switches the resource databases; wrappers check those (ours only).
//
// What is compared: STLport vectors empty; hash_maps with the same bucket count (193), every
// bucket null and no elements; plain fields bit for bit. Heap blocks differ by address only: a
// pointer field matches when both sides point to committed private memory (ShapeDiff). Not
// compared: the hash_maps' three functor bytes, which the original copies from its uninitialised
// stack.
#include "verify.h"

#include "../../src/game/acks_db.h"
#include "../../src/game/ai_map.h"
#include "../../src/game/item_db.h"
#include "../../src/game/logic_settings.h"
#include "../../src/game/model.h"
#include "../../src/game/quest_records.h"
#include "../../src/game/unit_db.h"
#include "../../src/game/world.h"
#include "../../src/hook/log.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <new>

namespace {

// ---- Helpers ---------------------------------------------------------------------------------------

// Storage for one of our objects, zeroed like the originals' globals in .bss.
template <typename T>
T* ZeroedStorage() {
    alignas(8) static std::uint8_t storage[sizeof(T)];
    std::memset(storage, 0, sizeof(storage));
    return reinterpret_cast<T*>(storage);
}

constexpr std::size_t kBuckets = 193;   // STLport's prime for 100

template <typename T>
bool EmptyVector(stlp::vector<T>& v) {
    return !v.Start() && !v.Finish() && !v.EndOfStorage();
}

// A hash_map as its constructor leaves it: kBuckets null buckets, storage exactly that size, no elements.
template <typename K, typename V, typename H>
bool EmptyMap(stlp::hash_map<K, V, H>& map) {
    auto* buckets = static_cast<stlp::vector<void*>*>(map.BucketVector());
    if (map.Size() != 0 || map.BucketCount() != kBuckets || buckets->Finish() != buckets->EndOfStorage()) return false;
    for (std::size_t b = 0; b < kBuckets; ++b)
        if (map.Bucket(b)) return false;
    return true;
}

bool EmptyTable(RecordTable& records, char* capacityEnd) { return !records.begin && !records.end && !capacityEnd; }

// The number of dwords of `ours` that differ from `theirs` (the first one's offset in *first), byte
// ranges in `skip` aside. A pointer to the object itself must have the same offset; other pointers
// match when both lead to committed private memory (heap blocks).
struct Skip {
    std::size_t offset, size;
};

constexpr int kLoggedDiffs = 8;

bool HeapBlock(std::uint32_t value) {
    if (value < 0x10000) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<void*>(static_cast<std::uintptr_t>(value)), &info, sizeof(info))) return false;
    return info.State == MEM_COMMIT && info.Type == MEM_PRIVATE;
}

int ShapeDiff(const void* theirsObject, const void* oursObject, std::size_t size, std::initializer_list<Skip> skip,
              std::size_t* first) {
    const auto* theirs = static_cast<const std::uint8_t*>(theirsObject);
    const auto* ours = static_cast<const std::uint8_t*>(oursObject);
    const auto inside = [size](std::uint32_t value, const std::uint8_t* base) {
        const auto b = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(base));
        return value >= b && value < b + size;
    };
    int wrong = 0;
    for (std::size_t o = 0; o + 4 <= size; o += 4) {
        std::uint8_t a[4], b[4];
        std::memcpy(a, theirs + o, 4);
        std::memcpy(b, ours + o, 4);
        for (const Skip& s : skip)
            for (std::size_t i = 0; i < 4; ++i)
                if (o + i >= s.offset && o + i < s.offset + s.size) a[i] = b[i] = 0;
        std::uint32_t x, y;
        std::memcpy(&x, a, 4);
        std::memcpy(&y, b, 4);
        bool same = x == y;
        if (!same && inside(x, theirs) && inside(y, ours))
            same = x - static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(theirs)) ==
                   y - static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(ours));
        else if (!same)
            same = !inside(x, theirs) && !inside(y, ours) && HeapBlock(x) && HeapBlock(y);
        if (same) continue;
        if (!wrong) *first = o;
        if (++wrong <= kLoggedDiffs) ei::Log("global constructor check: +0x%X %08X / ours %08X", static_cast<unsigned>(o), x, y);
    }
    return wrong;
}

// ---- The databases ---------------------------------------------------------------------------------

void SkillDatabaseStartupCheck() {
    SkillDatabase* theirs = SkillDb();
    auto* ours = ZeroedStorage<SkillDatabase>();
    const bool same = ours->Construct() == ours && EmptyVector(ours->skills) && EmptyVector(ours->levels) &&
                      EmptyVector(theirs->skills) && EmptyVector(theirs->levels);
    ei::verify::Check("static constructor: skill database 0051FB10", true, same, "not two empty vectors");
}
EI_STARTUP_CHECK(SkillDatabaseStartupCheck);

bool EmptyCodeTable(itemdb::CodeIndexedTable& t) { return EmptyTable(t.records, t.capacityEnd) && EmptyMap(t.byCode); }

void SpellDatabaseStartupCheck() {
    auto* theirs = reinterpret_cast<itemdb::SpellDatabase*>(std::uintptr_t(0x007C8510));
    auto* ours = ZeroedStorage<itemdb::SpellDatabase>();
    const bool result = ours->Construct() == ours;
    bool same = result;
    for (itemdb::SpellDatabase* db : {theirs, ours}) {
        same = same && EmptyCodeTable(db->spells) && EmptyCodeTable(db->modifiers);
        for (auto& list : db->lists) same = same && EmptyVector(list);
    }
    ei::verify::Check("static constructor: spell database 0057E2F0", true, same, "result %d; ours %zu / %zu buckets, theirs %zu / %zu",
                      result, ours->spells.byCode.BucketCount(), ours->modifiers.byCode.BucketCount(),
                      theirs->spells.byCode.BucketCount(), theirs->modifiers.byCode.BucketCount());
    ours->spells.byCode.Destroy();
    ours->modifiers.byCode.Destroy();
}
EI_STARTUP_CHECK(SpellDatabaseStartupCheck);

// The resource switch's resets (0057CB50, network games) construct the spell and item databases
// again after destroying them: ours only, the tables left empty. No scenario reaches them yet
// (ui_mp_host does not switch).
void* s_unusedOriginal;

itemdb::SpellDatabase* __fastcall SpellDatabaseConstructVerify(itemdb::SpellDatabase* self) {
    itemdb::SpellDatabase* result = self->Construct();
    bool empty = result == self && EmptyCodeTable(self->spells) && EmptyCodeTable(self->modifiers);
    for (auto& list : self->lists) empty = empty && EmptyVector(list);
    ei::verify::Check("SpellDatabase::Construct", true, empty, "not empty with 193 buckets");
    return result;
}
EI_VERIFY(0x0057E2F0, SpellDatabaseConstructVerify, 0, s_unusedOriginal);

itemdb::ItemDatabase* __fastcall ItemDatabaseConstructVerify(itemdb::ItemDatabase* self) {
    itemdb::ItemDatabase* result = self->Construct();
    bool empty = result == self;
    for (itemdb::IndexedRecordTable* t : {&self->materials, &self->weapons, &self->armors, &self->quickItems,
                                          &self->questItems, &self->loot})
        empty = empty && EmptyTable(t->records, t->capacityEnd) && EmptyMap(t->ByName());
    ei::verify::Check("ItemDatabase::Construct", true, empty, "not empty with 193 buckets");
    return result;
}
EI_VERIFY(0x0057E3D0, ItemDatabaseConstructVerify, 0, s_unusedOriginal);

void ItemDatabaseStartupCheck() {
    auto* theirs = reinterpret_cast<itemdb::ItemDatabase*>(std::uintptr_t(0x007C86D8));
    auto* ours = ZeroedStorage<itemdb::ItemDatabase>();
    bool same = ours->Construct() == ours;
    for (itemdb::ItemDatabase* db : {theirs, ours})
        for (itemdb::IndexedRecordTable* t : {&db->materials, &db->weapons, &db->armors, &db->quickItems, &db->questItems, &db->loot})
            same = same && EmptyTable(t->records, t->capacityEnd) && EmptyMap(t->ByName());
    ei::verify::Check("static constructor: item database 0057E3D0", true, same, "a table is not empty with 193 buckets");
    for (itemdb::IndexedRecordTable* t : {&ours->materials, &ours->weapons, &ours->armors, &ours->quickItems, &ours->questItems, &ours->loot})
        t->ByName().Destroy();
}
EI_STARTUP_CHECK(ItemDatabaseStartupCheck);

void QuestDatabaseStartupCheck() {
    auto* theirs = reinterpret_cast<QuestDatabase*>(std::uintptr_t(0x007C87A8));
    auto* ours = ZeroedStorage<QuestDatabase>();
    bool same = ours->Construct() == ours;
    for (QuestDatabase* db : {theirs, ours})
        same = same && EmptyVector(db->quests) && EmptyMap(db->questsByName) && EmptyVector(db->briefings.records) &&
               EmptyMap(db->briefings.index);
    ei::verify::Check("static constructor: quest database 006AE0F0", true, same, "not two empty tables with 193 buckets");
    ours->questsByName.Destroy();
    ours->briefings.index.Destroy();
}
EI_STARTUP_CHECK(QuestDatabaseStartupCheck);

void PrintsDatabaseStartupCheck() {
    auto* theirs = reinterpret_cast<PrintsDatabase*>(std::uintptr_t(0x007C89C8));
    auto* ours = ZeroedStorage<PrintsDatabase>();
    bool same = ours->Construct() == ours;
    for (PrintsDatabase* db : {theirs, ours})
        same = same && EmptyVector(db->blood) && EmptyMap(db->bloodByName) && EmptyVector(db->footprints) &&
               EmptyMap(db->footprintsByName) && EmptyVector(db->scorch) && EmptyMap(db->scorchByName);
    ei::verify::Check("static constructor: prints database 006D0250", true, same, "not three empty tables with 193 buckets");
    ours->bloodByName.Destroy();
    ours->footprintsByName.Destroy();
    ours->scorchByName.Destroy();
}
EI_STARTUP_CHECK(PrintsDatabaseStartupCheck);

void AckDatabaseStartupCheck() {
    acks::AckDatabase* theirs = &acks::Database();
    auto* ours = ZeroedStorage<acks::AckDatabase>();
    bool same = ours->Construct() == ours;
    for (acks::AckDatabase* db : {theirs, ours})
        same = same && EmptyVector(db->units.records) && EmptyMap(db->units.byName) && EmptyVector(db->models.records) &&
               EmptyMap(db->models.byName) && EmptyVector(db->extras.records) && EmptyMap(db->extras.byName);
    ei::verify::Check("static constructor: acks database 006D7220", true, same, "not three empty tables with 193 buckets");
    ours->units.byName.Destroy();
    ours->models.byName.Destroy();
    ours->extras.byName.Destroy();
}
EI_STARTUP_CHECK(AckDatabaseStartupCheck);

// ---- The logic settings and the multiplayer heroes --------------------------------------------------

void LogicSettingsStartupCheck() {
    const auto* theirs = reinterpret_cast<const std::uint8_t*>(&GameLogicSettings());
    auto* ours = ZeroedStorage<LogicSettings>();
    const bool result = ours->Construct() == ours;
    std::size_t first = 0;
    // Bytes 1..3 after the loaded flag are not written.
    const int wrong = ShapeDiff(theirs, ours, sizeof(LogicSettings), {{1, 3}}, &first);
    std::uint32_t a = 0, b = 0;
    std::memcpy(&a, theirs + first, 4);
    std::memcpy(&b, reinterpret_cast<const std::uint8_t*>(ours) + first, 4);
    ei::verify::Check("static constructor: logic settings 005CDFC0", true, result && wrong == 0 && ours->questsToRespawn == 3,
                      "result %d, %d fields differ, the first at +0x%X (%08X / ours %08X)", result, wrong,
                      static_cast<unsigned>(first), a, b);
}
EI_STARTUP_CHECK(LogicSettingsStartupCheck);

void SavedPartiesStartupCheck() {
    const auto* theirs = reinterpret_cast<const SavedParties*>(std::uintptr_t(0x007C82A0));
    auto* ours = ZeroedStorage<SavedParties>();
    const bool result = ours->Construct() == ours;
    std::size_t first = 0;
    // The party's variables (CParty +0x54, a hash_map) start with the functor bytes.
    const int wrong = ShapeDiff(theirs, ours, sizeof(SavedParties), {{offsetof(SavedParties, outsideGame_) + 0x54, 3}}, &first);
    ei::verify::Check("static constructor: multiplayer heroes 0065F580", true, result && wrong == 0,
                      "result %d, %d dwords differ, the first at +0x%X", result, wrong, static_cast<unsigned>(first));
    ours->Destruct();
}
EI_STARTUP_CHECK(SavedPartiesStartupCheck);

// ---- The worlds ------------------------------------------------------------------------------------
//
// Ours is built in zeroed storage of its own and destroyed with the original's destructor (0058AAD0
// / 00561B00: Teardown, then the members), as the game does at exit.

__declspec(naked) void __fastcall WorldServerDestructOriginal(CWorldServer* /*world*/) EI_ORIGINAL(0x0058AAD0)
__declspec(naked) void __fastcall WorldClientDestructOriginal(CWorldClient* /*world*/) EI_ORIGINAL(0x00561B00)

// The functor bytes of a hash_map member.
#define EI_FUNCTORS(Class, member) Skip{offsetof(Class, member), 3}

void WorldServerStartupCheck() {
    auto* theirs = reinterpret_cast<CWorldServer*>(std::uintptr_t(0x007AFF60));
    auto* ours = ZeroedStorage<CWorldServer>();
    const bool result = ours->Construct() == ours;
    bool maps = true;
    for (CWorldServer* world : {theirs, ours})
        for (ObjectIdMap* map : {&world->updateMap_, &world->objects_, &world->sentObjects_, &world->mapObjects_,
                                 &world->otherMapObjects_, &world->namedObjects_, &world->timedObjects_})
            maps = maps && EmptyMap(map->map) && map->maxId == 0;
    std::size_t first = 0;
    const int wrong = ShapeDiff(theirs, ours, sizeof(CWorldServer),
                                {EI_FUNCTORS(CWorldServer, updateMap_), EI_FUNCTORS(CWorldServer, objects_),
                                 EI_FUNCTORS(CWorldServer, sentObjects_), EI_FUNCTORS(CWorldServer, mapObjects_),
                                 EI_FUNCTORS(CWorldServer, otherMapObjects_), EI_FUNCTORS(CWorldServer, namedObjects_),
                                 EI_FUNCTORS(CWorldServer, timedObjects_),
                                 // the AI map's bridge heights (the hashtable constructor 005BCCD0)
                                 Skip{offsetof(CWorldServer, aiMap_) + offsetof(CAIMap, bridgeHeights), 3}},
                                &first);
    ei::verify::Check("static constructor: world server 00589ED0", true, result && maps && wrong == 0,
                      "result %d, id maps %d, %d dwords differ, the first at +0x%X", result, maps, wrong,
                      static_cast<unsigned>(first));
    WorldServerDestructOriginal(ours);
}
EI_STARTUP_CHECK(WorldServerStartupCheck);

void WorldClientStartupCheck() {
    auto* theirs = reinterpret_cast<CWorldClient*>(std::uintptr_t(0x007AF5B8));
    auto* ours = ZeroedStorage<CWorldClient>();
    const bool result = ours->Construct() == ours;
    bool parts = EmptyMap(theirs->dynamicNodes_) && EmptyMap(ours->dynamicNodes_) && EmptyMap(theirs->lineLists_) &&
                 EmptyMap(ours->lineLists_);
    for (CWorldClient* world : {theirs, ours})
        for (stlp::list<CEngineObject*>* list : {&world->heroes_, &world->objects_})
            parts = parts && list->Empty();
    for (auto name : {&CWorldClient::appName_, &CWorldClient::windowClass_, &CWorldClient::windowTitle_,
                      &CWorldClient::pausedTitle_})
        parts = parts && std::strcmp(theirs->*name, ours->*name) == 0;
    std::size_t first = 0;
    const int wrong = ShapeDiff(theirs, ours, sizeof(CWorldClient),
                                {EI_FUNCTORS(CWorldClient, dynamicNodes_), EI_FUNCTORS(CWorldClient, lineLists_)}, &first);
    ei::verify::Check("static constructor: world client 005612F0", true, result && parts && wrong == 0,
                      "result %d, lists, maps and names %d, %d dwords differ, the first at +0x%X", result, parts, wrong,
                      static_cast<unsigned>(first));
    WorldClientDestructOriginal(ours);
}
EI_STARTUP_CHECK(WorldClientStartupCheck);

} // namespace
