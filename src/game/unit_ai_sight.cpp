// The AI's eye (the world server's +0x2838): what a unit sees each brain tick (005CAA00), the
// nearby units (005CA1D0), seeing new ones (005CA5A0) and the eye's height and facing (005CA410).
#include "unit_ai.h"

#include "console.h"
#include "map_object.h"
#include "object_diplomacy.h"
#include "originals_ai.h"
#include "originals_library.h"
#include "sector_map.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "ei/x87.h"

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

AIBrain* BrainOfMemory(std::uint8_t* memory) { return reinterpret_cast<AIBrain*>(memory - offsetof(AIBrain, memory)); }
UnitController* ControllerOf(const CUnit* unit) { return reinterpret_cast<UnitController*>(unit->controller); }
bool IsDead(CEngineObject* object) { return static_cast<CMapObjectVirtuals*>(object)->IsDead() != 0; }
bool Allied(int side, int other) { return (GameDiplomacy()->allied_[side] & (1u << (other & 31))) != 0; }

// FCOMP 0; TEST AH,0x40: zero or unordered.
bool ZeroOrUnordered(double v) { return x87::IsNaN(static_cast<float>(v)) || v == 0.0; }

console::GameOStream& PutLong(console::GameOStream& os, long value) {
    return *static_cast<console::GameOStream*>(OstreamPutLongOriginal(&os, nullptr, value));
}

// "Unit <self> <text> <other>" (no line end).
console::GameOStream& LogPair(CUnit* self, const char* text, CEngineObject* other) {
    return PutLong(PutLong(*AiLog() << "Unit ", self->id_) << text, other->id_);
}

// The noticed point taken over inline (the original's copy of Notice), through CopyPoint2's
// address so its verify wrapper counts it (UpdateMemory's check relies on that).
void NoticeAt(AIBrain* brain, CUnit* at, int level, int kind) {
    float copy[2];
    const float* point = CopyPoint2Original(copy, &at->positionX);
    brain->noticedX = point[0];
    brain->noticedLevel = level;
    brain->noticedY = point[1];
    brain->noticedKind = kind;
}

// The callees through their addresses so their verify wrappers see these calls.
__declspec(naked) void __fastcall RefreshThroughAddress(AIViewer*, void*, CUnit*, std::uint8_t*) EI_ORIGINAL(0x005CA1D0)
__declspec(naked) void __fastcall SeeThroughAddress(AIViewer*, void*, CUnit*, std::uint8_t*) EI_ORIGINAL(0x005CA5A0)
__declspec(naked) void __fastcall PrepareThroughAddress(AIViewer*, void*, CUnit*) EI_ORIGINAL(0x005CA410)
} // namespace

void AIViewer::Prepare(CUnit* self) {
    if (cellX >= 0 && cellY >= 0 && cellX < map->width && cellY < map->height)
        eye.z = RoundToFloat(static_cast<double>(map->cells[cellY][cellX].groundLevel) / map->heightScale + eye.z);
    eye.z = RoundToFloat(static_cast<double>(self->above[2]) * kEyeHeightFactor + self->unknown_158 + self->center[2] + eye.z);
    const Quaternion& q = self->orientation;
    double a = static_cast<double>(q.y) * q.x - static_cast<double>(q.w) * q.z;
    a += a;
    double yy = static_cast<double>(q.y) * q.y;
    double c = static_cast<double>(q.w) * q.w - ((static_cast<double>(q.x) * q.x + static_cast<double>(q.z) * q.z) + yy) + (yy + yy);
    float cy = RoundToFloat(c);
    double d = static_cast<double>(q.y) * q.z + static_cast<double>(q.w) * q.x;
    float dz = RoundToFloat(d + d);
    ready = 1;
    facing.x = RoundToFloat(-a);
    facing.y = -cy;
    facing.z = -dz;
}

void __fastcall PrepareHook(AIViewer* self, void*, CUnit* unit) { self->Prepare(unit); }
EI_REPLACE(0x005CA410, PrepareHook);

void AIViewer::Refresh(CUnit* self, AIBrain* brain) {
    if (--brain->nearbyCountdown > 0) return;
    self->flags_ |= kFlagNearbyListed;
    brain->nearbyCountdown = (gamecrt::rand() & 3) + 8;
    int blockX = cellX / 32;
    int blockY = cellY / 32;
    UnitController* control = ControllerOf(self);
    float hearing = control->stats.valuesB0[2].current;
    float sight = RoundToFloat(static_cast<double>(control->moveFactors[3]) * control->stats.valuesB0[0].current);
    float reach = !(sight >= hearing) ? hearing : sight;
    int count = 0;
    const std::int16_t* offsets =
        brain->sightCells.Get(RoundToFloat(static_cast<double>(reach) * kSightCellsFactor * kSectorScale), &count);
    for (auto* node = brain->unknown_0F4.head; node; node = node->next) {
        if (node->data) node->data->Release();
        node->data = nullptr;
    }
    brain->unknown_0F4.RemoveAll();
    for (int i = 0; i < count; ++i) {
        AIObjectList* units = map->SectorMap()->UnitsAt(offsets[2 * i] + blockX, offsets[2 * i + 1] + blockY);
        if (!units) continue;
        for (auto* node = units->head; node; node = node->next) {
            CEngineObject* other = node->data;
            if (other == static_cast<CEngineObject*>(self)) continue;
            AddCountedTail(&brain->unknown_0F4, nullptr, other);
        }
    }
}

void __fastcall RefreshHook(AIViewer* self, void*, CUnit* unit, std::uint8_t* memory) { self->Refresh(unit, BrainOfMemory(memory)); }
EI_REPLACE(0x005CA1D0, RefreshHook);

void AIViewer::See(CUnit* self, AIBrain* brain) {
    UnitController* ours = ControllerOf(self);
    bool seesAll = self->player != nullptr || (self->flags_ & kFlagSeeAll) != 0;
    self->flags_ &= ~kFlagSeeAll;
    for (auto* node = brain->unknown_0F4.head; node;) {
        auto* other = static_cast<CUnit*>(node->data);
        node = node->next;
        bool dead = IsDead(other);
        bool hostile = self->IsHostileTo(other) || other->player != nullptr;
        if (dead) {
            if (!seesAll && hostile) continue;
            if (brain->unknown_12C.Find(other)) continue;
        } else {
            if (!seesAll && !hostile) continue;
            if (brain->visible.Find(other)) continue;
        }
        if (!ready) PrepareThroughAddress(this, nullptr, self);
        Vector3 theirEye;
        static_cast<CMapObjectVirtuals*>(static_cast<CEngineObject*>(other))->EyePoint(&theirEye);
        Vector3 d{RoundToFloat(static_cast<double>(theirEye.x) - eye.x), RoundToFloat(static_cast<double>(theirEye.y) - eye.y),
                  RoundToFloat(static_cast<double>(theirEye.z) - eye.z)};
        UnitController* theirs = ControllerOf(other);
        float coneLimit = ours->stats.unknown_044;
        float distance = RoundToFloat(std::sqrt(static_cast<double>(d.y) * d.y + static_cast<double>(d.x) * d.x));
        double sightRaw = static_cast<double>(theirs->moveFactors[2]) * ours->moveFactors[3] * theirs->stats.valuesE0[0].current *
                          ours->stats.valuesB0[0].current;
        float sight = RoundToFloat(sightRaw);
        bool seen = false;
        if (sightRaw >= distance) {
            double along = static_cast<double>(d.z) * facing.z + static_cast<double>(d.y) * facing.y + static_cast<double>(d.x) * facing.x;
            if (!(static_cast<double>(coneLimit) * distance > along)) {
                Vector3 to{RoundToFloat(static_cast<double>(eye.x) + d.x), RoundToFloat(static_cast<double>(eye.y) + d.y),
                           RoundToFloat(static_cast<double>(eye.z) + d.z)};
                seen = LineOfSightOriginal(map, nullptr, &eye, &to) * sight > distance;
            }
        }
        bool alive = !dead;
        if (!seen) {
            float behind = RoundToFloat(static_cast<double>(d.z) * facing.z + static_cast<double>(d.y) * facing.y +
                                        static_cast<double>(d.x) * facing.x);
            if ((std::bit_cast<std::uint32_t>(behind) & 0x80000000u) == 0 && ours->stats.unknown_048 > distance) {
                console::EndLine(LogPair(self, " sees unit ", other) << " with peripheral vision");
                seen = true;
            } else {
                if (dead) continue;
                if (!(static_cast<double>(theirs->stats.valuesE0[2].current) * ours->stats.valuesB0[2].current > distance)) continue;
                *AiLog() << "using sense life ";
                alive = true;
            }
        }
        if (seen && dead) {
            console::EndLine(LogPair(self, " sees corpse of unit ", other));
            AddCountedTail(&brain->unknown_12C, nullptr, other);
            if (Allied(self->side, other->side)) AddCountedTail(&brain->corpses, nullptr, other);
            continue;
        }
        if (alive) {
            brain->visible.AddHead(other);
            ++other->refCount_;
            double dx = static_cast<double>(self->positionX) - other->positionX;
            double dy = static_cast<double>(self->positionY) - other->positionY;
            double dz = static_cast<double>(self->positionZ) - other->positionZ;
            console::GameOStream& os = LogPair(self, " sees unit ", other) << " from ";
            (void)os;
            console::EndLine(AiLog()->PutFloat(std::sqrt(dz * dz + dy * dy + dx * dx)) << "m distance");
        }
    }
}

void __fastcall SeeHook(AIViewer* self, void*, CUnit* unit, std::uint8_t* memory) { self->See(unit, BrainOfMemory(memory)); }
EI_REPLACE(0x005CA5A0, SeeHook);

void AIViewer::Update(UnitController* owner, AIBrain* brain) {
    CUnit* self = owner->unit;
    bool hasPlayer = self->player != nullptr;
    float reachFactor = hasPlayer ? kOne : kNonPlayerReach;
    if (!self) return;
    ready = 0;
    eye = *reinterpret_cast<const Vector3*>(&self->positionX);
    float scale = RoundToFloat(kOne / (static_cast<double>(kOne) * kHalf));
    cellX = static_cast<int>(std::lrint(RoundToFloat(scale * static_cast<double>(eye.x) - kHalf)));
    cellY = static_cast<int>(std::lrint(RoundToFloat(scale * static_cast<double>(eye.y) - kHalf)));
    std::uint8_t* memory = brain->memory;
    RefreshThroughAddress(this, nullptr, self, memory);
    for (auto* node = brain->visible.head; node;) {
        auto* current = node;
        node = node->next;
        auto* other = static_cast<CUnit*>(current->data);
        Vector3 d{RoundToFloat(static_cast<double>(other->positionX) - self->positionX),
                  RoundToFloat(static_cast<double>(other->positionY) - self->positionY),
                  RoundToFloat(static_cast<double>(other->positionZ) - self->positionZ)};
        float distance = RoundToFloat(
            std::sqrt(static_cast<double>(d.z) * d.z + static_cast<double>(d.y) * d.y + static_cast<double>(d.x) * d.x));
        if (IsDead(other)) {
            if (Allied(self->side, other->side)) {
                // The original's inlined AddTail (a second copy follows below).
                brain->unknown_12C.AddTail(other);
                ++other->refCount_;
                if (brain->noticedLevel < kCorpseLevel) NoticeAt(brain, other, kCorpseLevel, kCorpseKind);
            }
            console::EndLine(LogPair(self, " sees corpse of unit ", other));
            AddCountedTail(&brain->unknown_12C, nullptr, other);
            if (Allied(self->side, other->side)) AddCountedTail(&brain->corpses, nullptr, other);
        } else {
            if (self->IsHostileTo(other) && !hasPlayer) continue;
            UnitController* theirs = ControllerOf(other);
            AIBrain* theirBrain = theirs->Brain();
            if (theirs->busy && theirBrain->target == static_cast<CEngineObject*>(self)) continue;
            double sight = static_cast<double>(theirs->moveFactors[2]) * owner->moveFactors[3] * theirs->stats.valuesE0[0].current *
                           owner->stats.valuesB0[0].current * reachFactor;
            if (sight > distance) continue;
            double sense = static_cast<double>(theirs->stats.valuesE0[2].current) * owner->stats.valuesB0[2].current * reachFactor;
            if (sense > distance) continue;
            if (theirBrain->AggressionEnemy() == static_cast<CEngineObject*>(self)) continue;
        }
        console::EndLine(LogPair(self, " lost from sight unit ", other));
        if (self->IsHostileTo(other) && brain->noticedLevel < kLostLevel) NoticeAt(brain, other, kLostLevel, kLostKind);
        if (current->data) current->data->Release();
        current->data = nullptr;
        brain->visible.RemoveAt(current);
    }
    SeeThroughAddress(this, nullptr, self, memory);
}

void __fastcall UpdateHook(AIViewer* self, void*, UnitController* owner, std::uint8_t* memory) {
    self->Update(owner, BrainOfMemory(memory));
}
EI_REPLACE(0x005CAA00, UpdateHook);

void __fastcall MarkSightCells(CUnit* seer, CUnit* observer, VisibilityGrid* grid) {
    Vector3 axis;
    seer->orientation.AxisY(&axis);
    Vector3 look{-axis.x, -axis.y, -axis.z};
    Vector3 eye;
    static_cast<CMapObjectVirtuals*>(static_cast<CEngineObject*>(seer))->EyePoint(&eye);
    grid->originX = kNoGridOrigin;
    grid->originY = kNoGridOrigin;
    GridCellsResizeOriginal(&grid->data, nullptr, 1, 1);
    std::memset(grid->data, 0, static_cast<std::size_t>(grid->height * grid->width));

    UnitController* mine = ControllerOf(seer);
    UnitController* theirs = ControllerOf(observer);
    float cone = mine->stats.unknown_044;
    float sight = RoundToFloat(static_cast<double>(theirs->moveFactors[2]) * mine->moveFactors[3] *
                               theirs->stats.valuesE0[0].current * mine->stats.valuesB0[0].current);
    // The larger of 0 and the sight (NaN kept).
    float radius = (kZero < sight || x87::IsNaN(sight)) ? sight : kZero;
    float height = RoundToFloat(observer->ModeSpeed());
    CAIMap* map = WorldServer()->AIMap();
    map->MarkVisibleCells(&eye, radius, &look, cone, height, grid);
    double hearing = static_cast<double>(mine->stats.valuesB0[2].current) * theirs->stats.valuesE0[2].current;
    if (!ZeroOrUnordered(hearing)) map->MarkVisibleCells(&eye, RoundToFloat(hearing), &look, kNoCone, kNoLineOfSight, grid);
    float peripheral = mine->stats.unknown_048;
    if (!ZeroOrUnordered(peripheral)) map->MarkVisibleCells(&eye, peripheral, &look, kZero, kNoLineOfSight, grid);
}
EI_REPLACE(0x005CD210, MarkSightCells);
