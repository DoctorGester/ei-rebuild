// Magic traps: who is inside, casting at newcomers, set-up from the zone record, lifetime, Save and Load and the
// update. See magic_trap.h and docs/notes/units.md ("Magic traps").
#include "magic_trap.h"

#include "base_manager.h"
#include "engine_object.h"
#include "game_singletons.h"
#include "map_object.h"
#include "spell.h"
#include "unit.h"
#include "unit_ai.h"
#include "world.h"

#include "lib/bytes.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

namespace {



// Is `unit` (its x / y) inside a circle, and alive? The first circle that holds it decides; a NaN
// distance is outside.
bool StaysInside(MagicTrap* trap, CMapObject* unit) {
    int count = std::ssize(trap->area);
    const TrapCircle* circle = trap->area.data();
    float x = unit->localPosition.x;
    float y = unit->localPosition.y;
    for (int i = 0; i < count; ++i, ++circle) {
        float dx = x - circle->x;
        float dy = y - circle->y;
        if (circle->radius * circle->radius > LengthSquared2DUnrounded(dy, dx)) return !unit->IsDead();
    }
    return false;
}

constexpr float kFar = 1e30f;                        // a squared distance

constexpr MapObjectFlag kTrapFlags = MapObjectFlag::Armed | MapObjectFlag::NotDrawn;
constexpr std::uint32_t kDefaultPeriod = 10;
// The fields saved after the points: period, countdown, single target, sides.
void* TailFields(MagicTrap* trap, int i) {
    void* fields[] = {&trap->period, &trap->countdown, &trap->singleTarget, &trap->sides};
    return fields[i];
}
constexpr int kTailFieldCount = 4;


// The spell, a counted reference: the new one's count raised before the old one is released.
void SetSpell(MagicTrap* trap, CSpell* spell) {
    trap->spell.Assign(spell);
}


} // namespace

void MagicTrap::UpdateVictims() {
    std::erase_if(inside, [this](const HeldRef<CUnit>& unit) { return !StaysInside(this, unit); });

    if (!(flags & MapObjectFlag::Armed)) return;
    WorldServer* context = ServerWorld();
    std::vector<CUnit*> found;
    std::vector<CUnit*> newcomers;
    FindTrapVictims(context, &area, sides, &found);
    for (CUnit* unit : found) {
        if (lib::Contains(inside, unit)) continue;
        inside.emplace_back(unit);
        newcomers.push_back(unit);
    }
    if (!newcomers.empty()) CastAtUnits(&newcomers);
}

void FindTrapVictims(WorldServer* context, const std::vector<TrapCircle>* area, std::uint32_t sides,
                                std::vector<CUnit*>* out) {
    out->clear();
    SectorMap* sectors = &context->aiMap.sectorMap;
    for (int i = 0; i < std::ssize(*area); ++i) {
        const TrapCircle& circle = (*area)[i];
        Vector3 point = {circle.x, circle.y, 0.0f};
        for (CUnit* unit : sectors->FindUnitsInRadius(point, circle.radius)) {
            if (unit->IsDead()) continue;
            int side = unit->side;
            if (sides & (1u << (side & 31))) out->push_back(unit);   // SHL by CL: the count is taken mod 32
        }
    }
}

void MagicTrap::CastAtUnits(std::vector<CUnit*>* units) {
    if (units->empty() || !spell) return;
    const Vector3& origin = localPosition;
    Vector3 from = {origin.x, origin.y, origin.z};
    auto* world = World();
    from.z = pc24::Add(world->RawSurfaceHeightAt(from.x, from.y), from.z);   // the double height: one rounding
    if (!(spell->targetFlags & SpellTargetFlag::AtUnits) && std::ssize(points) != 0) {
        for (int i = 0; i < std::ssize(points); ++i) {
            const TrapPoint& point = points[i];
            Vector3 at = {point.x, point.y, 0.0f};
            g_serverWorld.CastSpell(spell, nullptr, from, nullptr, at, 0);
        }
    } else {
        if (singleTarget != 0) {
            if (units->empty()) return;
            CUnit* closest = nullptr;
            for (CUnit* unit : *units) {
                float dx = unit->localPosition.x - origin.x;
                float dy = unit->localPosition.y - origin.y;
                float dz = unit->localPosition.z - origin.z;
                EI_NAN_AUDIT(dx, dy, dz);
                if (LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz) < kFar) closest = unit;
            }
            if (!closest) return;
            units->clear();
            units->push_back(closest);
        }
        for (CUnit* unit : *units) g_serverWorld.CastSpell(spell, nullptr, from, unit, unit->localPosition, 0);
    }
    countdown = period;
}

void MagicTrap::SetUp(const MagicTrapRecord* record) {
    sides = record->sides;
    CSpellManager* manager = WorldContext()->SpellManager();
    CSpell* made = manager->CreateSpellFromText(record->spell);
    spell.Assign(made);
    std::memcpy(&localPosition.x, &record->position, sizeof(record->position));    // bit for bit

    area = record->area;
    points = record->points;

    inside.clear();

    period = record->period;
    countdown = 0;
    singleTarget = record->singleTarget != 0 ? 1 : 0;
}

// --- Construction, destruction, Save / Load ------------------------------------------

MagicTrap::MagicTrap() {
    CreateMapObject();
    sides = 0xFFFFFFFFu;
    flags |= kTrapFlags;
    SetSpell(this, nullptr);
    area.clear();
    points.clear();
    inside.clear();
    countdown = 0;
    singleTarget = 0;
    period = static_cast<int>(kDefaultPeriod);
}

MagicTrap::~MagicTrap() {
    DestroyMapObject();
    SetSpell(this, nullptr);
    area.clear();
    points.clear();
    inside.clear();
}

void MagicTrap::Load(ByteStream* stream) { LoadTrap(stream); }
void MagicTrap::Save(ByteStream* stream) { SaveTrap(stream); }

void MagicTrap::SaveTrap(ByteStream* stream) {
    SaveMapObject(stream);
    std::int32_t value = spell ? spell->id : -1;
    stream->WriteValue(value);
    value = std::ssize(area);
    stream->WriteValue(value);
    for (int i = 0; i < std::ssize(area); ++i) stream->WriteValue(area[i]);
    value = std::ssize(inside);
    stream->WriteValue(value);
    for (CUnit* unit : inside) SaveRef(unit, stream);
    value = std::ssize(points);
    stream->WriteValue(value);
    for (int i = 0; i < std::ssize(points); ++i) stream->WriteValue(points[i]);
    for (int i = 0; i < kTailFieldCount; ++i) {
        lib::LoadBytes(value, TailFields(this, i));
        stream->WriteValue(value);
    }
}

void MagicTrap::LoadTrap(ByteStream* stream) {
    LoadMapObject(stream);
    CSpellManager* manager = WorldContext()->SpellManager();
    // The id and the two counts are read into one local: a read that fails leaves the previous value.
    std::int32_t value = 0;
    stream->ReadValue(value);
    SetSpell(this, value == -1 ? nullptr : manager->FindObjectAs<CSpell>(value));
    stream->ReadValue(value);
    area.resize(value);
    for (int i = 0; i < std::ssize(area); ++i) stream->ReadValue(area[i]);
    CountedListLoad(&inside, stream, WorldContext());
    stream->ReadValue(value);
    points.resize(value);
    for (int i = 0; i < std::ssize(points); ++i) stream->ReadValue(points[i]);
    for (int i = 0; i < kTailFieldCount; ++i) stream->Read(TailFields(this, i), 4);
}

// --- Create, Destroy, the update, firing, forgetting a removed object -------------

void MagicTrap::Create() {
    CreateMapObject();
    sides = 0xFFFFFFFFu;
    flags |= kTrapFlags;
    SetSpell(this, nullptr);
    area = {};
    points.clear();
    inside.clear();
    period = kDefaultPeriod;
    countdown = 0;
    singleTarget = 0;
}

void MagicTrap::DestroyTrap() {
    DestroyMapObject();
    SetSpell(this, nullptr);
    area = {};
    points.clear();
    inside.clear();
}

bool MagicTrap::RunTick() {
    CMapObject::Update();
    auto* world = WorldContext();
    if (!world->IsServer() || !spell) return true;
    if (period <= 0) {
        UpdateVictims();
        return true;
    }
    if (countdown > 0) --countdown;
    if (countdown <= 0) FireAtOccupants();
    return true;
}

void MagicTrap::FireAtOccupants() {
    WorldServer* context = ServerWorld();
    std::vector<CUnit*> found;
    if (flags & MapObjectFlag::Armed) {
        FindTrapVictims(context, &area, sides, &found);
        if (!found.empty()) CastAtUnits(&found);
    }
}

int MagicTrap::ForgetObject(EngineObject* object) {
    std::erase_if(inside, [object](const HeldRef<CUnit>& unit) { return unit.Get() == object; });
    return 0;
}
