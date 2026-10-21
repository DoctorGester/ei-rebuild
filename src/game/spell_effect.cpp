// Spell effects hitting units: the helpers the effect state machines (0067F870 casting, 00681BB0
// missiles, 00682650 lasting effects) call every tick to deliver a spell's damage.
#include "spell.h"

#include "map_object.h"
#include "sector_map.h"
#include "unit.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/mfc.h"

#include <cstring>
#include "originals_effects.h"
#include "originals_ai.h"
#include "game_data.h"
#include "lib/nan_audit.h"

namespace {
constexpr float kTargetThreshold = 0.5f;    // 0073B86C
constexpr float kWallHalfWidth = 1.0f;      // 0073B84C
void* const kPtrListVtable = ei::GamePointer<void>(0x0073D454);
constexpr int kPtrListBlockSize = 10;

using UnitList = mfc::CTypedPtrList<CEngineObject>;

// 005C0BD0 CSectorMap::FindUnitsInRadius (rewritten in sector_map.cpp; through its address so its
// verify wrapper sees the call).
CSectorMap* SectorMapOf(const CSpellEffect* effect) { return effect->world->Sectors(); }

void InitList(UnitList& list) {
    list.vtable = kPtrListVtable;
    list.count = 0;
    list.tail = nullptr;
    list.head = nullptr;
    list.free = nullptr;
    list.blocks = nullptr;
    list.blockSize = kPtrListBlockSize;
}

void DestroyList(UnitList& list) {
    list.vtable = kPtrListVtable;
    list.RemoveAll();
}

// The test of IsSpellTarget, inlined by the original in the area loops.
bool AcceptsAreaSpells(CEngineObject* object) {
    auto* unit = static_cast<CUnit*>(object);
    if (unit->sizeClass != 0) return true;
    float value;
    std::memcpy(&value, unit->GetPrototype() + 0x24, sizeof(value));
    EI_NAN_AUDIT(value);
    return value < kTargetThreshold;
}

// Delivers `hit` to `unit` unless the spell may not affect it (the loops' common tail).
void Deliver(CSpellEffect* effect, DamageInfo* hit, CEngineObject* unit) {
    if (effect->caster && !effect->Spell()->CanAffect(effect->caster, unit)) return;
    CEngineObject* attacker = (effect->flags & 1) ? nullptr : effect->caster;
    ObjectSlots(unit)->ReceiveHit(hit, attacker, effect->caster);
}
} // namespace

BOOL CSpellEffect::CallGetDamage(DamageInfo* out) { return GetDamage(out); }

int __fastcall IsSpellTarget(CEngineObject* unit) { return AcceptsAreaSpells(unit) ? 1 : 0; }
EI_REPLACE(0x00681B80, IsSpellTarget);

void CSpellEffect::HitUnit(DamageInfo* hit, CEngineObject* unit) { Deliver(this, hit, unit); }
EI_REPLACE_METHOD(0x006835B0, CSpellEffect::HitUnit);

void CSpellEffect::HitUnitsInRadius() {
    UnitList list;
    InitList(list);
    DamageInfo hit;
    CallGetDamage(&hit);
    SectorMapOf(this)->FindUnitsInRadius(&position, &list, radius);
    for (UnitList::Node* node = list.head; node;) {
        CEngineObject* unit = node->data;
        node = node->next;
        if (!AcceptsAreaSpells(unit)) continue;
        Deliver(this, &hit, unit);
    }
    DestroyList(list);
}
EI_REPLACE_METHOD(0x00683600, CSpellEffect::HitUnitsInRadius);

void CSpellEffect::HitUnitsAlong(const Vector3* direction) {
    UnitList list;
    InitList(list);
    DamageInfo hit;
    CallGetDamage(&hit);

    // The wall's normal in the ground plane, as the original computes it: s = 2 / sqrt(x^2 + y^2)
    // (rounded to float), normal = (s * -y, s * x, 0). The normal has length 2, so the test against 1
    // accepts the units within 0.5 of the plane.
    float x = direction->x;
    double minusY = -direction->y;
    float scale = static_cast<float>(2.0f / std::sqrt(x * x + minusY * minusY));
    float normalX = static_cast<float>(scale * minusY);
    float normalY = scale * x;
    float normalZ = 0.0f;

    SectorMapOf(this)->FindUnitsInRadius(&position, &list, radius);
    for (UnitList::Node* node = list.head; node;) {
        CEngineObject* unit = node->data;
        node = node->next;
        const CUnit* at = static_cast<CUnit*>(unit);
        double dz = at->position.z - position.z;
        double dy = at->position.y - position.y;
        double dx = at->position.x - position.x;
        float distance = static_cast<float>(DotUnrounded<Sum3::ZYX, double>(dx, dy, dz, normalX, normalY, normalZ));
        std::uint32_t bits;
        std::memcpy(&bits, &distance, sizeof(bits));
        bits &= 0x7FFFFFFF;
        std::memcpy(&distance, &bits, sizeof(bits));
        if (distance > kWallHalfWidth) continue;
        if (!AcceptsAreaSpells(unit)) continue;
        Deliver(this, &hit, unit);
    }
    DestroyList(list);
}
EI_REPLACE_METHOD(0x00683730, CSpellEffect::HitUnitsAlong);
