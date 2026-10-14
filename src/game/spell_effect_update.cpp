// A spell effect's later states: the per-tick dispatch (0067E6D0), the missiles' flight and impact
// (state 1, 00681BB0) and the lasting effects (state 2, 00682650), plus the release helpers
// 0067EE00-0067EE80. See docs/notes/items_spells.md.
#include "spell_effect_internal.h"
#include "combat.h"
#include "player_messages.h"
#include "unit.h"
#include "unit_ai.h"

using namespace spellfx;

namespace {

constexpr std::uintptr_t kSplashScale = 0x0073C538;    // 0.7: the impact particle's size per power
constexpr std::uintptr_t kRiseScale = 0x00743998;      // 1.5: impact lights rise faster and reach further
constexpr std::uintptr_t kNoiseKind = 0x0074396C;      // 4.0, truncated by _ftol
constexpr std::uintptr_t kWakeMargin = 0x0073BF34;     // 5.0
constexpr std::uintptr_t kPossessWake = 0x00743970;    // 2.0
constexpr std::uintptr_t kPossessWakeBase = 0x0073CCF8; // 20.0
constexpr float kExplosionLoudness = 5.0f;             // immediate
constexpr float kStrikeWidthImmediate = -7.0f;         // immediate in the lasting strike
constexpr int kEndedTimer = -80;                       // the effects linger this long after ending
void* const kPtrListVtable = reinterpret_cast<void*>(0x0073D454);

using UnitList = mfc::CTypedPtrList<CEngineObject>;

// A player's server connection (CPlayer::connection_ on the server is a CPlayerServer).
CPlayerServer* ServerOf(CPlayer* player) { return reinterpret_cast<CPlayerServer*>(player->connection_); }

// --- State 1 -------------------------------------------------------------------------------------

// 0 / 0x28 (the fire arrow) and 2 (the acid ray): the effect follows its missile; when the missile
// reports a hit (+0x14C) the target takes the damage, the missile, sound and light go, and a
// splash, an end sound and a rising light appear at the target's height. The two spells compute
// the light's rise differently (the arrow rounds the quotient before scaling it).
void MissileImpact(CSpellEffect* e, int particleType, const char* sound, bool arrow) {
    CEngineObject* missile = e->visual;
    if (!missile) return;
    std::memcpy(&e->position, &static_cast<CEffectMissile*>(missile)->localPosition_, 12);
    if (static_cast<CEffectMissile*>(missile)->arrived_ == 0) return;

    DamageInfo hit;
    SpellEffectGetDamageOriginal(e, nullptr, &hit);
    if (IsUnit(e->target)) EffectHitUnit(e, nullptr, &hit, e->target);
    e->ReleaseVisual();
    e->ReleaseSound();
    e->ReleaseLight();

    CEngineObject* target = e->target;   // no null check
    std::memcpy(&e->targetPoint, &static_cast<CUnit*>(target)->position, 12);
    Vector3 at;
    std::memcpy(&at, &static_cast<CUnit*>(target)->position, 12);
    at.z = static_cast<float>(UnitModeSpeed(static_cast<CUnit*>(target)));
    at.z = GroundPlus(e, kWorldSlotGridHeight, e->targetPoint, &at.z);
    float size = PowerScaled(e, kSplashScale);
    e->CreateParticle(0, particleType, &kZeroVector, size, 1.0f, &kZeroVector, e->target);
    e->CreateSound(0, sound, &at, &kZeroVector, kVolume, kHearing);

    Vector3 velocity;
    if (arrow) {
        Vector3 rise{0.0f, 0.0f, LightRise(e)};
        velocity.x = rise.x * ConstAt(kRiseScale);
        velocity.y = rise.y * ConstAt(kRiseScale);
        velocity.z = rise.z * ConstAt(kRiseScale);
    } else {
        velocity.x = 0.0f;
        velocity.y = 0.0f;
        velocity.z = LightRiseScaled(e, kRiseScale);
    }
    float range = PrototypeRange(e);
    Vector3 color = PrototypeColor(e);
    e->CreatePointLight(&at, &color, range * ConstAt(kRiseScale), &velocity);
    e->timer = 0x23;
    e->state = 2;
}

// 3: the fireball explodes when its timer runs out: everything in the radius is hit, a blast
// (the larger of the area curve and the power), an end sound, a rising light and, with a unit
// caster, a noise the AI hears. The missile's particle goes once the timer is below zero.
void FireballFlight(CSpellEffect* e) {
    int ticks = e->timer;
    e->timer = ticks - 1;
    if (ticks < 0) {
        e->position = e->targetPoint;
        EffectHitUnitsInRadius(e);
        e->ReleaseVisual();
        e->ReleaseSound();
        e->ReleaseLight();
        Vector3 at = e->targetPoint;
        float power = Power(e);
        float curve;
        AreaCurve(&e->radius, reinterpret_cast<const float*>(Prototype(e) + 0x28), &curve);
        float size = !(curve >= power) ? power : curve;
        CEngineObject* blast = e->CreateParticle(0, 0x2002, &at, size, 1.0f, &kZeroVector, nullptr);
        float radius = e->radius;
        float alpha = Power(e);
        ParticleSetColors(blast, nullptr, alpha, radius);
        e->CreateSound(0, GameString(0x00795754), &at, &kZeroVector, kVolume, kHearing);
        Vector3 velocity{0.0f, 0.0f, LightRiseScaled(e, kRiseScale)};
        float range = PrototypeRange(e);
        Vector3 color = PrototypeColor(e);
        e->CreatePointLight(&at, &color, range * ConstAt(kRiseScale), &velocity);
        if (IsUnit(e->caster)) {
            AiNoise(e->world->timeWheel_, nullptr, ControllerOf(e->caster), kExplosionLoudness, FtolOf(ConstPtr(kNoiseKind)),
                    &e->targetPoint);
        }
        e->timer = 0x23;
        e->state = 2;
    }
    if (e->timer < 0) e->ReleaseVisual();
}

// --- State 2 -------------------------------------------------------------------------------------

// The common count-down: ends the effect once the timer was already below zero.
void CountDown(CSpellEffect* e) {
    int ticks = e->timer;
    e->timer = ticks - 1;
    if (ticks < 0) e->state = 3;
}

// After the walls and clouds have ended: the timer keeps running down and the effect is removed
// 80 ticks later.
void Linger(CSpellEffect* e) {
    int ticks = e->timer;
    if (ticks < kEndedTimer) e->state = 3;
    e->timer = ticks - 1;
}

void RiseLight(CSpellEffect* e) {
    Vector3 rise{0.0f, 0.0f, LightRise(e)};
    EffectSetVelocity(e->light, nullptr, &rise);   // no null check
}

// 1 / 0x29: the lightning's bolt and sound go when the timer reaches 0, the light rises away.
void LightningFade(CSpellEffect* e) {
    if (e->timer == 0) {
        e->ReleaseVisual();
        e->ReleaseSound();
        RiseLight(e);
    }
    Linger(e);
}

// 4: the light strike's bolt is dropped every tick; at timer 5 a second bolt strikes from 30 above.
void LightStrikeEcho(CSpellEffect* e) {
    e->ReleaseVisual();
    if (e->timer == 5) {
        Vector3 top = e->position;
        top.z = top.z + ConstAt(kStrikeHeight);
        CEngineObject* bolt = CreateObject(e, kClassLightning);
        LightningSetEnds(bolt, nullptr, &top, &e->position, kStrikeWidthImmediate);
        e->SetVisual(bolt);
    }
    CountDown(e);
}

// 5: the acid column hits once, at timer 15.
void AcidColumn(CSpellEffect* e) {
    CountDown(e);
    if (e->timer == 15) HitAreaTargets(e);
}

// 6: the fire wall burns along its direction every 4th tick; at -1 it goes out.
void FireWall(CSpellEffect* e) {
    int ticks = e->timer;
    if (ticks >= 0) {
        if ((ticks & 3) == 0 && e->visual) {
            EffectHitUnitsAlong(e, nullptr, &static_cast<CEffectWall*>(e->visual)->direction_);
            e->flags |= 1;
        }
        e->timer = e->timer - 1;
        return;
    }
    if (ticks == -1) {
        Vector3 at = e->position;
        if (e->sound) std::memcpy(&at, &static_cast<CEffect*>(e->sound)->localPosition_, 12);
        e->UpdateFootprint(6, 0);
        e->ReleaseSound();
        e->ReleaseVisual();
        e->CreateSound(0, GameString(0x007957E4), &at, &kZeroVector, kVolume, kHearing);
        RiseLight(e);
    }
    Linger(e);
}

// 7: the light wall hits along its bolt every 4th tick; at -1 it goes out.
void LightWall(CSpellEffect* e) {
    int ticks = e->timer;
    if (ticks >= 0) {
        if ((ticks & 3) == 0 && e->visual) {
            CEffectLightning* bolt = static_cast<CEffectLightning*>(e->visual);
            Vector3 along;
            along.x = bolt->to_.x - bolt->from_.x;
            along.y = bolt->to_.y - bolt->from_.y;
            along.z = bolt->to_.z - bolt->from_.z;
            EffectHitUnitsAlong(e, nullptr, &along);
            e->flags |= 1;
        }
        e->timer = e->timer - 1;
        return;
    }
    if (ticks == -1) {
        RiseLight(e);
        e->UpdateFootprint(7, 0);
        e->ReleaseSound();
        e->ReleaseVisual();
    }
    Linger(e);
}

// 8: the poison cloud hits every 4th tick; at -1 it goes.
void PoisonCloud(CSpellEffect* e) {
    int ticks = e->timer;
    if (ticks >= 0) {
        if ((ticks & 3) == 0) {
            HitAreaTargets(e);
            e->flags |= 1;
        }
        e->timer = e->timer - 1;
        return;
    }
    if (ticks == -1) {
        e->CreateSound(0, GameString(0x007957FC), &e->targetPoint, &kZeroVector, kVolume, kHearing);
        RiseLight(e);
        e->UpdateFootprint(8, 0);
        e->ReleaseSound();
        e->ReleaseVisual();
    }
    Linger(e);
}

// 0x27: hits every 4th tick, counting up, forever (until something else ends it).
void CountingCloud(CSpellEffect* e) {
    int ticks = e->timer;
    e->timer = ticks + 1;
    if ((ticks & 3) == 0) HitAreaTargets(e);
}

// 0x12: fireworks wake the units around and frighten those in the radius (their AI memory notices
// the point at a level falling from 300 to 50 over the duration); at -1 they end.
void Fireworks(CSpellEffect* e, UnitList& units) {
    int ticks = e->timer - 1;
    e->timer = ticks;
    if (ticks >= 0) {
        float wake = e->radius + ConstAt(kWakeMargin);
        WorldWakeUnits(e->world, nullptr, &e->position, wake);
        SectorMapFindUnitsInRadius(e->world->Sectors(), nullptr, &e->position, &units, e->radius);
        for (auto* node = units.head; node;) {
            CEngineObject* unit = node->data;
            node = node->next;
            int duration = e->targetCount;
            int level = FrightLevel(&e->timer, &duration);
            AiMemoryNotice(ControllerOf(unit)->Brain()->memory, nullptr, &e->position, level, -1);
        }
        return;
    }
    if (ticks == -1) {
        e->CreateSound(0, GameString(0x007957B8), &e->position, &kZeroVector, kVolume, kHearing);
        e->ReleaseVisual();
        e->ReleaseSound();
        RiseLight(e);
    }
    if (e->timer < kEndedTimer) e->state = 3;
}

// 0x14: clairvoyance shows the caster's player every unit in the radius and wakes them; at timer
// 10 a closing particle and sound. Ends with the timer or without a caster.
void Clairvoyance(CSpellEffect* e, UnitList& units) {
    if (IsUnit(e->caster)) {
        auto* player = reinterpret_cast<CPlayer*>(static_cast<CUnit*>(e->caster)->player);
        if (player) {
            ScriptGroup* group = &ServerOf(player)->group50F8_;
            SectorMapFindUnitsInRadius(e->world->Sectors(), nullptr, &e->position, &units, e->radius);
            for (auto* node = units.head; node;) {
                CEngineObject* unit = node->data;
                node = node->next;
                GroupInsert(group, nullptr, unit);
            }
        }
        float wake = e->radius + ConstAt(kWakeMargin);
        WorldWakeUnits(e->world, nullptr, &e->position, wake);
    }
    int ticks = e->timer - 1;
    e->timer = ticks;
    if (ticks < 0 || !e->caster) e->state = 3;
    if (ticks == 10) {
        e->CreateParticle(0, 0x2018, &e->targetPoint, -e->radius, 1.0f, &kZeroVector, nullptr);
        e->CreateSound(0, GameString(0x0079579C), &e->position, &kZeroVector, kVolume, kHearing);
    }
}

// 0x15: possession: the caster's player sees the target, its controller's two lists and its id;
// the target is flagged (0x2000000) and the units around it wake.
void Possession(CSpellEffect* e) {
    if (!IsUnit(e->caster)) {
        e->state = 3;
        return;
    }
    auto* player = reinterpret_cast<CPlayer*>(static_cast<CUnit*>(e->caster)->player);
    if (player && e->target->GetClassId() == spellfx::kClassUnit) {   // no null check on the target
        CEngineObject* target = e->target;
        CUnitServer* server = ControllerOf(target);
        ScriptGroup* group = &ServerOf(player)->group50F8_;
        GroupInsert(group, nullptr, target);
        GroupInsertAll(group, nullptr, &server->Brain()->visible);
        GroupInsertAll(group, nullptr, &server->Brain()->seenCorpses);
        ServerOf(player)->unknown24_ = target->id_;
        target->flags_ |= 0x2000000;
        float sight = server->moveFactors[3] * server->stats.valuesB0[0].current;
        float floor = server->stats.valuesB0[2].current;
        float reach = !(sight >= floor) ? floor : sight;
        float wake;
        MultiplyAdd(&reach, ConstPtr(kPossessWake), ConstPtr(kPossessWakeBase), &wake);
        WorldWakeUnits(e->world, nullptr, &static_cast<CUnit*>(target)->position, wake);
    }
    int ticks = e->timer - 1;
    e->timer = ticks;
    if (ticks < 0) e->state = 3;
}

// 0x17: vision fog lasts while its target is free.
void VisionFog(CSpellEffect* e) {
    int ticks = e->timer;
    e->timer = ticks - 1;
    if (ticks < 0 || !e->target || ObjectSlots(e->target)->IsDead() != 0) e->state = 3;
}

// 0x1B: the link lasts while both units are free; it ends with a sound (and a complaint from the
// caster when a unit got busy).
void Link(CSpellEffect* e) {
    bool free = e->target && e->caster && ObjectSlots(e->target)->IsLargeUnit() == 0 &&
                ObjectSlots(e->caster)->IsLargeUnit() == 0 && ObjectSlots(e->target)->IsDead() == 0 &&
                ObjectSlots(e->caster)->IsDead() == 0;
    if (free) {
        int ticks = e->timer - 1;
        e->timer = ticks;
        if (ticks >= 0) return;
        e->CreateSound(0, GameString(0x007957D0), &e->position, &kZeroVector, kVolume, kHearing);
    } else {
        e->CreateSound(0, GameString(0x007957D0), &e->position, &kZeroVector, kVolume, kHearing);
        if (IsUnit(e->caster)) ControllerShout(ControllerOf(e->caster), nullptr, 0x19);
    }
    e->state = 3;
}

// 0x1C: teleport: when the timer comes down to the spell's duration (at least 1) the caster moves
// to the target point at its own height, if it fits there (else it complains, 0x10); then it gets
// the spell's buff record and its controller re-places it.
void Teleport(CSpellEffect* e) {
    int ticks = e->timer;
    e->timer = ticks - 1;
    if (ticks < 0 || !e->caster || ObjectSlots(e->caster)->IsDead() != 0) {
        e->state = 3;
        return;
    }
    int arrival = e->targetCount;
    if (arrival < 1) arrival = 1;
    if (e->timer != arrival) return;
    CEngineObject* caster = e->caster;
    std::memcpy(&e->targetPoint.z, &static_cast<CTransformObject*>(caster)->localPosition_.z, 4);
    if (!AiMapPassableWithoutSelf(e->world->AIMap(), nullptr, caster, &e->targetPoint)) {
        if (IsUnit(e->caster)) ControllerShout(ControllerOf(e->caster), nullptr, 0x10);
        return;
    }
    SectorMapMove(e->world->Sectors(), nullptr, e->caster, &e->targetPoint);
    e->ReleaseVisual();
    e->ReleaseSound();
    if (!IsUnit(e->caster)) return;
    MapEffect record;
    BuffRecordInit(&record);
    std::memcpy(&record, &e->unknown_50, sizeof(record));
    ObjectSlots(e->caster)->AddEffect(&record);
    ControllerReposition(ControllerOf(e->caster));
    RiseLight(e);
}

} // namespace

// --- CSpellEffect ---------------------------------------------------------------------------------

int CSpellEffect::Update() {
    if (!world) return 0;
    int type = static_cast<int>(spellId >> 16);
    switch (state) {
    case 0: Start(type); return 1;
    case 1: UpdateMissile(type); return 1;
    case 2: UpdateLasting(type); return 1;
    default:
        UpdateFootprint(type, 0);
        ReleaseVisual();
        ReleaseSound();
        ReleaseLight();
        return 0;
    }
}
EI_REPLACE_METHOD(0x0067E6D0, CSpellEffect::Update);

void CSpellEffect::UpdateMissile(int type) {
    switch (type) {
    case 0x00:
    case 0x28:
        MissileImpact(this, 0x2041, GameString(0x00795784), true);
        CountDown(this);
        break;
    case 0x02:
        MissileImpact(this, 0x2042, GameString(0x0079576C), false);
        CountDown(this);
        break;
    case 0x03: FireballFlight(this); break;
    default: break;
    }
}
EI_REPLACE_METHOD(0x00681BB0, CSpellEffect::UpdateMissile);

void CSpellEffect::UpdateLasting(int type) {
    UnitList units;   // the inline CPtrList the fireworks and clairvoyance fill
    units.vtable = kPtrListVtable;
    units.count = 0;
    units.tail = nullptr;
    units.head = nullptr;
    units.free = nullptr;
    units.blocks = nullptr;
    units.blockSize = kPtrListBlockSize;

    switch (type) {
    case 0x00:
    case 0x02:
    case 0x03:
    case 0x18:
    case 0x28: CountDown(this); break;
    case 0x01:
    case 0x29: LightningFade(this); break;
    case 0x04: LightStrikeEcho(this); break;
    case 0x05: AcidColumn(this); break;
    case 0x06: FireWall(this); break;
    case 0x07: LightWall(this); break;
    case 0x08: PoisonCloud(this); break;
    case 0x12: Fireworks(this, units); break;
    case 0x14: Clairvoyance(this, units); break;
    case 0x15: Possession(this); break;
    case 0x17: VisionFog(this); break;
    case 0x1B: Link(this); break;
    case 0x1C: Teleport(this); break;
    case 0x27: CountingCloud(this); break;
    default: break;
    }

    units.vtable = kPtrListVtable;
    units.RemoveAll();
}
EI_REPLACE_METHOD(0x00682650, CSpellEffect::UpdateLasting);

void CSpellEffect::ReleaseVisual() { Assign(this, &visual, nullptr); }
EI_REPLACE_METHOD(0x0067EE00, CSpellEffect::ReleaseVisual);

void CSpellEffect::ReleaseSound() { Assign(this, &sound, nullptr); }
EI_REPLACE_METHOD(0x0067EE40, CSpellEffect::ReleaseSound);

void CSpellEffect::ReleaseLight() { Assign(this, &light, nullptr); }
EI_REPLACE_METHOD(0x0067EE80, CSpellEffect::ReleaseLight);
