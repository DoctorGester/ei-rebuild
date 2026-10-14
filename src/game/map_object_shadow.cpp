// The shadow of a 3D object on the client. See map_object_shadow.h and docs/notes/map_objects.md
// ("Shadows").
#include "map_object_shadow.h"

#include "link_figure.h"        // CLinkMesh
#include "map_object.h"         // ObjectSlots
#include "ground_decal.h"        // DecalTexCoord
#include "map_object_figures.h"
#include "spell.h"              // CLight
#include "unit.h"               // CUnit
#include "unit_client_models.h" // CUnitClientSpecific
#include "maths.h"
#include "object.h"
#include "texture.h"
#include "world.h"              // CWorld::TextureManager

#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "render/render.h"
#include "originals_effects.h"
#include "originals_world.h"

#include <bit>
#include <cstring>
#include <vector>
#include "originals_render.h"

// The original keeps intermediate values on the FPU stack (double locals here) and rounds where
// it stores to a float (float locals); the order of every operation is the original's.
#pragma float_control(precise, on)

namespace {
// --- Globals ----------------------------------------------------------------------------------

int ShadowMode() { return *reinterpret_cast<const int*>(0x0079BD0C); }   // 0 off, 2 blurred
int ShadowSwitch(int classId) {
    if (classId == 0x50) return *reinterpret_cast<const int*>(0x0079BD14);   // units
    if (classId == 0x52) return *reinterpret_cast<const int*>(0x0079BD1C);
    return *reinterpret_cast<const int*>(0x0079BD18);
}
const Vector3* DefaultLight() { return reinterpret_cast<const Vector3*>(0x007AF230); }
void* TextureManager() { return *reinterpret_cast<void**>(0x007AF744); }
void* RenderDevice() { return *reinterpret_cast<void**>(0x007AF080); }
void* ShadowProjector() { return reinterpret_cast<void*>(0x007AF5B8); }
void* TextureStages() { return reinterpret_cast<void*>(0x007AF060); }
// The work buffers: projected points (two floats each), their pixels (two ints each), and the
// ground quad (one 4x4 block of floats).
AlignedRecordBuffer& Points() { return *reinterpret_cast<AlignedRecordBuffer*>(0x007AF1F8); }
AlignedRecordBuffer& Pixels() { return *reinterpret_cast<AlignedRecordBuffer*>(0x007AF1E0); }
AlignedRecordBuffer& Quads() { return *reinterpret_cast<AlignedRecordBuffer*>(0x007AF208); }

constexpr int kTypePointLight = 0x31;
constexpr int kClassUnitObject = 0x50;
constexpr std::uint32_t kFlagRefresh = 8;           // this->flags_: render the texture again
constexpr std::uint32_t kObjectHidden = 0x48;       // object->flags_
constexpr std::uint32_t kLightOff = 0x400;          // light->flags_
constexpr std::uint32_t kLightRemoved = 0x80;
constexpr std::uint32_t kLightStrong = 0x800;       // a point light counting ten times
constexpr std::uint32_t kPartHidden = 0x80;         // part->flags_
constexpr std::uint32_t kMaterialKeyed = 0x2000;
constexpr int kPointBufferSize = 0x1000;
constexpr int kTextureSize = 0x40;
constexpr std::uint32_t kFloatMax = 0x7F7FFFFF;
constexpr std::uint32_t kFloatLowest = 0xFF7FFFFF;
constexpr std::uint32_t kDefaultColour = 0x80FFFFFF;

constexpr float kRed = std::bit_cast<float>(0x3E99999Au);     // 0073C54C 0.3
constexpr float kGreen = std::bit_cast<float>(0x3F19999Au);   // 0073CDE0 0.6
constexpr float kBlue = std::bit_cast<float>(0x3DCCCCCDu);    // 0073B850 0.1
constexpr float kOne = 1.0f;                                  // 0073B84C
constexpr double kOneDouble = 1.0;                            // 0073BAC8
constexpr float kStrongFactor = 10.0f;                        // 0073BAAC
constexpr float kTiltSlope = std::bit_cast<float>(0xBEBA5A47u);  // 0073DF80 -tan(20 degrees)
constexpr float kFarSquared = 1e10f;                          // 0073DF7C
constexpr float kTextureScale = 63.0f;                        // 0073D044
constexpr float kAlphaScale = 128.0f;                         // 0073DF78
constexpr std::uint32_t kDrawDepth = 0x3F7FF972;

// pi, 1/180 and 20 (0073DE60, 0073BA30, 0073DF88): volatile, so the product is formed at run
// time with the FPU's precision like the original's.
volatile double g_pi = 3.141592653589793;
volatile double g_perDegree = std::bit_cast<double>(0x3F76C16C16C16C17ull);
volatile double g_minimumTilt = 20.0;


// --- Not rewritten, forwarders ----------------------------------------------------------------
// 00692010. Texture manager: a new 64x64 texture.
__declspec(naked) CEngineObject* __fastcall CreateShadowTexture(void*) EI_ORIGINAL(0x00692010)
// 004F1440 / 004F15C0. Scene: the view matrix and the rotation that look along a direction.
__declspec(naked) void __fastcall LightViewMatrix(void*, void*, float*, const float*) EI_ORIGINAL(0x004F1440)
__declspec(naked) void __fastcall LightRotation(void*, void*, const float*, Quaternion*) EI_ORIGINAL(0x004F15C0)
// 004545A0 / 004F65B0 / 00452E70. Aligned storage for 8-, 8- and 64-byte records.
__declspec(naked) void* __fastcall AllocatePoints(int) EI_ORIGINAL(0x004545A0)
void* __fastcall AllocatePixels(int a0) { return AllocAligned32Of8Original(a0); }
__declspec(naked) void* __fastcall AllocateQuads(int) EI_ORIGINAL(0x00452E70)
// 005A0E80. Figure mesh: rasterises its projected points into the texture.
__declspec(naked) int __fastcall RasterisePart(void*, void*, void*, void*, void*) EI_ORIGINAL(0x005A0E80)
// 00692140 / 00690FC0 / 00691030. The render surface, locked and unlocked.
__declspec(naked) void* __fastcall RenderSurface(void*) EI_ORIGINAL(0x00692140)
__declspec(naked) int __fastcall LockSurface(void*, void*, void*) EI_ORIGINAL(0x00690FC0)
__declspec(naked) void __fastcall UnlockSurface(void*) EI_ORIGINAL(0x00691030)
// 0056C750 (ClientProjectDecalOriginal, originals_world.h). The shadow projector: lays the texture's quad on
// the ground (the depth passed as its bits).
void __fastcall ProjectShadow(void* a0, void*, const float* a2, const float* a3, std::uint32_t a4, void* a5, void* a6,
                              void* a7, void* a8, int a9, int a10) {
    float depth;
    std::memcpy(&depth, &a4, 4);
    ClientProjectDecalOriginal(a0, nullptr, reinterpret_cast<const Vector3*>(a2), reinterpret_cast<const Vector3*>(a3),
                               depth, static_cast<VertexPacket* const*>(a5),
                               static_cast<lib::AlignedArray<VertexPacket>*>(a6),
                               static_cast<lib::AlignedArray<DecalTexCoord>*>(a7),
                               static_cast<lib::AlignedArray<std::uint16_t>*>(a8), a9, a10);
}
// 00501AF0. Texture stages: binds a texture.
void __fastcall SetTexture(void* a0, void*, int a2, CEngineObject* a3) { DeviceSetTextureOriginal(a0, nullptr, a2, (void*)a3); }
// 006CB020. Light manager: is the point lit, AL.
// 004EA4A0 (DeviceDrawFlatOriginal, originals_render.h). Device: draws the shadow geometry.
// Rewritten elsewhere, through their game addresses: 004018A0 Quaternion::PreMultiply,
// 005A0CD0 CFigureMesh::DrawOffset.
__declspec(naked) void __fastcall PreMultiplyAt(Quaternion*, void*, const Quaternion*) EI_ORIGINAL(0x004018A0)
__declspec(naked) int __fastcall ProjectPart(void*, void*, void*, void*, void*, void*) EI_ORIGINAL(0x005A0CD0)

// 004EBE60: *slot = value with counted references; the old value is released (its slot 4).
void AssignTexture(CEngineObject** slot, CEngineObject* value) {
    CEngineObject* old = *slot;
    *slot = value;
    if (value) ++value->refCount_;
    if (old) old->Release();
}

// The pipeline's stack the shadow is rendered with (+0x64).
constexpr int kShadowStack = 10;

const float* TopMatrix(const CTransformPipeline* pipeline) { return pipeline->Top(kShadowStack)->m; }

// The texture's +0x60 (CTexture::block_) holds the colour key of a keyed texture (flags_ 0x2000).
std::uint32_t ColourKey(const CTexture* texture) { return std::bit_cast<std::uint32_t>(texture->block_); }

// --- Work buffers -----------------------------------------------------------------------------

using Allocator = void*(__fastcall*)(int);

int GrowBy(const AlignedRecordBuffer& b) {
    if (b.growBy != 0) return b.growBy;
    int grow = b.size / 8;
    if (grow < 4) return 4;
    if (grow > 0x400) return 0x400;
    return grow;
}

// Size `size`; growing allocates fresh storage without keeping the contents.
void ResizeDiscarding(AlignedRecordBuffer& b, int size, Allocator allocate) {
    if (!b.data) {
        b.data = allocate(size);
        b.capacity = size;
        b.size = size;
        return;
    }
    if (size <= b.capacity) {
        b.size = size;
        return;
    }
    int capacity = GrowBy(b) + b.capacity;
    if (capacity <= size) capacity = size;
    void* fresh = allocate(capacity);
    gamecrt::operator_delete(static_cast<void**>(b.data)[-1]);
    b.data = fresh;
    b.size = size;
    b.capacity = capacity;
}

// Size 1 for the 64-byte quads, keeping the contents when growing.
void ResizeQuads(AlignedRecordBuffer& b) {
    if (!b.data) {
        b.data = AllocateQuads(1);
        b.capacity = 1;
        b.size = 1;
        return;
    }
    if (b.capacity >= 1) {
        b.size = 1;
        return;
    }
    int capacity = GrowBy(b) + b.capacity;
    if (capacity <= 1) capacity = 1;
    void* fresh = AllocateQuads(capacity);
    std::memcpy(fresh, b.data, static_cast<std::size_t>(b.size) * 64);
    gamecrt::operator_delete(static_cast<void**>(b.data)[-1]);
    b.data = fresh;
    b.size = 1;
    b.capacity = capacity;
}

float* PointAt(int index) { return static_cast<float*>(Points().data) + 2 * index; }
int* PixelAt(int index) { return static_cast<int*>(Pixels().data) + 2 * index; }

// --- Lights -----------------------------------------------------------------------------------

// The light's brightness: 0.1 blue + 0.6 green + 0.3 red, unrounded.
double Brightness(const CLight* light) {
    double blue = static_cast<double>(light->color_[2]) * kBlue;
    double green = static_cast<double>(light->color_[1]) * kGreen;
    double red = static_cast<double>(light->color_[0]) * kRed;
    return (blue + green) + red;
}

// A point light's share: from the light towards the object, scaled by its brightness and its
// falloff over the squared distance. False when the object is out of its range.
bool PointLightShare(const CObject3D* object, CLight* light, Vector3* out) {
    const Vector3& at = object->position;
    const Vector3& from = light->position_;
    double dx = static_cast<double>(at.x) - from.x;
    double dy = static_cast<double>(at.y) - from.y;
    double dz = static_cast<double>(at.z) - from.z;
    float distanceSquared = static_cast<float>(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz));
    if (distanceSquared >= light->rangeSquared_) return false;   // NaN counts as in range
    double falloff = kOne - static_cast<double>(light->inverseRangeSquared_) * distanceSquared;
    float intensity = static_cast<float>(Brightness(light) * falloff);
    Vector3 towards = {static_cast<float>(static_cast<double>(at.x) - from.x),
                       static_cast<float>(static_cast<double>(at.y) - from.y),
                       static_cast<float>(static_cast<double>(at.z) - from.z)};
    if (!(distanceSquared < 0.0f || distanceSquared > 0.0f)) {   // zero (or NaN)
        std::memcpy(&towards, DefaultLight(), sizeof(towards));
    } else {
        double inverse = kOneDouble / x87::fsqrt(distanceSquared);
        towards.x = static_cast<float>(towards.x * inverse);
        towards.y = static_cast<float>(towards.y * inverse);
        towards.z = static_cast<float>(towards.z * inverse);
    }
    double scale = intensity;
    if (light->flags_ & kLightStrong) scale = scale * kStrongFactor;
    out->z = static_cast<float>(towards.z * scale);
    out->y = static_cast<float>(towards.y * scale);
    out->x = static_cast<float>(towards.x * scale);
    return true;
}

// A directional light's share: its direction times its brightness.
void DirectionalShare(CLight* light, Vector3* out) {
    double brightness = Brightness(light);
    out->z = static_cast<float>(brightness * light->direction_.z);
    out->y = static_cast<float>(brightness * light->direction_.y);
    out->x = static_cast<float>(brightness * light->direction_.x);
}

// The normalised direction of `sum`, tilted down to at least 20 degrees below the horizontal.
void LightDirection(const Vector3& sum, Vector3* direction) {
    double lengthSquared = LengthSquaredUnrounded<Sum3::ZXY, double>(sum);   // (x + z) + y
    std::memcpy(direction, &sum, 12);
    if (lengthSquared < 0.0 || lengthSquared > 0.0) {
        double inverse = kOneDouble / x87::fsqrt(lengthSquared);
        direction->x = static_cast<float>(sum.x * inverse);
        direction->y = static_cast<float>(inverse * direction->y);
        direction->z = static_cast<float>(inverse * direction->z);
    }
    float horizontal = static_cast<float>(x87::fsqrt(LengthSquared2DUnrounded<double>(direction->y, direction->x)));
    float height = std::bit_cast<float>(std::bit_cast<std::uint32_t>(direction->z) & 0x7FFFFFFFu);
    double elevation = x87::fpatan(height, horizontal);   // FLD y; FLD x; FPATAN
    double minimum = g_pi * g_perDegree * g_minimumTilt;
    if (elevation >= minimum) return;   // NaN tilts
    double z = static_cast<double>(horizontal) * kTiltSlope;
    double inverse = kOne / x87::fsqrt(LengthSquaredUnrounded<Sum3::ZYX, double>(direction->x, direction->y, z));
    direction->x = static_cast<float>(inverse * direction->x);
    direction->y = static_cast<float>(inverse * direction->y);
    direction->z = static_cast<float>(inverse * z);
}

// The squared distance of a part from the object; parts beyond 1e10 are skipped (NaN is not).
bool PartTooFar(const FigurePart* part) { return LengthSquaredUnrounded<Sum3::XYZ, double>(part->position) > kFarSquared; }

FigurePart* PartAt(const CObject3D* object, int i) { return static_cast<FigurePart*>(object->parts.data[i]); }

// The bits a part is rasterised into: unused_00 is the callee's, the rest the target surface.
struct RasterTarget {
    std::uint8_t unused_00[0x30];
    std::uint32_t colourKey;   // +0x30
    void* bits;                // +0x34
    int width;                 // +0x38 0x200
    int pitch;                 // +0x3C
};
static_assert(sizeof(RasterTarget) == 0x40);

// The surface description the lock fills (only the pitch and the bits are read).
struct SurfaceDescription {   // DDSURFACEDESC2 (the members not read take its names)
    std::uint32_t size;        // +0x00 dwSize
    std::uint32_t flags;       // +0x04 dwFlags
    std::uint32_t height;      // +0x08 dwHeight
    std::uint32_t width;       // +0x0C dwWidth
    int pitch;                 // +0x10
    std::uint32_t backBufferCount;   // +0x14 dwBackBufferCount
    std::uint32_t mipMapCount; // +0x18 dwMipMapCount
    std::uint32_t alphaBitDepth;     // +0x1C dwAlphaBitDepth
    std::uint32_t reserved;    // +0x20 dwReserved
    void* bits;                // +0x24
    std::uint32_t colourKeys[8];     // +0x28 ddckCKDestOverlay, ddckCKDestBlt, ddckCKSrcOverlay, ddckCKSrcBlt
    std::uint32_t formatSize;  // +0x48 ddpfPixelFormat.dwSize
    std::uint32_t formatFlags; // +0x4C ddpfPixelFormat.dwFlags
    std::uint32_t fourCC;      // +0x50 ddpfPixelFormat.dwFourCC
    int bitsPerPixel;          // +0x54 ddpfPixelFormat.dwRGBBitCount
    std::uint32_t redMask;     // +0x58
    std::uint32_t greenMask;   // +0x5C
    std::uint32_t blueMask;    // +0x60
    std::uint32_t alphaMask;   // +0x64
    std::uint32_t caps[4];     // +0x68 ddsCaps
    std::uint32_t textureStage;   // +0x78 dwTextureStage
};
static_assert(sizeof(SurfaceDescription) == 0x7C);

// One corner's x and y in the light's rotation `m` (3x4 rows of 4).
double CornerX(const float* m, float a, float b) {
    return static_cast<double>(m[1]) * a + static_cast<double>(b) * m[0];
}
float CornerY(const float* m, float a, float b) {
    return static_cast<float>(static_cast<double>(a) * m[5] + static_cast<double>(m[4]) * b);
}
} // namespace

// --- The shadow -------------------------------------------------------------------------------

namespace {
// Renders the shadow texture again and projects it onto the ground. Returns false (with the
// fatal error's result in *failure) when the render surface cannot be locked.
bool RenderShadow(CObject3DClientSpecific* self, CSceneRenderer* context, int* failure) {
    CTransformPipeline* pipeline = context->pipeline_;
    void* device = context->graphicsProcessor_;

    // The lights reaching the object.
    Vector3 sum;
    std::memcpy(&sum, DefaultLight(), sizeof(sum));
    ObjectListIterator lights{nullptr, nullptr};
    for (auto* light = static_cast<CLight*>(lights.First(TheLightFactory())); light;
         light = static_cast<CLight*>(lights.Next())) {
        if (light->flags_ & kLightOff || light->flags_ & kLightRemoved) continue;
        Vector3 share;
        if (light->type_ == kTypePointLight) {
            if (!PointLightShare(self->object, light, &share)) continue;
        } else {
            DirectionalShare(light, &share);
        }
        sum.x = static_cast<float>(static_cast<double>(share.x) + sum.x);
        sum.y = static_cast<float>(static_cast<double>(share.y) + sum.y);
        sum.z = static_cast<float>(static_cast<double>(share.z) + sum.z);
    }
    Vector3 direction;
    LightDirection(sum, &direction);

    // Look from the light; the object turned to face it.
    Matrix4 view;
    LightViewMatrix(pipeline, nullptr, view.m, &direction.x);
    pipeline->PushMatrixProduct(view, kShadowStack);
    ResizeDiscarding(Points(), kPointBufferSize, AllocatePoints);
    ResizeDiscarding(Pixels(), kPointBufferSize, AllocatePixels);
    int count = 0;
    Quaternion turn{1.0f, 0.0f, 0.0f, 0.0f};
    LightRotation(pipeline, nullptr, &direction.x, &turn);
    CObject3D* object = self->object;
    PreMultiplyAt(&object->orientation, nullptr, &turn);
    ObjectWorldMatrix(&object->orientation, &object->position, &object->worldMatrix);
    turn.x = -turn.x;
    turn.y = -turn.y;
    turn.z = -turn.z;
    PreMultiplyAt(&self->object->orientation, nullptr, &turn);
    view = self->object->worldMatrix;
    std::memcpy(&self->bounds[0], &kFloatMax, 4);
    std::memcpy(&self->bounds[2], &kFloatLowest, 4);
    std::memcpy(&self->bounds[1], &kFloatMax, 4);
    std::memcpy(&self->bounds[3], &kFloatLowest, 4);
    view.m[11] = 0.0f;
    view.m[7] = 0.0f;
    view.m[3] = 0.0f;
    pipeline->PushMatrixProduct(view, kShadowStack);

    // Project the parts' points.
    for (int i = 0; i < self->object->parts.size; ++i) {
        FigurePart* part = PartAt(self->object, i);
        if (PartTooFar(part)) continue;
        if (part->flags_ & kPartHidden) continue;
        ObjectWorldMatrix(&part->orientation, &part->position, &part->worldMatrix);
        view = PartAt(self->object, i)->worldMatrix;
        pipeline->PushMatrixProduct(view, kShadowStack);
        if (CEngineObject* figure = self->object->figure) {
            const char* name = PartAt(self->object, i)->GetName();
            stlp::string key(name);
            count += reinterpret_cast<CLinkMesh*>(figure)->ProjectPart(&key, &PartAt(self->object, i)->offsets,
                                                                       TopMatrix(pipeline), PointAt(count), self->bounds);
        } else {
            count += ProjectPart(PartAt(self->object, i), nullptr, pipeline, const_cast<float*>(TopMatrix(pipeline)),
                                 PointAt(count), self->bounds);
        }
        pipeline->PopMatrix(kShadowStack);
    }
    pipeline->PopMatrix(kShadowStack);
    pipeline->PopMatrix(kShadowStack);
    ResizeDiscarding(Points(), count, AllocatePoints);

    // Points to texture pixels.
    float scaleX = static_cast<float>(kTextureScale / (static_cast<double>(self->bounds[2]) - self->bounds[0]));
    float scaleY = static_cast<float>(kTextureScale / (static_cast<double>(self->bounds[3]) - self->bounds[1]));
    float offsetX = static_cast<float>(-(static_cast<double>(scaleX) * self->bounds[0]));
    float offsetY = static_cast<float>(-(static_cast<double>(scaleY) * self->bounds[1]));
    Matrix4 toPixels = {};
    float* t = toPixels.m;
    t[0] = t[5] = t[10] = t[15] = 1.0f;   // 0051E1C0
    t[0] = scaleX;
    t[12] = offsetX;
    t[13] = offsetY;
    t[5] = scaleY;
    for (int k = 0; k < Points().size; ++k) {
        float x = static_cast<float>(static_cast<double>(t[0]) * PointAt(k)[0] + t[12]);
        PixelAt(k)[0] = x87::fistp(x);
        float y = static_cast<float>(static_cast<double>(t[5]) * PointAt(k)[1] + t[13]);
        PixelAt(k)[1] = x87::fistp(y);
    }
    ResizeDiscarding(Pixels(), count, AllocatePixels);

    // Rasterise into the render surface.
    void* surface = RenderSurface(static_cast<CWorld*>(self->ManagerContext())->TextureManager());
    SurfaceDescription description;
    if (!LockSurface(surface, nullptr, &description)) {
        *failure = game::FatalError(reinterpret_cast<const char*>(0x0078F584), self->GetName());
        if (lights.manager) --reinterpret_cast<CEngineObject*>(lights.manager)->refCount_;
        return false;
    }
    RasterTarget target{};
    CTexture* skin = self->partSets[0] ? self->partSets[0]->texture : nullptr;
    target.colourKey = skin && (skin->flags_ & kMaterialKeyed) ? ColourKey(skin) : 0;
    target.width = 0x200;
    target.pitch = description.pitch;
    // The shadow is rasterised into a CPU buffer of the surface's layout (the new renderer takes it
    // from there) and copied into the surface afterwards.
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(description.pitch) * kTextureSize);
    target.bits = pixels.data();
    std::memset(target.bits, 0, kTextureSize * kTextureSize * 2);
    count = 0;
    int keyedParts = 0;
    for (int i = 0; i < self->object->parts.size; ++i) {
        FigurePart* part = PartAt(self->object, i);
        if (PartTooFar(part)) continue;
        keyedParts |= (part->boxFlags >> 10) & 1;
        if (part->flags_ & kPartHidden) continue;
        if (CEngineObject* figure = self->object->figure) {
            const char* name = PartAt(self->object, i)->GetName();
            stlp::string key(name);
            count += reinterpret_cast<CLinkMesh*>(figure)->RasterisePart(&key, PixelAt(count), &target);
        } else {
            count += RasterisePart(part, nullptr, device, PixelAt(count), &target);
        }
    }
    if (ShadowMode() == 2) {
        auto* converter = static_cast<CPixelConverter*>(device);
        converter->BlurShadow(target.bits, kTextureSize, kTextureSize);
        converter->BlurShadow2(target.bits, kTextureSize, kTextureSize);
    }
    std::memcpy(description.bits, pixels.data(), pixels.size());
    UnlockSurface(surface);

    // Copy the surface into the shadow texture (the device's Load; in the switch-over both are in the
    // new renderer's memory, texture_shadow.cpp).
    auto* shadowTexture = static_cast<CTexture*>(self->texture);
    shadowTexture->LoadPixelsFrom(static_cast<CTextureDevice*>(RenderDevice()), static_cast<const CTexture*>(surface));
    // The new renderer takes the shadow from the CPU buffer, in the surface's pixel format (a
    // texture in its memory is read from there instead).
    render::TexturePixels(shadowTexture->surface_, pixels.data(), kTextureSize, kTextureSize, description.pitch,
                          description.bitsPerPixel, description.redMask, description.greenMask, description.blueMask,
                          description.alphaMask);

    // The light's rotation as a matrix.
    double x2 = static_cast<double>(turn.x) + turn.x;
    float y2 = static_cast<float>(static_cast<double>(turn.y) + turn.y);
    float z2 = static_cast<float>(static_cast<double>(turn.z) + turn.z);
    Vector3 at;
    std::memcpy(&at, &self->object->position, sizeof(at));
    float wx = static_cast<float>(x2 * turn.w);
    float wy = static_cast<float>(static_cast<double>(y2) * turn.w);
    float wz = static_cast<float>(static_cast<double>(z2) * turn.w);
    double xx = x2 * turn.x;
    double xy = static_cast<double>(y2) * turn.x;
    float xz = static_cast<float>(static_cast<double>(z2) * turn.x);
    float yy = static_cast<float>(static_cast<double>(y2) * turn.y);
    float yz = static_cast<float>(static_cast<double>(z2) * turn.y);
    double zzExact = static_cast<double>(z2) * turn.z;
    float zz = static_cast<float>(zzExact);
    float* m = view.m;
    m[0] = static_cast<float>(kOne - (zzExact + yy));
    m[1] = static_cast<float>(xy - wz);
    m[2] = static_cast<float>(static_cast<double>(xz) + wy);
    m[4] = static_cast<float>(xy + wz);
    m[5] = static_cast<float>(kOne - (static_cast<double>(zz) + xx));
    m[6] = static_cast<float>(static_cast<double>(yz) - wx);
    m[8] = static_cast<float>(static_cast<double>(xz) - wy);
    m[9] = static_cast<float>(static_cast<double>(yz) + wx);
    m[10] = static_cast<float>(kOne - (static_cast<double>(yy) + xx));

    // The texture's square on the ground: corner 0 at (minimum x, maximum y), then clockwise.
    ResizeQuads(Quads());
    {
        float a = self->bounds[3];
        float b = self->bounds[0];
        float z = static_cast<float>(static_cast<double>(b) * m[8] + static_cast<double>(a) * m[9]);
        float y = CornerY(m, a, b);
        float x = static_cast<float>(CornerX(m, a, b));
        (void)z;   // computed for the corner's Vector3, never used
        float* quad = static_cast<float*>(Quads().data);
        quad[0] = static_cast<float>(static_cast<double>(at.x) + x);
        quad[4] = static_cast<float>(static_cast<double>(at.y) + y);
        quad[8] = at.z;
        quad[12] = 1.0f;
    }
    const float corners[3][2] = {{self->bounds[3], self->bounds[2]},
                                 {self->bounds[1], self->bounds[2]},
                                 {self->bounds[1], self->bounds[0]}};
    for (int c = 0; c < 3; ++c) {
        float a = corners[c][0];
        float b = corners[c][1];
        float y = CornerY(m, a, b);
        double x = CornerX(m, a, b);
        float* quad = static_cast<float*>(Quads().data);
        quad[1 + c] = static_cast<float>(x + at.x);
        quad[5 + c] = static_cast<float>(static_cast<double>(y) + at.y);
        quad[9 + c] = at.z;
        quad[13 + c] = 1.0f;
    }

    // The point the shadow falls from: the object's offset point through its matrix.
    // Each row of the world matrix times the bounds' centre (+0x128): (y + z) + x, unrounded.
    const float* w = self->object->worldMatrix.m;
    const Vector3& c = self->object->center;
    auto product = [](float a, float b) { return static_cast<double>(a) * b; };
    double pz = (product(w[9], c.y) + product(w[10], c.z)) + product(w[8], c.x);
    float py = static_cast<float>((product(w[5], c.y) + product(w[6], c.z)) + product(w[4], c.x));
    float px = static_cast<float>((product(w[1], c.y) + product(w[2], c.z)) + product(w[0], c.x));
    Vector3 point = {static_cast<float>(static_cast<double>(px) + at.x),
                     static_cast<float>(static_cast<double>(py) + at.y), static_cast<float>(pz + at.z)};
    std::uint32_t radiusBits;
    std::memcpy(&radiusBits, &self->object->radius, 4);
    ProjectShadow(ShadowProjector(), nullptr, &direction.x, &point.x, radiusBits, &Quads(),
                  &self->vertices, &self->texCoords, &self->indices, 0, keyedParts == 0);

    if (self->vertices.size != 0) {
        pipeline->transform_->FitBounds(&self->sphere, self->vertices.data, self->vertices.size);
        const float* position = &self->object->position.x;
        self->sphere.center.x = static_cast<float>(static_cast<double>(self->sphere.center.x) - position[0]);
        self->sphere.center.y = static_cast<float>(static_cast<double>(self->sphere.center.y) - position[1]);
        self->sphere.center.z = static_cast<float>(static_cast<double>(self->sphere.center.z) - position[2]);
    } else {
        self->sphere.flags = 0;
    }
    self->flags_ &= ~kFlagRefresh;
    if (lights.manager) --reinterpret_cast<CEngineObject*>(lights.manager)->refCount_;
    return true;
}
} // namespace

int CObject3DClientSpecific::DrawShadow(CSceneRenderer* context) {
    if (ShadowMode() <= 0) return 1;
    if (!ShadowSwitch(object->GetClassId())) return 1;
    if (object->flags_ & kObjectHidden) return 1;
    if (ObjectSlots(object)->InvisibleSlot()) return 1;   // invisible

    if (!texture) {
        AssignTexture(&texture, CreateShadowTexture(TextureManager()));
        if (!texture) return game::FatalError(reinterpret_cast<const char*>(0x0078F5B8));
        char name[0x100];
        gamecrt::sprintf(name, reinterpret_cast<const char*>(0x0078F5B0), object->id_);   // "SR%d"
        *reinterpret_cast<mfc::CString*>(&static_cast<CTexture*>(texture)->name_) = name;
        flags_ |= kFlagRefresh;
    }
    std::uint32_t objectFlags = object->flags_;
    if ((objectFlags & 0x600) == 0x200 || !(objectFlags & 0x200) || (flags_ & kFlagRefresh)) {
        int failure;
        if (!RenderShadow(this, context, &failure)) return failure;
    }

    if (vertices.size == 0) return 1;
    std::uint32_t colour = kDefaultColour;
    if (object->GetClassId() == kClassUnitObject) {
        auto* half = static_cast<CUnitClientSpecific*>(reinterpret_cast<CUnit*>(object)->controller);
        float alpha = static_cast<float>(static_cast<double>(half->fadeIn) * kAlphaScale);
        colour = (static_cast<std::uint32_t>(x87::fistp(alpha)) << 24) | 0xFFFFFF;
    }
    SetTexture(TextureStages(), nullptr, 0, texture);
    std::uint8_t lit = IsLit(LightManager(), nullptr, &object->position);
    DeviceDrawFlatOriginal(RenderDevice(), nullptr, vertices.data, texCoords.data, vertices.size, indices.data, indices.size,
                           colour,
                           static_cast<std::uint32_t>(static_cast<std::uint8_t>(lit)) << 24, nullptr,
                           std::bit_cast<float>(kDrawDepth));
    return 1;
}
EI_REPLACE_METHOD(0x0051CE60, CObject3DClientSpecific::DrawShadow);
