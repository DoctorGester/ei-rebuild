// The AI's eye (the world server's aiEvents_): what a unit sees each brain tick, the
// nearby units, seeing new ones and the eye's height and facing.
#include "ai_grids.h"
#include "ai_map.h"
#include "combat.h"
#include "console.h"
#include "effects.h"
#include "game_singletons.h"
#include "map_object.h"
#include "object_diplomacy.h"
#include "sector_map.h"
#include "unit_ai.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"

#include "ei/x87.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <list>

namespace {
constexpr float kNoCone = -1.0f;                // every direction
constexpr float kNoLineOfSight = -1000.0f;      // below -100, no line-of-sight test
constexpr int kNoGridOrigin = 0x7FFFFFFF;
constexpr float kNonPlayerReach = 1.05f;        // units without a player keep sight a little longer
constexpr float kEyeHeightFactor = 1.8f;
constexpr float kSightCellsFactor = 2.0f;
constexpr float kSectorScale = 0.03125f;
constexpr int kCorpseLevel = 800;
constexpr int kCorpseKind = -3;
constexpr int kLostLevel = 1000;
constexpr int kLostKind = -2;
constexpr std::uint32_t kFlagNearbyListed = 0x80000;

bool IsDead(CMapObject* object) { return object->IsDead(); }

bool IsZero(double v) {
    EI_NAN_AUDIT(v);
    return v == 0.0;
}

// "Unit <self> <text> <other>" (no line end).
console::Stream& LogPair(CUnit* self, const char* text, CEngineObject* other) {
    return kAiLog << "Unit " << self->id_ << text << other->id_;
}

// The noticed point taken over inline (Notice's copy).
void NoticeAt(AIMemory* memory, CUnit* at, int level, int kind) {
    float copy[2];
    const float* point = CopyPoint2(copy, &at->localPosition_.x);
    memory->noticedX = point[0];
    memory->noticedLevel = level;
    memory->noticedY = point[1];
    memory->noticedKind = kind;
}

} // namespace

void AIViewer::Prepare(CUnit* self) {
    if (cellX >= 0 && cellY >= 0 && cellX < map->cells.width && cellY < map->cells.height)
        eye.z = static_cast<float>(static_cast<double>(map->cells[cellY][cellX].groundLevel) / map->heightScale + eye.z);
    eye.z = static_cast<float>(static_cast<double>(self->bounds.boxMax.z) * kEyeHeightFactor + self->eyeHeight + self->bounds.center.z + eye.z);
    Vector3 axis = RotatedAxisY(self->localRotation_);
    ready = 1;
    facing = -axis;
}

void AIViewer::Refresh(CUnit* self, AIMemory* memory) {
    if (--memory->nearbyCountdown > 0) return;
    self->flags_ |= kFlagNearbyListed;
    memory->nearbyCountdown = (libcrt::Rand() & 3) + 8;
    int blockX = cellX / 32;
    int blockY = cellY / 32;
    CUnitServer* control = self->ServerController();
    float hearing = control->stats.perception[2].current;
    float sight = static_cast<float>(static_cast<double>(control->moveFactors[3]) * control->stats.perception[0].current);
    float reach = sight >= hearing ? sight : hearing;
    int count = 0;
    const std::int16_t* offsets =
        memory->sightCells.Get(static_cast<float>(static_cast<double>(reach) * kSightCellsFactor * kSectorScale), &count);
    memory->nearby.clear();
    for (int i = 0; i < count; ++i) {
        std::list<CUnit*>* units = map->sectorMap.UnitsAt(offsets[2 * i] + blockX, offsets[2 * i + 1] + blockY);
        if (!units) continue;
        for (auto node = units->begin(); node != units->end(); ++node) {
            CUnit* other = *node;
            if (other == self) continue;
            // At the head, holding a reference: See walks the list
            // from the head, so the units of the last sector come first.
            memory->nearby.push_front(other);
        }
    }
}

void AIViewer::See(CUnit* self, AIMemory* memory) {
    CUnitServer* ours = self->ServerController();
    bool seesAll = self->player != nullptr || (self->flags_ & MapObjectFlag::SeeAll) != 0;
    self->flags_ &= ~MapObjectFlag::SeeAll;
    for (auto node = memory->nearby.begin(); node != memory->nearby.end();) {
        CUnit* other = *node;
        ++node;
        bool dead = IsDead(other);
        bool hostile = self->IsHostileTo(other) || other->player != nullptr;
        if (dead) {
            if (!seesAll && hostile) continue;
            if (lib::Contains(memory->seenCorpses, other)) continue;
        } else {
            if (!seesAll && !hostile) continue;
            if (lib::Contains(memory->visible, other)) continue;
        }
        if (!ready) Prepare(self);
        Vector3 theirEye;
        other->EyePoint(&theirEye);
        Vector3 d = theirEye - eye;
        CUnitServer* theirs = other->ServerController();
        float coneLimit = ours->stats.sightCone;
        float distance = Length2D({d.x, d.y});
        double sightRaw = static_cast<double>(theirs->moveFactors[2]) * ours->moveFactors[3] * theirs->stats.conspicuity[0].current *
                          ours->stats.perception[0].current;
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
            if (!SignBit(behind) && ours->stats.peripheralRange > distance) {
                console::EndLine(LogPair(self, " sees unit ", other) << " with peripheral vision");
                seen = true;
            } else {
                if (dead) continue;
                EI_NAN_AUDIT(theirs->stats.conspicuity[2].current, ours->stats.perception[2].current, distance);
                if (static_cast<double>(theirs->stats.conspicuity[2].current) * ours->stats.perception[2].current <= distance) continue;
                kAiLog << "using sense life ";
                alive = true;
            }
        }
        if (seen && dead) {
            console::EndLine(LogPair(self, " sees corpse of unit ", other));
            memory->seenCorpses.push_back(other);
            if (g_serverWorld.diplomacy_.IsAllied(self->side, other->side)) memory->corpses.push_back(other);
            continue;
        }
        if (alive) {
            memory->visible.push_front(other);
            double dx = static_cast<double>(self->localPosition_.x) - other->localPosition_.x;
            double dy = static_cast<double>(self->localPosition_.y) - other->localPosition_.y;
            double dz = static_cast<double>(self->localPosition_.z) - other->localPosition_.z;
            LogPair(self, " sees unit ", other) << " from ";
            console::EndLine(kAiLog << std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)) << "m distance");
        }
    }
}

void AIViewer::Update(CUnitServer* owner, AIMemory* memory) {
    CUnit* self = owner->unit;
    bool hasPlayer = self->player != nullptr;
    float reachFactor = hasPlayer ? 1.0f : kNonPlayerReach;
    if (!self) return;
    ready = 0;
    eye = *&self->localPosition_;
    float scale = 2.0f;
    cellX = x87::RoundToInt(static_cast<float>(scale * static_cast<double>(eye.x) - 0.5f));
    cellY = x87::RoundToInt(static_cast<float>(scale * static_cast<double>(eye.y) - 0.5f));
    Refresh(self, memory);
    for (auto node = memory->visible.begin(); node != memory->visible.end();) {
        auto current = node;
        ++node;
        CUnit* other = *current;
        Vector3 d{static_cast<float>(static_cast<double>(other->localPosition_.x) - self->localPosition_.x),
                  static_cast<float>(static_cast<double>(other->localPosition_.y) - self->localPosition_.y),
                  static_cast<float>(static_cast<double>(other->localPosition_.z) - self->localPosition_.z)};
        float distance = LengthZYX(d);
        if (IsDead(other)) {
            if (g_serverWorld.diplomacy_.IsAllied(self->side, other->side)) {
                // AddTail (a second copy follows below).
                memory->seenCorpses.push_back(other);
                if (memory->noticedLevel < kCorpseLevel) NoticeAt(memory, other, kCorpseLevel, kCorpseKind);
            }
            console::EndLine(LogPair(self, " sees corpse of unit ", other));
            memory->seenCorpses.push_back(other);
            if (g_serverWorld.diplomacy_.IsAllied(self->side, other->side)) memory->corpses.push_back(other);
        } else {
            if (self->IsHostileTo(other) && !hasPlayer) continue;
            CUnitServer* theirs = other->ServerController();
            AIBrain* theirBrain = &theirs->brain;
            if (theirs->moveMode2.onGuard && theirBrain->target == self) continue;
            double sight = static_cast<double>(theirs->moveFactors[2]) * owner->moveFactors[3] * theirs->stats.conspicuity[0].current *
                           owner->stats.perception[0].current * reachFactor;
            if (sight > distance) continue;
            double sense = static_cast<double>(theirs->stats.conspicuity[2].current) * owner->stats.perception[2].current * reachFactor;
            if (sense > distance) continue;
            if (theirBrain->AggressionEnemy() == self) continue;
        }
        console::EndLine(LogPair(self, " lost from sight unit ", other));
        if (self->IsHostileTo(other) && memory->noticedLevel < kLostLevel) NoticeAt(memory, other, kLostLevel, kLostKind);
        memory->visible.erase(current);
    }
    See(self, memory);
}

void MarkSightCells(CUnit* seer, CUnit* observer, FogGrid* grid) {
    Vector3 axis;
    seer->localRotation_.AxisY(&axis);
    Vector3 look{-axis.x, -axis.y, -axis.z};
    Vector3 eye;
    seer->EyePoint(&eye);
    grid->x0 = kNoGridOrigin;
    grid->y0 = kNoGridOrigin;
    ResizeGrid(grid->cells, 1, 1);
    std::memset(grid->cells.data, 0, static_cast<std::size_t>(grid->cells.height * grid->cells.width));

    CUnitServer* mine = seer->ServerController();
    CUnitServer* theirs = observer->ServerController();
    float cone = mine->stats.sightCone;
    float sight = static_cast<float>(static_cast<double>(theirs->moveFactors[2]) * mine->moveFactors[3] *
                               theirs->stats.conspicuity[0].current * mine->stats.perception[0].current);
    // The larger of 0 and the sight.
    EI_NAN_AUDIT(sight);
    float radius = 0.0f < sight ? sight : 0.0f;
    float height = static_cast<float>(observer->ModeSpeed());
    CAIMap* map = &g_serverWorld.aiMap_;
    map->MarkVisibleCells(&eye, radius, &look, cone, height, grid);
    double hearing = static_cast<double>(mine->stats.perception[2].current) * theirs->stats.conspicuity[2].current;
    if (!IsZero(hearing)) map->MarkVisibleCells(&eye, static_cast<float>(hearing), &look, kNoCone, kNoLineOfSight, grid);
    float peripheral = mine->stats.peripheralRange;
    if (!IsZero(peripheral)) map->MarkVisibleCells(&eye, peripheral, &look, 0.0f, kNoLineOfSight, grid);
}
