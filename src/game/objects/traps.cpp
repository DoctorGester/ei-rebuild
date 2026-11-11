// Magic traps: who is inside, casting at newcomers, set-up from the zone record, lifetime, Save and Load and the
// update. See objects/traps.h and docs/notes/units.md ("Magic traps").
#include "game/objects/traps.h"

#include "engine/core/base_manager.h"
#include "engine/core/core_streams.h"
#include "engine/core/engine_object.h"
#include "game/app/singletons.h"
#include "game/objects/map_object.h"
#include "game/ai/sector_map.h"
#include "game/spells/spell.h"
#include "game/units/unit.h"
#include "game/ai/unit_ai.h"
#include "game/world/world.h"
#include "lib/bytes.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/str.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {

// Is `unit` (its x / y) inside a circle, and alive? The first circle that holds it decides; a NaN
// distance is outside.
bool StaysInside(MagicTrap* trap, MapObject* unit) {
    int count = lib::Count(trap->area);
    const TrapCircle* circle = trap->area.data();
    float x = unit->localPosition.x;
    float y = unit->localPosition.y;
    for (int i = 0; i < count; ++i, ++circle) {
        float dx = x - circle->x;
        float dy = y - circle->y;
        if (circle->radius * circle->radius > LengthSquared2D(dy, dx)) return !unit->IsDead();
    }
    return false;
}

constexpr float kFar = 1e30f;                        // a squared distance

constexpr MapObjectFlag kTrapFlags = MapObjectFlag::Armed | MapObjectFlag::NotDrawn;
// The fields saved after the points: period, countdown, single target, sides.
void* TailFields(MagicTrap* trap, int i) {
    void* fields[] = {&trap->period, &trap->countdown, &trap->singleTarget, &trap->sides};
    return fields[i];
}
constexpr int kTailFieldCount = 4;

// The spell, a counted reference: the new one's count raised before the old one is released.
void SetSpell(MagicTrap* trap, Spell* spell) {
    trap->spell.Assign(spell);
}

} // namespace

void MagicTrap::UpdateVictims() {
    std::erase_if(inside, [this](const HeldRef<Unit>& unit) { return !StaysInside(this, unit); });

    if (!(flags & MapObjectFlag::Armed)) return;
    WorldServer* context = ServerWorld();
    std::vector<Unit*> found;
    std::vector<Unit*> newcomers;
    FindTrapVictims(context, area, sides, &found);
    for (Unit* unit : found) {
        if (lib::Contains(inside, unit)) continue;
        inside.emplace_back(unit);
        newcomers.push_back(unit);
    }
    if (!newcomers.empty()) CastAtUnits(&newcomers);
}

void FindTrapVictims(WorldServer* context, const std::vector<TrapCircle>& area, std::uint32_t sides,
                                std::vector<Unit*>* out) {
    out->clear();
    SectorMap* sectors = &context->aiMap.sectorMap;
    for (int i = 0; i < std::ssize(area); ++i) {
        const TrapCircle& circle = area[i];
        Vector3 point = {circle.x, circle.y, 0.0f};
        for (Unit* unit : sectors->FindUnitsInRadius(point, circle.radius)) {
            if (unit->IsDead()) continue;
            int side = unit->side;
            if (!(sides & (1u << (side & 31)))) continue;   // SHL by CL: the count is taken mod 32
            if (!lib::Contains(*out, unit)) out->push_back(unit);   // a unit in two circles is one victim
        }
    }
}

void MagicTrap::CastAtUnits(std::vector<Unit*>* units) {
    if (units->empty() || !spell) return;
    const Vector3& origin = localPosition;
    Vector3 from = origin;
    auto* world = OwningWorld();
    from.z = world->RawSurfaceHeightAt(from.x, from.y) + from.z;
    if (!(spell->targetFlags & SpellTargetFlag::AtUnits) && std::ssize(points) != 0) {
        for (int i = 0; i < std::ssize(points); ++i) {
            const Vector2& point = points[i];
            Vector3 at = {point.x, point.y, 0.0f};
            g_serverWorld.CastSpell(spell, nullptr, from, nullptr, at, 0);
        }
    } else {
        if (singleTarget != 0) {
            if (units->empty()) return;
            Unit* closest = nullptr;
            for (Unit* unit : *units) {
                Vector3 d = unit->localPosition - origin;
                if (LengthSquared(d) < kFar) closest = unit;
            }
            if (!closest) return;
            units->clear();
            units->push_back(closest);
        }
        for (Unit* unit : *units) g_serverWorld.CastSpell(spell, nullptr, from, unit, unit->localPosition, 0);
    }
    countdown = period;
}

void MagicTrap::SetUp(const MagicTrapRecord* record) {
    sides = record->sides;
    SpellManager* manager = GetManagerContext()->GetSpellManager();
    Spell* made = manager->CreateSpellFromText(record->spell);
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
    MapObject::Create();
    flags |= kTrapFlags;
    SetSpell(this, nullptr);
    area.clear();
    points.clear();
    inside.clear();
}

MagicTrap::~MagicTrap() {
    MapObject::Destroy();
    SetSpell(this, nullptr);
    area.clear();
    points.clear();
    inside.clear();
}

void MagicTrap::Load(ByteStream* stream) { LoadTrap(stream); }
void MagicTrap::Save(ByteStream* stream) { SaveTrap(stream); }

void MagicTrap::SaveTrap(ByteStream* stream) {
    MapObject::Save(stream);
    stream->WriteId(spell);
    stream->WriteCount(area.size());
    for (int i = 0; i < std::ssize(area); ++i) stream->WriteValue(area[i]);
    SaveUnitList(inside, stream);
    stream->WriteCount(points.size());
    for (int i = 0; i < std::ssize(points); ++i) stream->WriteValue(points[i]);
    for (int i = 0; i < kTailFieldCount; ++i) {
        stream->Write(TailFields(this, i), 4);
    }
}

void MagicTrap::LoadTrap(ByteStream* stream) {
    MapObject::Load(stream);
    SpellManager* manager = GetManagerContext()->GetSpellManager();
    const int spellId = stream->ReadId();
    SetSpell(this, spellId == kNoObjectId ? nullptr : manager->FindObjectAs<Spell>(spellId));
    area.resize(stream->ReadCount());
    for (int i = 0; i < std::ssize(area); ++i) stream->ReadValue(area[i]);
    inside = LoadUnitList(stream, OwningWorld());
    points.resize(stream->ReadCount());
    for (int i = 0; i < std::ssize(points); ++i) stream->ReadValue(points[i]);
    for (int i = 0; i < kTailFieldCount; ++i) stream->Read(TailFields(this, i), 4);
}

// --- Create, Destroy, the update, firing, forgetting a removed object -------------

void MagicTrap::Create() {
    MapObject::Create();
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
    MapObject::Destroy();
    SetSpell(this, nullptr);
    area = {};
    points.clear();
    inside.clear();
}

bool MagicTrap::RunTick() {
    MapObject::Update();
    auto* world = OwningWorld();
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
    std::vector<Unit*> found;
    if (flags & MapObjectFlag::Armed) {
        FindTrapVictims(context, area, sides, &found);
        if (!found.empty()) CastAtUnits(&found);
    }
}

int MagicTrap::ForgetObject(EngineObject* object) {
    std::erase_if(inside, [object](const HeldRef<Unit>& unit) { return unit.Get() == object; });
    return 0;
}

// --- Trap object -------------------------------------------------------------------------------------

// Placed traps (objects/traps.h) and the magic trap's class slots and deleting destructor.

namespace {
const char* const kTrapName = "CTrapObject";
const char* const kMagicTrapName = "CMagicTrapObject";
// The script the trap runs when set off (x, y, z).
const char* const kTrapScript = "Worldscript( CreateParticleSource( 100, %g, %g, %g, 1, \"Mushroom\"))";
constexpr float kTriggerRadius = 1.0f;
} // namespace

bool LockKey::AcceptedByTrap() const { return true; }

TrapObject::TrapObject() {
    MapObject::Create();
    flags &= ~MapObjectFlag::NotDrawn;
}

void TrapObject::Create() {
    MapObject::Create();
    flags &= ~MapObjectFlag::NotDrawn;
}

bool TrapObject::RunTick() {
    Update();
    if (!(flags & MapObjectFlag::Armed)) return false;
    if (!OwningWorld()->IsServer()) return false;
    auto* world = ServerWorld();
    std::vector<Unit*> units = world->aiMap.sectorMap.FindUnitsInRadius(localPosition, kTriggerRadius);
    if (!units.empty()) {
        std::string line;
        float x = localPosition.x;
        float y = localPosition.y;
        float z = world->RawSurfaceHeightAt(x, y);
        line = str::Format(kTrapScript, static_cast<double>(x), static_cast<double>(y), z);
        TheMissionScript()->Load(&line, 1);
        flags &= ~MapObjectFlag::Armed;
    }
    return false;
}

bool TrapObject::Disarm(const LockKey* key) {
    if (!(flags & MapObjectFlag::Armed) || !key->AcceptedByTrap()) return false;
    flags &= ~MapObjectFlag::Armed;
    return true;
}

int TrapObject::KeyFits(const LockKey* key) { return key->AcceptedByTrap(); }

void TrapObject::LoadTrapObject(ByteStream* stream) {
    MapObject::Load(stream);
    stream->ReadValue(savedValue);
}

void TrapObject::SaveTrapObject(ByteStream* stream) {
    MapObject::Save(stream);
    std::uint32_t value = savedValue;
    stream->WriteValue(value);
}

void TrapObject::Load(ByteStream* stream) { LoadTrapObject(stream); }
void TrapObject::Save(ByteStream* stream) { SaveTrapObject(stream); }
bool TrapObject::ApplyScience(const LockKey* key, EngineObject*) { return Disarm(key); }

const char* TrapObject::ClassName() { return kTrapName; }
ClassId TrapObject::GetClassId() { return ClassId::TrapObject; }
const char* MagicTrap::ClassName() { return kMagicTrapName; }
ClassId MagicTrap::GetClassId() { return ClassId::MagicTrap; }
