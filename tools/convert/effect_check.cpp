// ei-convert --effect-check: the original particle types' sources against the reference recorded from their C++
// emitters (effect_check.h; docs/notes/effects.md, "Checks").
#include "effect_check.h"

#include "effect_files/reference.h"
#include "game/app/singletons.h"
#include "engine/core/random.h"
#include "game/effects/effects.h"
#include "engine/particles/particle_data.h"
#include "engine/models/texture.h"
#include "game/objects/map_object.h"
#include "game/objects/map_object_manager.h"
#include "engine/scene/light.h"
#include "engine/scene/scene_renderer.h"
#include "game/terrain/terrain.h"
#include "game/units/unit.h"
#include "game/world/world.h"

#include "lib/bit_flags.h"
#include "lib/maths.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace effect_check {

namespace {

// What a case does to its source or carrier before a tick.
enum class Event : std::uint8_t {
    Scale,          // ScaleSize(value), as an effect object's new size does
    StopEmitting,
    StartEmitting,
    Stop,
    Remove,         // the carrier leaves the world (the tick lets go of it)
    Attach,         // the source follows the carrier (back in the world)
    Velocity,       // the carrier's (or a free source's) movement per tick from now
    Alpha,          // the source's alpha and typeParam, as game code sets them (a zone exit's half extents)
    Param,
    Turn,           // turnCos and turnSin of the angle `value` (a zone exit's)
    Extent,         // the source's extent (a zone exit's red: x not 0; a wall's length)
    Wind,           // the world's wind: its direction `vector` and strength `value`
    Yaw,            // the carrier turned to `value` radians about z
    Run,            // a unit carrier runs (MoveMode::Run)
    Walk,           // a unit carrier walks
    Die,            // a unit carrier dies (IsDead)
    Path,           // a path marker's glows: the carrier's points PositionAt(i + tick) for i up to `value` (none
                    // without a carrier), as PlayerClient::OnPathMarker hands them over
    Kind,           // a path marker's kind (0 green, 1 grey, 2 orange)
};

struct Step {
    int tick = 0;
    Event event = Event::Scale;
    float value = 0.0f;
    Vector3 vector;
};

struct Case {
    const char* name;
    ParticleType type;
    bool carried;           // attached to the carrier at the start
    float size;             // the effect object's size (ScaleSize(size / baseSize), as EffectParticle::Spawn)
    int ticks;
    std::vector<Step> steps;
    bool unit = false;      // the carrier is a hero (Unit) instead of a stone
    bool path = false;      // the carrier is a stone walking a path (PathStone)
};

// A stone whose path (PositionAt) steps every third tick along a curve, so some of its points repeat (ShapeChange's
// glows skip a point the last one was at).
class PathStone final : public CStone {
public:
    Vector3 PositionAt(int time) override {
        const int step = time / 3;
        return {localPosition.x + static_cast<float>(step % 17) * 0.35f, localPosition.y + lib::Sin(static_cast<float>(step) * 0.4f) * 2.0f,
                localPosition.z};
    }
};

const Vector3 kStill = {0.0f, 0.0f, 0.0f};

// Each type's cases: carried and free, moving (a fast carrier gives distance tries), rescaled, stopped and started
// again, the carrier gone and back.
const std::vector<Case>& Cases() {
    using enum ParticleType;
    using enum Event;
    static const std::vector<Case> cases = {
        {"FireStar on a unit", FireStar, true, 1.25f, 8, {}},
        {"FireStar on a fast unit", FireStar, true, 1.0f, 8, {{1, Velocity, 0.0f, {1600.0f, 0.0f, 0.0f}}, {3, Velocity, 0.0f, kStill}}},
        {"FireStar free", FireStar, false, 1.0f, 4, {}},
        {"FireStar started again", FireStar, true, 0.8f, 12, {{5, StartEmitting}, {9, Remove}, {10, StartEmitting}}},
        {"AcidStar on a unit", AcidStar, true, 0.7f, 6, {}},
        {"CurseStars on a walking unit", CurseStars, true, 1.0f, 60,
         {{1, Velocity, 0.0f, {0.05f, 0.02f, 0.0f}}, {30, Remove}}},
        {"CurseStars rescaled", CurseStars, true, 2.0f, 70,
         {{10, Scale, 0.5f}, {15, Scale, 3.0f}, {25, StopEmitting}, {30, StartEmitting}, {40, Stop}}},
        {"CurseStars free, then carried", CurseStars, false, 1.0f, 50,
         {{4, Attach}, {6, Velocity, 0.0f, {-0.03f, 0.04f, 0.01f}}, {20, Remove}, {25, Attach}, {30, StartEmitting}}},
        {"CurseStars stopped, let go, carried again", CurseStars, true, 1.5f, 70,
         {{10, StopEmitting}, {35, Remove}, {40, Attach}, {45, StartEmitting}}},
        {"PortalStar free", PortalStar, false, 1.0f, 26,
         {{10, StopEmitting}, {14, StartEmitting}, {16, Scale, 2.0f}, {20, Stop}}},
        {"PortalStar on a moving carrier", PortalStar, true, 0.6f, 16,
         {{1, Velocity, 0.0f, {0.2f, -0.1f, 0.0f}}, {10, Remove}}},
        {"Sparks on a unit", Sparks, true, 1.2f, 10, {}},
        {"Sparks on a moving unit, started again", Sparks, true, 0.9f, 16,
         {{1, Velocity, 0.0f, {1.5f, 0.4f, 0.0f}}, {3, StartEmitting}, {4, Velocity, 0.0f, kStill}, {11, StartEmitting}}},
        {"Sparks let go", Sparks, true, 1.0f, 10, {{3, Remove}}},
        {"FeebleMind on a unit", FeebleMind, true, 1.0f, 40, {}},
        {"FeebleMind rescaled", FeebleMind, true, 1.6f, 60,
         {{6, Scale, 0.5f}, {12, Scale, 2.5f}, {20, StopEmitting}, {26, StartEmitting}, {40, Stop}}},
        {"FeebleMind on a walking unit, let go", FeebleMind, true, 0.7f, 45,
         {{1, Velocity, 0.0f, {0.05f, -0.03f, 0.0f}}, {20, Remove}, {26, Attach}}},
        {"ZoneExit", ZoneExit, false, 1.0f, 30, {{1, Alpha, 3.0f}, {1, Param, 1.5f}, {1, Turn, 0.7f}}},
        {"ZoneExit red, resized", ZoneExit, false, 1.0f, 40,
         {{1, Alpha, 2.0f}, {1, Param, 4.0f}, {1, Turn, -2.1f}, {1, Extent, 0.0f, {1.0f, 0.0f, 0.0f}}, {12, Alpha, 6.5f},
          {12, Param, 7.0f}, {25, StopEmitting}, {30, StartEmitting}, {35, Stop}}},
        {"ZoneExit as made", ZoneExit, false, 1.0f, 20, {}},
        {"FireBall flying", FireBall, false, 1.0f, 30,
         {{1, Velocity, 0.0f, {0.3f, 0.1f, -0.02f}}, {20, Velocity, 0.0f, kStill}}},
        {"FireBall on a fast unit, rescaled", FireBall, true, 1.5f, 30,
         {{1, Velocity, 0.0f, {-0.6f, 0.45f, 0.05f}}, {10, Scale, 0.5f}, {15, StopEmitting}, {18, StartEmitting}, {24, Remove}}},
        {"FireBall still", FireBall, false, 0.8f, 20, {}},
        {"FireArrow flying", FireArrow, false, 1.0f, 30,
         {{1, Velocity, 0.0f, {0.5f, -0.2f, 0.05f}}, {16, StopEmitting}, {20, StartEmitting}}},
        {"FireArrow still, then flying", FireArrow, false, 1.0f, 25,
         {{6, Velocity, 0.0f, {0.0f, 0.4f, 0.0f}}, {14, Scale, 2.0f}, {18, Stop}}},
        {"RikArrowSmoke flying", RikArrowSmoke, false, 0.7f, 25, {{1, Velocity, 0.0f, {-0.35f, -0.35f, 0.1f}}}},
        {"AcidRay flying", AcidRay, false, 1.0f, 30,
         {{1, Velocity, 0.0f, {0.25f, 0.3f, 0.0f}}, {12, Scale, 1.5f}, {22, Velocity, 0.0f, kStill}}},
        {"AcidRay on a rising unit", AcidRay, true, 0.6f, 20, {{1, Velocity, 0.0f, {0.0f, 0.0f, 0.2f}}}},
        {"CampFire in the wind", CampFire, false, 1.0f, 45, {{1, Wind, 0.6f, {0.6f, 0.8f, 0.0f}}}},
        {"CampFire rescaled, stopped", CampFire, false, 1.5f, 60,
         {{1, Wind, 0.3f, {-0.5f, 0.7f, 0.1f}}, {10, Scale, 0.5f}, {20, StopEmitting}, {25, StartEmitting}, {40, Stop}}},
        {"CampFire on a moving carrier", CampFire, true, 0.8f, 40, {{1, Velocity, 0.0f, {0.1f, 0.05f, 0.0f}}, {20, Remove}}},
        {"Fire in the wind", Fire, false, 1.0f, 30, {{1, Wind, 0.8f, {0.0f, 1.0f, 0.0f}}, {12, Scale, 2.0f}}},
        {"Fire on a moving carrier", Fire, true, 1.2f, 25,
         {{1, Velocity, 0.0f, {-0.2f, 0.1f, 0.02f}}, {10, StopEmitting}, {15, Remove}}},
        {"Smoke in the wind", Smoke, false, 1.0f, 50, {{1, Wind, 0.5f, {0.7f, -0.7f, 0.0f}}, {30, Stop}}},
        {"Tendril along a wall", Tendril, false, 1.0f, 40,
         {{1, Extent, 0.0f, {3.0f, 1.0f, 0.0f}}, {1, Wind, 0.4f, {1.0f, 0.0f, 0.0f}}}},
        {"Tendril lengthened", Tendril, false, 0.7f, 30,
         {{1, Extent, 0.0f, {0.2f, 0.1f, 0.0f}}, {8, Extent, 0.0f, {9.0f, -4.0f, 0.5f}}, {15, Scale, 1.5f}}},
        {"Tendril as made", Tendril, false, 1.0f, 12, {}},
        {"VulcanSmoke", VulcanSmoke, false, 1.0f, 140, {{100, Stop}}},
        {"VulcanSmoke rescaled", VulcanSmoke, false, 0.6f, 40, {{10, Scale, 2.0f}, {20, StopEmitting}, {30, StartEmitting}}},
        {"Portal", Portal, false, 1.0f, 60, {}},
        {"Portal small, stopped, moving", Portal, false, 0.6f, 50,
         {{1, Velocity, 0.0f, {0.05f, 0.0f, 0.02f}}, {15, StopEmitting}, {22, StartEmitting}, {30, Scale, 2.5f}, {45, Stop}}},
        {"TeleportRing on a unit", TeleportRing, true, 1.0f, 20, {}},
        {"TeleportRing carried late, let go", TeleportRing, false, 1.3f, 30,
         {{3, Attach}, {5, Velocity, 0.0f, {0.0f, 0.1f, 0.05f}}, {10, Scale, 0.5f}, {15, Remove}, {20, Attach}, {22, Stop}}},
        {"Teleport out", Teleport, false, 1.0f, 45, {}},
        {"Teleport in, started again", Teleport, false, -1.0f, 45, {{38, StartEmitting}}},
        {"Teleport moving, rescaled", Teleport, false, 0.8f, 30,
         {{1, Velocity, 0.0f, {0.1f, 0.0f, 0.0f}}, {10, Scale, 1.5f}, {20, Stop}}},
        {"Banshee on a unit", Banshee, true, 1.0f, 16, {}},
        {"Banshee carried late, rescaled", Banshee, false, 1.4f, 16,
         {{3, Attach}, {6, Scale, 0.5f}, {9, Velocity, 0.0f, {0.1f, 0.0f, 0.0f}}}},
        {"BlueGas on a unit", BlueGas, true, 1.0f, 60, {}},
        {"BlueGas on a walking unit, let go", BlueGas, true, 2.0f, 70,
         {{1, Velocity, 0.0f, {0.05f, 0.03f, 0.0f}}, {10, Scale, 0.5f}, {30, Remove}, {35, Attach}, {50, StopEmitting}}},
        {"Nuke", Nuke, false, 1.0f, 60, {}},
        {"Nuke rescaled, started again", Nuke, false, 2.5f, 50, {{5, Scale, 0.8f}, {20, StartEmitting}, {28, StopEmitting}}},
        {"VisionStar1 on a unit, stopped", VisionStar1, true, 1.0f, 40, {{20, StopEmitting}}, true},
        {"VisionStar2 on a walking unit, stopped, started again", VisionStar2, true, 1.2f, 45,
         {{1, Velocity, 0.0f, {0.05f, 0.02f, 0.0f}}, {20, StopEmitting}, {30, StartEmitting}}, true},
        {"VisionStar3 let go, carried again", VisionStar3, true, 0.8f, 40, {{12, Remove}, {20, Attach}}},
        {"VisionStar4 rescaled, stopped", VisionStar4, true, 1.0f, 30, {{5, Scale, 2.0f}, {15, Stop}}, true},
        {"Regeneration on a unit, stopped", Regeneration, true, 1.0f, 35, {{15, StopEmitting}}, true},
        {"Regeneration on a turning stone, let go", Regeneration, true, 1.4f, 40,
         {{1, Yaw, 0.6f}, {8, Yaw, 2.5f}, {12, Remove}, {16, Attach}, {25, Stop}}},
        {"Stun on a unit that runs, then dies", Stun, true, 1.0f, 30,
         {{5, Run}, {10, Walk}, {12, Run}, {18, Walk}, {22, Die}}, true},
        {"Stun on a stone", Stun, true, 1.0f, 8, {}},
        {"Stun on a unit, let go", Stun, true, 1.3f, 20, {{3, Run}, {8, Remove}}, true},
        {"Stun carried late, rescaled", Stun, false, 1.0f, 20, {{3, Attach}, {8, Scale, 2.0f}, {14, StopEmitting}}, true},
        {"FeetCloud1 on a walking unit, stopped", FeetCloud1, true, 1.0f, 35,
         {{1, Velocity, 0.0f, {0.05f, -0.02f, 0.0f}}, {20, StopEmitting}}, true},
        {"FeetCloud2 on a stone, rescaled, let go", FeetCloud2, true, 1.0f, 35,
         {{5, Scale, 1.5f}, {15, Remove}, {20, Attach}}},
        {"StartTrans on a unit", StartTrans, true, 1.0f, 50, {}, true},
        {"StartTrans rescaled, let go", StartTrans, true, 1.0f, 30, {{4, Scale, 2.0f}, {10, Remove}}},
        {"Shower1 on a unit", Shower1, true, 1.0f, 15, {}, true},
        {"Shower2 with few rays", Shower2, true, 0.8f, 15, {{1, Alpha, 0.3f}}},
        {"Shower3 carried late, rescaled", Shower3, false, 1.0f, 20, {{3, Attach}, {5, Scale, 2.0f}}},
        {"Shower4 on a fast unit, started again", Shower4, true, 1.0f, 15,
         {{1, Velocity, 0.0f, {1.2f, 0.0f, 0.3f}}, {9, StartEmitting}}, true},
        {"Falling1 on a unit", Falling1, true, 1.0f, 40, {}, true},
        {"Falling2 on a stone, started again", Falling2, true, 1.0f, 40, {{3, StartEmitting}}},
        {"Falling3 on a fast stone", Falling3, true, 1.0f, 40, {{1, Velocity, 0.0f, {900.0f, 0.0f, 0.0f}}, {2, Velocity, 0.0f, kStill}}},
        {"Falling4 large", Falling4, true, 2.0f, 40, {}, true},
        {"SphereAcid on a unit", SphereAcid, true, 1.0f, 30, {}, true},
        {"SphereElectricity on a walking unit", SphereElectricity, true, 1.0f, 40,
         {{1, Velocity, 0.0f, {0.05f, 0.03f, 0.0f}}, {25, Velocity, 0.0f, kStill}}, true},
        {"SphereFire rescaled, let go", SphereFire, true, 1.0f, 35, {{10, Scale, 0.5f}, {20, Remove}, {25, Attach}}},
        {"Antimagic on a fast unit, stopped, started again", Antimagic, true, 1.0f, 30,
         {{1, Velocity, 0.0f, {0.3f, -0.1f, 0.02f}}, {15, StopEmitting}, {20, StartEmitting}}, true},
        {"SphereAcid small", SphereAcid, true, 0.05f, 15, {}},
        {"PoisonFog fading, laid again", PoisonFog, false, 2.5f, 70, {{15, StopEmitting}, {55, StartEmitting}}},
        {"PoisonFog strong, wrapping", PoisonFog, false, 1.2f, 50, {{1, Alpha, 3.5f}, {5, StopEmitting}}},
        {"PoisonFog moving fast", PoisonFog, false, 1.0f, 10,
         {{1, Velocity, 0.0f, {1600.0f, 0.0f, 0.0f}}, {3, Velocity, 0.0f, kStill}}},
        {"PoisonCloud stopped, rescaled", PoisonCloud, false, 2.0f, 40,
         {{12, StopEmitting}, {20, Scale, 2.0f}, {30, Stop}}},
        {"GroundDust1", GroundDust1, false, 1.0f, 12, {}},
        {"GroundDust2 on a moving carrier", GroundDust2, true, 1.0f, 12, {{1, Velocity, 0.0f, {0.1f, 0.0f, 0.0f}}}},
        {"GroundDust1 moving, started again", GroundDust1, false, 2.0f, 14,
         {{1, Velocity, 0.0f, {0.0f, -0.2f, 0.0f}}, {4, StartEmitting}}},
        {"Mushroom", Mushroom, false, 1.0f, 50, {}},
        {"Mushroom rescaled, started again", Mushroom, false, 1.5f, 45,
         {{8, Scale, 0.5f}, {20, StartEmitting}, {35, StartEmitting}}},
        {"Mushroom on a moving carrier", Mushroom, true, 1.0f, 30, {{1, Velocity, 0.0f, {0.1f, 0.05f, 0.0f}}, {20, Remove}}},
        {"Transform on a unit", Transform, true, 1.0f, 40, {}, true},
        {"Link", Link, false, 1.0f, 30, {{20, StopEmitting}}},
        {"Link along a slope, rescaled", Link, false, 1.0f, 25,
         {{1, Extent, 0.0f, {3.0f, 4.0f, 2.0f}}, {10, Scale, 2.0f}, {18, Stop}}},
        {"Link straight up, carried", Link, true, 0.8f, 20, {{1, Extent, 0.0f, {0.0f, 0.0f, 6.0f}}, {15, Stop}}, true},
        {"Modifier1 on a stone", Modifier1, true, 1.0f, 30, {}},
        {"Modifier3 inwards on a unit", Modifier3, true, -0.8f, 30, {}, true},
        {"Modifier4 on a unit, stopped", Modifier4, true, 1.3f, 25, {{8, StopEmitting}}, true},
        {"Modifier5 on a turned stone", Modifier5, true, 1.0f, 25, {{1, Yaw, 0.7f}}},
        {"Modifier9 small", Modifier9, true, 0.5f, 25, {}},
        {"Modifier12 carried late", Modifier12, false, 1.0f, 25, {{3, Attach}}, true},
        {"ClayRing", ClayRing, false, 2.0f, 25, {}},
        {"ClayRing inwards", ClayRing, false, -1.5f, 25, {}},
        {"ClayRing on a moving carrier", ClayRing, true, 1.0f, 25, {{1, Velocity, 0.0f, {0.1f, 0.1f, 0.0f}}}},
        {"Silence on a unit, stopped", Silence, true, 1.0f, 50, {{25, StopEmitting}}, true},
        {"Silence let go, carried again", Silence, true, 1.5f, 45, {{15, Remove}, {18, Attach}}},
        {"Silence carried late, moving", Silence, false, 1.0f, 30, {{3, Attach}, {5, Velocity, 0.0f, {0.05f, 0.0f, 0.0f}}}, true},
        {"Bag on a unit", Bag, true, 1.0f, 30, {}, true},
        {"Bag let go", Bag, true, 1.0f, 25, {{12, Remove}}},
        {"Bag on a moving stone, rescaled", Bag, true, 0.6f, 30, {{1, Velocity, 0.0f, {0.05f, 0.05f, 0.0f}}, {10, Scale, 2.0f}}},
        {"Cylinder1 on a unit", Cylinder1, true, 1.0f, 30, {}, true},
        {"Cylinder2 on a moving stone", Cylinder2, true, 1.4f, 30, {{1, Velocity, 0.0f, {0.05f, -0.05f, 0.0f}}}},
        {"Casting on a unit", Casting, true, 1.0f, 60, {}, true},
        {"Casting on a moving stone, let go", Casting, true, 1.3f, 55,
         {{1, Velocity, 0.0f, {0.05f, 0.02f, 0.0f}}, {30, Remove}}},
        {"Casting carried late, stopped, started again", Casting, false, 1.0f, 60,
         {{3, Attach}, {20, StopEmitting}, {25, StartEmitting}, {50, StartEmitting}}, true},
        {"Healing on a unit", Healing, true, 1.0f, 55, {}, true},
        {"Healing large on a stone, let go", Healing, true, 2.0f, 50, {{20, Remove}}},
        {"Healing rescaled, started again", Healing, true, 0.6f, 60, {{5, Scale, 2.0f}, {40, StartEmitting}}, true},
        {"CastingFire on a unit, stopped", CastingFire, true, 1.0f, 40, {{20, StopEmitting}}, true},
        {"CastingElectricity on a stone, let go", CastingElectricity, true, 1.2f, 35, {{15, Remove}}},
        {"CastingAcid carried late, moving", CastingAcid, false, 1.0f, 35,
         {{3, Attach}, {5, Velocity, 0.0f, {0.05f, 0.0f, 0.0f}}, {25, Stop}}, true},
        {"CastingDivination on a unit, stopped, started again", CastingDivination, true, 1.0f, 45,
         {{15, StopEmitting}, {18, StartEmitting}, {30, StopEmitting}}, true},
        {"CastingDivination on a stone, let go", CastingDivination, true, 1.0f, 30, {{12, Remove}}},
        {"CastingFire rescaled on a stone, stopped", CastingFire, true, 1.5f, 40, {{8, Scale, 0.5f}, {22, StopEmitting}}},
        {"CastingIllusion on a unit, stopped", CastingIllusion, true, 1.0f, 45, {{25, StopEmitting}}, true},
        {"CastingDomination on a moving stone, let go", CastingDomination, true, 1.0f, 40,
         {{1, Velocity, 0.0f, {0.05f, -0.03f, 0.0f}}, {20, Remove}}},
        {"CastingEnchantment carried late, rescaled", CastingEnchantment, false, 1.0f, 40,
         {{3, Attach}, {10, Scale, 2.0f}, {30, StopEmitting}}, true},
        {"CastingHealing on a unit, stopped, started again", CastingHealing, true, 0.8f, 45,
         {{20, StopEmitting}, {24, StartEmitting}, {35, Stop}}, true},
        {"FireBlast", FireBlast, false, 1.0f, 30, {}},
        {"FireBlast large, moving, started again", FireBlast, false, 2.5f, 30,
         {{1, Velocity, 0.0f, {0.2f, -0.1f, 0.0f}}, {4, Scale, 0.6f}, {10, StartEmitting}, {20, Stop}}},
        {"FireBlast on a carrier, let go", FireBlast, true, 0.8f, 25, {{1, Velocity, 0.0f, {0.05f, 0.05f, 0.0f}}, {4, Remove}}},
        {"LightningBlast", LightningBlast, false, 1.0f, 25, {}},
        {"LightningBlast large, moving", LightningBlast, false, 2.0f, 25, {{1, Velocity, 0.0f, {0.1f, 0.05f, 0.0f}}}},
        {"LightningBlast stopped before its bolt, started again", LightningBlast, false, 1.0f, 25,
         {{1, StopEmitting}, {4, StartEmitting}, {16, StartEmitting}}},
        {"LightningBlast carried, rescaled", LightningBlast, true, 1.2f, 25, {{6, Scale, 0.5f}, {10, Remove}}},
        {"Geyser", Geyser, false, 1.0f, 80, {}},
        {"Geyser rescaled, stopped, started again", Geyser, false, 1.5f, 70,
         {{10, Scale, 0.5f}, {30, StopEmitting}, {42, StartEmitting}, {60, Stop}}},
        {"Geyser on a moving carrier", Geyser, true, 1.0f, 40, {{1, Velocity, 0.0f, {0.05f, 0.02f, 0.0f}}, {30, Remove}}},
        {"Tornado moving", Tornado, false, 1.0f, 130, {{1, Velocity, 0.0f, {0.1f, 0.05f, 0.0f}}}},
        {"Tornado turning, stopped", Tornado, false, 1.4f, 140,
         {{1, Velocity, 0.0f, {-0.08f, 0.1f, 0.0f}}, {40, Velocity, 0.0f, {0.12f, 0.0f, 0.01f}}, {60, StopEmitting},
          {80, Velocity, 0.0f, kStill}, {90, Scale, 0.5f}, {120, Stop}}},
        {"Tornado on a carrier, let go", Tornado, true, 0.8f, 60, {{1, Velocity, 0.0f, {0.05f, 0.0f, 0.0f}}, {30, Remove}}},
        {"CurseHolder on a unit", CurseHolder, true, 1.0f, 120, {{20, Velocity, 0.0f, {0.05f, -0.03f, 0.0f}}, {70, Velocity, 0.0f, kStill}}, true},
        {"CurseHolder on a stone, let go", CurseHolder, true, 1.3f, 50, {{1, Velocity, 0.0f, {0.02f, 0.02f, 0.0f}}, {20, Remove}}},
        {"CurseHolder carried late, rescaled, stopped", CurseHolder, false, 0.7f, 60,
         {{5, Attach}, {10, Scale, 2.0f}, {30, StopEmitting}, {40, StartEmitting}, {50, Stop}}, true},
        {"ShapeChange green", ShapeChange, true, 1.0f, 20, {{1, Path, 40.0f}}, false, true},
        {"ShapeChange grey, short", ShapeChange, true, 1.0f, 16, {{1, Kind, 1.0f}, {1, Path, 5.0f}}, false, true},
        {"ShapeChange orange, long", ShapeChange, true, 1.0f, 20, {{1, Kind, 2.0f}, {1, Path, 300.0f}}, false, true},
        {"ShapeChange free", ShapeChange, false, 1.0f, 6, {{1, Path, 10.0f}}},
    };
    return cases;
}

std::uint64_t g_draws = 0;

void CountDraw(IsaacRandom& random, std::uint32_t, const void*) {
    if (&random == &g_particleRandom) ++g_draws;
}

// FNV-1a 64 over 32-bit words.
struct Hash {
    std::uint64_t value = 0xCBF29CE484222325ull;
    void Add(std::uint32_t word) {
        for (int i = 0; i < 4; ++i) {
            value ^= (word >> (8 * i)) & 0xFF;
            value *= 0x100000001B3ull;
        }
    }
    void Add(float f) { Add(std::bit_cast<std::uint32_t>(f)); }
    void Add(const Vector3& v) {
        Add(v.x);
        Add(v.y);
        Add(v.z);
    }
};

constexpr std::uint32_t kSourceFlags = lib::Bits(ParticleSourceFlag::Emitting) | lib::Bits(ParticleSourceFlag::AttachedOnly) |
                                       lib::Bits(ParticleSourceFlag::KeepAlive) | lib::Bits(ParticleSourceFlag::TicksWhilePaused) |
                                       lib::Bits(ParticleSourceFlag::OnPart) | lib::Bits(ParticleSourceFlag::IdlesOffScreen);

std::string Hex(float f) { return std::format("{:08X}", std::bit_cast<std::uint32_t>(f)); }
std::string Hex(const Vector3& v) { return Hex(v.x) + " " + Hex(v.y) + " " + Hex(v.z); }

// The world's wind before the first case (each case starts with it).
Vector3 g_calmDirection;
float g_calmStrength = 0.0f;

// One case run: its header line and one line per tick; `dump` (when not null) gets every particle.
std::vector<std::string> Run(const Case& c, int index, std::ofstream* dump) {
    std::vector<std::string> lines;
    g_clientWorld.windDirection = g_calmDirection;
    g_clientWorld.windStrength = g_calmStrength;
    IsaacRandom seed;
    for (int i = 0; i < 256; ++i) seed.memory[i] = static_cast<std::uint32_t>(i) * 0x9E3779B9u + static_cast<std::uint32_t>(index);
    g_particleRandom = IsaacRandom();
    g_particleRandom.InitFrom(seed);

    g_clientWorld.particleManager = std::make_unique<ParticleManager>();
    g_clientWorld.particleManager->Init(&g_clientWorld);
    // No manager: deleted at the end, as lua_check's stones. A hero is the particle manager's (a unit's effect point
    // asks its world).
    MapObject* carrier = nullptr;
    Unit* unit = nullptr;
    if (c.unit) {
        unit = new Unit();
        unit->type = static_cast<int>(MapObjectType::Hero);
        unit->SetOwner(g_clientWorld.particleManager.get());
        carrier = unit;
    } else if (c.path) {
        carrier = new PathStone();
    } else {
        carrier = new CStone();
    }
    carrier->localPosition = {12.5f, -7.25f, 3.0f};
    carrier->bounds.flags |= lib::Bits(BoundsFlag::Sphere);
    carrier->bounds.radius = 0.55f;
    carrier->bounds.center = {0.0f, 0.0f, 0.9f};
    if (unit) {
        carrier->bounds.boxMin = {-0.4f, -0.4f, -0.9f};
        carrier->bounds.boxMax = {0.4f, 0.4f, 0.9f};
    }

    ParticleSource* s = g_clientWorld.particleManager->CreateSource(static_cast<int>(c.type));
    if (!s) {
        lines.push_back(std::format("case {}: no source", c.name));
    } else {
        s->AddRef();
        s->localPosition = c.carried ? kStill : Vector3{3.0f, 4.0f, 0.5f};
        s->ScaleSize(c.size / s->baseSize);
        s->Attach(nullptr);
        if (c.carried) s->Attach(carrier);
        lines.push_back(std::format("case {}: texture {} frames {} columns {} {} max {} flags {:X} part {}", c.name,
                                    s->texture ? s->texture->name : "", s->frameCount, s->frameColumns,
                                    s->additiveBlend ? "additive" : "alpha", s->maxParticles, s->flags & kSourceFlags,
                                    static_cast<int>(s->attachPoint)));
        Vector3 velocity = kStill;
        for (int tick = 1; tick <= c.ticks; ++tick) {
            for (const Step& step : c.steps) {
                if (step.tick != tick) continue;
                switch (step.event) {
                case Event::Scale: s->ScaleSize(step.value); break;
                case Event::StopEmitting: s->StopEmitting(); break;
                case Event::StartEmitting: s->StartEmitting(); break;
                case Event::Stop: s->Stop(); break;
                case Event::Remove: carrier->flags |= lib::Bits(MapObjectFlag::Removed); break;
                case Event::Attach:
                    carrier->flags &= ~lib::Bits(MapObjectFlag::Removed);
                    s->Attach(carrier);
                    break;
                case Event::Velocity: velocity = step.vector; break;
                case Event::Alpha: s->alpha = step.value; break;
                case Event::Param: s->typeParam = step.value; break;
                case Event::Turn:
                    s->turnCos = lib::Cos(step.value);
                    s->turnSin = lib::Sin(step.value);
                    break;
                case Event::Extent: s->extent = step.vector; break;
                case Event::Wind:
                    g_clientWorld.windDirection = step.vector;
                    g_clientWorld.windStrength = step.value;
                    break;
                case Event::Yaw: carrier->localRotation = Quaternion::AxisAngle({0.0f, 0.0f, 1.0f}, step.value); break;
                case Event::Run: if (unit) unit->moveMode = MoveMode::Run; break;
                case Event::Walk: if (unit) unit->moveMode = MoveMode::Walk; break;
                case Event::Die: if (unit) unit->dead = 1; break;
                case Event::Path: {
                    s->typeParam = step.value;
                    std::vector<Vector3> points;
                    for (int i = 0; s->attachedTo && i < 2500 && !(static_cast<float>(i) > step.value); ++i)
                        points.push_back(s->attachedTo->PositionAt(i + g_clientWorld.tick));
                    effectdata::SetInput(*s, "path", points);
                    break;
                }
                case Event::Kind: effectdata::SetInput(*s, "kind", step.value); break;
                }
            }
            carrier->localPosition += velocity;
            if (!s->attachedTo) s->localPosition += velocity;
            g_draws = 0;
            const bool alive = s->RunTick();
            Hash hash;
            for (const Particle* p : s->particles) {
                hash.Add(p->pos);
                hash.Add(p->size);
                hash.Add(p->prevPos);
                hash.Add(p->prevSize);
                hash.Add(p->color);
                hash.Add(p->prevColor);
                hash.Add(static_cast<std::uint32_t>(p->frame));
                hash.Add(static_cast<std::uint32_t>(p->prevFrame));
                hash.Add(static_cast<std::uint32_t>(p->dead));
            }
            hash.Add(s->origin);
            hash.Add(s->clip.center);
            hash.Add(s->clip.boxMin);
            hash.Add(s->clip.boxMax);
            lines.push_back(std::format("{} {} flags {:X} {} particles {} draws {} hash {:016X}", tick,
                                        alive ? "alive" : "done", s->flags & kSourceFlags,
                                        s->attachedTo ? "attached" : "free", s->particles.size(), g_draws, hash.value));
            if (dump) {
                *dump << c.name << " " << lines.back() << "\n";
                *dump << "  origin " << Hex(s->origin) << " bounds " << Hex(s->clip.center) << " " << Hex(s->clip.boxMin)
                      << " " << Hex(s->clip.boxMax) << "\n";
                int i = 0;
                for (const Particle* p : s->particles)
                    *dump << std::format("  {} pos {} size {} color {:08X} frame {} prev {} {} {:08X} {} dead {}\n", i++,
                                         Hex(p->pos), Hex(p->size), p->color, p->frame, Hex(p->prevPos), Hex(p->prevSize),
                                         p->prevColor, p->prevFrame, p->dead);
            }
        }
        s->Attach(nullptr);
        s->Release();
    }
    delete carrier;
    g_clientWorld.particleManager.reset();
    return lines;
}

std::vector<std::string> Lines(std::string_view text) {
    std::vector<std::string> lines;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(at, end - at);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!line.empty() && line.front() != '#') lines.emplace_back(line);
        at = end + 1;
    }
    return lines;
}

} // namespace

int Main(const std::string& record, const std::string& dump) {
    effectdata::g_effects.Reset();
    // The sources take their textures by name (no archive opened: the check reads no game data).
    g_clientWorld.textureManager = std::make_unique<TextureManager>();
    g_clientWorld.textureManager->SetContext(&g_clientWorld);
    // A terrain without sectors: the ground is 0 everywhere (the effects that spawn on the ground).
    g_clientWorld.terrain = std::make_unique<terrain::Terrain>();
    // A sun of its own colour (Tornado's particles take it).
    auto* sun = new Light();
    sun->color[0] = 0.8f;
    sun->color[1] = 0.7f;
    sun->color[2] = 0.55f;
    Light* oldSun = g_sceneRenderer.sun.Take();
    g_sceneRenderer.sun.Assign(sun);
    g_calmDirection = g_clientWorld.windDirection;
    g_calmStrength = g_clientWorld.windStrength;
    IsaacRandom::SetDrawObserver(CountDraw);
    std::ofstream dumpFile;
    if (!dump.empty()) dumpFile.open(dump, std::ios::binary);

    std::vector<std::string> output;
    const std::vector<Case>& cases = Cases();
    for (std::size_t i = 0; i < cases.size(); ++i) {
        std::vector<std::string> lines = Run(cases[i], static_cast<int>(i), dumpFile.is_open() ? &dumpFile : nullptr);
        output.insert(output.end(), lines.begin(), lines.end());
    }
    IsaacRandom::SetDrawObserver(nullptr);
    g_sceneRenderer.sun.Take();
    delete sun;
    g_sceneRenderer.sun.Assign(oldSun);
    if (oldSun) oldSun->Release();
    g_clientWorld.textureManager.reset();
    g_clientWorld.terrain.reset();
    g_clientWorld.windDirection = g_calmDirection;
    g_clientWorld.windStrength = g_calmStrength;

    if (!record.empty()) {
        std::ofstream file(record, std::ios::binary);
        file << "# ei-convert --effect-check's reference (tools/convert/effect_check.h): each case's ticks as the type's C++\n"
                "# emitter ran them. Recorded once per type, before the type moved to data; never rewritten for a type\n"
                "# already on data.\n";
        for (const std::string& line : output) file << line << "\n";
        std::printf("effect_check: %zu cases recorded into %s\n", cases.size(), record.c_str());
        return file ? 0 : 1;
    }

    const std::vector<std::string> reference =
        Lines(std::string_view(reinterpret_cast<const char*>(kReference), sizeof(kReference)));
    int failures = 0;
    std::string current;
    std::string failedCase;
    std::size_t matched = 0;
    for (std::size_t i = 0; i < output.size() || i < reference.size(); ++i) {
        const std::string& got = i < output.size() ? output[i] : std::string("(none)");
        const std::string& want = i < reference.size() ? reference[i] : std::string("(none)");
        if (got.starts_with("case ")) current = got;
        if (got == want) {
            ++matched;
            continue;
        }
        if (current != failedCase) {
            std::printf("effect_check: %s\n  reference: %s\n  this build: %s\n", current.c_str(), want.c_str(), got.c_str());
            failedCase = current;
            ++failures;
        }
    }
    std::printf("effect_check: %zu cases, %zu of %zu lines as the reference, %d cases differ\n", cases.size(), matched,
                reference.size(), failures);
    return failures ? 1 : 0;
}

}   // namespace effect_check
