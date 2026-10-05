// Checks for magic_trap.cpp. UpdateVictims: ours only (it releases and takes unit references and casts spells).
// Afterwards the list of units inside is well formed (links, tail, count) and holds no unit twice;
// when the trap is not armed (nothing was added and no spell cast, which could kill a newcomer at
// once) every unit in it is alive and inside a circle by the trap's own test. Reached by magic traps of the zone files (none is known on zone1:
// the verify count shows whether a scenario got there).
#include "verify.h"

#include "../../src/game/engine_object.h"
#include "../../src/game/magic_trap.h"

#include "ei/vcall.h"

#include <cstdint>
#include <cstring>

namespace {
void* s_unused;

template <typename T>
T& Field(void* base, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + offset);
}

struct Node {
    Node* next;
    Node* prev;
    CEngineObject* unit;
};

bool Inside(MagicTrap* trap, CEngineObject* unit) {
    int count = Field<int>(trap, 0x248);
    const TrapCircle* circle = Field<const TrapCircle*>(trap, 0x244);
    float x = Field<float>(unit, 0x1C);
    float y = Field<float>(unit, 0x20);
    for (int i = 0; i < count; ++i, ++circle) {
        float dx = x - circle->x;
        float dy = y - circle->y;
        if (circle->radius * circle->radius > dy * dy + dx * dx) return true;
    }
    return false;
}
} // namespace

static void __fastcall UpdateVictimsVerify(MagicTrap* trap, void*) {
    trap->UpdateVictims();
    bool armed = (Field<std::uint32_t>(trap, 0x08) & 0x80000) != 0;
    Node* head = Field<Node*>(trap, 0x26C);
    Node* tail = Field<Node*>(trap, 0x270);
    int count = Field<int>(trap, 0x274);
    const char* problem = nullptr;
    int n = 0;
    Node* last = nullptr;
    for (Node* node = head; node && !problem; node = node->next) {
        if (node->prev != last) problem = "broken link";
        else if (!node->unit) problem = "null unit";
        else if (!armed && ei::VCall<int>(node->unit, 0xDC / 4) != 0) problem = "dead unit kept";
        else if (!armed && !Inside(trap, node->unit)) problem = "unit outside kept";
        for (Node* other = head; other != node && !problem; other = other->next)
            if (other->unit == node->unit) problem = "unit twice";
        last = node;
        ++n;
    }
    if (!problem && (tail != last || n != count)) problem = "tail or count";
    ei::verify::Check("MagicTrap::UpdateVictims", true, problem == nullptr, "trap %d: %s (%d units, count %d)",
                      reinterpret_cast<CEngineObject*>(trap)->id_, problem ? problem : "", n, count);
}
EI_VERIFY(0x0052A680, UpdateVictimsVerify, 0, s_unused);

// FindTrapVictims (0052A920) only fills its output list (the sector map query and the units'
// slot 55 are read-only): the original runs into an empty scratch list, ours into the caller's;
// the two must hold the same units in the same order. The caller keeps ours (the scratch list is
// freed). Reached from UpdateVictims of an armed trap only.
// 0052A920: PUSH -1; PUSH 0x71E550 (7 bytes).
static void(__fastcall* s_findVictims)(void*, const TrapArea*, std::uint32_t, mfc::CTypedPtrList<CEngineObject>*);
static void __fastcall FindVictimsVerify(void* context, const TrapArea* area, std::uint32_t sides,
                                         mfc::CTypedPtrList<CEngineObject>* out) {
    using UnitList = mfc::CTypedPtrList<CEngineObject>;
    UnitList expected{reinterpret_cast<void*>(0x0073D454), nullptr, nullptr, 0, nullptr, nullptr, 10};
    s_findVictims(context, area, sides, &expected);
    FindTrapVictims(context, area, sides, out);
    int position = 0;
    const UnitList::Node* a = expected.head;
    const UnitList::Node* b = out->head;
    while (a && b && a->data == b->data) {
        a = a->next;
        b = b->next;
        ++position;
    }
    bool same = !a && !b && expected.count == out->count;
    ei::verify::Check("FindTrapVictims", true, same, "sides %08X, %d circles: %d vs %d units, first difference at %d",
                      sides, area->count, expected.count, out->count, position);
    expected.RemoveAll();
}
EI_VERIFY(0x0052A920, FindVictimsVerify, 7, s_findVictims);

// CastAtUnits (0052A4B0) casts spells: ours only. Afterwards, when it had units and a spell, the
// countdown (+0x288) equals the period (+0x284), except when +0x28C found no unit; with +0x28C on
// the units path the list holds exactly one unit, one that was in it before; otherwise the list
// is unchanged (same nodes). Without units or a spell nothing changes. Reached from UpdateVictims
// when an armed trap takes in newcomers.
static void __fastcall CastAtUnitsVerify(MagicTrap* trap, void*, mfc::CTypedPtrList<CEngineObject>* units) {
    using UnitList = mfc::CTypedPtrList<CEngineObject>;
    void* spell = Field<void*>(trap, 0x23C);
    int countBefore = units->count;
    UnitList::Node* headBefore = units->head;
    std::uint32_t countdownBefore = Field<std::uint32_t>(trap, 0x288);
    bool narrowing = countBefore != 0 && spell &&
                     ((Field<std::uint32_t>(spell, 0x3C) & 0x10000000) != 0 || Field<int>(trap, 0x25C) == 0) &&
                     Field<int>(trap, 0x28C) != 0;
    // The units the list held, for the narrowing check (only the first 64 are remembered).
    CEngineObject* before[64];
    int remembered = 0;
    for (UnitList::Node* node = units->head; node && remembered < 64; node = node->next) before[remembered++] = node->data;

    trap->CastAtUnits(units);

    const char* problem = nullptr;
    std::uint32_t countdown = Field<std::uint32_t>(trap, 0x288);
    if (countBefore == 0 || !spell) {
        if (countdown != countdownBefore || units->count != countBefore) problem = "changed without units or spell";
    } else if (narrowing) {
        if (units->count == countBefore && units->head == headBefore && countdown == countdownBefore) {
            // No unit closer than 1e30: nothing happened.
        } else if (units->count != 1 || !units->head || units->head != units->tail) {
            problem = "narrowed list not a single unit";
        } else {
            bool known = remembered < countBefore;   // more than 64 units: not checked
            for (int i = 0; i < remembered && !known; ++i) known = before[i] == units->head->data;
            if (!known) problem = "narrowed to a unit not in the list";
            else if (countdown != Field<std::uint32_t>(trap, 0x284)) problem = "countdown not restarted";
        }
    } else {
        if (units->count != countBefore || units->head != headBefore) problem = "list changed";
        else if (countdown != Field<std::uint32_t>(trap, 0x284)) problem = "countdown not restarted";
    }
    ei::verify::Check("MagicTrap::CastAtUnits", true, problem == nullptr, "trap %d, %d units: %s",
                      reinterpret_cast<CEngineObject*>(trap)->id_, countBefore, problem ? problem : "");
}
EI_VERIFY(0x0052A4B0, CastAtUnitsVerify, 0, s_unused);

// SetUp (0052A2C0) makes a spell and releases references: ours only. Afterwards the sides, position,
// circles, points and period equal the record's (bitwise), the countdown is 0, +0x28C is the
// record's byte as 0 / 1, the list of units inside is empty with no blocks, and the trap holds a
// spell exactly when the record's text made one (not checked again: the spell manager is not
// asked twice). Reached by the zone loader (005927C0) for every magic trap of a zone file; none is
// known on zone1.
static void __fastcall SetUpVerify(MagicTrap* trap, void*, const MagicTrapRecord* record) {
    trap->SetUp(record);
    const char* problem = nullptr;
    const auto* area = &Field<TrapArea>(trap, 0x240);
    const auto* points = &Field<TrapPoints>(trap, 0x254);
    if (Field<std::uint32_t>(trap, 0x238) != record->sides) problem = "sides";
    else if (std::memcmp(&Field<float>(trap, 0x1C), record->position, sizeof(record->position)) != 0) problem = "position";
    else if (area->count != record->area.count ||
             (area->count > 0 && std::memcmp(area->circles, record->area.circles, area->count * sizeof(TrapCircle)) != 0))
        problem = "circles";
    else if (points->count != record->points.count ||
             (points->count > 0 && std::memcmp(points->points, record->points.points, points->count * 2 * sizeof(float)) != 0))
        problem = "points";
    else if (Field<void*>(trap, 0x26C) || Field<void*>(trap, 0x270) || Field<int>(trap, 0x274) != 0 ||
             Field<void*>(trap, 0x278) || Field<void*>(trap, 0x27C))
        problem = "units inside not emptied";
    else if (Field<std::uint32_t>(trap, 0x284) != record->period || Field<std::uint32_t>(trap, 0x288) != 0)
        problem = "period / countdown";
    else if (Field<int>(trap, 0x28C) != (record->singleTarget ? 1 : 0)) problem = "single target";
    ei::verify::Check("MagicTrap::SetUp", true, problem == nullptr, "trap %d: %s",
                      reinterpret_cast<CEngineObject*>(trap)->id_, problem ? problem : "");
}
EI_VERIFY(0x0052A2C0, SetUpVerify, 0, s_unused);
