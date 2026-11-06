// The shadow map's casters the game does not draw this frame (render::ShadowCasterMesh). Only reads the game's
// objects. See docs/notes/renderer.md, "The renderer's source files".
#include "game/scene/light.h"
#include "game/world/world_client_shadow_casters.h"

#include "game/scene/camera.h"
#include "game/scene/drawn_motion.h"
#include "game/models/figure_mesh.h"
#include "game/models/link_figure.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "game/objects/map_object_shadow.h"
#include "game/app/options.h"
#include "game/players/player.h"
#include "render/render.h"
#include "game/spells/spell.h"
#include "game/terrain/terrain.h"
#include "game/terrain/terrain_sector.h"
#include "game/scene/scene_renderer.h"
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

// Row-major 4 x 4 matrices, out = M * (x, y, z, 1): a x b.
Matrix4 Multiply(const Matrix4& a, const Matrix4& b) {
    Matrix4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a.m[i * 4 + k] * b.m[k * 4 + j];
            r.m[i * 4 + j] = s;
        }
    return r;
}

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

// The part's positions with its per-vertex offsets added (as the draws add them), in `moved`.
const VertexPacket* MovedPositions(const VertexPacket* positions, int packets, const std::vector<Vector3>& offsets,
                                   std::vector<VertexPacket>& moved) {
    moved.assign(positions, positions + packets);
    const int count = static_cast<int>((std::min)(std::ssize(offsets), static_cast<std::ptrdiff_t>(packets) * 4));
    for (int i = 0; i < count; ++i) {
        VertexPacket& packet = moved[static_cast<std::size_t>(i >> 2)];
        packet.block[0][i & 3] = offsets[i].x + packet.block[0][i & 3];
        packet.block[1][i & 3] = offsets[i].y + packet.block[1][i & 3];
        packet.block[2][i & 3] = offsets[i].z + packet.block[2][i & 3];
    }
    return moved.data();
}

// Whether the world's draw handed over this frame every part HandOverParts would (Object3DClientSpecific::casterParts:
// it drew no part HandOverParts leaves out, so as many means the same parts).
bool AllPartsDrawn(const Object3DClientSpecific* half) {
    if (half->casterFrame != render::CasterFrame()) return false;
    int parts = 0;
    for (const std::unique_ptr<TextureParts>& set : half->partSets) {
        if (!set) continue;
        for (const auto& group : set->groups)
            for (const FigureMesh* part : group->parts) {
                if (!part || (part->flags & FigureMeshFlag::Hidden)) continue;
                if (!(LengthSquared(part->localPosition) > kPartRangeSquared)) ++parts;
            }
    }
    return parts == half->casterParts;
}

// Hands over the parts of `object` (`unit`: a unit) that were not drawn this frame, placed by `world` (and the
// extra scale), each with its part set's texture.
void HandOverParts(Object3D* object, Object3DClientSpecific* half, const Matrix4& world, bool unit) {
    static std::vector<VertexPacket> moved;
    Matrix4 objectMatrix = world;
    const Vector3& scale = half->figureScale;
    if (Scaled(scale)) {
        Matrix4 s{};
        s.m[0] = scale.x;
        s.m[5] = scale.y;
        s.m[10] = scale.z;
        s.m[15] = 1.0f;
        objectMatrix = Multiply(world, s);
    }
    LinkMesh* figure = object->figure;
    for (const std::unique_ptr<TextureParts>& set : half->partSets) {
        if (!set) continue;
        const Surface* holder = set->texture ? set->texture->surface.Get() : nullptr;
        for (int g = 0; g < std::ssize(set->groups); ++g) {
            const PartGroup* group = set->groups[g].get();
            for (int p = 0; p < std::ssize(group->parts); ++p) {
                FigureMesh* part = group->parts[p];
                if (!part || (part->flags & FigureMeshFlag::Hidden)) continue;
                if (LengthSquared(part->localPosition) > kPartRangeSquared) continue;
                if (render::CasterPartDrawn(part)) continue;
                render::ShadowMesh mesh{};
                const VertexPacket* positions = nullptr;
                int packets = 0;
                if (figure) {
                    const LinkPartRanges* r = part->PartIn(figure);
                    if (!r) continue;
                    positions = figure->vertices.data() + r->vertices.start;
                    packets = r->vertices.count;
                    mesh.uvs = figure->texCoords.data() + r->texCoords.start;
                    mesh.uvCount = r->texCoords.count;
                    mesh.corners = figure->corners.data() + r->corners.start;
                    mesh.cornerCount = r->corners.count;
                    mesh.indices = figure->indices.data() + r->indices.start;
                    mesh.indexCount = r->indices.count;
                } else {
                    const MeshBuffers& b = part->buffers;
                    positions = b.vertices;
                    packets = b.vertexCount;
                    mesh.uvs = b.texCoords;
                    mesh.uvCount = b.texCoordCount;
                    mesh.corners = b.corners;
                    mesh.cornerCount = b.cornerCount;
                    mesh.indices = b.indices;
                    mesh.indexCount = b.indexCount;
                }
                if (!positions || packets <= 0) continue;
                mesh.positions =
                    !part->offsets.empty() ? MovedPositions(positions, packets, part->offsets, moved) : positions;
                mesh.positionCount = packets * 4;
                mesh.uOffset = part->textureOffsetU;
                mesh.vOffset = part->textureOffsetV;
                mesh.model = Multiply(objectMatrix, Placement(part->localRotation, part->localPosition));
                mesh.texture = holder;
                mesh.owner = part;
                mesh.shared = figure;
                mesh.moved = !part->offsets.empty();
                mesh.slots = &part->meshSlots;
                mesh.unit = unit;
                render::ShadowCasterMesh(object, mesh);
            }
        }
    }
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
                if (!render::ShadowCasterReaches(sphere)) {
                    continue;
                }
                if (AllPartsDrawn(half)) continue;
                // The game's own verdict when it drew the object's shadow this frame, else its tests.
                float strength = render::CasterStrength(object);
                if (strength < 0.0f) {
                    const std::optional<float> cast = ShadowCastStrength(object);
                    if (!cast) continue;
                    strength = *cast;
                }
                if (strength < kLeastStrength) continue;
                HandOverParts(object, half, placement, unit);
            }
        }
}
