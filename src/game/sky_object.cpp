// CSkyObject and CSky's lighting (A-S7, 004D9110-004D9DE0). See sky_object.h.
#include "sky_object.h"
#include "texture.h"
#include "world.h"
// windows.h (texture.h) renames CEngineObject's GetClassName.
#undef GetClassName

#include "originals_core.h"
#include "render/render.h"   // MeshChanged

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/crt.h"
#include "game_data.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <new>

namespace {

constexpr const char* kClassName = "CSkyObject";
constexpr int kClassId = 0x20;
constexpr float kColourScale = 255.0f;   // 0073C370
constexpr float kLightScale = 150.0f;    // 0073D618
constexpr float kHorizon = 1.0f;         // 0073B84C

// A vertex's row `row` (x 0, y 1, z 2) in its packet: of the positions, of the normals.
float Row(const lib::AlignedArray<VertexPacket>& packets, int vertex, int row) {
    return packets.data[vertex / 4].block[row][vertex % 4];
}
float Row(const lib::AlignedArray<NormalPacket>& packets, int vertex, int row) {
    return std::bit_cast<float>(packets.data[vertex / 4].w[row * 4 + vertex % 4]);
}

// _ftol: truncation.
std::uint32_t Truncate(float v) { return static_cast<std::uint32_t>(static_cast<int>(v)); }

} // namespace

CSkyObject::CSkyObject() {
    distances_.Construct();
    CreateMesh();
}

CSkyObject::~CSkyObject() { distances_.FreeData(); }

const char* CSkyObject::GetClassName() { return kClassName; }
int CSkyObject::GetClassId() { return kClassId; }

CSkyObject* CSkyObject::Construct() { return new (this) CSkyObject(); }
EI_REWRITTEN(0x004D9110, CSkyObject::Construct);

void CSkyObject::Destruct() { this->CSkyObject::~CSkyObject(); }
EI_REWRITTEN(0x004D92B0, CSkyObject::Destruct);

CSkyObject* CSkyObject::DeletingDestruct(unsigned flags) {
    this->CSkyObject::~CSkyObject();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x004D9290, CSkyObject::DeletingDestruct);

void CSkyObject::Light(const float* direction, SkyColour ambient, SkyColour light, SkyColour) {
    const std::uint32_t r = Truncate(ambient.r * kColourScale);
    const std::uint32_t g = Truncate(ambient.g * kColourScale);
    const std::uint32_t b = Truncate(ambient.b * kColourScale);
    const std::uint32_t lit = ((r | 0xFFFFFF00u) << 8 | g) << 8 | b;
    const int count = colorsA_.size;
    if (distances_.size != count) {
        distances_.SetSize(count);
        for (int i = 0; i < distances_.size; ++i) {
            const float z = Row(vertices_, i, 2);
            distances_.data[i] = static_cast<float>(z >= kHorizon ? std::sqrt(-Row(normals_, i, 2)) : 0.0f);
        }
    }
    for (int i = 0; i < colorsA_.size; ++i) {
        const float x = Row(normals_, i, 0);
        const float y = Row(normals_, i, 1);
        const float z = Row(normals_, i, 2);
        const float distance = distances_.data[i];
        EI_NAN_AUDIT(distance);
        if (distance == 0.0f) {
            colorsB_.data[i] = 0;
            colorsA_.data[i] = (r << 8 | g) << 8 | b;
            continue;
        }
        float cosine = DotUnrounded<Sum3::ZYX>(x, y, z, direction[0], direction[1], direction[2]);
        cosine = cosine >= 0.0f ? cosine : 0.0f;
        const float strength = cosine * cosine;
        std::uint32_t specular = Truncate(distance * kColourScale);
        specular = specular << 8 | Truncate(light.r * strength * kLightScale);
        specular = specular << 8 | Truncate(light.g * strength * kLightScale);
        specular = specular << 8 | Truncate(light.b * strength * kLightScale);
        colorsB_.data[i] = specular;
        colorsA_.data[i] = lit;
    }
    render::MeshChanged(this, render::MeshChange::Colours);   // the colours the new renderer keeps (R16d)
}

void CSky::Light(const float* direction, SkyColour ambient, SkyColour light, SkyColour unused) {
    if (object) object->Light(direction, ambient, light, unused);
}

namespace {
constexpr int kSkyTextureKind = 0x3D;
const char* const kSkyTextureName = "sky00";
constexpr float kSkyDrop = 16.0f;                // 0073CA98: the dome sits this far below the eye
constexpr float kSkyTurnRate = 1.0f / 3000.0f;   // 0073D61C: radians per world tick
// Two 16-byte statics the draw passes to renderer slot 14 (not read there).
const void* const kSkyDrawA = ei::GamePointer<const void>(0x007AA890);
const void* const kSkyDrawB = ei::GamePointer<const void>(0x007AA8A0);

} // namespace

CSky* CSky::Construct() {
    object = nullptr;
    material = nullptr;
    texture = nullptr;
    turns = 1;
    TextureHolderLoad(reinterpret_cast<CTexture**>(&texture), nullptr, kSkyTextureKind, kSkyTextureName);
    return this;
}
EI_REWRITTEN(0x004D9680, CSky::Construct);

void CSky::Draw(CSceneRenderer* renderer, const Vector3* eye) {
    if (!object) return;
    const float x = eye->x, y = eye->y, z = eye->z - kSkyDrop;
    if (turns) {
        const Vector3 up{0.0f, 0.0f, 1.0f};
        Quaternion turn;
        // 00578B40 (thiscall on the client glue, not read): the world time in ticks, whole ticks plus
        // the fraction of the current one.
        object->localRotation_ = *turn.SetAxisAngle(up, static_cast<float>(static_cast<double>(GameTime()) * kSkyTurnRate));
    }
    object->localPosition_ = Vector3{x, y, z};
    float* m = object->worldMatrix_.m;
    object->localRotation_.ToMatrix(m);
    m[3] = object->localPosition_.x;
    m[7] = object->localPosition_.y;
    m[11] = object->localPosition_.z;
    m[12] = 0.0f;
    m[13] = 0.0f;
    m[14] = 0.0f;
    m[15] = 1.0f;
    CTransformPipeline* pipeline = renderer->pipeline_;
    pipeline->PushMatrix(m, CTransformPipeline::kClipStack);
    pipeline->PushMatrix(m, 1);
    TextureHolderEnsureLoaded(reinterpret_cast<CTexture**>(&texture));
    renderer->SetTexture(0, texture);
    renderer->DrawFigure(object, kSkyDrawA, kSkyDrawB, 0, 0xFF, nullptr);
    const int stacks[2] = {CTransformPipeline::kClipStack, 1};
    for (int stack : stacks) {
        int& count = pipeline->stacks_[stack].count;
        if (count != 0) --count;
    }
}
EI_REWRITTEN(0x004D96E0, CSky::Draw);

void CSky::Destruct() {
    if (texture) static_cast<CEngineObject*>(texture)->Release();
    if (material) material->Release();
    if (object) object->Release();
}
EI_REPLACE_METHOD(0x005636A0, CSky::Destruct);

namespace {
const char* __fastcall ClassNameHook(CSkyObject* self) { return self->CSkyObject::GetClassName(); }
int __fastcall ClassIdHook(CSkyObject* self) { return self->CSkyObject::GetClassId(); }
} // namespace
EI_REWRITTEN(0x004D9270, ClassNameHook);
EI_REWRITTEN(0x004D9280, ClassIdHook);
EI_REWRITTEN(0x004D9360, CSkyObject::Light);
EI_REWRITTEN(0x004D9DE0, CSky::Light);

// The engine's aligned array constructors (identical copies), on the shared template.
EI_REWRITTEN(0x004D9E80, lib::AlignedArray<std::uint32_t>::Construct);
EI_REWRITTEN(0x004D9E90, lib::AlignedArray<std::uint32_t>::Construct);
EI_REWRITTEN(0x004D9EA0, lib::AlignedArray<std::uint32_t>::Construct);
// S13: more copies, of the scene loader's point block (00475F10), the unit skins' bytes (0056FCC0),
// the figure factory's packets (0056FD00) and the terrain's arrays (0056FD30 the texture classes,
// frames and visible columns; 00570DE0 the wave phases; 00570E10 the page materials; 00570E40 the
// water materials; 00570E70 the vertex tiles).
EI_REWRITTEN(0x00475F10, lib::AlignedArray<std::uint32_t>::Construct);
EI_REWRITTEN(0x0056FCC0, lib::AlignedArray<std::uint32_t>::Construct);
EI_REWRITTEN(0x0056FD00, lib::AlignedArray<std::uint32_t>::Construct);
EI_REWRITTEN(0x0056FD30, lib::AlignedArray<std::uint32_t>::Construct);
EI_REWRITTEN(0x00570DE0, lib::AlignedArray<std::uint32_t>::Construct);
EI_REWRITTEN(0x00570E10, lib::AlignedArray<std::uint32_t>::Construct);
EI_REWRITTEN(0x00570E40, lib::AlignedArray<std::uint32_t>::Construct);
EI_REWRITTEN(0x00570E70, lib::AlignedArray<std::uint32_t>::Construct);
