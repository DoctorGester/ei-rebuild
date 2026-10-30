// Unit's own virtuals: the per-tick update and the virtuals that pass map object
// events on to the unit's controller, plus standing on the ground. See unit.h and docs/notes/units.md.
#include "combat.h"

#include "ai_map.h"
#include "ai_motion.h"
#include "game_singletons.h"
#include "item_db.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "object_tables.h"
#include "unit.h"
#include "unit_create.h"
#include "unit_db.h"
#include "world.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"
#include "lib/vecmath.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace {
constexpr const char* kUnitClassName = "CUnit";
constexpr std::uint8_t kMovesOnWater = 2;     // UnitParams::movementKind

} // namespace

// The map object's (map_object.cpp) and the 3D object's (map_object_figures.cpp) bodies these slots run are
// called directly.

void Unit::Tick() {
    controller->RunTick();
    Update();
}

void Unit::RecomputeScale() {
    MapObject::RecomputeScale();
    Controller()->Resettle();
}

void Unit::EffectsChanged() {
    Controller()->EffectsChanged();
}

void Unit::ApplyBaseScale() { SetFigureScale(&baseScale.x); }

void Unit::LoadFigures() {
    if (flags & MapObjectFlag::FiguresLoaded) return;
    Object3D::LoadFigures();
    Controller()->FiguresLoaded();
}

void Unit::AttachObject(EngineObject* object) {
    if (!object || SideTypeOf(object) != SideObjectType::Unit) {
        MapObject::AttachObject(object);
        return;
    }
    if (controller) {
        Controller()->unit = nullptr;
        controller->Release();
    }
    controller = object;
    Controller()->unit = this;
    controller->AddRef();
}

int Unit::StatsChanged() {
    PackVitals();
    if (controller) Controller()->RepackStats();
    return 1;
}

void Unit::ScaleEffectChanged(bool scaleEffect) {
    if (controller) Controller()->RefreshVitals(scaleEffect);
}

void Unit::SetStats(const UnitStats* stats) {
    ClassId classId = controller->GetClassId();
    if (classId != ClassId::UnitServer) return;
    ServerController()->SetStats(stats);
}

void Unit::UpdateSizeClasses() {
    Controller()->UpdateSizeClasses();
}

float* Unit::StandPoint(float* out) {
    float x = localPosition.x;
    float y = localPosition.y;
    float lift = ModeSpeed();   // inlined
    World* world = OwningWorld();
    double height;   // the world's double, or the surface plus z (one rounding each)
    if (params.movementKind == kMovesOnWater) {
        if (controller->GetClassId() == ClassId::UnitServer) {
            height = pc24::Add(world->SurfaceHeightAt(x, y), localPosition.z);
        } else {
            height = pc24::Add(world->SurfaceHeightAt(x, y), heightOffset);
        }
    } else {
        height = world->GroundHeightAt(x, y);
    }
    out[0] = x;
    out[1] = y;
    out[2] = pc24::Add(height, lift);
    return out;
}

void Unit::PlaceOnGround(float offset) {
    float x = localPosition.x;
    float y = localPosition.y;
    heightOffset = offset;
    World* world = OwningWorld();
    if (params.movementKind == kMovesOnWater) {
        if (controller->GetClassId() == ClassId::UnitServer) {
            localPosition.z = pc24::Add(world->SurfaceHeightAt(x, y), localPosition.z);
        } else {
            localPosition.z = pc24::Add(world->SurfaceHeightAt(x, y), heightOffset);
        }
    } else {
        localPosition.z = static_cast<float>(world->GroundHeightAt(x, y));
    }
}

void Unit::SetFromStats(UnitStats* stats) {
    stats->Pack(&params);
    float curve = stats->HitPointCurve();
    float limit = ScaleHitPointsByEffects(this, curve);
    // Truncated as _ftol does (the low half of the 64-bit integer; 0 for a NaN).
    packedVitals.maxHitPoints = static_cast<std::int16_t>(static_cast<std::int64_t>(limit));
    packedVitals.hitPoints = static_cast<std::int16_t>(static_cast<std::int64_t>(stats->Reduce(limit)));

}

int Unit::Level() {
    if (IsTextKey(id)) return Controller()->Level();
    const auto* prototype =
        unitdb::PrototypeRecord(prototypeId);
    return static_cast<int>(prototype->level);
}

// --- The virtuals -------------------------------------------------------------

void Unit::Destroy() { DestroyUnit(); }
void Unit::ClearState() { ReleaseUnit(); }
bool Unit::RunTick() {
    Tick();
    return true;
}
const char* Unit::GetName() { return scriptName.c_str(); }
void Unit::Load(ByteStream* stream) { LoadUnit(stream); }
void Unit::Save(ByteStream* stream) { SaveUnit(stream); }
const char* Unit::ClassName() { return kUnitClassName; }
ClassId Unit::GetClassId() { return ClassId::Unit; }
bool Unit::IsSynchronised() { return true; }
Vector3 Unit::EyePoint() {
    Vector3 out;
    StandPoint(&out.x);
    return out;
}
bool Unit::IsLargeUnit() { return obstacle != 0; }
bool Unit::IsDead() { return dead; }
void Unit::UpdateMoveFactors() { Controller()->MoveFactorsChanged(); }
void Unit::RefreshWounds() { Controller()->RefreshWounds(); }
double Unit::MoveSpeed() {
    return GetMoveSpeeds().forward;
}
float Unit::Radius() { return unitRadius; }

// --- Unit prototype stats ----------------------------------------------------------------------------

// Unit::TakePrototype: a unit's packed vitals and parameters as its prototype gives them (the zone loader,
// before the object is placed). See unit.h.

void Unit::TakePrototype(std::string_view prototype) {
    // The stack copies start uninitialised; the resets and the builder (its defaults too, for an unknown
    // prototype) write every field the packers read.
    UnitVitals vitals{};
    UnitStats stats{};
    vitals.Reset();
    stats.Reset();
    MapObjectManagerOf(this)->BuildFromPrototype(&vitals, &stats, prototype);
    vitals.Pack(&packedVitals);
    stats.Pack(&params);
}

// --- Unit size class ---------------------------------------------------------------------------------

// A unit's size class, standing height and radius. See unit.h and docs/notes/units.md
// ("Creation, databases, save").

namespace {

constexpr float kHeightScale = 0.9f;
constexpr float kRadiusExtra = 0.05f;
constexpr float kPlayerExtra = 0.2f;
constexpr float kMaxHeight = 2.0f;
constexpr float kDeepPrototype = 1.5f;
constexpr float kShallowHeight = 0.01f;
constexpr float kDefaultRadius = 0.75f;

} // namespace

void Unit::UpdateSizeClass() {
    Controller()->UpdateMoveFactors();   // the server's controller; empty on the others
    if (controller->GetClassId() != ClassId::UnitServer) {
        sizeClass = params.movementKind == kMovesOnWater ? 0 : 3;
        unitRadius = kDefaultRadius;
        return;
    }
    // The server controller's size classes, read unsigned.
    const UnitStats& combat = ServerController()->stats;
    if (moveMode == 0) sizeClass = static_cast<std::uint16_t>(combat.sizeClass20);
    else if (moveMode == 1) sizeClass = static_cast<std::uint16_t>(combat.sizeClass60);
    else sizeClass = static_cast<std::uint16_t>(combat.sizeClass);

    // The larger half extent, selected bitwise: above.y when above.x is below it or NaN.
    float extentX = bounds.boxMax.x;
    float extentY = bounds.boxMax.y;
    float extent = MaxKeepB(extentX, extentY);
    float height = extent * kHeightScale;
    standingHeight = height;
    unitRadius = height + kRadiusExtra;
    if (player) standingHeight = height + kPlayerExtra;
    // At most 2, selected the same way: 2 when below the height or NaN.
    float limited = standingHeight;
    standingHeight = MinKeepA(kMaxHeight, limited);
    if (sizeClass != 0) return;
    // The prototype by prototypeId; out of range it reads address 0x24 (kept).
    auto* prototype = unitdb::PrototypeRecord(prototypeId);
    // Only for a value above 1.5 (NaN not).
    if (prototype->height > kDeepPrototype) standingHeight = kShallowHeight;
}

// --- Unit points -------------------------------------------------------------------------------------

// Two Unit virtuals giving points of the unit: the position predicted along its
// path and the point its current animation acts on. See docs/notes/units.md ("Unit points").

namespace {

constexpr int kOrderPath = 2;
const char* const kNoEntry = "Can't find animation entry index\n %i\n for \"%s\".\n";

} // namespace

// A dead unit gives its action point; otherwise a unit following a path order (kind 2) without flag
// 0x8000000 gives its motion's position at `time`, and any other its position.
static Vector3 PredictPosition(Unit* unit, int time) {
    // The controller's motion virtual: its motion (the order in its first 0x20 bytes).
    PathOrder* path = unit->Controller()->Path();
    if (unit->IsDead()) return unit->AimPoint();
    MotionOrder* order = &path->motionOrder;
    if (order->kind == kOrderPath && !(unit->flags & MapObjectFlag::Planned)) {
        MotionSample sample = unit->motion.Sample(unit->OwningWorld(), order, static_cast<float>(time));
        float toWorld = 0.5f;
        return {toWorld * sample.x, toWorld * sample.y, unit->localPosition.z};
    }
    return unit->localPosition;
}

// For a dead unit, the point the unit's current animation acts on: a death
// animation offsets the position along the orientation away from its BlowSide: Back -Y, Front +Y by the
// box's max Y, MinusX +X, PlusX -X by its max X. The point is kept only where the AI map lets size
// class 3 stand (on the server); otherwise, and for a living unit, the position.
static Vector3 ActionPoint(Unit* unit) {
    if (!unit->IsDead()) return unit->localPosition;
    auto index = static_cast<std::uint8_t>(unit->animationEntry);
    Vector3 offset = {0.0f, 0.0f, 0.0f};
    const FigureAnimation* entry = unit->AnimationAt(index);
    if (!entry) {
        game::DebugLog(kNoEntry, static_cast<unsigned>(index), unit->name);
    } else if (entry->Kind() == AnimationKind::Death) {
        BlowSide side = entry->Side();
        Vector3 axis;
        if (side == BlowSide::Front) {
            axis = unit->localRotation.AxisY();
            float reach = unit->bounds.boxMax.y;
            offset = axis * reach;
        } else if (side == BlowSide::Back) {
            axis = unit->localRotation.AxisY();
            float reach = unit->bounds.boxMax.y;
            offset = -axis * reach;
        } else if (side == BlowSide::MinusX) {
            axis = unit->localRotation.AxisX();
            float reach = unit->bounds.boxMax.x;
            offset = axis * reach;
        } else if (side == BlowSide::PlusX) {
            axis = unit->localRotation.AxisX();
            float reach = unit->bounds.boxMax.x;
            offset = -axis * reach;
        }
    }
    // The world's IsServer; the server's AI map.
    auto* world = unit->ServerWorld();
    if (world->IsServer()) {
        Vector3 point = {offset.x + unit->localPosition.x, offset.y + unit->localPosition.y, offset.z + unit->localPosition.z};
        if (!world->aiMap.IsPassableForClass(3, point)) return unit->localPosition;
    }
    return {offset.x + unit->localPosition.x, offset.y + unit->localPosition.y, offset.z + unit->localPosition.z};
}

Vector3 Unit::PositionAt(int time) { return PredictPosition(this, time); }
Vector3 Unit::AimPoint() { return ActionPoint(this); }

// --- Unit vision -------------------------------------------------------------------------------------

// A unit's sight on the client (its range by daylight, marked on the client world as time moves on), a
// weapon prototype's skill bonus and the map object's visibility level.

namespace {

constexpr float kPercent = 0.01f;
constexpr float kNoSkill = 0.0f;

} // namespace

// The skill bonus byte of the weapon prototype `itemId >> 16`: skillBonus[0] for weapon types 0-4 and 8,
// skillBonus[1] for 5 and 6. An unknown prototype reads its type from address 0x0C (it faults),
// other types index 0x7FFFFFFF bytes on (kept).
float WeaponPrototypeSkillBonus(std::uint32_t itemId) {
    const auto* weapon =
        itemdb::WeaponAt(itemId >> 16);
    int type = weapon ? weapon->type : *reinterpret_cast<const volatile int*>(std::uintptr_t{0x0C});   // faults like the game
    int skill = 0x7FFFFFFF;
    if (type < 5 || type == 8) skill = 0;
    else if (type == 5 || type == 6) skill = 1;
    if (!weapon) return kNoSkill;
    return weapon->skillBonus[skill];
}

float UnitParams::VisionRange(float daylight) {
    float day = perception[1] * daylight * kPercent;
    return (day + (1.0f - daylight)) * perception[0];
}

void Unit::AdvanceTime(float time) {
    if (controller) Controller()->AdvanceTime(time);
    if (!IsTextKey(id)) return;
    float daylight = static_cast<float>(g_clientWorld.NightLevel());
    float range = params.VisionRange(daylight);

    if (moveMode == 0) range = range;
    g_clientWorld.MarkFootprintsInSight(&localPosition.x, range);
}

int MapObject::SetVisibilityLevel(float level) {
    MapObject* slots = this;
    EI_NAN_AUDIT(level);
    if (level == 0.0f) return slots->SetVisibilityMode(0);
    if (level == 1.0f) return slots->SetVisibilityMode(1);
    return slots->SetVisibilityMode(2);
}
