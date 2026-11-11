// SkyObject and Sky's lighting. See sky_object.h.
#include "engine/scene/sky_object.h"

#include "render/render.h"   // MeshChanged
#include "engine/scene/device.h"
#include "engine/scene/scene_renderer.h"
#include "engine/models/link_figure.h"   // FigureMeshDrawer
#include "engine/models/texture.h"
#include "game/world/world.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <new>
#include <vector>

namespace {

constexpr const char* kClassName = "CSkyObject";
constexpr float kColourScale = 255.0f;
constexpr float kLightScale = 150.0f;
constexpr float kHorizon = 1.0f;

// A vertex's row `row` (x 0, y 1, z 2) in its packet: of the positions, of the normals.
float Row(const std::vector<VertexPacket>& packets, int vertex, int row) {
    return packets[vertex / 4].block[row][vertex % 4];
}
float Row(const std::vector<NormalPacket>& packets, int vertex, int row) {
    return packets[vertex / 4].block[row][vertex % 4];
}

// _ftol: truncation.
std::uint32_t Truncate(float v) { return static_cast<std::uint32_t>(static_cast<int>(v)); }

constexpr int kSkyTextureKind = static_cast<int>(TextureKind::SkyFace);
const char* const kSkyTextureName = "sky00";
constexpr float kSkyDrop = 16.0f;                // the dome sits this far below the eye
constexpr float kSkyTurnRate = 1.0f / 3000.0f;   // radians per world tick

} // namespace

SkyObject::SkyObject() = default;

SkyObject::~SkyObject() = default;

const char* SkyObject::ClassName() { return kClassName; }
ClassId SkyObject::GetClassId() { return ClassId::SkyObject; }

void SkyObject::ApplyLight(const Vector3& direction, SkyColour ambient, SkyColour light) {
    const std::uint32_t r = Truncate(ambient.r * kColourScale);
    const std::uint32_t g = Truncate(ambient.g * kColourScale);
    const std::uint32_t b = Truncate(ambient.b * kColourScale);
    const std::uint32_t lit = ((r | 0xFFFFFF00u) << 8 | g) << 8 | b;
    const int count = lib::Count(colorsA);
    if (std::ssize(distances) != count) {
        distances.resize(count);
        for (int i = 0; i < std::ssize(distances); ++i) {
            const float z = Row(vertices, i, 2);
            distances[i] = z >= kHorizon ? std::sqrt(-Row(normals, i, 2)) : 0.0f;
        }
    }
    for (int i = 0; i < std::ssize(colorsA); ++i) {
        const float x = Row(normals, i, 0);
        const float y = Row(normals, i, 1);
        const float z = Row(normals, i, 2);
        const float distance = distances[i];
        if (distance == 0.0f) {
            colorsB[i] = 0;
            colorsA[i] = (r << 8 | g) << 8 | b;
            continue;
        }
        float cosine = Dot(Vector3{x, y, z}, Vector3{direction.x, direction.y, direction.z});
        cosine = MaxKeepB(cosine, 0.0f);
        const float strength = cosine * cosine;
        std::uint32_t specular = Truncate(distance * kColourScale);
        specular = specular << 8 | Truncate(light.r * strength * kLightScale);
        specular = specular << 8 | Truncate(light.g * strength * kLightScale);
        specular = specular << 8 | Truncate(light.b * strength * kLightScale);
        colorsB[i] = specular;
        colorsA[i] = lit;
    }
    render::MeshChanged(this, render::MeshChange::Colours);   // the colours the new renderer keeps
}

void Sky::ApplyLight(const Vector3& direction, SkyColour ambient, SkyColour light) {
    if (object) object->ApplyLight(direction, ambient, light);
}

Sky::Sky() {
    LoadNamedTexture(texture, kSkyTextureKind, kSkyTextureName);
}

void Sky::Draw(SceneRenderer* renderer, const Vector3& eye) {
    if (!object) return;
    const float x = eye.x, y = eye.y, z = eye.z - kSkyDrop;
    if (turns) {
        const Vector3 up{0.0f, 0.0f, 1.0f};
        // The world time in ticks: whole ticks plus
        // the fraction of the current one.
        object->localRotation = Quaternion::AxisAngle(up, GameTime() * kSkyTurnRate);
    }
    object->localPosition = Vector3{x, y, z};
    float* m = object->worldMatrix.m;
    object->localRotation.ToMatrix(m);
    m[3] = object->localPosition.x;
    m[7] = object->localPosition.y;
    m[11] = object->localPosition.z;
    m[12] = 0.0f;
    m[13] = 0.0f;
    m[14] = 0.0f;
    m[15] = 1.0f;
    EnsureTextureLoaded(texture);
    renderer->SetTexture(0, texture);
    // Hazed towards the horizon, the fog's colour at the eye's level (render::MeshFog::Horizon).
    TextureDevice* device = renderer->device;
    const render::MeshFog fog = device->fog;
    device->fog = render::MeshFog::Horizon;
    device->eyeHeight = kSkyDrop;
    object->drawer->DrawPlain(device, &object->worldMatrix);
    device->fog = fog;
}

Sky::~Sky() {
    texture.Reset();
    material.Reset();
    object.Reset();
}
