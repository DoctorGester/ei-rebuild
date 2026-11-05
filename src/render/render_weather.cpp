// The weather on the surfaces: how wet the ground and the objects are, the puddles' share and the snow's cover, each
// following the client's weather over time, the rain's rings' clock (shaders/terrain.hlsl, mesh.hlsl), each unit's
// own wetness and snow under the roofs above it, and the roof map that keeps the ground and the other meshes under a
// roof dry (shaders/roof.hlsli). See docs/notes/renderer.md, section 13, item 11b.
#include "../platform/log.h"
#include "render.h"
#include "render_internal.h"

#include "lib/maths.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace render {

// How fast each follows the weather, in world ticks (about 18 a second) for 1 - 1/e of the way: the ground is wet
// within half a minute of rain and dries over a few; the puddles fill and dry slower; snow settles over minutes (about
// half the cover in a minute, three quarters in two) and melts over a few, faster in rain.
constexpr double kWetTicks = 200.0, kDryTicks = 1600.0;
constexpr double kPuddleTicks = 450.0, kPuddleDryTicks = 2400.0;
constexpr double kSnowTicks = 1600.0, kMeltTicks = 2400.0, kRainMeltTicks = 900.0;
// A unit wets and takes snow as fast as the ground but dries and loses its snow by itself, faster: about 50 s and
// 100 s for 1 - 1/e of the way (in rain its snow melts as the ground's).
constexpr double kUnitDryTicks = 900.0, kUnitMeltTicks = 1800.0;
// For this long after a landscape's load the surfaces take the settled weather as it is: the weather's message comes a
// tick or more after the zone's.
constexpr double kSettleTicks = 32.0;
// A unit not drawn for longer than this is forgotten: when it is met again it starts from the ground's state.
constexpr double kUnitForgetTicks = 64.0;
// The rings' clock runs over this many client ticks: a whole number of both ring layers' periods (terrain.hlsl's
// kRingPeriodA / B: 16 and 21 ticks).
constexpr double kRingWrap = 16.0 * 21.0 * 10.0;
// A roof is looked for straight up from five points of a unit's box at this share of its height: the middle of its
// top and four points a quarter of its width and depth from it.
constexpr float kRoofLook = 0.75f;
constexpr float kRoofSpread = 0.25f;
constexpr int kRoofPoints = 5;
// The roof map: kRoofMapSize texels a side over the visible ground and kRoofMargin around it (its larger side in steps
// of kRoofStep units, so that the texel size changes seldom; the texels' corners on whole texels of the landscape, so
// the dry patches do not swim while the camera moves), seen straight down from kRoofReach above the highest visible
// ground (a roof higher still is drawn at that height) to kRoofBelow under the lowest. A receiver less than
// kRoofTolerance under the highest roof at its point is in the open (a roof's own top, the ground at the foot of a
// rock), its point looked up kRoofOffset out along its normal (a wall's face beside its own top) and filtered over
// kRoofSoft units for a soft edge. Made only while the weather shows on the surfaces (above kWeatherShown).
constexpr int kRoofMapSize = 2048;
constexpr float kRoofStep = 8.0f;
constexpr float kRoofMargin = 2.0f;
constexpr float kRoofReach = 24.0f;
constexpr float kRoofBelow = 2.0f;
constexpr float kRoofTolerance = 0.2f;
constexpr float kRoofOffset = 0.2f;
constexpr float kRoofSoft = 0.3f;
constexpr float kWeatherShown = 1e-3f;

struct Surfaces {
    bool started = false;
    double time = 0.0;    // the latest world time followed to
    double loaded = 0.0;  // the world time of the first hand-over after a landscape's load
    bool settling = false;   // within kSettleTicks of a landscape's load: the state is the settled weather
    float wet = 0.0f, puddles = 0.0f, snow = 0.0f;
    float rain = 0.0f;              // the rain falling this frame (0 .. 1)
    float settledRain = 0.0f;       // the settled weather: 1 rain, else 0
    float settledSnow = 0.0f;       // 1 snow, else 0
} g_surfaces;

// A unit's own weather, kept by its object while it is drawn (and up to kUnitForgetTicks ticks after).
struct UnitSurface {
    double time = 0.0;
    float wet = 0.0f, snow = 0.0f, shelter = 0.0f;
    Vector3 at{};   // the middle of its box's foot (the harness's)
};
static std::unordered_map<const Object3D*, UnitSurface> g_units;
static const Object3D* g_interfaceUnit = nullptr;   // BeginInterfaceUnit .. EndInterfaceUnit

// A mesh that can roof a unit this frame: an object's (not a unit's) mesh drawn in the world, its model matrix into
// the landscape's space and its box there; the matrix is inverted when a point first lies under the box.
struct Roof {
    const MeshBatch* batch = nullptr;
    Matrix4 model;
    Box box;
    bool inverted = false;
    bool invertible = false;
    Matrix4 toModel{};
};

// The frame's lists, kept for their room.
static std::vector<Roof> g_roofs;
static std::unordered_map<const Object3D*, Box> g_unitBoxes;
// The frame's roof map (PrepareRoofs): its area and depth range, the landscape's space to its clip space.
struct RoofFit {
    bool on = false;
    float extent = 0.0f, range = 0.0f;
    Matrix4 clip{};
} g_roofFit;
static CasterDraws g_roofDraws("ei-roof-casters");
static gpu::Texture g_roofTexture;
static bool g_roofShelter = true;   // SetRoofShelter
static RoofInfo g_roofInfo;
constexpr int kModelUnknown = -1;
static std::vector<int> g_modelOf;   // per frame matrix: into g_models, kModelUnknown not yet
static std::vector<Matrix4> g_models;

// `now` moved towards `target` over `step` ticks: rising with `rise`, falling with `fall` as its time constant.
static float Follow(float now, float target, double step, double rise, double fall) {
    const double ticks = target > now ? rise : fall;
    return now + (target - now) * (1.0f - lib::Exp(static_cast<float>(-step / ticks)));
}

// A mesh's model-space box (MeshBatch::box) placed by `model`, added to a box in the landscape's space.
static void ExtendPlaced(Box& box, const float local[6], const Matrix4& model) {
    for (int k = 0; k < 8; ++k)
        box.Extend(
            TransformColumn(model, Vector3{local[(k & 1) ? 3 : 0], local[(k & 2) ? 4 : 1], local[(k & 4) ? 5 : 2]}));
}

// Of the rays `wanted` (a bit each) from `from[k]` along `along` (the mesh's model space, the same for all), those
// that meet one of the batch's triangles ahead of them.
static unsigned RaysMeetMesh(const Frame& f, const MeshBatch& batch, const Vector3* from, const Vector3& along,
                             unsigned wanted) {
    auto corner = [&](int k) {
        return batch.cached ? CachedCorner(*batch.cached, k)
                            : f.meshVertices[f.meshIndices[static_cast<std::size_t>(k)]].position;
    };
    unsigned met = 0;
    const int end = batch.firstIndex + batch.indexCount - 2;
    for (int k = batch.firstIndex; k < end && met != wanted; k += 3) {
        const Vector3 a = corner(k);
        const Vector3 e1 = corner(k + 1) - a, e2 = corner(k + 2) - a;
        const Vector3 p = Cross(along, e2);
        const float det = Dot(e1, p);
        if (std::fabs(det) < 1e-12f) continue;
        const float inverse = 1.0f / det;
        for (int r = 0; r < kRoofPoints; ++r) {
            const unsigned bit = 1u << r;
            if (!(wanted & bit) || (met & bit)) continue;
            const Vector3 t = from[r] - a;
            const float u = Dot(t, p) * inverse;
            if (u < 0.0f || u > 1.0f) continue;
            const Vector3 q = Cross(t, e1);
            const float v = Dot(along, q) * inverse;
            if (v < 0.0f || u + v > 1.0f) continue;
            if (Dot(e2, q) * inverse > 0.0f) met |= bit;
        }
    }
    return met;
}

// The share of a unit's top under a roof: of kRoofPoints points of its box at kRoofLook of its height, those with a
// roof's triangle straight above (the rays under a roof's box tested together against its triangles).
static float Shelter(const Frame& f, const Box& unit) {
    const float cx = (unit.low.x + unit.high.x) * 0.5f, cy = (unit.low.y + unit.high.y) * 0.5f;
    const float dx = (unit.high.x - unit.low.x) * kRoofSpread, dy = (unit.high.y - unit.low.y) * kRoofSpread;
    const float z = unit.low.z + (unit.high.z - unit.low.z) * kRoofLook;
    const Vector3 points[kRoofPoints] = {
        {cx, cy, z}, {cx - dx, cy - dy, z}, {cx + dx, cy - dy, z}, {cx - dx, cy + dy, z}, {cx + dx, cy + dy, z},
    };
    constexpr unsigned kAll = (1u << kRoofPoints) - 1;
    unsigned roofed = 0;
    for (Roof& roof : g_roofs) {
        if (roof.box.high.z <= z) continue;
        unsigned under = 0;
        for (int k = 0; k < kRoofPoints; ++k) {
            const Vector3& p = points[k];
            if (p.x >= roof.box.low.x && p.x <= roof.box.high.x && p.y >= roof.box.low.y && p.y <= roof.box.high.y)
                under |= 1u << k;
        }
        under &= ~roofed;
        if (!under) continue;
        if (!roof.inverted) {
            roof.inverted = true;
            roof.invertible = InvertMatrix(roof.model, &roof.toModel);
        }
        if (!roof.invertible) continue;
        Vector3 from[kRoofPoints];
        for (int k = 0; k < kRoofPoints; ++k) from[k] = TransformColumn(roof.toModel, points[k]);
        const Vector3 along = TransformColumnNormal(roof.toModel, Vector3{0.0f, 0.0f, 1.0f});
        roofed |= RaysMeetMesh(f, *roof.batch, from, along, under);
        if (roofed == kAll) break;
    }
    int count = 0;
    for (int k = 0; k < kRoofPoints; ++k) count += (roofed >> k) & 1;
    return static_cast<float>(count) / static_cast<float>(kRoofPoints);
}

// The world's model meshes after the ground: the units' boxes (by object), and the other objects' meshes as roofs (one
// for all passes of a mesh; not the light added).
static void CollectUnitsAndRoofs(const Frame& f) {
    g_roofs.clear();
    g_unitBoxes.clear();
    g_models.clear();
    if (f.worldRanges.empty()) return;
    const std::optional<WorldView> view = WorldCamera(f);
    if (!view) return;
    g_modelOf.assign(f.matrices.size(), kModelUnknown);
    auto modelOf = [&](const MeshBatch& batch) {
        int& index = g_modelOf[static_cast<std::size_t>(batch.matrix)];
        if (index == kModelUnknown) {
            g_models.push_back(view->inverse.Under(f.matrices[static_cast<std::size_t>(batch.matrix)]));
            index = static_cast<int>(g_models.size()) - 1;
        }
        return index;
    };
    const MeshBatch* last = nullptr;
    for (const WorldMesh& mesh : f.worldMeshes) {
        const MeshBatch& batch = f.meshBatches[static_cast<std::size_t>(mesh.batch)];
        if (batch.flat || batch.lines || !batch.caster || !batch.boxed || mesh.additive) continue;
        if (last && mesh.repeat && batch.matrix == last->matrix) continue;
        last = &batch;
        const Matrix4& placed = g_models[static_cast<std::size_t>(modelOf(batch))];
        if (batch.figure) {
            ExtendPlaced(g_unitBoxes[batch.caster], batch.box, placed);
        } else {
            g_roofs.push_back(Roof{&batch, placed, Box{}});
            ExtendPlaced(g_roofs.back().box, batch.box, placed);
        }
    }
}

// The wetness and the puddles follow the settled rain, the snow cover the settled snow (not the layer's fade, which
// lasts the weather message's 20 or 40 ticks, nor its mode, which stays snow through a fade from snow to rain). The
// time only goes forward: the drawn frame's fraction of a tick can run past the next tick while the client waits for
// the server (app_main's backlog), so the time handed over steps back a little now and then; only a landscape's load
// starts the state again (a step back taken for a load made the snow and the wetness come at once).
void PrepareSurfaces(const ShadowFrame& shadow) {
    Surfaces& surfaces = g_surfaces;
    if (!shadow.haveClouds) {
        surfaces.rain = surfaces.settledRain = surfaces.settledSnow = 0.0f;
        return;
    }
    const CloudWeather& weather = shadow.weather;
    surfaces.rain = weather.snow ? 0.0f : std::clamp(weather.rain, 0.0f, 1.0f);
    const float rain = weather.settled == 1 ? 1.0f : 0.0f, snow = weather.settled == 2 ? 1.0f : 0.0f;
    surfaces.settledRain = rain;
    surfaces.settledSnow = snow;
    if (!surfaces.started) {
        surfaces.started = true;
        surfaces.time = surfaces.loaded = weather.time;
    }
    surfaces.settling = weather.time - surfaces.loaded <= kSettleTicks;
    if (surfaces.settling) {
        surfaces.wet = surfaces.puddles = rain;
        surfaces.snow = snow;
        surfaces.time = std::max(surfaces.time, weather.time);
        return;
    }
    const double step = std::max(weather.time - surfaces.time, 0.0);
    surfaces.time = std::max(surfaces.time, weather.time);
    surfaces.wet = Follow(surfaces.wet, rain, step, kWetTicks, kDryTicks);
    surfaces.puddles = Follow(surfaces.puddles, rain, step, kPuddleTicks, kPuddleDryTicks);
    surfaces.snow = Follow(surfaces.snow, snow, step, kSnowTicks, rain > 0.0f ? kRainMeltTicks : kMeltTicks);
}

void PrepareUnitWeather(Frame& f) {
    Timer timer(FrameStats::ShadowMs);
    const Surfaces& surfaces = g_surfaces;
    CollectUnitsAndRoofs(f);
    // Each unit's wetness and snow follow the settled weather on it (none under a roof), on the ground's time, which
    // only goes forward; a unit first met, not met for longer than kUnitForgetTicks or while the ground settles after a
    // load starts from the ground's state times its exposure.
    for (const auto& [object, box] : g_unitBoxes) {
        const float shelter = Shelter(f, box), open = 1.0f - shelter;
        auto [it, added] = g_units.try_emplace(object);
        UnitSurface& unit = it->second;
        const double step = std::max(surfaces.time - unit.time, 0.0);
        if (added || surfaces.settling || step > kUnitForgetTicks) {
            unit.wet = surfaces.wet * open;
            unit.snow = surfaces.snow * open;
        } else {
            unit.wet = Follow(unit.wet, surfaces.settledRain * open, step, kWetTicks, kUnitDryTicks);
            unit.snow = Follow(unit.snow, surfaces.settledSnow * open, step, kSnowTicks,
                            surfaces.settledRain > 0.0f ? kRainMeltTicks : kUnitMeltTicks);
        }
        unit.time = surfaces.time;
        unit.shelter = shelter;
        unit.at = Vector3{(box.low.x + box.high.x) * 0.5f, (box.low.y + box.high.y) * 0.5f, box.low.z};
    }
    std::erase_if(g_units, [&](const auto& entry) { return surfaces.time - entry.second.time > kUnitForgetTicks; });
    // A unit's figure parts, in the world (its own weather) and in the interface (the same unit's).
    for (MeshBatch& batch : f.meshBatches) {
        if (!batch.figure && !batch.interfaceUnit) continue;
        auto it = g_units.find(batch.figure ? batch.caster : batch.interfaceUnit);
        batch.weather[0] = it != g_units.end() ? it->second.wet : surfaces.wet;
        batch.weather[1] = it != g_units.end() ? it->second.snow : surfaces.snow;
    }
}

// The roof map's fit around the visible ground (the sun map's), while the weather shows on the surfaces; its roofs,
// drawn by DrawRoofMap, are the units' (CollectUnitsAndRoofs) and the sun map's casters outside the view but the units'
// (a roof over the visible ground often stands above the view's top edge).
void PrepareRoofs(Frame& f) {
    g_roofFit.on = false;
    g_roofInfo = RoofInfo{};
    const Surfaces& surfaces = g_surfaces;
    if (!g_roofShelter ||
        std::max(std::max(surfaces.wet, surfaces.puddles), std::max(surfaces.snow, surfaces.rain)) <= kWeatherShown)
        return;
    Box ground;
    if (!VisibleGround(f, &ground)) return;
    const Vector3 &low = ground.low, &high = ground.high;
    Timer timer(FrameStats::ShadowMs);
    float extent = std::max(high.x - low.x, high.y - low.y) + 2.0f * kRoofMargin;
    extent = std::ceil(extent / kRoofStep) * kRoofStep;
    const float texel = extent / kRoofMapSize;
    const float x0 = std::floor(((low.x + high.x) * 0.5f - extent * 0.5f) / texel) * texel;
    const float y0 = std::floor(((low.y + high.y) * 0.5f - extent * 0.5f) / texel) * texel;
    const float top = high.z + kRoofReach, range = top - (low.z - kRoofBelow);
    RoofFit& fit = g_roofFit;
    fit = RoofFit{};
    fit.on = true;
    fit.extent = extent;
    fit.range = range;
    const float sx = 2.0f / extent;
    fit.clip.m[0] = sx;
    fit.clip.m[3] = -x0 * sx - 1.0f;
    fit.clip.m[5] = sx;
    fit.clip.m[7] = -y0 * sx - 1.0f;
    fit.clip.m[10] = -1.0f / range;
    fit.clip.m[11] = top / range;
    fit.clip.m[15] = 1.0f;
    g_roofInfo.made = true;
    g_roofInfo.size = kRoofMapSize;
    g_roofInfo.extent = extent;
}

void RoofUniforms(const Matrix4& light, ShadowParams* params) {
    const RoofFit& fit = g_roofFit;
    Matrix4 fromMap;
    if (!fit.on || !InvertMatrix(light, &fromMap)) return;
    // The sun map's clip space to the roof map's, then to its texture's u, v (v down the rows) and depth.
    const Matrix4 clip = Product(fit.clip, fromMap);
    for (int k = 0; k < 4; ++k) {
        params->roofRows[0][k] = 0.5f * clip.m[k] + 0.5f * clip.m[12 + k];
        params->roofRows[1][k] = -0.5f * clip.m[4 + k] + 0.5f * clip.m[12 + k];
        params->roofRows[2][k] = clip.m[8 + k];
    }
    params->roofScale[0] = 1.0f / fit.extent;
    params->roofScale[1] = -1.0f / fit.extent;
    params->roofScale[2] = -1.0f / fit.range;
    params->roofStep = kRoofSoft / fit.extent;
    params->roofOn = 1;
    params->roofOpenDepth = kRoofTolerance / fit.range;
    params->roofNormalOffset = kRoofOffset;
}

// The roofs straight down into the roof map (depth only; cut out by their texture's alpha as in the sun map).
void DrawRoofMap() {
    if (!g_roofFit.on) return;
    Timer timer(FrameStats::ShadowMs);
    if (!g_roofTexture) g_roofTexture = MakeShadowDepth(kRoofMapSize, kRoofMapSize, "ei-roof-map");
    if (!g_roofTexture) {
        g_roofFit.on = false;
        g_roofInfo.made = false;
        return;
    }
    g_roofDraws.Clear();
    for (const Roof& roof : g_roofs) {
        const MeshBatch& batch = *roof.batch;
        Geometry geometry;
        int first = 0;
        if (!BindMeshBatch(batch, geometry, &first)) continue;
        const PassState& state = batch.state;
        const StageState& stage = state.stages[0];
        CasterInstance instance{Product(g_roofFit.clip, roof.model), {batch.uOffset, batch.vOffset}, {}};
        CasterAlpha(state, instance.alpha);
        g_roofDraws.Add(geometry, batch.cached != nullptr, stage.texture ? stage.texture : SurfaceView(nullptr),
                        StageSampler(stage), first, batch.indexCount, instance);
    }
    AddOutsideRoofs(g_roofDraws, g_roofFit.clip);
    g_roofDraws.Sort(0, g_roofDraws.Count());
    const bool draw = g_roofDraws.Upload();
    gpu::PassDesc pass;
    pass.depth = {g_roofTexture, 0, gpu::Load::Clear, 1.0f, true};
    pass.label = "ei-roof-map";
    if (!gpu::BeginPass(pass)) Fatal("Can't draw the frame: the roof map's pass could not begin (%s).", gpu::Error());
    ++g_stats.counts[FrameStats::Passes];
    if (draw) g_roofDraws.Draw(0, g_roofDraws.Count());
    gpu::EndPass();
    g_stats.counts[FrameStats::ShadowDraws] += g_roofDraws.Count();
    g_roofInfo.roofs = static_cast<int>(g_roofDraws.Count());
}

void BindRoofMap(int slot) {
    const gpu::TextureBinding b{g_roofFit.on && g_roofTexture ? g_roofTexture : NoShadowTexture(), ShadowCompareSampler()};
    gpu::BindTextures(gpu::Stage::Fragment, slot, {&b, 1});
}

const Object3D* CurrentInterfaceUnit() { return g_interfaceUnit; }

void SurfaceUniforms(TerrainFsParams* fs) {
    const Surfaces& surfaces = g_surfaces;
    fs->wet = surfaces.wet;
    fs->puddles = surfaces.puddles;
    fs->snow = surfaces.snow;
    fs->rain = surfaces.rain;
    const double ticks = KeptSceneLight().clientTicks;
    fs->rainClock = static_cast<float>(ticks - std::floor(ticks / kRingWrap) * kRingWrap);
}

void MeshWeatherUniforms(MeshFsParams* fs) {
    const Surfaces& surfaces = g_surfaces;
    fs->wet = surfaces.wet;
    fs->snow = surfaces.snow;
    const KeptScene& light = KeptSceneLight();
    fs->weatherSun[0] = -light.sun.x;
    fs->weatherSun[1] = -light.sun.y;
    fs->weatherSun[2] = -light.sun.z;
    for (int k = 0; k < 3; ++k) fs->weatherSunColour[k] = light.sunColour[k];
}

void SurfaceState(float* wet, float* puddles, float* snow) {
    *wet = g_surfaces.wet;
    *puddles = g_surfaces.puddles;
    *snow = g_surfaces.snow;
}

void BeginInterfaceUnit(const Object3D* object) { g_interfaceUnit = object; }
void EndInterfaceUnit() { g_interfaceUnit = nullptr; }

void SetRoofShelter(bool on) {
    if (on != g_roofShelter) ei::Log("render: the roofs' shelter from the weather %s", on ? "on" : "off");
    g_roofShelter = on;
}

RoofInfo RoofState() { return g_roofInfo; }

void LandscapeLoaded() {
    g_surfaces.started = false;
    g_units.clear();
}

UnitWeather UnitWeatherState(const Object3D* object) {
    UnitWeather out;
    auto it = g_units.find(object);
    if (it == g_units.end()) return out;
    out.known = true;
    out.wet = it->second.wet;
    out.snow = it->second.snow;
    out.shelter = it->second.shelter;
    out.at = it->second.at;
    return out;
}

}   // namespace render
