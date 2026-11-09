// The GPU lighting's scene light (docs/notes/renderer.md section 13): what the world's draw hands over once a frame
// (render::SceneLighting), the terrain shader's uniforms (shaders/scene_lighting.hlsli) and the frame's point lights
// with their grid (PreparePointLights, shaders/point_lights.hlsli). A figure part's light (render::MeshLighting) is
// kept with its sun in the part's model space (FigureLight), a ground print's is the scene's (GroundDecalLight); either
// goes into the mesh's record at its draw (MeshLightRecord).
#include "render.h"
#include "render_internal.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace render {

static KeptScene g_scene;

void SceneLighting(const SceneLights& lights) {
    KeptScene& s = g_scene;
    const Vector3& d = lights.sunDirection;
    const float length = std::sqrt(LengthSquared(d));
    s.sun = length > 1e-09f ? Vector3{d.x / length, d.y / length, d.z / length} : Vector3{0.0f, 0.0f, -1.0f};
    for (int k = 0; k < 3; ++k) {
        s.sunColour[k] = lights.sunColour[k];
        s.ambientColour[k] = lights.ambientColour[k];
        s.daySunColour[k] = lights.daySunColour[k];
        s.dayAmbientColour[k] = lights.dayAmbientColour[k];
    }
    s.camera = lights.camera;
    s.clientTicks = lights.clientTicks;
    s.lights.clear();
    for (int i = 0; i < lights.pointCount && lights.points; ++i)
        if (lights.points[i].inverseRangeSquared > 0.0f) s.lights.push_back(lights.points[i]);
}

// The direction the shadow map's light travels: the frame's sun as the map takes it, else the scene's.
static Vector3 MapSun(const ShadowFrame& shadow) {
    if (shadow.haveSun)
        if (const std::optional<Vector3> travel = MapLightDirection(shadow.sun)) return *travel;
    return g_scene.sun;
}

SceneLightParams SceneLightUniforms(const Frame& f) {
    const KeptScene& s = g_scene;
    SceneLightParams p{};
    const Vector3 mapSun = MapSun(f.shadow);
    const float sun[3] = {s.sun.x, s.sun.y, s.sun.z};
    const float map[3] = {mapSun.x, mapSun.y, mapSun.z};
    const float camera[3] = {s.camera.x, s.camera.y, s.camera.z};
    for (int k = 0; k < 3; ++k) {
        p.sun[k] = sun[k];
        p.sunColour[k] = s.sunColour[k];
        p.ambient[k] = s.ambientColour[k];
        p.mapSun[k] = map[k];
        p.camera[k] = camera[k];
    }
    return p;
}

// The point lights' grid: square cells of at least kLightCell landscape units, at most kMostLightCells along a side (a
// wider spread of lights takes larger cells).
constexpr float kLightCell = 8.0f;
constexpr int kMostLightCells = 128;

// Calls `each` with every cell of the grid (`g`'s, cells found as the shader finds them) that the light's reach (a
// circle over x and y) touches.
template <typename Each>
static void CellsReached(const PointLightParams& g, const ScenePointLight& l, Each each) {
    const float range = 1.0f / std::sqrt(l.inverseRangeSquared), cell = 1.0f / g.gridScale;
    int low[2], high[2];
    const float at[2] = {l.position.x, l.position.y};
    for (int k = 0; k < 2; ++k) {
        const float from = (at[k] - range - g.gridOrigin[k]) * g.gridScale;
        const float to = (at[k] + range - g.gridOrigin[k]) * g.gridScale;
        low[k] = std::clamp(static_cast<int>(std::floor(from)), 0, g.gridSize[k] - 1);
        high[k] = std::clamp(static_cast<int>(std::floor(to)), 0, g.gridSize[k] - 1);
    }
    for (int y = low[1]; y <= high[1]; ++y)
        for (int x = low[0]; x <= high[0]; ++x) {
            const float left = g.gridOrigin[0] + static_cast<float>(x) * cell;
            const float bottom = g.gridOrigin[1] + static_cast<float>(y) * cell;
            const float dx = std::max({left - at[0], 0.0f, at[0] - left - cell});
            const float dy = std::max({bottom - at[1], 0.0f, at[1] - bottom - cell});
            if (LengthSquared2D(dx, dy) <= range * range) each(static_cast<std::size_t>(y * g.gridSize[0] + x));
        }
}

// The frame's lights and grid as uploaded (kept across frames for their room), and the kept lights in view.
static std::vector<PointLight> g_records;
static std::vector<std::uint32_t> g_words;
static std::vector<int> g_inView;
static StreamBuffer g_recordBuffer(gpu::BufferUse::Storage, "ei-point-lights");
static StreamBuffer g_gridBuffer(gpu::BufferUse::Storage, "ei-point-light-grid");

void PreparePointLights(Frame& f) {
    for (const WorldMesh& w : f.worldMeshes) {
        MeshBatch& batch = f.meshBatches[static_cast<std::size_t>(w.batch)];
        if (!batch.flat && batch.lighting >= 0) batch.landscape = batch.placement;
    }
    // Every kept light (at least one record: the buffers are bound whole) with its shadow's map.
    const std::vector<ScenePointLight>& lights = g_scene.lights;
    g_records.assign(std::max<std::size_t>(lights.size(), 1), PointLight{});
    for (std::size_t k = 0; k < lights.size(); ++k) {
        const ScenePointLight& l = lights[k];
        PointLight& r = g_records[k];
        r.at[0] = l.position.x;
        r.at[1] = l.position.y;
        r.at[2] = l.position.z;
        r.at[3] = l.inverseRangeSquared;
        for (int j = 0; j < 3; ++j) r.colour[j] = l.colour[j];
        r.colour[3] = l.specular ? 1.0f : 0.0f;
        r.shadow[0] = -1;
    }
    PointShadowFrame& ps = f.shadow.point;
    for (int row = 0; row < kMostPointShadows; ++row)
        if (ps.rowLights[row] >= 0) g_records[static_cast<std::size_t>(ps.rowLights[row])].shadow[0] = row;
    // The grid over the lights whose reach is in the camera's view: none without a view.
    g_inView.clear();
    Box reach;
    if (const std::optional<WorldView> view = f.worldRanges.empty() ? std::nullopt : WorldCamera(f))
        for (std::size_t k = 0; k < lights.size(); ++k) {
            const ScenePointLight& l = lights[k];
            const float range = 1.0f / std::sqrt(l.inverseRangeSquared);
            if (!SphereInView(view->clip, l.position, range)) continue;
            g_inView.push_back(static_cast<int>(k));
            reach.Extend(l.position - Vector3{range, range, range});
            reach.Extend(l.position + Vector3{range, range, range});
        }
    PointLightParams& g = ps.params;
    const float width = std::max(reach.high.x - reach.low.x, 0.0f), depth = std::max(reach.high.y - reach.low.y, 0.0f);
    const float cell = std::max(kLightCell, std::max(width, depth) / kMostLightCells);
    g.gridOrigin[0] = reach.low.x;
    g.gridOrigin[1] = reach.low.y;
    g.gridSize[0] = std::clamp(static_cast<int>(std::ceil(width / cell)), 1, kMostLightCells);
    g.gridSize[1] = std::clamp(static_cast<int>(std::ceil(depth / cell)), 1, kMostLightCells);
    g.gridScale = 1.0f / cell;
    // Per cell two words, its first light's word and its count, then the cells' lights: counted, placed, filled.
    const auto cells = static_cast<std::size_t>(g.gridSize[0] * g.gridSize[1]);
    g_words.assign(cells * 2, 0);
    for (int k : g_inView)
        CellsReached(g, lights[static_cast<std::size_t>(k)], [](std::size_t cell) { ++g_words[cell * 2 + 1]; });
    auto next = static_cast<std::uint32_t>(cells * 2);
    for (std::size_t c = 0; c < cells; ++c) {
        g_words[c * 2] = next;
        next += g_words[c * 2 + 1];
        g_words[c * 2 + 1] = 0;
    }
    g_words.resize((next + 3) / 4 * 4);   // whole uint4s
    for (int k : g_inView)
        CellsReached(g, lights[static_cast<std::size_t>(k)], [k](std::size_t cell) {
            const std::uint32_t count = g_words[cell * 2 + 1]++;
            g_words[g_words[cell * 2] + count] = static_cast<std::uint32_t>(k);
        });
    // Each buffer is written once a frame, from its start.
    g_recordBuffer.Append(g_records.data(), g_records.size() * sizeof(PointLight));
    g_gridBuffer.Append(g_words.data(), g_words.size() * sizeof(std::uint32_t));
    g_stats.counts[FrameStats::Count::UniformBytes] +=
        g_records.size() * sizeof(PointLight) + g_words.size() * sizeof(std::uint32_t);
}

void BindPointLights(int slot) {
    const gpu::Buffer buffers[2] = {g_recordBuffer.Buffer(), g_gridBuffer.Buffer()};
    gpu::BindStorageBuffers(gpu::Stage::Fragment, slot, buffers);
}

MeshLight FigureLight(const MeshLighting& l) {
    MeshLight light;
    // The sun into the model space: by the rows of the model matrix's upper 3 x 3's inverse (the cross products of its
    // columns over the determinant).
    const float* m = l.model.m;
    const Vector3 ax{m[0], m[4], m[8]}, ay{m[1], m[5], m[9]}, az{m[2], m[6], m[10]};
    Vector3 r0 = Cross(ay, az), r1 = Cross(az, ax), r2 = Cross(ax, ay);
    const float determinant = Dot(ax, r0);
    if (std::fabs(determinant) > 1e-20f) {
        r0 /= determinant;
        r1 /= determinant;
        r2 /= determinant;
    } else {   // singular: taken as the identity
        r0 = Vector3{1.0f, 0.0f, 0.0f};
        r1 = Vector3{0.0f, 1.0f, 0.0f};
        r2 = Vector3{0.0f, 0.0f, 1.0f};
    }
    const float* diffuse = l.diffuseColour;
    if (l.sun) {
        const Vector3 toSun = -l.sunDirection;   // as the figure part draw's TransformSun (no translation)
        light.sun = Vector3{Dot(r0, toSun), Dot(r1, toSun), Dot(r2, toSun)};
        for (int k = 0; k < 3; ++k) light.sunColour[k] = diffuse[k] * l.sunColour[k] * l.coefficient;
    }
    for (int k = 0; k < 3; ++k) {
        light.ambientColour[k] = l.ambientColour[k];
        light.emissiveColour[k] = l.emissiveColour[k];
        light.pointTint[k] = diffuse[k] * l.coefficient;
    }
    return light;
}

MeshLight GroundDecalLight(const Frame& f) {
    const KeptScene& scene = g_scene;
    MeshLight light;
    light.groundPrint = true;
    light.sun = -scene.sun;
    light.mapSun = MapSun(f.shadow);
    for (int k = 0; k < 3; ++k) {
        light.sunColour[k] = scene.sunColour[k];
        light.ambientColour[k] = scene.ambientColour[k];
        light.daySunColour[k] = scene.daySunColour[k];
        light.dayAmbientColour[k] = scene.dayAmbientColour[k];
    }
    return light;
}

void MeshLightRecord(const MeshLight& light, MeshRecord& record, PartLight& p) {
    record.lit[0] = light.groundPrint ? 0 : 1;
    record.lit[1] = light.groundPrint ? 1 : 0;
    const float sun[3] = {light.sun.x, light.sun.y, light.sun.z};
    const float mapSun[3] = {light.mapSun.x, light.mapSun.y, light.mapSun.z};
    for (int k = 0; k < 3; ++k) {
        p.sun[k] = sun[k];
        p.sunColour[k] = light.sunColour[k];
        p.ambient[k] = light.ambientColour[k];
        p.emissive[k] = light.emissiveColour[k];
        p.mapSun[k] = mapSun[k];
        p.daySunColour[k] = light.daySunColour[k];
        p.dayAmbient[k] = light.dayAmbientColour[k];
        p.pointTint[k] = light.pointTint[k];
    }
}

const KeptScene& KeptSceneLight() { return g_scene; }

} // namespace render
