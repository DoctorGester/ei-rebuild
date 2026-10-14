// CSkyObject and CSky's lighting (A-S7, 004D9110-004D9DE0). See sky_object.h.
#include "sky_object.h"

#include "originals_core.h"
#include "originals_render.h"   // LoadTextureOriginal, TextureEnsureLoadedOriginal

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "lib/crt.h"

#include <cmath>
#include <cstring>
#include <new>

namespace {

constexpr std::uintptr_t kClassName = std::uintptr_t(0x0078DB14);   // "CSkyObject"
constexpr int kClassId = 0x20;
constexpr float kColourScale = 255.0f;   // 0073C370
constexpr float kLightScale = 150.0f;    // 0073D618
constexpr float kHorizon = 1.0f;         // 0073B84C

// A vertex's row `row` (x 0, y 1, z 2) in its packet.
float Row(const lib::AlignedArray<VertexPacket>& packets, int vertex, int row) {
    return packets.data[vertex / 4].block[row][vertex % 4];
}

// _ftol: truncation.
std::uint32_t Truncate(float v) { return static_cast<std::uint32_t>(static_cast<int>(v)); }

} // namespace

const char* CSkyObject::GetClassName() { return reinterpret_cast<const char*>(kClassName); }
int CSkyObject::GetClassId() { return kClassId; }

CSkyObject* CSkyObject::Construct() {
    ConstructBase();
    figureOwner_ = 0;
    new (&name_) mfc::CString();
    new (&directory_) mfc::CString();
    offsets_.Construct();
    diffuse_.Construct();
    specular_.Construct();
    std::memset(bounds_, 0, sizeof(bounds_));
    ei::SetVtable(this, kFigureVtableAddress);
    Create();
    vertices_.Construct();
    normals_.Construct();
    corners_.Construct();
    words_.Construct();
    indices_.Construct();
    texCoords_.Construct();
    distances_.Construct();
    ei::SetVtable(this, kVtableAddress);
    Create();
    return this;
}
EI_REPLACE_METHOD(0x004D9110, CSkyObject::Construct);

void CSkyObject::Destruct() {
    distances_.FreeData();
    texCoords_.FreeData();
    indices_.FreeData();
    words_.FreeData();
    corners_.FreeData();
    normals_.FreeData();
    vertices_.FreeData();
    FigureDestructBodyOriginal(this);
}
EI_REPLACE_METHOD(0x004D92B0, CSkyObject::Destruct);

CSkyObject* CSkyObject::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x004D9290, CSkyObject::DeletingDestruct);

void CSkyObject::Light(const float* direction, SkyColour ambient, SkyColour light, SkyColour) {
    const std::uint32_t r = Truncate(ambient.r * kColourScale);
    const std::uint32_t g = Truncate(ambient.g * kColourScale);
    const std::uint32_t b = Truncate(ambient.b * kColourScale);
    const std::uint32_t lit = ((r | 0xFFFFFF00u) << 8 | g) << 8 | b;
    const int count = diffuse_.size;
    if (distances_.size != count) {
        distances_.SetSize(count);
        for (int i = 0; i < distances_.size; ++i) {
            const float z = Row(vertices_, i, 2);
            distances_.data[i] = !(z >= kHorizon) ? 0.0f : std::sqrt(-Row(normals_, i, 2));
        }
    }
    for (int i = 0; i < diffuse_.size; ++i) {
        const float x = Row(normals_, i, 0);
        const float y = Row(normals_, i, 1);
        const float z = Row(normals_, i, 2);
        const float distance = distances_.data[i];
        if (distance == 0.0f || x87::IsNaN(distance)) {
            specular_.data[i] = 0;
            diffuse_.data[i] = (r << 8 | g) << 8 | b;
            continue;
        }
        float cosine = DotUnrounded<Sum3::ZYX>(x, y, z, direction[0], direction[1], direction[2]);
        if (!(cosine >= 0.0f)) cosine = 0.0f;
        const float strength = cosine * cosine;
        std::uint32_t specular = Truncate(distance * kColourScale);
        specular = specular << 8 | Truncate(light.r * strength * kLightScale);
        specular = specular << 8 | Truncate(light.g * strength * kLightScale);
        specular = specular << 8 | Truncate(light.b * strength * kLightScale);
        specular_.data[i] = specular;
        diffuse_.data[i] = lit;
    }
}

void CSky::Light(const float* direction, SkyColour ambient, SkyColour light, SkyColour unused) {
    if (object) object->Light(direction, ambient, light, unused);
}

namespace {
constexpr int kSkyTextureKind = 0x3D;
const char* const kSkyTextureName = reinterpret_cast<const char*>(std::uintptr_t(0x0078DB20));   // "sky00"
constexpr float kSkyDrop = 16.0f;                // 0073CA98: the dome sits this far below the eye
constexpr float kSkyTurnRate = 1.0f / 3000.0f;   // 0073D61C: radians per world tick
// Two 16-byte statics the draw passes to renderer slot 14 (not read there).
const void* const kSkyDrawA = reinterpret_cast<const void*>(std::uintptr_t(0x007AA890));
const void* const kSkyDrawB = reinterpret_cast<const void*>(std::uintptr_t(0x007AA8A0));

// 00578B40 (the client glue, not read): the world time in ticks, whole ticks plus the fraction of
// the current one (ST0).
__declspec(naked) double __fastcall ClientWorldTime(void* /*glue*/) EI_ORIGINAL(0x00578B40)
} // namespace

CSky* CSky::Construct() {
    object = nullptr;
    material = nullptr;
    texture = nullptr;
    turns = 1;
    LoadTextureOriginal(&texture, nullptr, kSkyTextureKind, kSkyTextureName);
    return this;
}
EI_REPLACE_METHOD(0x004D9680, CSky::Construct);

void CSky::Draw(CSceneRenderer* renderer, const Vector3* eye) {
    if (!object) return;
    const float x = eye->x, y = eye->y, z = eye->z - kSkyDrop;
    if (turns) {
        const Vector3 up{0.0f, 0.0f, 1.0f};
        Quaternion turn;
        object->localRotation_ = *turn.SetAxisAngle(up, static_cast<float>(ClientWorldTime(nullptr) * kSkyTurnRate));
    }
    object->localPosition_ = Vector3{x, y, z};
    float* m = object->matrix_.m;
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
    TextureEnsureLoadedOriginal(&texture);
    renderer->SetTexture(0, texture);
    renderer->DrawFigure(object, kSkyDrawA, kSkyDrawB, 0, 0xFF, nullptr);
    const int stacks[2] = {CTransformPipeline::kClipStack, 1};
    for (int stack : stacks) {
        int& count = pipeline->stacks_[stack].count;
        if (count != 0) --count;
    }
}
EI_REPLACE_METHOD(0x004D96E0, CSky::Draw);

namespace {
const char* __fastcall ClassNameHook(CSkyObject* self) { return self->CSkyObject::GetClassName(); }
int __fastcall ClassIdHook(CSkyObject* self) { return self->CSkyObject::GetClassId(); }
} // namespace
EI_REPLACE(0x004D9270, ClassNameHook);
EI_REPLACE(0x004D9280, ClassIdHook);
EI_REPLACE_METHOD(0x004D9360, CSkyObject::Light);
EI_REPLACE_METHOD(0x004D9DE0, CSky::Light);

// The engine's aligned array constructors (identical copies), on the shared template.
EI_REPLACE_METHOD(0x004D9E80, lib::AlignedArray<std::uint32_t>::Construct);
EI_REPLACE_METHOD(0x004D9E90, lib::AlignedArray<std::uint32_t>::Construct);
EI_REPLACE_METHOD(0x004D9EA0, lib::AlignedArray<std::uint32_t>::Construct);
