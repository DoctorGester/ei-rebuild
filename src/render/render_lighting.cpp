// The GPU lighting's scene light (docs/notes/renderer.md section 13): what the world's draw hands
// over once a frame (render::SceneLighting) kept for the terrain shader's uniforms
// (shaders/scene_lighting.hlsli). The point lights nearest the camera (by the distance to their reach)
// are kept, at most kMostSceneLights. A figure part's light (render::MeshLighting) is kept in the part's model space
// (FigureLight), a ground print's is the scene's (GroundDecalLight); either becomes the mesh shader's uniforms at its
// draw (MeshLightUniforms).
#include "render.h"
#include "render_internal.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace render {

static KeptScene g_scene;
// The lights in the order SceneLighting, FigureLight and GroundDecalLight take them (by distance, then index),
// kept for its room.
static std::vector<std::pair<float, int>> g_order;
// FigureLight's: the parts' camera (one per frame in the world's draw), inverted once.
static CameraInverse g_partCamera;

void SceneLighting(const SceneLights& lights) {
    KeptScene s;
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
    // The nearest point lights: by how far the camera is outside each one's reach.
    std::vector<std::pair<float, int>>& order = g_order;
    order.clear();
    for (int i = 0; i < lights.pointCount && lights.points; ++i) {
        const ScenePointLight& l = lights.points[i];
        if (!(l.inverseRangeSquared > 0.0f)) continue;
        const float distance = std::sqrt(LengthSquared(l.position - lights.camera));
        const float outside = distance - 1.0f / std::sqrt(l.inverseRangeSquared);
        order.emplace_back(outside, i);
    }
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    s.count = std::min(static_cast<int>(order.size()), kMostSceneLights);
    for (int k = 0; k < s.count; ++k) {
        const ScenePointLight& l = lights.points[order[static_cast<std::size_t>(k)].second];
        KeptLight& kept = s.lights[k];
        kept.at = l.position;
        kept.inverseRangeSquared = l.inverseRangeSquared;
        for (int j = 0; j < 3; ++j) kept.colour[j] = l.colour[j];
        kept.specular = l.specular;
        kept.shadows = l.shadows;
    }
    g_scene = s;
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
    p.pointCount = s.count;
    for (int k = 0; k < s.count; ++k) {
        const KeptLight& l = s.lights[k];
        p.pointAt[k][0] = l.at.x;
        p.pointAt[k][1] = l.at.y;
        p.pointAt[k][2] = l.at.z;
        p.pointAt[k][3] = l.inverseRangeSquared;
        for (int j = 0; j < 3; ++j) p.pointColour[k][j] = l.colour[j];
        p.pointSpecular[k / 4][k % 4] = l.specular ? 1 : 0;
    }
    for (int k = 0; k < kMostSceneLights; ++k) p.pointShadows[k / 4][k % 4] = PointShadowSlot(f, k);
    return p;
}

MeshLight FigureLight(const MeshLighting& l) {
    MeshLight light;
    Matrix4 model = l.model;
    if (l.camera) model = g_partCamera.Invert(*l.camera) ? g_partCamera.Under(l.model) : Matrix4{};
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
    if (l.sun) {
        light.sun = toModel(-l.sunDirection);   // as the figure part draw's TransformSun (no translation)
        for (int k = 0; k < 3; ++k) light.sunColour[k] = diffuse[k] * l.sunColour[k] * l.coefficient;
    }
    for (int k = 0; k < 3; ++k) {
        light.ambientColour[k] = l.ambientColour[k];
        light.emissiveColour[k] = l.emissiveColour[k];
    }
    // The lights nearest the part's origin.
    std::vector<std::pair<float, int>>& order = g_order;
    order.clear();
    for (int i = 0; i < l.lightCount && l.lights; ++i)
        order.emplace_back(LengthSquared(l.lights[i].position - at), i);
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    light.count = (std::min)(static_cast<int>(order.size()), kMostMeshLights);
    for (int k = 0; k < light.count; ++k) {
        const MeshLightSource& s = l.lights[order[static_cast<std::size_t>(k)].second];
        MeshLight::Point& point = light.points[k];
        point.at = toModel(s.position - at);
        point.inverseRangeSquared = s.inverseRangeSquared;
        for (int j = 0; j < 3; ++j) point.colour[j] = diffuse[j] * s.colour[j] * l.coefficient;
        point.sceneLight = SceneLightIndex(s.position);
    }
    return light;
}

MeshLight GroundDecalLight(const Frame& f, const float box[6]) {
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
    // The lights that reach the box's sphere, the nearest to their reach first.
    const Vector3 low{box[0], box[1], box[2]}, high{box[3], box[4], box[5]};
    const Vector3 centre = (low + high) * 0.5f;
    const float radius = std::sqrt(LengthSquared(high - low)) * 0.5f;
    std::vector<std::pair<float, int>>& order = g_order;
    order.clear();
    for (int k = 0; k < scene.count; ++k) {
        const KeptLight& l = scene.lights[k];
        if (!(l.inverseRangeSquared > 0.0f)) continue;
        const float outside = std::sqrt(LengthSquared(l.at - centre)) - 1.0f / std::sqrt(l.inverseRangeSquared);
        if (outside < radius) order.emplace_back(outside, k);
    }
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    light.count = std::min(static_cast<int>(order.size()), kMostMeshLights);
    for (int j = 0; j < light.count; ++j) {
        const int k = order[static_cast<std::size_t>(j)].second;
        const KeptLight& l = scene.lights[k];
        MeshLight::Point& point = light.points[j];
        point.at = l.at;
        point.inverseRangeSquared = l.inverseRangeSquared;
        for (int i = 0; i < 3; ++i) point.colour[i] = l.colour[i];
        point.sceneLight = k;
        point.specular = l.specular;
    }
    return light;
}

MeshLightParams MeshLightUniforms(const Frame& frame, const MeshLight& light) {
    MeshLightParams p{};
    p.lit = light.groundPrint ? 0 : 1;
    p.groundPrint = light.groundPrint ? 1 : 0;
    p.lightCount = light.count;
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
    }
    for (int k = 0; k < kMostMeshLights; ++k) {
        const MeshLight::Point& point = light.points[k];
        p.pointAt[k][0] = point.at.x;
        p.pointAt[k][1] = point.at.y;
        p.pointAt[k][2] = point.at.z;
        p.pointAt[k][3] = point.inverseRangeSquared;
        for (int j = 0; j < 3; ++j) p.pointColour[k][j] = point.colour[j];
        p.pointShadows[k / 4][k % 4] = PointShadowSlot(frame, point.sceneLight);
        p.pointSpecular[k / 4][k % 4] = point.specular ? 1 : 0;
    }
    return p;
}

const KeptScene& KeptSceneLight() { return g_scene; }

int SceneLightIndex(const Vector3& position) {
    for (int k = 0; k < g_scene.count; ++k) {
        const Vector3& at = g_scene.lights[k].at;
        if (at.x == position.x && at.y == position.y && at.z == position.z) return k;
    }
    return -1;
}

} // namespace render
