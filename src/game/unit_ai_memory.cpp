// The perception memory's queries over the visible units (005CB260, 005CB480, 005CB670), the
// exposure to hostile units (005CB850), corpses seen (005CBE90), forcing
// an object into sight (005CBC80) and the counted lists' AddTail (005CD500).
#include "unit_ai.h"

#include "console.h"
#include "map_object.h"
#include "object_diplomacy.h"
#include "originals_ai.h"
#include "originals_library.h"
#include "world.h"

#include "ei/hook.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace {
constexpr float kZero = 0.0f;                   // 0073B858
const float kFar = 1e30f;                       // 7149F2CA

AIBrain* BrainOfMemory(std::uint8_t* memory) { return reinterpret_cast<AIBrain*>(memory - offsetof(AIBrain, memory)); }
UnitController* ControllerOf(const CUnit* unit) { return reinterpret_cast<UnitController*>(unit->controller); }
bool IsDead(CEngineObject* object) { return static_cast<CMapObjectVirtuals*>(object)->IsDead() != 0; }
bool Allied(int side, int other) { return (GameDiplomacy()->allied_[side] & (1u << (other & 31))) != 0; }

float RoundToFloat(double v) {
    volatile float r = static_cast<float>(v);
    return r;
}

// (a - b) squared in 3D, summed as the original: z, then x, then y.
double Distance2(const CUnit* a, const float* b) {
    double dx = static_cast<double>(a->positionX) - b[0];
    double dy = static_cast<double>(a->positionY) - b[1];
    double dz = static_cast<double>(a->positionZ) - b[2];
    return dz * dz + dx * dx + dy * dy;
}

console::GameOStream& PutLong(console::GameOStream& os, long value) {
    return *static_cast<console::GameOStream*>(OstreamPutLongOriginal(&os, nullptr, value));
}
} // namespace

AIObjectList::Node* __fastcall AddCountedTail(AIObjectList* list, void*, CEngineObject* object) {
    AIObjectList::Node* node = list->AddTail(object);
    if (object) ++object->refCount_;
    return node;
}
EI_REPLACE(0x005CD500, AddCountedTail);

void AIBrain::FindEnemies(CUnit* self, AIObjectList* out) {
    out->RemoveAll();
    for (auto* node = visible.head; node; node = node->next) {
        auto* other = static_cast<CUnit*>(node->data);
        if (!self->IsHostileTo(other) || IsDead(other) || reports.Contains(other)) continue;
        out->AddTail(other);
    }
}

void __fastcall FindEnemiesHook(std::uint8_t* memory, void*, CUnit* self, AIObjectList* out) {
    BrainOfMemory(memory)->FindEnemies(self, out);
}
EI_REPLACE(0x005CB260, FindEnemiesHook);

void AIBrain::FindEnemiesNear(CUnit* self, AIObjectList* out, const Vector3* point, float radius) {
    out->RemoveAll();
    float radius2 = radius > kZero ? RoundToFloat(static_cast<double>(radius) * radius) : -1.0f;
    for (auto* node = visible.head; node; node = node->next) {
        auto* other = static_cast<CUnit*>(node->data);
        if (!other->IsHostileTo(self) || IsDead(other) || reports.Contains(other)) continue;
        if (Distance2(other, &point->x) >= radius2) {
            AIBrain* theirs = ControllerOf(other)->Brain();
            auto* mode = static_cast<AggressionMode*>(theirs->FindMode(kModeAggression));
            if (!mode || (mode->fixed && !mode->enemy)) continue;
            auto* enemy = static_cast<CUnit*>(theirs->AggressionEnemy());
            if (enemy && enemy->GetClassId() == kClassUnit && !Allied(self->side, enemy->side)) continue;
            if (!theirs->seesEnemy) continue;
        }
        out->AddTail(other);
    }
}

void __fastcall FindEnemiesNearHook(std::uint8_t* memory, void*, CUnit* self, AIObjectList* out, const Vector3* point,
                                    float radius) {
    BrainOfMemory(memory)->FindEnemiesNear(self, out, point, radius);
}
EI_REPLACE(0x005CB480, FindEnemiesNearHook);

CUnit* AIBrain::NearestEnemy(CUnit* self) {
    CUnit* nearest = nullptr;
    float best = kFar;
    for (auto* node = visible.head; node; node = node->next) {
        auto* other = static_cast<CUnit*>(node->data);
        if (!self->IsHostileTo(other) || IsDead(other)) continue;
        double dx = static_cast<double>(self->positionX) - other->positionX;
        double dy = static_cast<double>(self->positionY) - other->positionY;
        double dz = static_cast<double>(self->positionZ) - other->positionZ;
        double d2 = dz * dz + dx * dx + dy * dy;
        if (!(d2 >= best)) {
            best = RoundToFloat(d2);
            nearest = other;
        }
    }
    return nearest;
}

CUnit* __fastcall NearestEnemyHook(std::uint8_t* memory, void*, CUnit* self) { return BrainOfMemory(memory)->NearestEnemy(self); }
EI_REPLACE(0x005CB670, NearestEnemyHook);

void AIBrain::ForceSee(CUnit* self, CEngineObject* object) {
    if (visible.Find(object)) return;
    auto* other = static_cast<CUnit*>(object);
    UnitController* theirs = ControllerOf(other);
    UnitController* ours = ControllerOf(self);
    double dx = static_cast<double>(self->positionX) - other->positionX;
    double dy = static_cast<double>(self->positionY) - other->positionY;
    double dz = static_cast<double>(self->positionZ) - other->positionZ;
    float distance = RoundToFloat(std::sqrt(dz * dz + dy * dy + dx * dx));
    AIBrain* theirBrain = theirs->Brain();
    bool attacksUs = (theirBrain->action == kActionAttack || theirBrain->action == kActionCast) &&
                     theirBrain->target == static_cast<CEngineObject*>(self);
    if (!attacksUs) {
        double sight = static_cast<double>(theirs->moveFactors[2]) * ours->moveFactors[3] * theirs->stats.valuesE0[0].current *
                       ours->stats.valuesB0[0].current;
        double near = static_cast<double>(theirs->stats.valuesE0[2].current) * ours->stats.valuesB0[2].current;
        if (sight > distance && near > distance) return;
    }
    if (IsDead(object)) {
        console::GameOStream& os = PutLong(*AiLog() << "Unit ", self->id_) << " sees corpse of unit ";
        console::EndLine(PutLong(os, other->id_));
        AddCountedTail(&unknown_12C, nullptr, object);
        if (Allied(self->side, other->side)) AddCountedTail(&corpses, nullptr, object);
    } else {
        visible.AddHead(object);
        ++object->refCount_;
        console::GameOStream& os = PutLong(*AiLog() << "Unit ", self->id_) << " is forced to see unit ";
        PutLong(os, other->id_) << " from ";
        console::EndLine(AiLog()->PutFloat(distance) << "m distance");
    }
}

void AIBrain::SeeCorpse(CUnit* self, CEngineObject* corpse) {
    console::GameOStream& os = PutLong(*AiLog() << "Unit ", self->id_) << " sees corpse of unit ";
    console::EndLine(PutLong(os, corpse->id_));
    AddCountedTail(&unknown_12C, nullptr, corpse);
    if (Allied(self->side, static_cast<CUnit*>(corpse)->side)) AddCountedTail(&corpses, nullptr, corpse);
}

void __fastcall SeeCorpseHook(std::uint8_t* memory, void*, CUnit* self, CEngineObject* corpse) {
    BrainOfMemory(memory)->SeeCorpse(self, corpse);
}
EI_REPLACE(0x005CBE90, SeeCorpseHook);

namespace {
constexpr float kMaxExposure = 3.0f;            // 0073FE90

bool IsNaN(double v) {
    auto bits = std::bit_cast<std::uint64_t>(v);
    return (bits & 0x7FF0000000000000ull) == 0x7FF0000000000000ull && (bits & 0x000FFFFFFFFFFFFFull) != 0;
}

// distance / range, at most 3 (NaN kept); 3 for a zero (or NaN) range.
float Exposure(float distance, double range) {
    if (range == 0.0 || IsNaN(range)) return kMaxExposure;
    float ratio = RoundToFloat(distance / range);
    return !(ratio >= kMaxExposure) ? ratio : kMaxExposure;
}

// The smaller of the two; NaN `value` wins.
void KeepLower(float& slot, float value) { slot = !(value >= slot) ? value : slot; }
} // namespace

void AIBrain::MeasureExposure(UnitController* self, float* out) {
    for (int i = 0; i < 5; ++i) out[i] = kMaxExposure;
    CUnit* unit = self->unit;
    Vector3 eye;
    static_cast<CMapObjectVirtuals*>(static_cast<CEngineObject*>(unit))->EyePoint(&eye);
    for (auto* node = unknown_0F4.head; node; node = node->next) {
        auto* other = static_cast<CUnit*>(node->data);
        if (IsDead(other) || !unit->IsHostileTo(other)) continue;
        UnitController* theirs = ControllerOf(other);
        double dx = static_cast<double>(other->positionX) - unit->positionX;
        double dy = static_cast<double>(other->positionY) - unit->positionY;
        double dz = static_cast<double>(other->positionZ) - unit->positionZ;
        float distance = RoundToFloat(std::sqrt(dz * dz + dy * dy + dx * dx));
        float sight = RoundToFloat(static_cast<double>(theirs->moveFactors[3]) * self->moveFactors[2] *
                                   theirs->stats.valuesB0[0].current * self->stats.valuesE0[0].current);
        if (!(distance >= sight)) {
            Vector3 theirEye;
            Vector3 to = *static_cast<CMapObjectVirtuals*>(static_cast<CEngineObject*>(other))->EyePoint(&theirEye);
            double range = LineOfSightOriginal(WorldServer()->AIMap(), nullptr, &eye, &to) * sight;
            KeepLower(out[0], Exposure(distance, range));
        }
        KeepLower(out[4], Exposure(distance, static_cast<double>(theirs->stats.valuesB0[2].current) * self->stats.valuesE0[2].current));
        KeepLower(out[1], Exposure(distance, static_cast<double>(theirs->stats.valuesB0[4].current) * self->stats.valuesE0[4].current));
        KeepLower(out[2], Exposure(distance, static_cast<double>(theirs->moveFactors[1]) * self->moveFactors[0] *
                                                 theirs->stats.valuesB0[3].current * self->stats.valuesE0[3].current));
    }
}

void __fastcall MeasureExposureHook(std::uint8_t* memory, void*, UnitController* self, float* out) {
    BrainOfMemory(memory)->MeasureExposure(self, out);
}
EI_REPLACE(0x005CB850, MeasureExposureHook);

void __fastcall ForceSeeHook(std::uint8_t* memory, void*, CUnit* self, CEngineObject* object) {
    BrainOfMemory(memory)->ForceSee(self, object);
}
EI_REPLACE(0x005CBC80, ForceSeeHook);
