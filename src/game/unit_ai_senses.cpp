// The unit AI's senses: the eye (the world server's AI events: what a unit sees each brain tick, the nearby
// units, seeing new ones, the eye's height and facing), hearing, and the perception memory's queries. See
// unit_ai.h and docs/notes/unit_ai.md.
#include "ai_map.h"
#include "combat.h"
#include "console.h"
#include "effects.h"
#include "ei/x87.h"
#include "game_singletons.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "lib/str.h"
#include "lib/vecmath.h"
#include "map_object.h"
#include "object_diplomacy.h"
#include "sector_map.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <list>
#include <string>

namespace {
constexpr float kNoCone = -1.0f;                // every direction
constexpr float kNoLineOfSight = -1000.0f;      // below -100, no line-of-sight test
constexpr int kNoGridOrigin = 0x7FFFFFFF;
constexpr float kNonPlayerReach = 1.05f;        // units without a player keep sight a little longer
constexpr float kEyeHeightFactor = 1.8f;
constexpr float kSightCellsFactor = 2.0f;
constexpr float kSectorScale = 0.03125f;
constexpr std::uint32_t kFlagNearbyListed = 0x80000;


bool IsZero(double v) {
    EI_NAN_AUDIT(v);
    return v == 0.0;
}

// "Unit <self> <text> <other>" (no line end).
console::Stream& LogPair(CUnit* self, const char* text, EngineObject* other) {
    return kAiLog << "Unit " << self->id << text << other->id;
}

// The noticed point taken over without Notice's checks.
void NoticeAt(AIMemory* memory, CUnit* at, int level, int decay) {
    memory->noticedX = at->localPosition.x;
    memory->noticedLevel = level;
    memory->noticedY = at->localPosition.y;
    memory->noticedDecay = decay;
}

} // namespace

void AIViewer::Prepare(CUnit* self) {
    if (cellX >= 0 && cellY >= 0 && cellX < map->cells.width && cellY < map->cells.height)
        eye.z = static_cast<float>(static_cast<double>(map->cells[cellY][cellX].groundLevel) / map->heightScale + eye.z);
    eye.z = static_cast<float>(static_cast<double>(self->bounds.boxMax.z) * kEyeHeightFactor + self->eyeHeight + self->bounds.center.z + eye.z);
    Vector3 axis = RotatedAxisY(self->localRotation);
    ready = 1;
    facing = -axis;
}

void AIViewer::Refresh(CUnit* self, AIMemory* memory) {
    if (--memory->nearbyCountdown > 0) return;
    self->flags |= kFlagNearbyListed;
    memory->nearbyCountdown = (libcrt::Rand() & 3) + 8;
    int blockX = cellX / 32;
    int blockY = cellY / 32;
    UnitServer* control = self->ServerController();
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
    UnitServer* ours = self->ServerController();
    bool seesAll = self->player != nullptr || (self->flags & MapObjectFlag::SeeAll) != 0;
    self->flags &= ~MapObjectFlag::SeeAll;
    for (auto node = memory->nearby.begin(); node != memory->nearby.end();) {
        CUnit* other = *node;
        ++node;
        bool dead = other->IsDead();
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
        UnitServer* theirs = other->ServerController();
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
                seen = map->LineOfSight(eye, to) * sight > distance;
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
            if (g_serverWorld.diplomacy.IsAllied(self->side, other->side)) memory->corpses.push_back(other);
            continue;
        }
        if (alive) {
            memory->visible.push_front(other);
            double dx = static_cast<double>(self->localPosition.x) - other->localPosition.x;
            double dy = static_cast<double>(self->localPosition.y) - other->localPosition.y;
            double dz = static_cast<double>(self->localPosition.z) - other->localPosition.z;
            LogPair(self, " sees unit ", other) << " from ";
            console::EndLine(kAiLog << std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)) << "m distance");
        }
    }
}

void AIViewer::Update(UnitServer* owner, AIMemory* memory) {
    CUnit* self = owner->unit;
    bool hasPlayer = self->player != nullptr;
    float reachFactor = hasPlayer ? 1.0f : kNonPlayerReach;
    if (!self) return;
    ready = 0;
    eye = *&self->localPosition;
    float scale = 2.0f;
    cellX = x87::RoundToInt(static_cast<float>(scale * static_cast<double>(eye.x) - 0.5f));
    cellY = x87::RoundToInt(static_cast<float>(scale * static_cast<double>(eye.y) - 0.5f));
    Refresh(self, memory);
    for (auto node = memory->visible.begin(); node != memory->visible.end();) {
        auto current = node;
        ++node;
        CUnit* other = *current;
        Vector3 d{static_cast<float>(static_cast<double>(other->localPosition.x) - self->localPosition.x),
                  static_cast<float>(static_cast<double>(other->localPosition.y) - self->localPosition.y),
                  static_cast<float>(static_cast<double>(other->localPosition.z) - self->localPosition.z)};
        float distance = LengthZYX(d);
        if (other->IsDead()) {
            if (g_serverWorld.diplomacy.IsAllied(self->side, other->side)) {
                // AddTail (a second copy follows below).
                memory->seenCorpses.push_back(other);
                if (memory->noticedLevel < kNoticeCorpseLevel) NoticeAt(memory, other, kNoticeCorpseLevel, kNoticeCorpseDecay);
            }
            console::EndLine(LogPair(self, " sees corpse of unit ", other));
            memory->seenCorpses.push_back(other);
            if (g_serverWorld.diplomacy.IsAllied(self->side, other->side)) memory->corpses.push_back(other);
        } else {
            if (self->IsHostileTo(other) && !hasPlayer) continue;
            UnitServer* theirs = other->ServerController();
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
        if (self->IsHostileTo(other) && memory->noticedLevel < kNoticeSeenLevel)
            NoticeAt(memory, other, kNoticeSeenLevel, kNoticeSeenDecay);
        memory->visible.erase(current);
    }
    See(self, memory);
}

void MarkSightCells(CUnit* seer, CUnit* observer, FogGrid* grid) {
    Vector3 axis;
    seer->localRotation.AxisY(&axis);
    Vector3 look{-axis.x, -axis.y, -axis.z};
    Vector3 eye;
    seer->EyePoint(&eye);
    grid->x0 = kNoGridOrigin;
    grid->y0 = kNoGridOrigin;
    grid->cells.Resize(1, 1);
    grid->cells.data.assign(grid->cells.data.size(), 0);

    UnitServer* mine = seer->ServerController();
    UnitServer* theirs = observer->ServerController();
    float cone = mine->stats.sightCone;
    float sight = static_cast<float>(static_cast<double>(theirs->moveFactors[2]) * mine->moveFactors[3] *
                               theirs->stats.conspicuity[0].current * mine->stats.perception[0].current);
    // The larger of 0 and the sight.
    EI_NAN_AUDIT(sight);
    float radius = 0.0f < sight ? sight : 0.0f;
    float height = static_cast<float>(observer->ModeSpeed());
    AIMap* map = &g_serverWorld.aiMap;
    map->MarkVisibleCells(eye, radius, look, cone, height, grid);
    double hearing = static_cast<double>(mine->stats.perception[2].current) * theirs->stats.conspicuity[2].current;
    if (!IsZero(hearing)) map->MarkVisibleCells(eye, static_cast<float>(hearing), look, kNoCone, kNoLineOfSight, grid);
    float peripheral = mine->stats.peripheralRange;
    if (!IsZero(peripheral)) map->MarkVisibleCells(eye, peripheral, look, 0.0f, kNoLineOfSight, grid);
}

// --- Hearing: the heard calls' Hear / AddClaim / RemoveClaims, the perception memory's HearCall / AddClaim
// and the AI events' Noise.

namespace {
constexpr float kNoiseRadius = 50.0f;   // the units asked for around a noise
const char* const kHeardFormat = "Unit %i heard unit %i from %fm distance";

} // namespace

void AIHeardCalls::Hear(const Vector3& point, int delay, std::string_view text) {
    std::erase_if(*this, [delay](const AICallRecord& call) { return call.hostileMask == 0 && call.delay >= delay; });
    push_back({point, delay, 0, std::string(text)});
}

void AIHeardCalls::AddClaim(std::uint32_t mask, int delay, std::string_view text) {
    // The point is left as the stack held it (saved as memory.callN.point); 0,0,0 is the
    // canonical value, which the harness gives game.exe too (harness_save_leftovers.cpp).
    push_back({Vector3{0.0f, 0.0f, 0.0f}, delay, static_cast<int>(mask), std::string(text)});
}

void AIHeardCalls::RemoveClaims() {
    std::erase_if(*this, [](const AICallRecord& call) { return call.hostileMask != 0; });
}

void AIMemory::HearCall(const Vector3& point, int delay, std::string_view text) { calls.Hear(point, delay, text); }
void AIMemory::AddClaim(std::uint32_t mask, int delay, std::string_view text) { calls.AddClaim(mask, delay, text); }

void AIViewer::Noise(UnitServer* source, float loudness, int delay, const Vector3& at) {
    CUnit* unit = source->unit;
    std::list<CUnit*> units = map->sectorMap.FindUnitsInRadius(at, kNoiseRadius);
    for (auto node = units.begin(); node != units.end();) {
        auto* other = *node;
        ++node;
        if (!other->IsHostileTo(unit)) continue;
        UnitServer* control = other->ServerController();
        AIBrain* brain = &control->brain;
        // A unit that noticed something at least as loud as a call hears nothing.
        if (brain->memory.noticedLevel >= kNoticeCallLevel) continue;
        const double hearing = static_cast<double>(control->moveFactors[1]) * control->stats.perception[3].current;
        const float distance = vecmath::Distance<Sum3::XYZ>(at, other->localPosition);
        EI_NAN_AUDIT(hearing, loudness, distance);
        if (hearing * loudness <= distance) continue;
        std::string text;
        if (console::g_filterMask & 1) {
            const double between = vecmath::Distance<Sum3::XYZ>(other->localPosition, unit->localPosition);
            text = str::Format(kHeardFormat, other->id, unit->id, between);
        }
        brain->memory.calls.Hear(unit->localPosition, delay, text);
    }
}

AIEventPresence* AIViewer::StepNoise(const Vector3& position, UnitServer* source) {
    const AITerrainType* ground = g_serverWorld.aiMap.MaterialRecordAt(position);
    float loudness = source->moveFactors[0] * source->stats.conspicuity[3].current;
    loudness *= ground->noise;
    Noise(source, loudness, AIBrain::kCallDelay, source->unit->localPosition);
    return nullptr;
}

// --- The perception memory's queries: the visible units, the exposure to hostile units, corpses seen,
// forcing an object into sight.

namespace {
const float kFar = 1e30f;


// (a - b) squared in 3D, summed z, then x, then y.
double Distance2(const CUnit* a, const float* b) {
    double dx = static_cast<double>(a->localPosition.x) - b[0];
    double dy = static_cast<double>(a->localPosition.y) - b[1];
    double dz = static_cast<double>(a->localPosition.z) - b[2];
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

UnitList AIMemory::FindEnemies(CUnit* self) {
    UnitList out;
    for (auto& node : visible) {
        CUnit* other = node;
        if (!self->IsHostileTo(other) || other->IsDead() || reports.Contains(other)) continue;
        out.push_back(other);
    }
    return out;
}

UnitList AIMemory::FindEnemiesNear(CUnit* self, const Vector3& point, float radius) {
    UnitList out;
    float radius2 = radius > 0.0f ? static_cast<float>(static_cast<double>(radius) * radius) : -1.0f;
    for (auto& node : visible) {
        CUnit* other = node;
        if (!other->IsHostileTo(self) || other->IsDead() || reports.Contains(other)) continue;
        if (Distance2(other, &point.x) >= radius2) {
            AIBrain* theirs = &other->ServerController()->brain;
            auto* mode = static_cast<AggressionMode*>(theirs->FindMode(AIModeType::Aggression));
            if (!mode || (mode->fixed && !mode->enemy)) continue;
            CUnit* enemy = UnitOrNull(theirs->AggressionEnemy());
            if (enemy && !g_serverWorld.diplomacy.IsAllied(self->side, enemy->side)) continue;
            if (!theirs->memory.seesEnemy) continue;
        }
        out.push_back(other);
    }
    return out;
}

CUnit* AIMemory::NearestEnemy(CUnit* self) {
    CUnit* nearest = nullptr;
    float best = kFar;
    for (auto& node : visible) {
        CUnit* other = node;
        if (!self->IsHostileTo(other) || other->IsDead()) continue;
        double dx = static_cast<double>(self->localPosition.x) - other->localPosition.x;
        double dy = static_cast<double>(self->localPosition.y) - other->localPosition.y;
        double dz = static_cast<double>(self->localPosition.z) - other->localPosition.z;
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
    UnitServer* theirs = other->ServerController();
    UnitServer* ours = self->ServerController();
    double dx = static_cast<double>(self->localPosition.x) - other->localPosition.x;
    double dy = static_cast<double>(self->localPosition.y) - other->localPosition.y;
    double dz = static_cast<double>(self->localPosition.z) - other->localPosition.z;
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
    if (object->IsDead()) {
        console::EndLine(kAiLog << "Unit " << self->id << " sees corpse of unit " << other->id);
        seenCorpses.push_back(object);
        if (g_serverWorld.diplomacy.IsAllied(self->side, other->side)) corpses.push_back(object);
    } else {
        visible.push_front(object);
        kAiLog << "Unit " << self->id << " is forced to see unit " << other->id << " from ";
        console::EndLine(kAiLog << distance << "m distance");
    }
}

void AIMemory::SeeCorpse(CUnit* self, CUnit* corpse) {
    console::EndLine(kAiLog << "Unit " << self->id << " sees corpse of unit " << corpse->id);
    seenCorpses.push_back(corpse);
    if (g_serverWorld.diplomacy.IsAllied(self->side, corpse->side)) corpses.push_back(corpse);
}

void AIMemory::MeasureExposure(UnitServer* self, float* out) {
    for (int i = 0; i < 5; ++i) out[i] = kMaxExposure;
    CUnit* unit = self->unit;
    Vector3 eye;
    unit->EyePoint(&eye);
    for (auto& node : nearby) {
        CUnit* other = node;
        if (other->IsDead() || !unit->IsHostileTo(other)) continue;
        UnitServer* theirs = other->ServerController();
        double dx = static_cast<double>(other->localPosition.x) - unit->localPosition.x;
        double dy = static_cast<double>(other->localPosition.y) - unit->localPosition.y;
        double dz = static_cast<double>(other->localPosition.z) - unit->localPosition.z;
        float distance = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)));
        float sight = static_cast<float>(static_cast<double>(theirs->moveFactors[3]) * self->moveFactors[2] *
                                   theirs->stats.perception[0].current * self->stats.conspicuity[0].current);
        EI_NAN_AUDIT(distance, sight);
        if (distance < sight) {
            Vector3 theirEye;
            Vector3 to = *other->EyePoint(&theirEye);
            double range = g_serverWorld.aiMap.LineOfSight(eye, to) * sight;
            KeepLower(out[0], Exposure(distance, range));
        }
        KeepLower(out[4], Exposure(distance, static_cast<double>(theirs->stats.perception[2].current) * self->stats.conspicuity[2].current));
        KeepLower(out[1], Exposure(distance, static_cast<double>(theirs->stats.perception[4].current) * self->stats.conspicuity[4].current));
        KeepLower(out[2], Exposure(distance, static_cast<double>(theirs->moveFactors[1]) * self->moveFactors[0] *
                                                 theirs->stats.perception[3].current * self->stats.conspicuity[3].current));
    }
}
