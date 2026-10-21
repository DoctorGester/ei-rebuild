// The perception memory's queries over the visible units (005CB260, 005CB480, 005CB670), the
// exposure to hostile units (005CB850), corpses seen (005CBE90), forcing
// an object into sight (005CBC80) and the counted lists' AddTail (005CD500).
#include "ai_map.h"
#include "combat.h"
#include "unit_ai.h"

#include "console.h"
#include "map_object.h"
#include "object_diplomacy.h"
#include "originals_ai.h"
#include "originals_library.h"
#include "world.h"
#include "game_singletons.h"

#include "ei/hook.h"
#include "lib/nan_audit.h"
#include "lib/crt_iostream.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace {
constexpr float kZero = 0.0f;                   // 0073B858
const float kFar = 1e30f;                       // 7149F2CA

bool IsDead(CEngineObject* object) { return static_cast<CMapObjectVirtuals*>(object)->IsDead() != 0; }
bool Allied(int side, int other) { return (GameDiplomacy()->allied_[side] & (1u << (other & 31))) != 0; }

// (a - b) squared in 3D, summed as the original: z, then x, then y.
double Distance2(const CUnit* a, const float* b) {
    double dx = static_cast<double>(a->position.x) - b[0];
    double dy = static_cast<double>(a->position.y) - b[1];
    double dz = static_cast<double>(a->position.z) - b[2];
    return LengthSquaredUnrounded<Sum3::ZXY>(dx, dy, dz);
}

console::GameOStream& PutLong(console::GameOStream& os, long value) {
    return *reinterpret_cast<console::GameOStream*>(reinterpret_cast<libcrt::Ostream*>(&os)->PutInt(value));
}
} // namespace

AIObjectList::Node* __fastcall AddCountedTail(AIObjectList* list, void*, CUnit* object) {
    AIObjectList::Node* node = list->AddTail(object);
    if (object) ++object->refCount_;
    return node;
}
EI_REWRITTEN(0x005CD500, AddCountedTail);

void AIMemory::FindEnemies(CUnit* self, AIObjectList* out) {
    out->RemoveAll();
    for (auto* node = visible.head; node; node = node->next) {
        auto* other = node->data;
        if (!self->IsHostileTo(other) || IsDead(other) || reports.Contains(other)) continue;
        out->AddTail(other);
    }
}

EI_REWRITTEN(0x005CB260, AIMemory::FindEnemies);

void AIMemory::FindEnemiesNear(CUnit* self, AIObjectList* out, const Vector3* point, float radius) {
    out->RemoveAll();
    float radius2 = radius > kZero ? static_cast<float>(static_cast<double>(radius) * radius) : -1.0f;
    for (auto* node = visible.head; node; node = node->next) {
        auto* other = node->data;
        if (!other->IsHostileTo(self) || IsDead(other) || reports.Contains(other)) continue;
        if (Distance2(other, &point->x) >= radius2) {
            AIBrain* theirs = other->ServerController()->Brain();
            auto* mode = static_cast<AggressionMode*>(theirs->FindMode(kModeAggression));
            if (!mode || (mode->fixed && !mode->enemy)) continue;
            auto* enemy = static_cast<CUnit*>(theirs->AggressionEnemy());
            if (enemy && enemy->GetClassId() == kClassUnit && !Allied(self->side, enemy->side)) continue;
            if (!theirs->memory.seesEnemy) continue;
        }
        out->AddTail(other);
    }
}

EI_REWRITTEN(0x005CB480, AIMemory::FindEnemiesNear);

CUnit* AIMemory::NearestEnemy(CUnit* self) {
    CUnit* nearest = nullptr;
    float best = kFar;
    for (auto* node = visible.head; node; node = node->next) {
        auto* other = node->data;
        if (!self->IsHostileTo(other) || IsDead(other)) continue;
        double dx = static_cast<double>(self->position.x) - other->position.x;
        double dy = static_cast<double>(self->position.y) - other->position.y;
        double dz = static_cast<double>(self->position.z) - other->position.z;
        double d2 = LengthSquaredUnrounded<Sum3::ZXY>(dx, dy, dz);
        EI_NAN_AUDIT(d2, best);
        if (d2 < best) {
            best = static_cast<float>(d2);
            nearest = other;
        }
    }
    return nearest;
}

EI_REWRITTEN(0x005CB670, AIMemory::NearestEnemy);

void AIMemory::ForceSee(CUnit* self, CUnit* object) {
    if (visible.Find(object)) return;
    CUnit* other = object;
    CUnitServer* theirs = other->ServerController();
    CUnitServer* ours = self->ServerController();
    double dx = static_cast<double>(self->position.x) - other->position.x;
    double dy = static_cast<double>(self->position.y) - other->position.y;
    double dz = static_cast<double>(self->position.z) - other->position.z;
    float distance = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)));
    AIBrain* theirBrain = theirs->Brain();
    bool attacksUs = (theirBrain->action == AIBrain::kActionAttack || theirBrain->action == AIBrain::kActionCast) &&
                     theirBrain->target == self;
    if (!attacksUs) {
        double sight = static_cast<double>(theirs->moveFactors[2]) * ours->moveFactors[3] * theirs->stats.valuesE0[0].current *
                       ours->stats.valuesB0[0].current;
        double nearValue = static_cast<double>(theirs->stats.valuesE0[2].current) * ours->stats.valuesB0[2].current;
        if (sight > distance && nearValue > distance) return;
    }
    if (IsDead(object)) {
        console::GameOStream& os = PutLong(*AiLog() << "Unit ", self->id_) << " sees corpse of unit ";
        console::EndLine(PutLong(os, other->id_));
        AddCountedTail(&seenCorpses, nullptr, object);
        if (Allied(self->side, other->side)) AddCountedTail(&corpses, nullptr, object);
    } else {
        visible.AddHead(object);
        ++object->refCount_;
        console::GameOStream& os = PutLong(*AiLog() << "Unit ", self->id_) << " is forced to see unit ";
        PutLong(os, other->id_) << " from ";
        console::EndLine(AiLog()->PutFloat(distance) << "m distance");
    }
}
EI_REWRITTEN(0x005CBC80, AIMemory::ForceSee);

void AIMemory::SeeCorpse(CUnit* self, CUnit* corpse) {
    console::GameOStream& os = PutLong(*AiLog() << "Unit ", self->id_) << " sees corpse of unit ";
    console::EndLine(PutLong(os, corpse->id_));
    AddCountedTail(&seenCorpses, nullptr, corpse);
    if (Allied(self->side, corpse->side)) AddCountedTail(&corpses, nullptr, corpse);
}

EI_REWRITTEN(0x005CBE90, AIMemory::SeeCorpse);

namespace {
constexpr float kMaxExposure = 3.0f;            // 0073FE90

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

void AIMemory::MeasureExposure(CUnitServer* self, float* out) {
    for (int i = 0; i < 5; ++i) out[i] = kMaxExposure;
    CUnit* unit = self->unit;
    Vector3 eye;
    unit->EyePoint(&eye);
    for (auto* node = nearby.head; node; node = node->next) {
        auto* other = node->data;
        if (IsDead(other) || !unit->IsHostileTo(other)) continue;
        CUnitServer* theirs = other->ServerController();
        double dx = static_cast<double>(other->position.x) - unit->position.x;
        double dy = static_cast<double>(other->position.y) - unit->position.y;
        double dz = static_cast<double>(other->position.z) - unit->position.z;
        float distance = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)));
        float sight = static_cast<float>(static_cast<double>(theirs->moveFactors[3]) * self->moveFactors[2] *
                                   theirs->stats.valuesB0[0].current * self->stats.valuesE0[0].current);
        EI_NAN_AUDIT(distance, sight);
        if (distance < sight) {
            Vector3 theirEye;
            Vector3 to = *other->EyePoint(&theirEye);
            double range = TheServerWorld()->AIMap()->LineOfSight(&eye, &to) * sight;
            KeepLower(out[0], Exposure(distance, range));
        }
        KeepLower(out[4], Exposure(distance, static_cast<double>(theirs->stats.valuesB0[2].current) * self->stats.valuesE0[2].current));
        KeepLower(out[1], Exposure(distance, static_cast<double>(theirs->stats.valuesB0[4].current) * self->stats.valuesE0[4].current));
        KeepLower(out[2], Exposure(distance, static_cast<double>(theirs->moveFactors[1]) * self->moveFactors[0] *
                                                 theirs->stats.valuesB0[3].current * self->stats.valuesE0[3].current));
    }
}

EI_REWRITTEN(0x005CB850, AIMemory::MeasureExposure);
