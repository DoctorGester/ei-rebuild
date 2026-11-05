// The GPU lighting's scene light (docs/notes/renderer.md section 13): what the world's draw hands
// over once a frame (render::SceneLighting) kept for the terrain shader's uniforms
// (shaders/scene_lighting.hlsli). The point lights nearest the camera (by the distance to their reach)
// are kept, at most kMostSceneLights. A figure part's light (render::MeshLighting) becomes the mesh
// shader's uniforms (MeshLightUniforms) in the part's model space, a ground print's the scene's (GroundDecalLight).
#include "render.h"
#include "render_internal.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace render {

// Until the first hand-over: a sun from above, half ambient.
static SceneLightParams g_params = {{0.0f, 0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 0.0f}, {0.5f, 0.5f, 0.5f, 0.0f}};
// Per kept point light: it casts point shadows (ScenePointLight::shadows).
static bool g_shadows[kMostSceneLights] = {};
// The daylight colours (SceneLights::daySunColour, dayAmbientColour): until the first hand-over the sun's and the
// ambient light's above.
static float g_daySunColour[3] = {1.0f, 1.0f, 1.0f};
static float g_dayAmbientColour[3] = {0.5f, 0.5f, 0.5f};
static double g_clientTicks = 0.0;

void SceneLighting(const SceneLights& lights) {
    SceneLightParams p{};
    const Vector3& d = lights.sunDirection;
    const float length = std::sqrt(LengthSquared(d));
    const float sun[3] = {d.x, d.y, d.z};
    for (int k = 0; k < 3; ++k) {
        p.sun[k] = length > 1e-09f ? sun[k] / length : (k == 2 ? -1.0f : 0.0f);
        p.sunColour[k] = lights.sunColour[k];
        p.ambient[k] = lights.ambientColour[k];
        g_daySunColour[k] = lights.daySunColour[k];
        g_dayAmbientColour[k] = lights.dayAmbientColour[k];
    }
    p.camera[0] = lights.camera.x;
    p.camera[1] = lights.camera.y;
    p.camera[2] = lights.camera.z;
    g_clientTicks = lights.clientTicks;
    // The nearest point lights: by how far the camera is outside each one's reach.
    static std::vector<std::pair<float, int>> order;
    order.clear();
    for (int i = 0; i < lights.pointCount && lights.points; ++i) {
        const ScenePointLight& l = lights.points[i];
        if (!(l.inverseRangeSquared > 0.0f)) continue;
        const float distance = std::sqrt(LengthSquared(l.position - lights.camera));
        const float outside = distance - 1.0f / std::sqrt(l.inverseRangeSquared);
        order.emplace_back(outside, i);
    }
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    int count = std::min(static_cast<int>(order.size()), kMostSceneLights);
    bool shadows[kMostSceneLights] = {};
    for (int k = 0; k < count; ++k) {
        const ScenePointLight& l = lights.points[order[static_cast<std::size_t>(k)].second];
        p.pointAt[k][0] = l.position.x;
        p.pointAt[k][1] = l.position.y;
        p.pointAt[k][2] = l.position.z;
        p.pointAt[k][3] = l.inverseRangeSquared;
        for (int j = 0; j < 3; ++j) p.pointColour[k][j] = l.colour[j];
        p.pointColour[k][3] = l.specular ? 1.0f : 0.0f;
        shadows[k] = l.shadows;
    }
    p.counts[0] = static_cast<float>(count);
    g_params = p;
    std::copy(std::begin(shadows), std::end(shadows), g_shadows);
}

void LightUniforms(SceneLightParams* params) {
    *params = g_params;
    const Frame& f = g_frame;
    for (int k = 0; k < kMostSceneLights; ++k)
        params->shadowSlots[k / 4][k % 4] = static_cast<float>(PointShadowSlot(f, k) + 1);
    const ShadowFrame& sh = g_frame.shadow;
    Vector3 travel{params->sun[0], params->sun[1], params->sun[2]};
    if (sh.haveSun) MapLightDirection(sh.sun, &travel);
    params->mapSun[0] = travel.x;
    params->mapSun[1] = travel.y;
    params->mapSun[2] = travel.z;
}

void MeshLightUniforms(const MeshLighting& l, MeshLightParams* params) {
    MeshLightParams p{};
    Matrix4 model = l.model;
    if (l.camera && !UnderCamera(*l.camera, l.model, &model)) model = Matrix4{};
    // The model matrix's inverse: rows of its upper 3 x 3's inverse (the cross products of its columns
    // over the determinant); the translation `at` taken off first.
    const float* m = model.m;
    const Vector3 ax{m[0], m[4], m[8]}, ay{m[1], m[5], m[9]}, az{m[2], m[6], m[10]};
    const Vector3 at{m[3], m[7], m[11]};
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
    auto toModel = [&](const Vector3& v) {
        return Vector3{Dot(r0, v), Dot(r1, v), Dot(r2, v)};
    };
    const float* diffuse = l.diffuseColour;
    p.on[0] = 1.0f;
    if (l.sun) {
        const Vector3 towards = toModel(-l.sunDirection);   // as the figure part draw's TransformSun (no translation)
        p.sun[0] = towards.x;
        p.sun[1] = towards.y;
        p.sun[2] = towards.z;
        for (int k = 0; k < 3; ++k) p.sunColour[k] = diffuse[k] * l.sunColour[k] * l.coefficient;
    }
    for (int k = 0; k < 3; ++k) {
        p.ambient[k] = l.ambientColour[k];
        p.emissive[k] = l.emissiveColour[k];
    }
    // The lights nearest the part's origin.
    static std::vector<std::pair<float, int>> order;
    order.clear();
    for (int i = 0; i < l.lightCount && l.lights; ++i)
        order.emplace_back(LengthSquared(l.lights[i].position - at), i);
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    const int count = (std::min)(static_cast<int>(order.size()), kMostMeshLights);
    for (int k = 0; k < count; ++k) {
        const MeshLightSource& s = l.lights[order[static_cast<std::size_t>(k)].second];
        const Vector3 position = toModel(s.position - at);
        p.pointAt[k][0] = position.x;
        p.pointAt[k][1] = position.y;
        p.pointAt[k][2] = position.z;
        p.pointAt[k][3] = s.inverseRangeSquared;
        for (int j = 0; j < 3; ++j) p.pointColour[k][j] = diffuse[j] * s.colour[j] * l.coefficient;
        p.pointColour[k][3] = static_cast<float>(SceneLightIndex(s.position) + 1);   // its point shadow at the draw
    }
    p.on[2] = static_cast<float>(count);
    *params = p;
}

void GroundDecalLight(const Frame& frame, const float box[6], MeshLightParams* params) {
    SceneLightParams scene;
    LightUniforms(&scene);
    MeshLightParams p{};
    p.on[1] = 1.0f;
    for (int k = 0; k < 3; ++k) {
        p.sun[k] = -scene.sun[k];
        p.sunColour[k] = scene.sunColour[k];
        p.ambient[k] = scene.ambient[k];
        p.mapSun[k] = scene.mapSun[k];
        p.daySunColour[k] = g_daySunColour[k];
        p.dayAmbient[k] = g_dayAmbientColour[k];
    }
    // The lights that reach the box's sphere, the nearest to their reach first.
    const Vector3 low{box[0], box[1], box[2]}, high{box[3], box[4], box[5]};
    const Vector3 centre = (low + high) * 0.5f;
    const float radius = std::sqrt(LengthSquared(high - low)) * 0.5f;
    static std::vector<std::pair<float, int>> order;
    order.clear();
    for (int k = 0; k < static_cast<int>(scene.counts[0]); ++k) {
        const float* at = scene.pointAt[k];
        if (!(at[3] > 0.0f)) continue;
        const float outside = std::sqrt(LengthSquared(Vector3{at[0], at[1], at[2]} - centre)) - 1.0f / std::sqrt(at[3]);
        if (outside < radius) order.emplace_back(outside, k);
    }
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    const int count = std::min(static_cast<int>(order.size()), kMostMeshLights);
    for (int j = 0; j < count; ++j) {
        const int k = order[static_cast<std::size_t>(j)].second;
        for (int i = 0; i < 4; ++i) p.pointAt[j][i] = scene.pointAt[k][i];
        for (int i = 0; i < 3; ++i) p.pointColour[j][i] = scene.pointColour[k][i];
        p.pointColour[j][3] = static_cast<float>(PointShadowSlot(frame, k) + 1);
        p.pointSpecular[j / 4][j % 4] = scene.pointColour[k][3];
    }
    p.on[2] = static_cast<float>(count);
    *params = p;
}

const SceneLightParams& KeptSceneLight() { return g_params; }

double KeptClientTicks() { return g_clientTicks; }

bool SceneLightCastsShadows(int k) { return k >= 0 && k < kMostSceneLights && g_shadows[k]; }

int SceneLightIndex(const Vector3& position) {
    const int count = static_cast<int>(g_params.counts[0]);
    for (int k = 0; k < count; ++k) {
        const float* at = g_params.pointAt[k];
        if (at[0] == position.x && at[1] == position.y && at[2] == position.z) return k;
    }
    return -1;
}

} // namespace render
