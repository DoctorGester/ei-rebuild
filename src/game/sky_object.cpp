// SkyObject and Sky's lighting. See sky_object.h.
#include "sky_object.h"

#include "render/render.h"   // MeshChanged
#include "texture.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"

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

SkyObject::SkyObject() {
    CreateMesh();
}

SkyObject::~SkyObject() {  }

const char* SkyObject::ClassName() { return kClassName; }
ClassId SkyObject::GetClassId() { return ClassId::SkyObject; }

void SkyObject::Light(const float* direction, SkyColour ambient, SkyColour light) {
    const std::uint32_t r = Truncate(ambient.r * kColourScale);
    const std::uint32_t g = Truncate(ambient.g * kColourScale);
    const std::uint32_t b = Truncate(ambient.b * kColourScale);
    const std::uint32_t lit = ((r | 0xFFFFFF00u) << 8 | g) << 8 | b;
    const int count = std::ssize(colorsA);
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
        EI_NAN_AUDIT(distance);
        if (distance == 0.0f) {
            colorsB[i] = 0;
            colorsA[i] = (r << 8 | g) << 8 | b;
            continue;
        }
        float cosine = DotUnrounded<Sum3::ZYX>(x, y, z, direction[0], direction[1], direction[2]);
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

void Sky::Light(const float* direction, SkyColour ambient, SkyColour light) {
    if (object) object->Light(direction, ambient, light);
}

Sky::Sky() {
    turns = 1;
    LoadNamedTexture(texture, kSkyTextureKind, kSkyTextureName);
}

void Sky::Draw(SceneRenderer* renderer, const Vector3& eye) {
    if (!object) return;
    const float x = eye.x, y = eye.y, z = eye.z - kSkyDrop;
    if (turns) {
        const Vector3 up{0.0f, 0.0f, 1.0f};
        Quaternion turn;
        // The world time in ticks: whole ticks plus
        // the fraction of the current one.
        object->localRotation = *turn.SetAxisAngle(up, GameTime() * kSkyTurnRate);
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
    TransformPipeline* pipeline = renderer->pipeline;
    pipeline->PushMatrix(m, TransformPipeline::kClipStack);
    pipeline->PushMatrix(m, 1);
    EnsureTextureLoaded(texture);
    renderer->SetTexture(0, texture);
    renderer->DrawFigure(object, nullptr, 0xFF, nullptr);
    const int stacks[2] = {TransformPipeline::kClipStack, 1};
    for (int stack : stacks) {
        int& count = pipeline->stacks[stack].count;
        if (count != 0) --count;
    }
}

Sky::~Sky() {
    texture.Reset();
    material.Reset();
    object.Reset();
}
