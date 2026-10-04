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
