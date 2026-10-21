// The AI's eye (the world server's +0x2838): what a unit sees each brain tick (005CAA00), the
// nearby units (005CA1D0), seeing new ones (005CA5A0) and the eye's height and facing (005CA410).
#include "ai_map.h"
#include "combat.h"
#include "unit_ai.h"

#include "console.h"
#include "map_object.h"
#include "object_diplomacy.h"
#include "originals_ai.h"
#include "originals_library.h"
#include "sector_map.h"
#include "world.h"
#include "game_singletons.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/crt_iostream.h"
#include "ei/x87.h"
#include "lib/nan_audit.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {
constexpr float kOne = 1.0f;                    // 0073FE80 / 0073B84C
constexpr float kZero = 0.0f;                   // 0073B858
constexpr float kNoCone = -1.0f;                // pushed as 0xBF800000: every direction
constexpr float kNoLineOfSight = -1000.0f;      // pushed as 0xC47A0000: below -100, no line-of-sight test
constexpr int kNoGridOrigin = 0x7FFFFFFF;
constexpr float kHalf = 0.5f;                   // 0073B86C
constexpr float kNonPlayerReach = 1.05f;        // 0073FEAC: units without a player keep sight a little longer
constexpr float kEyeHeightFactor = 1.8f;        // 0073EBD0
constexpr float kSightCellsFactor = 2.0f;       // 0073FE8C
constexpr float kSectorScale = 0.03125f;        // 0073FA48
constexpr int kCorpseLevel = 800;
constexpr int kCorpseKind = -3;
constexpr int kLostLevel = 1000;
constexpr int kLostKind = -2;
constexpr std::uint32_t kFlagSeeAll = 0x2000000;    // CUnit::flags_: sees every unit once
constexpr std::uint32_t kFlagNearbyListed = 0x80000;

CUnitServer* ControllerOf(const CUnit* unit) { return reinterpret_cast<CUnitServer*>(unit->controller); }
bool IsDead(CEngineObject* object) { return static_cast<CMapObjectVirtuals*>(object)->IsDead() != 0; }
bool Allied(int side, int other) { return (GameDiplomacy()->allied_[side] & (1u << (other & 31))) != 0; }

// FCOMP 0; TEST AH,0x40: zero.
bool IsZero(double v) {
    EI_NAN_AUDIT(v);
    return v == 0.0;
}

console::GameOStream& PutLong(console::GameOStream& os, long value) {
    return *reinterpret_cast<console::GameOStream*>(reinterpret_cast<libcrt::Ostream*>(&os)->PutInt(value));
}

// "Unit <self> <text> <other>" (no line end).
console::GameOStream& LogPair(CUnit* self, const char* text, CEngineObject* other) {
    return PutLong(PutLong(*AiLog() << "Unit ", self->id_) << text, other->id_);
}

// The noticed point taken over inline (the original's copy of Notice), through CopyPoint2's
// address so its verify wrapper counts it (AIMemory::Update's check relies on that).
void NoticeAt(AIMemory* memory, CUnit* at, int level, int kind) {
    float copy[2];
    const float* point = CopyPoint2(copy, &at->position.x);
    memory->noticedX = point[0];
    memory->noticedLevel = level;
    memory->noticedY = point[1];
    memory->noticedKind = kind;
}

} // namespace

void AIViewer::Prepare(CUnit* self) {
    if (cellX >= 0 && cellY >= 0 && cellX < map->width && cellY < map->height)
        eye.z = static_cast<float>(static_cast<double>(map->cells[cellY][cellX].groundLevel) / map->heightScale + eye.z);
    eye.z = static_cast<float>(static_cast<double>(self->above.z) * kEyeHeightFactor + self->eyeHeight + self->center.z + eye.z);
    Vector3 axis = RotatedAxisY(self->orientation);
    ready = 1;
    facing = -axis;
}

EI_REPLACE_METHOD(0x005CA410, AIViewer::Prepare);

void AIViewer::Refresh(CUnit* self, AIMemory* memory) {
    if (--memory->nearbyCountdown > 0) return;
    self->flags_ |= kFlagNearbyListed;
    memory->nearbyCountdown = (gamecrt::rand() & 3) + 8;
    int blockX = cellX / 32;
    int blockY = cellY / 32;
    CUnitServer* control = ControllerOf(self);
    float hearing = control->stats.valuesB0[2].current;
    float sight = static_cast<float>(static_cast<double>(control->moveFactors[3]) * control->stats.valuesB0[0].current);
    float reach = sight >= hearing ? sight : hearing;
    int count = 0;
    const std::int16_t* offsets =
        memory->sightCells.Get(static_cast<float>(static_cast<double>(reach) * kSightCellsFactor * kSectorScale), &count);
    for (auto* node = memory->nearby.head; node; node = node->next) {
        if (node->data) node->data->Release();
        node->data = nullptr;
    }
    memory->nearby.RemoveAll();
    for (int i = 0; i < count; ++i) {
        AIObjectList* units = map->SectorMap()->UnitsAt(offsets[2 * i] + blockX, offsets[2 * i + 1] + blockY);
        if (!units) continue;
        for (auto* node = units->head; node; node = node->next) {
            CEngineObject* other = node->data;
            if (other == static_cast<CEngineObject*>(self)) continue;
            // At the head, holding a reference (the original's inlined AddHead): See walks the list
            // from the head, so the units of the last sector come first.
            memory->nearby.AddHead(other);
            if (other) ++other->refCount_;
        }
    }
}

EI_REPLACE_METHOD(0x005CA1D0, AIViewer::Refresh);

void AIViewer::See(CUnit* self, AIMemory* memory) {
    CUnitServer* ours = ControllerOf(self);
    bool seesAll = self->player != nullptr || (self->flags_ & kFlagSeeAll) != 0;
    self->flags_ &= ~kFlagSeeAll;
    for (auto* node = memory->nearby.head; node;) {
        auto* other = static_cast<CUnit*>(node->data);
        node = node->next;
        bool dead = IsDead(other);
        bool hostile = self->IsHostileTo(other) || other->player != nullptr;
        if (dead) {
            if (!seesAll && hostile) continue;
            if (memory->seenCorpses.Find(other)) continue;
        } else {
            if (!seesAll && !hostile) continue;
            if (memory->visible.Find(other)) continue;
        }
        if (!ready) Prepare(self);
        Vector3 theirEye;
        static_cast<CMapObjectVirtuals*>(static_cast<CEngineObject*>(other))->EyePoint(&theirEye);
        Vector3 d = theirEye - eye;
        CUnitServer* theirs = ControllerOf(other);
        float coneLimit = ours->stats.sightCone;
        float distance = Length2D({d.x, d.y});
        double sightRaw = static_cast<double>(theirs->moveFactors[2]) * ours->moveFactors[3] * theirs->stats.valuesE0[0].current *
                          ours->stats.valuesB0[0].current;
        float sight = static_cast<float>(sightRaw);
        bool seen = false;
        if (sightRaw >= distance) {
            double along = DotZYX(d, facing);
            EI_NAN_AUDIT(coneLimit, distance, along);
            if (static_cast<double>(coneLimit) * distance <= along) {
                Vector3 to = eye + d;
                seen = map->LineOfSight(&eye, &to) * sight > distance;
            }
        }
        bool alive = !dead;
        if (!seen) {
            float behind = DotZYX(d, facing);
            if ((std::bit_cast<std::uint32_t>(behind) & 0x80000000u) == 0 && ours->stats.peripheralRange > distance) {
                console::EndLine(LogPair(self, " sees unit ", other) << " with peripheral vision");
                seen = true;
            } else {
                if (dead) continue;
                EI_NAN_AUDIT(theirs->stats.valuesE0[2].current, ours->stats.valuesB0[2].current, distance);
                if (static_cast<double>(theirs->stats.valuesE0[2].current) * ours->stats.valuesB0[2].current <= distance) continue;
                *AiLog() << "using sense life ";
                alive = true;
            }
        }
        if (seen && dead) {
            console::EndLine(LogPair(self, " sees corpse of unit ", other));
            AddCountedTail(&memory->seenCorpses, nullptr, other);
            if (Allied(self->side, other->side)) AddCountedTail(&memory->corpses, nullptr, other);
            continue;
        }
        if (alive) {
            memory->visible.AddHead(other);
            ++other->refCount_;
            double dx = static_cast<double>(self->position.x) - other->position.x;
            double dy = static_cast<double>(self->position.y) - other->position.y;
            double dz = static_cast<double>(self->position.z) - other->position.z;
            console::GameOStream& os = LogPair(self, " sees unit ", other) << " from ";
            (void)os;
            console::EndLine(AiLog()->PutFloat(std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz))) << "m distance");
        }
    }
}

EI_REPLACE_METHOD(0x005CA5A0, AIViewer::See);

void AIViewer::Update(CUnitServer* owner, AIMemory* memory) {
    CUnit* self = owner->unit;
    bool hasPlayer = self->player != nullptr;
    float reachFactor = hasPlayer ? kOne : kNonPlayerReach;
    if (!self) return;
    ready = 0;
    eye = *&self->position;
    float scale = static_cast<float>(kOne / (static_cast<double>(kOne) * kHalf));
    cellX = x87::RoundToInt(static_cast<float>(scale * static_cast<double>(eye.x) - kHalf));
    cellY = x87::RoundToInt(static_cast<float>(scale * static_cast<double>(eye.y) - kHalf));
    Refresh(self, memory);
    for (auto* node = memory->visible.head; node;) {
        auto* current = node;
        node = node->next;
        auto* other = static_cast<CUnit*>(current->data);
        Vector3 d{static_cast<float>(static_cast<double>(other->position.x) - self->position.x),
                  static_cast<float>(static_cast<double>(other->position.y) - self->position.y),
                  static_cast<float>(static_cast<double>(other->position.z) - self->position.z)};
        float distance = LengthZYX(d);
        if (IsDead(other)) {
            if (Allied(self->side, other->side)) {
                // The original's inlined AddTail (a second copy follows below).
                memory->seenCorpses.AddTail(other);
                ++other->refCount_;
                if (memory->noticedLevel < kCorpseLevel) NoticeAt(memory, other, kCorpseLevel, kCorpseKind);
            }
            console::EndLine(LogPair(self, " sees corpse of unit ", other));
            AddCountedTail(&memory->seenCorpses, nullptr, other);
            if (Allied(self->side, other->side)) AddCountedTail(&memory->corpses, nullptr, other);
        } else {
            if (self->IsHostileTo(other) && !hasPlayer) continue;
            CUnitServer* theirs = ControllerOf(other);
            AIBrain* theirBrain = theirs->Brain();
            if (theirs->Busy() && theirBrain->target == static_cast<CEngineObject*>(self)) continue;
            double sight = static_cast<double>(theirs->moveFactors[2]) * owner->moveFactors[3] * theirs->stats.valuesE0[0].current *
                           owner->stats.valuesB0[0].current * reachFactor;
            if (sight > distance) continue;
            double sense = static_cast<double>(theirs->stats.valuesE0[2].current) * owner->stats.valuesB0[2].current * reachFactor;
            if (sense > distance) continue;
            if (theirBrain->AggressionEnemy() == static_cast<CEngineObject*>(self)) continue;
        }
        console::EndLine(LogPair(self, " lost from sight unit ", other));
        if (self->IsHostileTo(other) && memory->noticedLevel < kLostLevel) NoticeAt(memory, other, kLostLevel, kLostKind);
        if (current->data) current->data->Release();
        current->data = nullptr;
        memory->visible.RemoveAt(current);
    }
    See(self, memory);
}

EI_REPLACE_METHOD(0x005CAA00, AIViewer::Update);

void __fastcall MarkSightCells(CUnit* seer, CUnit* observer, VisibilityGrid* grid) {
    Vector3 axis;
    seer->orientation.AxisY(&axis);
    Vector3 look{-axis.x, -axis.y, -axis.z};
    Vector3 eye;
    static_cast<CMapObjectVirtuals*>(static_cast<CEngineObject*>(seer))->EyePoint(&eye);
    grid->originX = kNoGridOrigin;
    grid->originY = kNoGridOrigin;
    reinterpret_cast<SearchGrid<std::uint8_t>*>(&grid->data)->Resize(1, 1);   // the cells (+8) as a SearchGrid
    std::memset(grid->data, 0, static_cast<std::size_t>(grid->height * grid->width));

    CUnitServer* mine = ControllerOf(seer);
    CUnitServer* theirs = ControllerOf(observer);
    float cone = mine->stats.sightCone;
    float sight = static_cast<float>(static_cast<double>(theirs->moveFactors[2]) * mine->moveFactors[3] *
                               theirs->stats.valuesE0[0].current * mine->stats.valuesB0[0].current);
    // The larger of 0 and the sight.
    EI_NAN_AUDIT(sight);
    float radius = kZero < sight ? sight : kZero;
    float height = static_cast<float>(observer->ModeSpeed());
    CAIMap* map = TheServerWorld()->AIMap();
    map->MarkVisibleCells(&eye, radius, &look, cone, height, grid);
    double hearing = static_cast<double>(mine->stats.valuesB0[2].current) * theirs->stats.valuesE0[2].current;
    if (!IsZero(hearing)) map->MarkVisibleCells(&eye, static_cast<float>(hearing), &look, kNoCone, kNoLineOfSight, grid);
    float peripheral = mine->stats.peripheralRange;
    if (!IsZero(peripheral)) map->MarkVisibleCells(&eye, peripheral, &look, kZero, kNoLineOfSight, grid);
}
EI_REPLACE(0x005CD210, MarkSightCells);
