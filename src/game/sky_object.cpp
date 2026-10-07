// CSkyObject and CSky's lighting (A-S7, 004D9110-004D9DE0). See sky_object.h.
#include "sky_object.h"

#include "originals_core.h"

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
    unknown_98 = 0;
    new (&name_) mfc::CString();
    new (&unknown_124) mfc::CString();
    unknown_128.Construct();
    diffuse_.Construct();
    specular_.Construct();
    std::memset(unknown_188, 0, sizeof(unknown_188));
    ei::SetVtable(this, kFigureVtableAddress);
    Create();
    vertices_.Construct();
    normals_.Construct();
    unknown_1E0.Construct();
    unknown_1F0.Construct();
    unknown_200.Construct();
    unknown_210.Construct();
    distances_.Construct();
    ei::SetVtable(this, kVtableAddress);
    Create();
    return this;
}
EI_REPLACE_METHOD(0x004D9110, CSkyObject::Construct);

void CSkyObject::Destruct() {
    distances_.FreeData();
    unknown_210.FreeData();
    unknown_200.FreeData();
    unknown_1F0.FreeData();
    unknown_1E0.FreeData();
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
        float cosine = z * direction[2] + y * direction[1] + x * direction[0];
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
