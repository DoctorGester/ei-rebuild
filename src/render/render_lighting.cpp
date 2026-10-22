// The GPU lighting's scene light (R16i; docs/notes/renderer.md section 16): what the world's draw hands
// over once a frame (render::SceneLighting) kept for the terrain and mesh shaders' uniforms
// (shaders/scene_lighting.hlsli). The point lights nearest the camera (by the distance to their reach)
// are kept, at most kMostSceneLights.
#include "render.h"
#include "render_internal.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace render {

namespace {

using namespace detail;

// Until the first hand-over: a sun from above, half ambient.
LightParams g_params = {{0.0f, 0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 0.0f}, {0.5f, 0.5f, 0.5f, 0.0f}, {}, {}};

} // namespace

void SceneLighting(const SceneLights& lights) {
    if (!Active()) return;
    LightParams p{};
    const Vector3& d = lights.sunDirection;
    const double length = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ, double>(d));
    const float sun[3] = {d.x, d.y, d.z};
    for (int k = 0; k < 3; ++k) {
        p.sun[k] = length > 1e-9 ? static_cast<float>(sun[k] / length) : (k == 2 ? -1.0f : 0.0f);
        p.sunColour[k] = lights.sunColour[k];
        p.ambient[k] = lights.ambientColour[k];
    }
    // The nearest point lights: by how far the camera is outside each one's reach.
    static std::vector<std::pair<double, int>> order;
    order.clear();
    for (int i = 0; i < lights.pointCount && lights.points; ++i) {
        const ScenePointLight& l = lights.points[i];
        if (!(l.inverseRangeSquared > 0.0f)) continue;
        const double distance = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ, double>(l.position - lights.camera));
        const double outside = distance - 1.0 / std::sqrt(static_cast<double>(l.inverseRangeSquared));
        order.emplace_back(outside, i);
    }
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    int count = std::min(static_cast<int>(order.size()), kMostSceneLights);
    for (int k = 0; k < count; ++k) {
        const ScenePointLight& l = lights.points[order[static_cast<std::size_t>(k)].second];
        p.points[k][0][0] = l.position.x;
        p.points[k][0][1] = l.position.y;
        p.points[k][0][2] = l.position.z;
        for (int j = 0; j < 3; ++j) p.points[k][1][j] = l.colour[j];
        p.points[k][0][3] = l.inverseRangeSquared;
        p.points[k][1][3] = l.specular ? 1.0f : 0.0f;
    }
    p.counts[0] = static_cast<float>(count);
    g_params = p;
}

bool GpuTerrainLighting() { return Active(); }

namespace detail {

void LightUniforms(LightParams* params) { *params = g_params; }

Vector3 SceneSunDirection() { return Vector3{g_params.sun[0], g_params.sun[1], g_params.sun[2]}; }

} // namespace detail

} // namespace render
