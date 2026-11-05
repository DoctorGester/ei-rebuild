// The unit AI's senses: the eye (the world server's AI events: what a unit sees each brain tick, the nearby
// units, seeing new ones, the eye's height and facing), hearing, and the perception memory's queries. See
// unit_ai.h and docs/notes/unit_ai.md.
#include "ai_map.h"
#include "combat.h"
#include "console.h"
#include "effects.h"
#include "game_singletons.h"
#include "map_object.h"
#include "object_diplomacy.h"
#include "sector_map.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/rounding.h"
#include "lib/str.h"
#include "lib/vecmath.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace {
constexpr float kNoCone = -1.0f;                // every direction
constexpr float kNoLineOfSight = -1000.0f;      // below -100, no line-of-sight test
constexpr int kNoGridOrigin = 0x7FFFFFFF;
constexpr float kNonPlayerReach = 1.05f;        // units without a player keep sight a little longer
constexpr float kEyeHeightFactor = 1.8f;
constexpr float kSightCellsFactor = 2.0f;
constexpr float kSectorScale = 0.03125f;

// "Unit <self> <text> <other>" (no line end).
console::Stream& LogPair(Unit* self, const char* text, EngineObject* other) {
    return kAiLog << "Unit " << self->id << text << other->id;
}

// How far `seer` sees `seen`: the product of their move factors and sight ratings.
float SightRange(UnitServer* seer, UnitServer* seen) {
    return seen->moveFactors[2] * seer->moveFactors[3] * seen->stats.conspicuity[0].current *
           seer->stats.perception[0].current;
}

} // namespace

void AIViewer::Prepare(Unit* self) {
    if (map->cells.Contains(eyeCell))
        eye.z = static_cast<float>(map->cells[eyeCell].groundLevel) / map->heightScale + eye.z;
    eye.z = self->bounds.boxMax.z * kEyeHeightFactor + self->eyeHeight + self->bounds.center.z + eye.z;
    Vector3 axis = (self->localRotation).AxisY();
    ready = 1;
    facing = -axis;
}

void AIViewer::Refresh(Unit* self, AIMemory* memory) {
    if (--memory->nearbyCountdown > 0) return;
    // Every refresh of the nearby list sets this bit, which is also the "relations changed" bit: the same tick's
    // UnitServer::PropagateHostility (after Think) then clears it and swaps hostility claims with the allies in the
    // list, whether or not any relation changed.
    self->flags |= MapObjectFlag::RelationsChanged;
    memory->nearbyCountdown = (libcrt::Rand() & 3) + 8;
    const Point block = eyeCell / 32;
    UnitServer* control = self->ServerController();
    float hearing = control->stats.perception[2].current;
    float sight = control->moveFactors[3] * control->stats.perception[0].current;
    float reach = MaxKeepB(sight, hearing);
    int count = 0;
    const std::int16_t* offsets = memory->sightCells.Get(reach * kSightCellsFactor * kSectorScale, &count);
    memory->nearby.clear();
    for (int i = 0; i < count; ++i) {
        std::vector<Unit*>* units = map->sectorMap.UnitsAt(offsets[2 * i] + block.x, offsets[2 * i + 1] + block.y);
        if (!units) continue;
        for (Unit* other : *units) {
            if (other != self) memory->nearby.push_back(other);
        }
    }
    // The original adds each unit at the head: See walks the list from the head, so the units of the last sector
    // come first.
    std::ranges::reverse(memory->nearby);
}

void AIViewer::See(Unit* self, AIMemory* memory) {
    UnitServer* ours = self->ServerController();
    bool seesAll = self->player != nullptr || (self->flags & MapObjectFlag::SeeAll) != 0;
    self->flags &= ~MapObjectFlag::SeeAll;
    for (Unit* other : memory->nearby) {
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
        Vector3 theirEye = other->EyePoint();
        Vector3 d = theirEye - eye;
        UnitServer* theirs = other->ServerController();
        float coneLimit = ours->stats.sightCone;
        float distance = Length2D(XY(d));
        float sight = SightRange(ours, theirs);
        bool seen = false;
        if (sight >= distance) {
            float along = Dot(d, facing);
            if (coneLimit * distance <= along) {
                Vector3 to = eye + d;
                seen = map->LineOfSight(eye, to) * sight > distance;
            }
        }
        bool alive = !dead;
        if (!seen) {
            float behind = Dot(d, facing);
            if (!SignBit(behind) && ours->stats.peripheralRange > distance) {
                console::EndLine(LogPair(self, " sees unit ", other) << " with peripheral vision");
                seen = true;
            } else {
                if (dead) continue;
                if (theirs->stats.conspicuity[2].current * ours->stats.perception[2].current <= distance) continue;
                console::EndLine(kAiLog << "using sense life");
                alive = true;
            }
        }
        if (seen && dead) {
            memory->SeeCorpse(self, other);
            continue;
        }
        if (alive) {
            memory->visible.insert(memory->visible.begin(), other);
            LogPair(self, " sees unit ", other) << " from ";
            console::EndLine(kAiLog << std::sqrt(LengthSquared(self->localPosition - other->localPosition)) << "m distance");
        }
    }
}

namespace {

// Whether `owner`'s unit still sees the visible unit `other`. A dead one is not seen: it is recorded as a corpse
// first.
bool StillSees(UnitServer* owner, AIMemory* memory, Unit* other, float reachFactor) {
    Unit* self = owner->unit;
    Vector3 d = other->localPosition - self->localPosition;
    float distance = Length(d);
    if (other->IsDead()) {
        if (g_serverWorld.diplomacy.IsAllied(self->side, other->side) && memory->noticedLevel < kNoticeCorpseLevel)
            memory->SetNoticed(other->localPosition, kNoticeCorpseLevel, kNoticeCorpseDecay);
        memory->SeeCorpse(self, other);
        return false;
    }
    if (self->IsHostileTo(other) && self->player == nullptr) return true;
    UnitServer* theirs = other->ServerController();
    AIBrain* theirBrain = &theirs->brain;
    if (theirs->moveMode2.onGuard && theirBrain->target == self) return true;
    float sight = SightRange(owner, theirs) * reachFactor;
    if (sight > distance) return true;
    float sense = theirs->stats.conspicuity[2].current * owner->stats.perception[2].current * reachFactor;
    if (sense > distance) return true;
    return theirBrain->AggressionEnemy() == self;
}

} // namespace

void AIViewer::Update(UnitServer* owner, AIMemory* memory) {
    Unit* self = owner->unit;
    bool hasPlayer = self->player != nullptr;
    float reachFactor = hasPlayer ? 1.0f : kNonPlayerReach;
    ready = 0;
    eye = self->localPosition;
    eyeCell = WorldToCell(eye);
    Refresh(self, memory);
    HeldUnits& visible = memory->visible;
    for (std::size_t i = 0; i < visible.size();) {
        Unit* other = visible[i];
        if (StillSees(owner, memory, other, reachFactor)) {
            ++i;
            continue;
        }
        console::EndLine(LogPair(self, " lost from sight unit ", other));
        if (self->IsHostileTo(other) && memory->noticedLevel < kNoticeSeenLevel)
            memory->SetNoticed(other->localPosition, kNoticeSeenLevel, kNoticeSeenDecay);
        visible.erase(visible.begin() + static_cast<std::ptrdiff_t>(i));
    }
    See(self, memory);
}

void MarkSightCells(Unit* seer, Unit* observer, FogGrid* grid) {
    Vector3 axis = seer->localRotation.AxisY();
    Vector3 look = -axis;
    Vector3 eye = seer->EyePoint();
    grid->x0 = kNoGridOrigin;
    grid->y0 = kNoGridOrigin;
    grid->cells.Resize(1, 1);
    grid->cells.data.assign(grid->cells.data.size(), 0);

    UnitServer* mine = seer->ServerController();
    UnitServer* theirs = observer->ServerController();
    float cone = mine->stats.sightCone;
    float sight = SightRange(mine, theirs);
    // The larger of 0 and the sight.
    float radius = 0.0f < sight ? sight : 0.0f;
    float height = observer->ModeSpeed();
    AIMap& map = g_serverWorld.aiMap;
    map.MarkVisibleCells(eye, radius, look, cone, height, grid);
    float hearing = mine->stats.perception[2].current * theirs->stats.conspicuity[2].current;
    if (hearing != 0.0f) map.MarkVisibleCells(eye, hearing, look, kNoCone, kNoLineOfSight, grid);
    float peripheral = mine->stats.peripheralRange;
    if (peripheral != 0.0f) map.MarkVisibleCells(eye, peripheral, look, 0.0f, kNoLineOfSight, grid);
}

// --- Hearing: the heard calls' Hear / AddClaim / RemoveClaims, the perception memory's HearCall / AddClaim
// and the AI events' Noise.

namespace {
constexpr float kNoiseRadius = 50.0f;   // the units asked for around a noise
const char* const kHeardFormat = "Unit %i heard unit %i from %fm distance";

} // namespace

void AIHeardCalls::Hear(const Vector3& point, int delay, std::string_view text) {
    std::erase_if(calls, [delay](const AICallRecord& call) { return call.hostileMask == 0 && call.delay >= delay; });
    calls.push_back({point, delay, 0, std::string(text)});
}

void AIHeardCalls::AddClaim(std::uint32_t mask, int delay, std::string_view text) {
    // A claim has no point (saved as zero).
    calls.push_back({Vector3{0.0f, 0.0f, 0.0f}, delay, static_cast<int>(mask), std::string(text)});
}

void AIHeardCalls::RemoveClaims() {
    std::erase_if(calls, [](const AICallRecord& call) { return call.hostileMask != 0; });
}

void AIMemory::HearCall(const Vector3& point, int delay, std::string_view text) { calls.Hear(point, delay, text); }
void AIMemory::AddClaim(std::uint32_t mask, int delay, std::string_view text) { calls.AddClaim(mask, delay, text); }

void AIViewer::Noise(UnitServer* source, float loudness, int delay, const Vector3& at) {
    Unit* unit = source->unit;
    UnitList units = map->sectorMap.FindUnitsInRadius(at, kNoiseRadius);
    for (Unit* other : units) {
        if (!other->IsHostileTo(unit)) continue;
        UnitServer* control = other->ServerController();
        AIBrain* brain = &control->brain;
        // A unit that noticed something at least as loud as a call hears nothing.
        if (brain->memory.noticedLevel >= kNoticeCallLevel) continue;
        const float hearing = control->moveFactors[1] * control->stats.perception[3].current;
        const float distance = Distance(at, other->localPosition);
        if (hearing * loudness <= distance) continue;
        std::string text;
        if (console::g_filterMask & console::kLogAi) {
            const float between = Distance(other->localPosition, unit->localPosition);
            text = str::Format(kHeardFormat, other->id, unit->id, between);
        }
        brain->memory.calls.Hear(unit->localPosition, delay, text);
    }
}

void AIViewer::StepNoise(const Vector3& position, UnitServer* source) {
    const AITerrainType* ground = g_serverWorld.aiMap.MaterialRecordAt(position);
    float loudness = source->moveFactors[0] * source->stats.conspicuity[3].current;
    loudness *= ground->noise;
    Noise(source, loudness, AIBrain::kCallDelay, source->unit->localPosition);
}

// --- The perception memory's queries: the visible units, the exposure to hostile units, corpses seen,
// forcing an object into sight.

namespace {
constexpr float kMaxExposure = 3.0f;

// distance / range, at most 3 (NaN kept); 3 for a zero range.
float Exposure(float distance, float range) {
    if (range == 0.0f) return kMaxExposure;
    float ratio = distance / range;
    return MinKeepA(ratio, kMaxExposure);
}

// The smaller of the two; NaN `value` wins.
void KeepLower(float& slot, float value) { slot = MinKeepA(value, slot); }

} // namespace

UnitList AIMemory::FindEnemies(Unit* self) {
    UnitList out;
    for (Unit* other : visible) {
        if (!self->IsHostileTo(other) || other->IsDead() || reports.Contains(other)) continue;
        out.push_back(other);
    }
    return out;
}

UnitList AIMemory::FindEnemiesNear(Unit* self, const Vector3& point, float radius) {
    UnitList out;
    float radius2 = radius > 0.0f ? radius * radius : -1.0f;
    for (Unit* other : visible) {
        if (!other->IsHostileTo(self) || other->IsDead() || reports.Contains(other)) continue;
        if (LengthSquared(other->localPosition - point) >= radius2) {
            AIBrain* theirs = &other->ServerController()->brain;
            auto* mode = theirs->FindModeOf<AggressionMode>();
            if (!mode || (mode->fixed && !mode->enemy)) continue;
            Unit* enemy = UnitOrNull(theirs->AggressionEnemy());
            if (enemy && !g_serverWorld.diplomacy.IsAllied(self->side, enemy->side)) continue;
            if (!theirs->memory.seesEnemy) continue;
        }
        out.push_back(other);
    }
    return out;
}

Unit* AIMemory::NearestEnemy(Unit* self) {
    Unit* nearest = nullptr;
    float best = kFarSq;
    for (Unit* other : visible) {
        if (!self->IsHostileTo(other) || other->IsDead()) continue;
        float d2 = LengthSquared(self->localPosition - other->localPosition);
        if (d2 < best) {
            best = d2;
            nearest = other;
        }
    }
    return nearest;
}

void AIMemory::ForceSee(Unit* self, Unit* object) {
    if (lib::Contains(visible, object)) return;
    Unit* other = object;
    UnitServer* theirs = other->ServerController();
    UnitServer* ours = self->ServerController();
    float distance = std::sqrt(LengthSquared(self->localPosition - other->localPosition));
    AIBrain* theirBrain = &theirs->brain;
    bool attacksUs = (theirBrain->action == AIAction::Attack || theirBrain->action == AIAction::Cast) &&
                     theirBrain->target == self;
    if (!attacksUs) {
        float sight = SightRange(ours, theirs);
        float nearValue = theirs->stats.conspicuity[2].current * ours->stats.perception[2].current;
        if (sight > distance && nearValue > distance) return;
    }
    if (object->IsDead()) {
        SeeCorpse(self, object);
    } else {
        visible.insert(visible.begin(), object);
        kAiLog << "Unit " << self->id << " is forced to see unit " << other->id << " from ";
        console::EndLine(kAiLog << distance << "m distance");
    }
}

void AIMemory::SeeCorpse(Unit* self, Unit* corpse) {
    console::EndLine(kAiLog << "Unit " << self->id << " sees corpse of unit " << corpse->id);
    seenCorpses.push_back(corpse);
    if (g_serverWorld.diplomacy.IsAllied(self->side, corpse->side)) corpses.push_back(corpse);
}

std::array<float, 5> AIMemory::MeasureExposure(UnitServer* self) {
    std::array<float, 5> out;
    out.fill(kMaxExposure);
    Unit* unit = self->unit;
    Vector3 eye = unit->EyePoint();
    for (Unit* other : nearby) {
        if (other->IsDead() || !unit->IsHostileTo(other)) continue;
        UnitServer* theirs = other->ServerController();
        float distance = std::sqrt(LengthSquared(other->localPosition - unit->localPosition));
        float sight = theirs->moveFactors[3] * self->moveFactors[2] * theirs->stats.perception[0].current *
                      self->stats.conspicuity[0].current;
        if (distance < sight) {
            Vector3 to = other->EyePoint();
            float range = g_serverWorld.aiMap.LineOfSight(eye, to) * sight;
            KeepLower(out[0], Exposure(distance, range));
        }
        KeepLower(out[4], Exposure(distance, theirs->stats.perception[2].current * self->stats.conspicuity[2].current));
        KeepLower(out[1], Exposure(distance, theirs->stats.perception[4].current * self->stats.conspicuity[4].current));
        KeepLower(out[2], Exposure(distance, theirs->moveFactors[1] * self->moveFactors[0] *
                                                 theirs->stats.perception[3].current * self->stats.conspicuity[3].current));
    }
    return out;
}
