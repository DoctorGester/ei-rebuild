// Checks for unit_create.cpp.
//
// The prototype name lookup inserts unknown names: when the name is already indexed both versions
// run and must return the same record (no other change), otherwise only ours runs and the name
// must then be indexed with a null record. The others notify controllers and players, release
// items or send messages: ours only, with what must hold afterwards.
#include "verify.h"

#include "../../src/game/combat.h"
#include "../../src/game/map_object.h"
#include "../../src/game/unit.h"
#include "../../src/game/unit_create.h"

#include "ei/x87.h"

#include <cstring>

namespace {
void* s_unused;

bool Broken(const CEngineObject* item) {
    return item && x87::fistp(Field<float>(reinterpret_cast<const char*>(item), 0x20)) <= 0;
}
} // namespace

// 00514930: PUSH -1; PUSH 0x71D330 (7 bytes).
static char*(__fastcall* s_findByName)(UnitPrototypeTable*, void*, const char*);
static char* __fastcall FindByNameVerify(UnitPrototypeTable* self, void*, const char* name) {
    stlp::string key(name);
    bool indexed = self->byName.Find(key) != nullptr;
    std::size_t count = self->byName.Size();
    if (indexed) {
        char* expected = s_findByName(self, nullptr, name);
        char* actual = self->FindByName(name);
        ei::verify::Check("UnitPrototypeTable::FindByName", expected, actual, "%s", name);
        ei::verify::Check("UnitPrototypeTable::FindByName (count)", count, self->byName.Size(), "%s", name);
        return expected;
    }
    char* actual = self->FindByName(name);
    auto* node = self->byName.Find(key);
    bool ok = actual == nullptr && node && node->value == nullptr && self->byName.Size() == count + 1;
    ei::verify::Check("UnitPrototypeTable::FindByName (inserted)", true, ok, "%s", name);
    return actual;
}
EI_VERIFY(0x00514930, FindByNameVerify, 7, s_findByName);

// 00552F40. Ours only: the statistics are the source with the slots refilled (not compared), the
// companion's hit points and mana are at their maxima, and the companion is returned.
static std::uintptr_t __fastcall SetStatsVerify(CUnitServer* self, void*, const UnitStats* stats) {
    std::uintptr_t result = self->SetStats(stats);
    const char* vitals = reinterpret_cast<const char*>(reinterpret_cast<CMapObject*>(self->unit)->companion);
    bool ok = result == reinterpret_cast<std::uintptr_t>(vitals) && std::memcmp(vitals + 0x28, vitals + 0x2C, 4) == 0 &&
              std::memcmp(vitals + 0x34, vitals + 0x38, 4) == 0 && (self->unit->changedParts & (1 << 4));
    ei::verify::Check("CUnitServer::SetStats", true, ok, "unit %d", self->unit->id_);
    return result;
}
EI_VERIFY(0x00552F40, SetStatsVerify, 0, s_unused);

// 00552780. Ours only (sends a message): counted.
static void __fastcall ShoutVerify(CUnitServer* self, void*, std::uint32_t code) {
    self->Shout(code);
    ei::verify::Check("CUnitServer::Shout", true, true, "");
}
EI_VERIFY(0x00552780, ShoutVerify, 0, s_unused);

// 00532AC0. Ours only (releases items, sends messages). With a player, no broken armour piece or
// weapon is left; the weapons that remain kept their order, packed towards slot 0 past the gaps
// the broken ones left. Without a player nothing changes.
static void __fastcall DropBrokenEquipmentVerify(CUnit* self) {
    CEngineObject* weapons[4];
    CEngineObject* armor[7];
    std::memcpy(weapons, self->weapons, sizeof(weapons));
    std::memcpy(armor, self->armor, sizeof(armor));
    bool hasPlayer = self->player != nullptr;
    bool armorBroken[7];
    for (int i = 0; i < 7; ++i) armorBroken[i] = Broken(armor[i]);
    // Expected weapons: the broken ones removed and the slots above moved down.
    CEngineObject* expected[4];
    std::memcpy(expected, weapons, sizeof(expected));
    for (int i = 3; i >= 0; --i) {
        if (!Broken(expected[i])) continue;
        for (int k = i; k < 3; ++k) expected[k] = expected[k + 1];
        expected[3] = nullptr;
    }
    self->DropBrokenEquipment();
    bool ok;
    if (!hasPlayer) {
        ok = std::memcmp(weapons, self->weapons, sizeof(weapons)) == 0 && std::memcmp(armor, self->armor, sizeof(armor)) == 0;
    } else {
        ok = std::memcmp(expected, self->weapons, sizeof(expected)) == 0;
        for (int i = 0; i < 7; ++i) ok = ok && self->armor[i] == (armorBroken[i] ? nullptr : armor[i]);
    }
    ei::verify::Check("CUnit::DropBrokenEquipment", true, ok, "unit %d", self->id_);
}
EI_VERIFY(0x00532AC0, DropBrokenEquipmentVerify, 0, s_unused);

// --- unit_create_stats.cpp --------------------------------------------------------------------

namespace {
// The NPC lookup: the name already indexed -> both versions, same record; else ours only, and
// the name is then indexed with a null record.
template <typename Table>
char* LookupVerify(const char* label, Table* self, const char* name,
                   char*(__fastcall* original)(Table*, void*, const char*)) {
    stlp::string key(name);
    bool indexed = self->byName.Find(key) != nullptr;
    std::size_t count = self->byName.Size();
    if (indexed) {
        char* expected = original(self, nullptr, name);
        char* actual = self->FindByName(name);
        ei::verify::Check(label, expected, actual, "%s", name);
        return expected;
    }
    char* actual = self->FindByName(name);
    auto* node = self->byName.Find(key);
    bool ok = actual == nullptr && node && node->value == nullptr && self->byName.Size() == count + 1;
    ei::verify::Check(label, true, ok, "%s (inserted)", name);
    return actual;
}

// A scratch copy of an output block (null stays null).
template <std::size_t N>
struct Scratch {
    unsigned char bytes[N];
    void* real;
    explicit Scratch(void* p) : real(p) {
        if (p) std::memcpy(bytes, p, N);
    }
    template <typename T> T* Get() { return real ? reinterpret_cast<T*>(bytes) : nullptr; }
    int FirstDifference() const {
        if (!real) return -1;
        for (std::size_t i = 0; i < N; ++i)
            if (bytes[i] != static_cast<unsigned char*>(real)[i]) return static_cast<int>(i);
        return -1;
    }
};
} // namespace

// 005144D0: PUSH -1; PUSH 0x71D2D0 (7 bytes). The copy of 00514930.
static char*(__fastcall* s_findPrototype)(UnitPrototypeTable*, void*, const char*);
static char* __fastcall FindPrototypeVerify(UnitPrototypeTable* self, void*, const char* name) {
    return LookupVerify("UnitPrototypeTable::FindByName (005144D0)", self, name, s_findPrototype);
}
EI_VERIFY(0x005144D0, FindPrototypeVerify, 7, s_findPrototype);

// 005145D0: PUSH -1; PUSH 0x71D300 (7 bytes).
static char*(__fastcall* s_findNpc)(NpcPrototypeTable*, void*, const char*);
static char* __fastcall FindNpcVerify(NpcPrototypeTable* self, void*, const char* name) {
    return LookupVerify("NpcPrototypeTable::FindByName", self, name, s_findNpc);
}
EI_VERIFY(0x005145D0, FindNpcVerify, 7, s_findNpc);

// 005134F0: PUSH EBP; MOV EBP,ESP; SUB ESP,8 (6 bytes). Output only: the original into scratch
// copies of what it writes (the statistics, or the vitals without statistics), ours into the
// real ones; bytes and EAX compared.
static int(__fastcall* s_setDefaults)(UnitStatsBuilder*, void*, UnitVitals*, UnitStats*);
static int __fastcall SetDefaultsVerify(UnitStatsBuilder* self, void*, UnitVitals* vitals, UnitStats* stats) {
    Scratch<sizeof(UnitVitals)> v(stats ? nullptr : vitals);
    Scratch<sizeof(UnitStats)> s(stats);
    int expected = s_setDefaults(self, nullptr, stats ? vitals : v.Get<UnitVitals>(), s.Get<UnitStats>());
    int actual = self->SetDefaults(vitals, stats);
    int dv = v.FirstDifference();
    int ds = s.FirstDifference();
    ei::verify::Check("UnitStatsBuilder::SetDefaults", true, expected == actual && dv < 0 && ds < 0,
                      "vitals +0x%X, stats +0x%X, result %d vs %d", dv, ds, expected, actual);
    return expected;
}
EI_VERIFY(0x005134F0, SetDefaultsVerify, 6, s_setDefaults);

// 005136D0: PUSH EBP; MOV EBP,ESP; SUB ESP,0xC (6 bytes). Output only, as above (the lookups it
// makes first may insert the name, which both runs then find).
static int(__fastcall* s_buildFromPrototype)(UnitStatsBuilder*, void*, UnitVitals*, UnitStats*, const char*);
static int __fastcall BuildFromPrototypeVerify(UnitStatsBuilder* self, void*, UnitVitals* vitals, UnitStats* stats,
                                               const char* name) {
    Scratch<sizeof(UnitVitals)> v(vitals);
    Scratch<sizeof(UnitStats)> s(stats);
    int expected = s_buildFromPrototype(self, nullptr, v.Get<UnitVitals>(), s.Get<UnitStats>(), name);
    int actual = self->BuildFromPrototype(vitals, stats, name);
    int dv = v.FirstDifference();
    int ds = s.FirstDifference();
    ei::verify::Check("UnitStatsBuilder::BuildFromPrototype", true, expected == actual && dv < 0 && ds < 0,
                      "%s: vitals +0x%X, stats +0x%X, result %d vs %d", name, dv, ds, expected, actual);
    return expected;
}
EI_VERIFY(0x005136D0, BuildFromPrototypeVerify, 6, s_buildFromPrototype);

// 00513DA0: PUSH EBP; MOV EBP,ESP; SUB ESP,0xC (6 bytes). Output only (the statistics).
static int(__stdcall* s_buildNpcStats)(UnitStats*, const char*);
static int __stdcall BuildNpcStatsVerify(UnitStats* stats, const char* name) {
    Scratch<sizeof(UnitStats)> s(stats);
    int expected = s_buildNpcStats(s.Get<UnitStats>(), name);
    int actual = BuildNpcStats(stats, name);
    int ds = s.FirstDifference();
    ei::verify::Check("BuildNpcStats", true, expected == actual && ds < 0, "%s: stats +0x%X, result %d vs %d", name,
                      ds, expected, actual);
    return expected;
}
EI_VERIFY(0x00513DA0, BuildNpcStatsVerify, 6, s_buildNpcStats);

// 00513240: MOV EAX,[ESP+4]; PUSH ESI (5 bytes). Pure but for the lookup's insertion of an
// unknown name, which the second run then finds: both run, results compared.
static int(__stdcall* s_prototypeObjectClass)(const char*);
static int __stdcall PrototypeObjectClassVerify(const char* name) {
    int expected = s_prototypeObjectClass(name);
    int actual = PrototypeObjectClass(name);
    ei::verify::Check("PrototypeObjectClass", expected, actual, "%s", name);
    return expected;
}
EI_VERIFY(0x00513240, PrototypeObjectClassVerify, 5, s_prototypeObjectClass);

// 00524CD0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x14 (6 bytes). Writes only the statistics: ours,
// snapshot, restore, the original; the 0x704 bytes compared.
static void(__fastcall* s_resetToBase)(UnitStats*, void*, float);
static void __fastcall ResetToBaseVerify(UnitStats* self, void*, float load) {
    unsigned char before[sizeof(UnitStats)];
    unsigned char ours[sizeof(UnitStats)];
    std::memcpy(before, self, sizeof(before));
    self->ResetToBase(load);
    std::memcpy(ours, self, sizeof(ours));
    std::memcpy(self, before, sizeof(before));
    s_resetToBase(self, nullptr, load);
    int diff = -1;
    for (int i = 0; i < static_cast<int>(sizeof(ours)) && diff < 0; ++i)
        if (ours[i] != reinterpret_cast<unsigned char*>(self)[i]) diff = i;
    ei::verify::Check("UnitStats::ResetToBase", true, diff < 0, "first difference at +0x%X", diff);
}
EI_VERIFY(0x00524CD0, ResetToBaseVerify, 6, s_resetToBase);

// 00513B60. Ours only (hands statistics to the object, rescales it): returns 1 and the
// prototype index is -1 or a valid index.
static int __fastcall ApplyPrototypeVerify(UnitStatsBuilder* self, void*, CEngineObject* object, const char* name) {
    int result = self->ApplyPrototype(object, name);
    const RecordTable& table = unitdb::Prototypes();
    int index = *reinterpret_cast<int*>(reinterpret_cast<char*>(object) + 0x18);
    std::uint32_t count = static_cast<std::uint32_t>((table.end - table.begin) / unitdb::kPrototypeSize);
    bool ok = result == 1 && (index == -1 || static_cast<std::uint32_t>(index) < count);
    ei::verify::Check("UnitStatsBuilder::ApplyPrototype", true, ok, "%s: object %d index %d", name, object->id_,
                      index);
    return result;
}
EI_VERIFY(0x00513B60, ApplyPrototypeVerify, 0, s_unused);

// 00512730. Ours only (allocates and constructs): an object with a vtable for the known class
// ids, null for the others.
static CEngineObject* __fastcall CreateObjectVerify(CMapObjectManager* self, void*, int classId) {
    CEngineObject* result = self->CreateObject(classId);
    bool known = (classId >= 0x32 && classId <= 0x3C) || classId == 0x300D || classId == 0x3010;
    bool ok = known ? result != nullptr && *reinterpret_cast<void**>(result) != nullptr : result == nullptr;
    ei::verify::Check("CMapObjectManager::CreateObject", true, ok, "class %X", classId);
    return result;
}
EI_VERIFY(0x00512730, CreateObjectVerify, 0, s_unused);
