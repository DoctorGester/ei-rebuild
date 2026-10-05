// Checks for magic_trap.cpp: ours only (it releases and takes unit references and casts spells).
// Afterwards the list of units inside is well formed (links, tail, count) and holds no unit twice;
// when the trap is not armed (nothing was added and no spell cast, which could kill a newcomer at
// once) every unit in it is alive and inside a circle by the trap's own test. Reached by magic traps of the zone files (none is known on zone1:
// the verify count shows whether a scenario got there).
#include "verify.h"

#include "../../src/game/engine_object.h"
#include "../../src/game/magic_trap.h"

#include "ei/vcall.h"

#include <cstdint>

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
