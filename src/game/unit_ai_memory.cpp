// The perception memory's queries over the visible units (005CB260, 005CB480, 005CB670), forcing
// an object into sight (005CBC80) and the counted lists' AddTail (005CD500).
#include "unit_ai.h"

#include "console.h"
#include "map_object.h"
#include "object_diplomacy.h"
#include "originals_library.h"

#include "ei/hook.h"

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

void __fastcall ForceSeeHook(std::uint8_t* memory, void*, CUnit* self, CEngineObject* object) {
    BrainOfMemory(memory)->ForceSee(self, object);
}
EI_REPLACE(0x005CBC80, ForceSeeHook);
