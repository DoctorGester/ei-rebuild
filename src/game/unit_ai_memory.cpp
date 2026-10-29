// The perception memory's queries over the visible units, the
// exposure to hostile units, corpses seen, forcing
// an object into sight and the counted lists' AddTail.
#include "ai_map.h"
#include "combat.h"
#include "console.h"
#include "game_singletons.h"
#include "map_object.h"
#include "object_diplomacy.h"
#include "unit_ai.h"
#include "world.h"

#include "lib/nan_audit.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace {
const float kFar = 1e30f;

bool IsDead(CMapObject* object) { return object->IsDead(); }

// (a - b) squared in 3D, summed z, then x, then y.
double Distance2(const CUnit* a, const float* b) {
    double dx = static_cast<double>(a->localPosition_.x) - b[0];
    double dy = static_cast<double>(a->localPosition_.y) - b[1];
    double dz = static_cast<double>(a->localPosition_.z) - b[2];
    return LengthSquaredUnrounded<Sum3::ZXY>(dx, dy, dz);
}

constexpr float kMaxExposure = 3.0f;

// distance / range, at most 3 (NaN kept); 3 for a zero range.
float Exposure(float distance, double range) {
    EI_NAN_AUDIT(range);
    if (range == 0.0) return kMaxExposure;
    float ratio = static_cast<float>(distance / range);
    return ratio >= kMaxExposure ? kMaxExposure : ratio;
}

// The smaller of the two; NaN `value` wins.
void KeepLower(float& slot, float value) { slot = value >= slot ? slot : value; }

} // namespace

void AIMemory::FindEnemies(CUnit* self, UnitList* out) {
    out->clear();
    for (auto& node : visible) {
        CUnit* other = node;
        if (!self->IsHostileTo(other) || IsDead(other) || reports.Contains(other)) continue;
        out->push_back(other);
    }
}

void AIMemory::FindEnemiesNear(CUnit* self, UnitList* out, const Vector3* point, float radius) {
    out->clear();
    float radius2 = radius > 0.0f ? static_cast<float>(static_cast<double>(radius) * radius) : -1.0f;
    for (auto& node : visible) {
        CUnit* other = node;
        if (!other->IsHostileTo(self) || IsDead(other) || reports.Contains(other)) continue;
        if (Distance2(other, &point->x) >= radius2) {
            AIBrain* theirs = &other->ServerController()->brain;
            auto* mode = static_cast<AggressionMode*>(theirs->FindMode(AIModeType::Aggression));
            if (!mode || (mode->fixed && !mode->enemy)) continue;
            CUnit* enemy = UnitOrNull(theirs->AggressionEnemy());
            if (enemy && !g_serverWorld.diplomacy_.IsAllied(self->side, enemy->side)) continue;
            if (!theirs->memory.seesEnemy) continue;
        }
        out->push_back(other);
    }
}

CUnit* AIMemory::NearestEnemy(CUnit* self) {
    CUnit* nearest = nullptr;
    float best = kFar;
    for (auto& node : visible) {
        CUnit* other = node;
        if (!self->IsHostileTo(other) || IsDead(other)) continue;
        double dx = static_cast<double>(self->localPosition_.x) - other->localPosition_.x;
        double dy = static_cast<double>(self->localPosition_.y) - other->localPosition_.y;
        double dz = static_cast<double>(self->localPosition_.z) - other->localPosition_.z;
        double d2 = LengthSquaredUnrounded<Sum3::ZXY>(dx, dy, dz);
        EI_NAN_AUDIT(d2, best);
        if (d2 < best) {
            best = static_cast<float>(d2);
            nearest = other;
        }
    }
    return nearest;
}

void AIMemory::ForceSee(CUnit* self, CUnit* object) {
    if (lib::Contains(visible, object)) return;
    CUnit* other = object;
    CUnitServer* theirs = other->ServerController();
    CUnitServer* ours = self->ServerController();
    double dx = static_cast<double>(self->localPosition_.x) - other->localPosition_.x;
    double dy = static_cast<double>(self->localPosition_.y) - other->localPosition_.y;
    double dz = static_cast<double>(self->localPosition_.z) - other->localPosition_.z;
    float distance = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)));
    AIBrain* theirBrain = &theirs->brain;
    bool attacksUs = (theirBrain->action == AIBrain::kActionAttack || theirBrain->action == AIBrain::kActionCast) &&
                     theirBrain->target == self;
    if (!attacksUs) {
        double sight = static_cast<double>(theirs->moveFactors[2]) * ours->moveFactors[3] * theirs->stats.conspicuity[0].current *
                       ours->stats.perception[0].current;
        double nearValue = static_cast<double>(theirs->stats.conspicuity[2].current) * ours->stats.perception[2].current;
        if (sight > distance && nearValue > distance) return;
    }
    if (IsDead(object)) {
        console::EndLine(kAiLog << "Unit " << self->id_ << " sees corpse of unit " << other->id_);
        seenCorpses.push_back(object);
        if (g_serverWorld.diplomacy_.IsAllied(self->side, other->side)) corpses.push_back(object);
    } else {
        visible.push_front(object);
        kAiLog << "Unit " << self->id_ << " is forced to see unit " << other->id_ << " from ";
        console::EndLine(kAiLog << distance << "m distance");
    }
}

void AIMemory::SeeCorpse(CUnit* self, CUnit* corpse) {
    console::EndLine(kAiLog << "Unit " << self->id_ << " sees corpse of unit " << corpse->id_);
    seenCorpses.push_back(corpse);
    if (g_serverWorld.diplomacy_.IsAllied(self->side, corpse->side)) corpses.push_back(corpse);
}

void AIMemory::MeasureExposure(CUnitServer* self, float* out) {
    for (int i = 0; i < 5; ++i) out[i] = kMaxExposure;
    CUnit* unit = self->unit;
    Vector3 eye;
    unit->EyePoint(&eye);
    for (auto& node : nearby) {
        CUnit* other = node;
        if (IsDead(other) || !unit->IsHostileTo(other)) continue;
        CUnitServer* theirs = other->ServerController();
        double dx = static_cast<double>(other->localPosition_.x) - unit->localPosition_.x;
        double dy = static_cast<double>(other->localPosition_.y) - unit->localPosition_.y;
        double dz = static_cast<double>(other->localPosition_.z) - unit->localPosition_.z;
        float distance = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)));
        float sight = static_cast<float>(static_cast<double>(theirs->moveFactors[3]) * self->moveFactors[2] *
                                   theirs->stats.perception[0].current * self->stats.conspicuity[0].current);
        EI_NAN_AUDIT(distance, sight);
        if (distance < sight) {
            Vector3 theirEye;
            Vector3 to = *other->EyePoint(&theirEye);
            double range = g_serverWorld.aiMap_.LineOfSight(&eye, &to) * sight;
            KeepLower(out[0], Exposure(distance, range));
        }
        KeepLower(out[4], Exposure(distance, static_cast<double>(theirs->stats.perception[2].current) * self->stats.conspicuity[2].current));
        KeepLower(out[1], Exposure(distance, static_cast<double>(theirs->stats.perception[4].current) * self->stats.conspicuity[4].current));
        KeepLower(out[2], Exposure(distance, static_cast<double>(theirs->moveFactors[1]) * self->moveFactors[0] *
                                                 theirs->stats.perception[3].current * self->stats.conspicuity[3].current));
    }
}
