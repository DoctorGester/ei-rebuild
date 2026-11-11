// The shadow map's casters the game does not draw this frame (render::CasterMesh). Only reads the game's
// objects. See docs/notes/renderer.md, "The renderer's source files".
#include "engine/scene/light.h"
#include "game/world/world_client_shadow_casters.h"

#include "engine/scene/camera.h"
#include "engine/scene/drawn_motion.h"
#include "engine/models/figure_mesh.h"
#include "engine/models/link_figure.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "game/objects/map_object_shadow.h"
#include "engine/data/options.h"
#include "game/players/player.h"
#include "render/render.h"
#include "game/spells/spell.h"
#include "game/terrain/terrain.h"
#include "game/terrain/terrain_sector.h"
#include "engine/scene/scene_renderer.h"
#include "game/world/frame_lists.h"
#include "game/world/world.h"

#include "lib/vecmath.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint32_t kBoundsValid = 1 | 2;           // Object3D::boundsFlags
constexpr float kPartRangeSquared = 1e9f;                    // parts farther from the object are not drawn
constexpr float kLeastStrength = 0.5f;                  // as the map: units fading in cast from half
constexpr int kSectorShift = 4;                         // 16-unit sectors
constexpr std::uint32_t kHeightScanEvery = 32;          // frames between scans of the highest object

// The placement it builds (ObjectWorldMatrix of a rotation and a position), into a local.
Matrix4 Placement(const Quaternion& rotation, const Vector3& position) {
    Matrix4 m;
    ObjectWorldMatrix(&rotation, position, &m);
    return m;
}

// The object's extra scale (Object3DClientSpecific::figureScale) as Draw applies it: only when not 1, 1, 1.
bool Scaled(const Vector3& s) {
    constexpr std::uint32_t kUnitScaleBits = 0x3F800000;
    return !(std::bit_cast<std::uint32_t>(s.x) == kUnitScaleBits && std::bit_cast<std::uint32_t>(s.y) == kUnitScaleBits &&
             std::bit_cast<std::uint32_t>(s.z) == kUnitScaleBits);
}

float LargestScale(const Vector3& s) {
    return (std::max)((std::max)(std::fabs(s.x), std::fabs(s.y)), (std::max)(std::fabs(s.z), 1.0f));
}

// The object's bounding sphere in the world (its bounds' centre through its placement and extra
// scale, the radius by the largest scale); false without valid bounds.
bool WorldSphere(const Object3D* object, const Matrix4& world, const Vector3& scale, render::ShadowCasterSphere* sphere) {
    if ((object->bounds.flags & kBoundsValid) != kBoundsValid) return false;
    const Vector3& center = object->bounds.center;
    const Vector3 c{center.x * scale.x, center.y * scale.y, center.z * scale.z};
    const Vector3 out = TransformColumn(world, c);
    *sphere = {out.x, out.y, out.z, object->bounds.radius * LargestScale(scale)};
    return std::isfinite(sphere->x) && std::isfinite(sphere->y) && std::isfinite(sphere->z) && std::isfinite(sphere->radius);
}

// The top of an object's sphere, without its rotation: a bound for the ground rectangle's sweep.
float TopOf(const Object3D* object, const Vector3& scale) {
    const Vector3& center = object->bounds.center;
    const Vector3 c{center.x * scale.x, center.y * scale.y, center.z * scale.z};
    return object->localPosition.z + std::sqrt(LengthSquared(c)) + object->bounds.radius * LargestScale(scale);
}

// Hands over the parts of `object` (`unit`: a unit) as casters alone, placed by `world` and the extra scale as
// Object3DClientSpecific::DrawParts places them, each with its part set's texture.
void HandOverParts(Object3D* object, Object3DClientSpecific* half, const Matrix4& world, bool unit) {
    Matrix4 placement = world;
    const Vector3& scale = half->figureScale;
    if (Scaled(scale))
        for (int row = 0; row < 4; ++row) {
            placement.m[row * 4] *= scale.x;
            placement.m[row * 4 + 1] *= scale.y;
            placement.m[row * 4 + 2] *= scale.z;
        }
    render::BeginCaster(object, &half->casterMark, unit);
    for (const std::unique_ptr<TextureParts>& set : half->partSets) {
        if (!set) continue;
        const Surface* texture = set->texture ? set->texture->surface.Get() : nullptr;
        for (const auto& group : set->groups)
            for (FigureMesh* part : group->parts) {
                if (!part || (part->flags & FigureMeshFlag::Hidden) || LengthSquared(part->localPosition) > kPartRangeSquared)
                    continue;
                Matrix4 local, model;
                ObjectWorldMatrix(&part->localRotation, part->localPosition, &local);
                model.Multiply(placement.m, local.m);
                part->drawer->HandOverCaster(model, texture);
            }
    }
    render::EndCaster();
}

// The highest top of any object on the map (TopOf) and the largest radius, scanned again every
// kHeightScanEvery frames and for another world: how far back towards the sun a caster can stand.
struct HighestObject {
    const WorldClient* world = nullptr;
    std::uint32_t frames = 0;
    float top = 0.0f, widest = 0.0f;
};
HighestObject g_highest;

const HighestObject& Highest(const WorldClient* world) {
    if (g_highest.world == world && g_highest.frames++ % kHeightScanEvery != 0) return g_highest;
    if (g_highest.world != world) g_highest = HighestObject{world, 1, 0.0f, 0.0f};
    float top = 0.0f, widest = 0.0f;   // the ground is above 0
    const SectorGrid& grid = world->objectGrid;
    for (std::uint32_t y = 0; y < grid.height; ++y)
        for (std::uint32_t x = 0; x < grid.width; ++x) {
            const auto& cell = grid.rows[y][x];
            for (const MapObject* object : cell) {
                if (!object || !object->attached || (object->bounds.flags & kBoundsValid) != kBoundsValid) continue;
                const Vector3& scale = object->ClientSpecific()->figureScale;
                top = (std::max)(top, TopOf(object, scale));
                widest = (std::max)(widest, object->bounds.radius * LargestScale(scale));
            }
        }
    g_highest.top = top;
    g_highest.widest = widest;
    return g_highest;
}

} // namespace

void HandOverSceneLights(WorldClient* world) {
    terrain::Terrain* terrain = world->terrain.get();
    Light* sun = terrain && terrain->sun ? terrain->sun : g_sceneRenderer.sun;
    if (!sun) return;
    static std::vector<render::ScenePointLight> points;
    points.clear();
    if (terrain && terrain->mapLoaded) {
        for (auto& node : terrain->lights) {
            Light* light = node.second;
            if (!light) continue;
            render::ScenePointLight p{};
            p.id = static_cast<std::uint32_t>(node.first);
            p.position = drawn::LightPosition(light);
            p.inverseRangeSquared = light->inverseRangeSquared;
            for (int k = 0; k < 3; ++k) p.colour[k] = light->color[k];
            p.specular = lib::Has(light->flags, LightFlag::Specular);
            // An object's own light (a party unit's at night, a quest light: MapObjectClientSpecific::CreateLight)
            // is Off, which the game's own shadows skip too: it casts no point shadow.
            p.shadows = !lib::Has(light->flags, LightFlag::Off);
            points.push_back(p);
        }
    }
    render::SceneLights s{};
    for (int k = 0; k < 3; ++k) s.sunColour[k] = sun->color[k];
    s.sunDirection = sun->direction;
    s.ambientColour[0] = world->frameAmbient.r;
    s.ambientColour[1] = world->frameAmbient.g;
    s.ambientColour[2] = world->frameAmbient.b;
    LightColour daySun, dayAmbient;
    world->DaylightColours(&daySun, &dayAmbient);
    s.daySunColour[0] = daySun.r;
    s.daySunColour[1] = daySun.g;
    s.daySunColour[2] = daySun.b;
    s.dayAmbientColour[0] = dayAmbient.r;
    s.dayAmbientColour[1] = dayAmbient.g;
    s.dayAmbientColour[2] = dayAmbient.b;
    if (world->camera) s.camera = camera::FrameView().position;
    s.points = points.data();
    s.pointCount = static_cast<int>(points.size());
    s.clientTicks = world->tick + static_cast<double>(world->tickFraction);
    render::SceneLighting(s);
}

void HandOverTerrainCasters(WorldClient* world) {
    terrain::Terrain* terrain = world->terrain.get();
    if (!options::g_options.shadows || !terrain || !terrain->mapLoaded ||
        !lib::Has(terrain->landscapeFlags, terrain::LandscapeFlag::SunShadows) || terrain->sectorGrid.empty())
        return;
    render::ShadowCasterRect area;
    if (!render::ShadowTerrainArea(terrain->heightRange, &area)) return;
    constexpr float kSector = 32.0f;   // vertices a sector side, one unit apart
    auto sector = [](float v, int count) {
        int s = v > 0.0f ? static_cast<int>(v / kSector) : 0;
        return (std::min)(s, count - 1);
    };
    // A sector's vertices reach one unit into the next (its 33rd column and row): one sector more below.
    const int firstX = (std::max)(sector(area.x0, terrain->sectorsX) - 1, 0), lastX = sector(area.x1, terrain->sectorsX);
    const int firstY = (std::max)(sector(area.y0, terrain->sectorsY) - 1, 0), lastY = sector(area.y1, terrain->sectorsY);
    for (int sx = firstX; sx <= lastX; ++sx)
        for (int sy = firstY; sy <= lastY; ++sy) {
            if (const terrain::SectorData* cached = terrain->CachedSector(sx, sy)) render::ShadowTerrainSector(cached);
        }
}

void HandOverOutsideCasters(WorldClient* world) {
    const std::uint32_t flags = world->flags;
    if (!options::g_options.shadows || !(flags & (WorldClientFlag::DrawObjects | WorldClientFlag::DrawUnits)) ||
        !world->terrain)
        return;   // no shadows, no objects drawn
    const SectorGrid& grid = world->objectGrid;
    if (!grid.rows || grid.width == 0 || grid.height == 0) return;
    struct WalkTimer {
        WalkTimer() { render::BeginShadowWalk(); }
        ~WalkTimer() { render::EndShadowWalk(); }
        WalkTimer(const WalkTimer&) = delete;
        WalkTimer& operator=(const WalkTimer&) = delete;
    } timer;
    render::ShadowCasterRect area;
    const HighestObject& highest = Highest(world);
    if (!render::ShadowCasterArea(highest.top, highest.widest, &area)) return;
    // Only a multiplayer spectator's view leaves out the units it cannot see.
    const bool allUnits = !(options::g_options.drawVisibleOnly != 0 && world->session->sessionKind == 1);

    auto cell = [](float v, std::uint32_t cells) {
        if (!(v > 0.0f)) return 0u;
        const auto c = static_cast<std::uint32_t>(v) >> kSectorShift;
        return (std::min)(c, cells - 1);
    };
    const std::uint32_t firstX = cell(area.x0, grid.width), lastX = cell(area.x1, grid.width);
    const std::uint32_t firstY = cell(area.y0, grid.height), lastY = cell(area.y1, grid.height);
    for (std::uint32_t y = firstY; y <= lastY; ++y)
        for (std::uint32_t x = firstX; x <= lastX; ++x) {
            // A cell whose spheres the view placed this frame and none of which can reach is passed over whole.
            if (const CasterCell* placed = ViewCasterCell(x, y); placed && placed->complete) {
                if (!placed->any) continue;
                if (!render::ShadowCastersMayReach({placed->low, placed->high, placed->size, placed->radius})) continue;
            }
            const auto& list = grid.rows[y][x];
            for (MapObject* object : list) {
                if (!object || !object->attached) continue;
                const bool unit = object->GetClassId() == ClassId::Unit;
                if (!(flags & (unit ? WorldClientFlag::DrawUnits : WorldClientFlag::DrawObjects))) continue;
                if (unit && !allUnits && !(object->flags & MapObjectFlag::InParty)) continue;
                Object3DClientSpecific* half = object->ClientSpecific();
                const drawn::Pose shown = drawn::PoseOf(object);
                const Matrix4 placement = Placement(shown.rotation, shown.position);
                render::ShadowCasterSphere sphere{};
                if (!WorldSphere(object, placement, half->figureScale, &sphere)) continue;
                if (!render::ShadowCasterReaches(sphere)) continue;   // (the test moves the map's near plane)
                if (render::CasterDrawn(half->casterMark)) continue;     // drawn whole in the view
                // The game's own verdict when it drew the object's shadow this frame, else its tests.
                float strength = render::CasterStrength(half->casterMark);
                if (strength < 0.0f) {
                    const std::optional<float> cast = ShadowCastStrength(object);
                    if (!cast) continue;
                    strength = *cast;
                }
                if (strength < kLeastStrength) continue;
                render::CastsShadow(object, half->casterMark, strength);
                HandOverParts(object, half, placement, unit);
            }
        }
}
