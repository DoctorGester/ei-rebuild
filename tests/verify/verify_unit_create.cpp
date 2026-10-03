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
