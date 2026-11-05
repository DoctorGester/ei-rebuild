// The weather on the surfaces: how wet the ground and the objects are, the puddles' share and the snow's cover, each
// following the client's weather over time, the rain's rings' clock (shaders/terrain.hlsl, mesh.hlsl), each unit's
// own wetness and snow under the roofs above it, and the roof map that keeps the ground and the other meshes under a
// roof dry (shaders/roof.hlsli). See docs/notes/renderer.md, section 13, item 11b.
#include "render.h"
#include "render_internal.h"

#include "lib/maths.h"
#include "../platform/log.h"

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
static std::unordered_map<const void*, UnitSurface> g_units;
static const void* g_interfaceUnit = nullptr;   // BeginInterfaceUnit .. EndInterfaceUnit

struct Box {
    Vector3 low{1e30f, 1e30f, 1e30f}, high{-1e30f, -1e30f, -1e30f};
};

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
static std::unordered_map<const void*, Box> g_unitBoxes;
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
constexpr int kModelUnknown = -2;
static std::vector<int> g_modelOf;   // per frame matrix: into g_models, -1 none, kModelUnknown not yet
static std::vector<Matrix4> g_models;

// `now` moved towards `target` over `step` ticks: rising with `rise`, falling with `fall` as its time constant.
static float Follow(float now, float target, double step, double rise, double fall) {
    const double ticks = target > now ? rise : fall;
    return now + (target - now) * (1.0f - lib::Exp(static_cast<float>(-step / ticks)));
}

static void Extend(Box& box, const Vector3& p) {
    box.low = Vector3{std::min(box.low.x, p.x), std::min(box.low.y, p.y), std::min(box.low.z, p.z)};
    box.high = Vector3{std::max(box.high.x, p.x), std::max(box.high.y, p.y), std::max(box.high.z, p.z)};
}

// A mesh's model-space box (MeshBatch::box) placed by `model`, added to a box in the landscape's space.
static void ExtendPlaced(Box& box, const float local[6], const Matrix4& model) {
    for (int k = 0; k < 8; ++k)
        Extend(box, TransformColumn(model, Vector3{local[(k & 1) ? 3 : 0], local[(k & 2) ? 4 : 1], local[(k & 4) ? 5 : 2]}));
}

// Of the rays `wanted` (a bit each) from `from[k]` along `along` (the mesh's model space, the same for all), those
// that meet one of the batch's triangles ahead of them.
static unsigned RaysMeetMesh(const Frame& f, const MeshBatch& b, const Vector3* from, const Vector3& along, unsigned wanted) {
    auto corner = [&](int k) {
        return b.cached ? CachedCorner(*b.cached, k) : f.meshVertices[f.meshIndices[static_cast<std::size_t>(k)]].position;
    };
    unsigned met = 0;
    const int end = b.firstIndex + b.indexCount - 2;
    for (int k = b.firstIndex; k < end && met != wanted; k += 3) {
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
    for (Roof& r : g_roofs) {
        if (r.box.high.z <= z) continue;
        unsigned under = 0;
        for (int k = 0; k < kRoofPoints; ++k) {
            const Vector3& p = points[k];
            if (p.x >= r.box.low.x && p.x <= r.box.high.x && p.y >= r.box.low.y && p.y <= r.box.high.y) under |= 1u << k;
        }
        under &= ~roofed;
        if (!under) continue;
        if (!r.inverted) {
            r.inverted = true;
            r.invertible = InvertMatrix(r.model, &r.toModel);
        }
        if (!r.invertible) continue;
        Vector3 from[kRoofPoints];
        for (int k = 0; k < kRoofPoints; ++k) from[k] = TransformColumn(r.toModel, points[k]);
        const Vector3 along = TransformColumnNormal(r.toModel, Vector3{0.0f, 0.0f, 1.0f});
        roofed |= RaysMeetMesh(f, *r.batch, from, along, under);
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
    Matrix4 camera;
    Vector3 eye;
    if (f.worldRanges.empty() || !WorldCamera(f, &camera, &eye)) return;
    g_modelOf.assign(f.matrices.size(), kModelUnknown);
    auto modelOf = [&](const MeshBatch& b) {
        int& index = g_modelOf[static_cast<std::size_t>(b.matrix)];
        if (index != kModelUnknown) return index;
        Matrix4 model;
        index = -1;
        if (UnderCamera(camera, f.matrices[static_cast<std::size_t>(b.matrix)], &model)) {
            g_models.push_back(model);
            index = static_cast<int>(g_models.size()) - 1;
        }
        return index;
    };
    const MeshBatch* last = nullptr;
    bool started = false;
    for (const Frame::Range& r : f.worldRanges) {
        const std::size_t end = std::min(r.end, f.commands.size());
        for (std::size_t k = r.first; k < end; ++k) {
            const Command& c = f.commands[k];
            if (c.kind == Command::Terrain) started = started || !f.terrainBatches[static_cast<std::size_t>(c.index)].water;
            if (!started || c.kind != Command::Mesh) continue;
            const MeshBatch& b = f.meshBatches[static_cast<std::size_t>(c.index)];
            if (!b.model || b.flat || b.lines || b.indexCount <= 0 || !b.caster || !b.boxed) continue;
            if (b.state.blend && b.state.destBlend == 2) continue;   // D3DBLEND_ONE: light added
            if (last && b.cached == last->cached && b.firstIndex == last->firstIndex && b.matrix == last->matrix) continue;
            last = &b;
            const int model = modelOf(b);
            if (model < 0) continue;
            const Matrix4& placed = g_models[static_cast<std::size_t>(model)];
            if (b.figure) {
                ExtendPlaced(g_unitBoxes[b.caster], b.box, placed);
            } else {
                g_roofs.push_back(Roof{&b, placed, Box{}});
                ExtendPlaced(g_roofs.back().box, b.box, placed);
            }
        }
    }
}

// The wetness and the puddles follow the settled rain, the snow cover the settled snow (not the layer's fade, which
// lasts the weather message's 20 or 40 ticks, nor its mode, which stays snow through a fade from snow to rain). The
// time only goes forward: the drawn frame's fraction of a tick can run past the next tick while the client waits for
// the server (app_main's backlog), so the time handed over steps back a little now and then; only a landscape's load
// starts the state again (a step back taken for a load made the snow and the wetness come at once).
void PrepareSurfaces(const ShadowFrame& shadow) {
    Surfaces& s = g_surfaces;
    if (!shadow.haveClouds) {
        s.rain = s.settledRain = s.settledSnow = 0.0f;
        return;
    }
    const CloudWeather& w = shadow.weather;
    s.rain = w.snow ? 0.0f : std::clamp(w.rain, 0.0f, 1.0f);
    const float rain = w.settled == 1 ? 1.0f : 0.0f, snow = w.settled == 2 ? 1.0f : 0.0f;
    s.settledRain = rain;
    s.settledSnow = snow;
    if (!s.started) {
        s.started = true;
        s.time = s.loaded = w.time;
    }
    s.settling = w.time - s.loaded <= kSettleTicks;
    if (s.settling) {
        s.wet = s.puddles = rain;
        s.snow = snow;
        s.time = std::max(s.time, w.time);
        return;
    }
    const double step = std::max(w.time - s.time, 0.0);
    s.time = std::max(s.time, w.time);
    s.wet = Follow(s.wet, rain, step, kWetTicks, kDryTicks);
    s.puddles = Follow(s.puddles, rain, step, kPuddleTicks, kPuddleDryTicks);
    s.snow = Follow(s.snow, snow, step, kSnowTicks, rain > 0.0f ? kRainMeltTicks : kMeltTicks);
}

void PrepareUnitWeather(Frame& f) {
    StatsTimer timer(&FrameStats::shadowMs);
    const Surfaces& s = g_surfaces;
    CollectUnitsAndRoofs(f);
    // Each unit's wetness and snow follow the settled weather on it (none under a roof), on the ground's time, which
    // only goes forward; a unit first met, not met for longer than kUnitForgetTicks or while the ground settles after a
    // load starts from the ground's state times its exposure.
    for (const auto& [object, box] : g_unitBoxes) {
        const float shelter = Shelter(f, box), open = 1.0f - shelter;
        auto [it, added] = g_units.try_emplace(object);
        UnitSurface& u = it->second;
        const double step = std::max(s.time - u.time, 0.0);
        if (added || s.settling || step > kUnitForgetTicks) {
            u.wet = s.wet * open;
            u.snow = s.snow * open;
        } else {
            u.wet = Follow(u.wet, s.settledRain * open, step, kWetTicks, kUnitDryTicks);
            u.snow = Follow(u.snow, s.settledSnow * open, step, kSnowTicks,
                            s.settledRain > 0.0f ? kRainMeltTicks : kUnitMeltTicks);
        }
        u.time = s.time;
        u.shelter = shelter;
        u.at = Vector3{(box.low.x + box.high.x) * 0.5f, (box.low.y + box.high.y) * 0.5f, box.low.z};
    }
    std::erase_if(g_units, [&](const auto& entry) { return s.time - entry.second.time > kUnitForgetTicks; });
    // A unit's figure parts, in the world (its own weather) and in the interface (the same unit's).
    for (MeshBatch& b : f.meshBatches) {
        if (!b.figure && !b.interfaceUnit) continue;
        auto it = g_units.find(b.figure ? b.caster : b.interfaceUnit);
        b.weather[0] = it != g_units.end() ? it->second.wet : s.wet;
        b.weather[1] = it != g_units.end() ? it->second.snow : s.snow;
    }
}

// The roof map's fit around the visible ground (the sun map's), while the weather shows on the surfaces; its roofs,
// drawn by DrawRoofMap, are the units' (CollectUnitsAndRoofs) and the sun map's casters outside the view but the units'
// (a roof over the visible ground often stands above the view's top edge).
void PrepareRoofs(Frame& f) {
    g_roofFit.on = false;
    g_roofInfo = RoofInfo{};
    const Surfaces& s = g_surfaces;
    if (!g_roofShelter || std::max(std::max(s.wet, s.puddles), std::max(s.snow, s.rain)) <= kWeatherShown) return;
    Vector3 low, high;
    if (!VisibleGround(f, &low, &high)) return;
    StatsTimer timer(&FrameStats::shadowMs);
    float extent = std::max(high.x - low.x, high.y - low.y) + 2.0f * kRoofMargin;
    extent = std::ceil(extent / kRoofStep) * kRoofStep;
    const float texel = extent / kRoofMapSize;
    const float x0 = std::floor(((low.x + high.x) * 0.5f - extent * 0.5f) / texel) * texel;
    const float y0 = std::floor(((low.y + high.y) * 0.5f - extent * 0.5f) / texel) * texel;
    const float top = high.z + kRoofReach, range = top - (low.z - kRoofBelow);
    RoofFit& m = g_roofFit;
    m = RoofFit{};
    m.on = true;
    m.extent = extent;
    m.range = range;
    const float sx = 2.0f / extent;
    m.clip.m[0] = sx;
    m.clip.m[3] = -x0 * sx - 1.0f;
    m.clip.m[5] = sx;
    m.clip.m[7] = -y0 * sx - 1.0f;
    m.clip.m[10] = -1.0f / range;
    m.clip.m[11] = top / range;
    m.clip.m[15] = 1.0f;
    g_roofInfo.made = true;
    g_roofInfo.size = kRoofMapSize;
    g_roofInfo.extent = extent;
}

void RoofUniforms(const Matrix4& light, ShadowParams* p) {
    const RoofFit& m = g_roofFit;
    Matrix4 fromMap;
    if (!m.on || !InvertMatrix(light, &fromMap)) return;
    // The sun map's clip space to the roof map's, then to its texture's u, v (v down the rows) and depth.
    const Matrix4 clip = Product(m.clip, fromMap);
    for (int k = 0; k < 4; ++k) {
        p->roofRows[0][k] = 0.5f * clip.m[k] + 0.5f * clip.m[12 + k];
        p->roofRows[1][k] = -0.5f * clip.m[4 + k] + 0.5f * clip.m[12 + k];
        p->roofRows[2][k] = clip.m[8 + k];
    }
    p->roofScale[0] = 1.0f / m.extent;
    p->roofScale[1] = -1.0f / m.extent;
    p->roofScale[2] = -1.0f / m.range;
    p->roofScale[3] = kRoofSoft / m.extent;
    p->roof[0] = 1.0f;
    p->roof[1] = kRoofTolerance / m.range;
    p->roof[2] = kRoofOffset;
}

// The roofs straight down into the roof map (depth only; cut out by their texture's alpha as in the sun map).
void DrawRoofMap() {
    if (!g_roofFit.on) return;
    StatsTimer timer(&FrameStats::shadowMs);
    if (!g_roofTexture) g_roofTexture = MakeShadowDepth(kRoofMapSize, kRoofMapSize, "ei-roof-map");
    if (!g_roofTexture) {
        g_roofFit.on = false;
        g_roofInfo.made = false;
        return;
    }
    g_roofDraws.Clear();
    for (const Roof& r : g_roofs) {
        const MeshBatch& batch = *r.batch;
        Geometry g;
        int first = 0;
        if (!BindMeshBatch(batch, g, &first)) continue;
        const PassState& s = batch.state;
        const StageState& st = s.stages[0];
        CasterInstance instance{Product(g_roofFit.clip, r.model), {batch.uOffset, batch.vOffset}, {}};
        if ((s.alphaTest || s.blend) && st.hasTexture) {
            instance.alpha[0] = 1.0f;
            instance.alpha[1] = s.alphaTest ? (s.alphaReference + 0.5f) / 255.0f : 0.5f;
        }
        g_roofDraws.Add(g, batch.cached != nullptr, st.texture ? st.texture : SurfaceView(nullptr), StageSampler(st),
                        first, batch.indexCount, instance);
    }
    AddOutsideRoofs(g_roofDraws, g_roofFit.clip);
    g_roofDraws.Sort(0, g_roofDraws.Count());
    const bool draw = g_roofDraws.Upload();
    gpu::PassDesc pass;
    pass.depth = {g_roofTexture, 0, gpu::Load::Clear, 1.0f, true};
    pass.label = "ei-roof-map";
    if (!gpu::BeginPass(pass)) Fatal("Can't draw the frame: the roof map's pass could not begin (%s).", gpu::Error());
    ++g_stats.passes;
    if (draw) g_roofDraws.Draw(0, g_roofDraws.Count());
    gpu::EndPass();
    g_stats.shadowDraws += g_roofDraws.Count();
    g_roofInfo.roofs = static_cast<int>(g_roofDraws.Count());
}

void BindRoofMap(int slot) {
    const gpu::TextureBinding b{g_roofFit.on && g_roofTexture ? g_roofTexture : NoShadowTexture(), ShadowCompareSampler()};
    gpu::BindTextures(gpu::Stage::Fragment, slot, {&b, 1});
}

const void* CurrentInterfaceUnit() { return g_interfaceUnit; }

void SurfaceUniforms(float weather[4], float rain[4]) {
    const Surfaces& s = g_surfaces;
    weather[0] = s.wet;
    weather[1] = s.puddles;
    weather[2] = s.snow;
    weather[3] = s.rain;
    const double ticks = KeptClientTicks();
    rain[0] = static_cast<float>(ticks - std::floor(ticks / kRingWrap) * kRingWrap);
    rain[1] = rain[2] = rain[3] = 0.0f;
}

void MeshWeatherUniforms(float weather[4], float sun[4], float sunColour[4]) {
    const Surfaces& s = g_surfaces;
    weather[0] = s.wet;
    weather[1] = s.snow;
    const SceneLightParams& light = KeptSceneLight();
    for (int k = 0; k < 3; ++k) {
        sun[k] = -light.sun[k];
        sunColour[k] = light.sunColour[k];
    }
}

void SurfaceState(float* wet, float* puddles, float* snow) {
    *wet = g_surfaces.wet;
    *puddles = g_surfaces.puddles;
    *snow = g_surfaces.snow;
}

void BeginInterfaceUnit(const void* object) { g_interfaceUnit = object; }
void EndInterfaceUnit() { g_interfaceUnit = nullptr; }

void SetRoofShelter(bool on) {
#if defined(EI_RENDER_VALIDATION)
    if (on != g_roofShelter) ei::Log("render: the roofs' shelter from the weather %s", on ? "on" : "off");
    g_roofShelter = on;
#else
    (void)on;
#endif
}

RoofInfo RoofState() { return g_roofInfo; }

void LandscapeLoaded() {
    g_surfaces.started = false;
    g_units.clear();
}

UnitWeather UnitWeatherState(const void* object) {
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

} // namespace render
