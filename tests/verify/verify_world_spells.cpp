// Checks for spell casting (src/game/world_spells.cpp). Both create missiles and drive AI, so only
// ours runs; afterwards the missile list's links are consistent (head/tail, every node's
// neighbours, its count).
#include "verify.h"

#include "../../src/game/world.h"

namespace {
void* s_unused;

bool MissileListConsistent(const CWorldServer* world, int* count) {
    const auto& list = world->projectiles_;
    int n = 0;
    const ProjectileList::Node* previous = nullptr;
    for (const auto* node = list.head; node; node = node->next, ++n) {
        if (node->prev != previous || !node->data) return false;
        previous = node;
    }
    *count = n;
    return list.tail == previous && n == list.count;
}
} // namespace

// 0058E850.
static void __fastcall CastSpellVerify(CWorldServer* self, void*, const void* spell, CEngineObject* caster,
                                       const Vector3* from, CEngineObject* target, const Vector3* to, int extra) {
    self->CastSpell(spell, caster, from, target, to, extra);
    int count = 0;
    bool ok = MissileListConsistent(self, &count);
    ei::verify::Check("CWorldServer::CastSpell", true, ok, "missiles %d", count);
}
EI_VERIFY(0x0058E850, CastSpellVerify, 0, s_unused);

// 0058E670.
static void __fastcall CreateMissileVerify(CWorldServer* self, void*, const void* spell, CEngineObject* caster,
                                           Vector3 from, CEngineObject* target, const Vector3* to, int extra) {
    self->CreateMissile(spell, caster, from, target, to, extra);
    int count = 0;
    bool ok = MissileListConsistent(self, &count);
    ei::verify::Check("CWorldServer::CreateMissile", true, ok, "missiles %d", count);
}
EI_VERIFY(0x0058E670, CreateMissileVerify, 0, s_unused);
