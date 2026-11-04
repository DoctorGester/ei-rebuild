// The weather on the surfaces: how wet the ground and the objects are, the puddles' share and the snow's cover, each
// following the client's weather over time, the rain's rings' clock (shaders/terrain.hlsl, mesh.hlsl), and each unit's
// own wetness and snow under the roofs above it. See docs/notes/renderer.md, section 13, item 11b.
#include "render.h"
#include "render_internal.h"

#include "lib/maths.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace render::detail {

namespace {

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
std::unordered_map<const void*, UnitSurface> g_units;

struct Box {
    Vector3 low{1e30f, 1e30f, 1e30f}, high{-1e30f, -1e30f, -1e30f};
};

// A mesh that can roof a unit this frame: an object's (not a unit's) mesh drawn in the world, its model matrix into
// the landscape's space and its box there; the matrix is inverted when a point first lies under the box.
struct Roof {
    const MeshBatch* batch;
    Matrix4 model;
    Box box;
    bool inverted = false;
    bool invertible = false;
    Matrix4 toModel{};
};

// The frame's lists, kept for their room.
std::vector<Roof> g_roofs;
std::unordered_map<const void*, Box> g_unitBoxes;
constexpr int kModelUnknown = -2;
std::vector<int> g_modelOf;   // per frame matrix: into g_models, -1 none, kModelUnknown not yet
std::vector<Matrix4> g_models;

// `now` moved towards `target` over `step` ticks: rising with `rise`, falling with `fall` as its time constant.
float Follow(float now, float target, double step, double rise, double fall) {
    const double ticks = target > now ? rise : fall;
    return now + (target - now) * (1.0f - lib::Exp(static_cast<float>(-step / ticks)));
}

void Extend(Box& box, const Vector3& p) {
    box.low = Vector3{std::min(box.low.x, p.x), std::min(box.low.y, p.y), std::min(box.low.z, p.z)};
    box.high = Vector3{std::max(box.high.x, p.x), std::max(box.high.y, p.y), std::max(box.high.z, p.z)};
}

// A mesh's model-space box (MeshBatch::box) placed by `model`, added to a box in the landscape's space.
void ExtendPlaced(Box& box, const float local[6], const Matrix4& model) {
    for (int k = 0; k < 8; ++k)
        Extend(box, TransformColumn(model, Vector3{local[(k & 1) ? 3 : 0], local[(k & 2) ? 4 : 1], local[(k & 4) ? 5 : 2]}));
}

// Of the rays `wanted` (a bit each) from `from[k]` along `along` (the mesh's model space, the same for all), those
// that meet one of the batch's triangles ahead of them.
unsigned RaysMeetMesh(const Frame& f, const MeshBatch& b, const Vector3* from, const Vector3& along, unsigned wanted) {
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
float Shelter(const Frame& f, const Box& unit) {
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
void CollectUnitsAndRoofs(const Frame& f) {
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

} // namespace

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
    for (MeshBatch& b : f.meshBatches) {
        if (!b.figure) continue;
        auto it = g_units.find(b.caster);
        b.weather[0] = it != g_units.end() ? it->second.wet : s.wet;
        b.weather[1] = it != g_units.end() ? it->second.snow : s.snow;
    }
}

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

} // namespace render::detail

namespace render {

void LandscapeLoaded() {
    detail::g_surfaces.started = false;
    detail::g_units.clear();
}

UnitWeather UnitWeatherState(const void* object) {
    UnitWeather out;
    auto it = detail::g_units.find(object);
    if (it == detail::g_units.end()) return out;
    out.known = true;
    out.wet = it->second.wet;
    out.snow = it->second.snow;
    out.shelter = it->second.shelter;
    out.at = it->second.at;
    return out;
}

} // namespace render
