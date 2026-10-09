// The shadow of a 3D object on the client. See map_object_shadow.h and docs/notes/map_objects.md
// ("Shadows").
#include "map_object_shadow.h"

#include "map_object_figures.h"
#include "maths.h"
#include "object.h"
#include "texture.h"

#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "render/render.h"
#include "originals_effects.h"

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
const float* DefaultLight() { return reinterpret_cast<const float*>(0x007AF230); }   // three floats
CBaseManager* LightList() { return *reinterpret_cast<CBaseManager**>(0x007AF748); }
void* TextureManager() { return *reinterpret_cast<void**>(0x007AF744); }
void* RenderDevice() { return *reinterpret_cast<void**>(0x007AF080); }
void* LightManager() { return *reinterpret_cast<void**>(0x007AF738); }
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

template <typename T>
T& At(void* base, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

// FLD y; FLD x; FPATAN: atan(y / x) in ST0.
__declspec(naked) double __cdecl Fpatan(double /*y*/, double /*x*/) {
    __asm {
        fld qword ptr [esp + 4]
        fld qword ptr [esp + 12]
        fpatan
        ret
    }
}

// --- Not rewritten, forwarders ----------------------------------------------------------------
// 00692010. Texture manager: a new 64x64 texture.
__declspec(naked) CEngineObject* __fastcall CreateShadowTexture(void*) EI_ORIGINAL(0x00692010)
// 004F1440 / 004F15C0. Scene: the view matrix and the rotation that look along a direction.
__declspec(naked) void __fastcall LightViewMatrix(void*, void*, float*, const float*) EI_ORIGINAL(0x004F1440)
__declspec(naked) void __fastcall LightRotation(void*, void*, const float*, Quaternion*) EI_ORIGINAL(0x004F15C0)
// 004D9880. 4x4 matrix: this = a * b.
inline void __fastcall MultiplyMatrix(float* out, void*, const float* a, const float* b) { MatrixMultiplyOriginal(out, nullptr, a, b); }
// 004D9AA0. A quaternion into a 4x4 matrix.
void __fastcall OrientationToMatrix(void* a0, void*, float* a2) { QuaternionToMatrixOriginal((const void*)a0, nullptr, a2); }
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
// 0056C750. The shadow projector: lays the texture's quad on the ground.
__declspec(naked) void __fastcall ProjectShadow(void*, void*, const float*, const float*, std::uint32_t, void*, void*,
                                                void*, void*, int, int) EI_ORIGINAL(0x0056C750)
// 00501AF0. Texture stages: binds a texture.
void __fastcall SetTexture(void* a0, void*, int a2, CEngineObject* a3) { DeviceSetTextureOriginal(a0, nullptr, a2, (void*)a3); }
// 006CB020. Light manager: is the point lit, AL.
// 004EA4A0. Device: draws the shadow geometry.
__declspec(naked) void __fastcall DrawGeometry(void*, void*, void*, void*, int, void*, int, std::uint32_t,
                                               std::uint32_t, int, std::uint32_t) EI_ORIGINAL(0x004EA4A0)
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

// --- The scene's matrix stack -----------------------------------------------------------------

SceneMatrixStack& Stack(void* scene) { return At<SceneMatrixStack>(scene, 0x64); }

void PushMatrix(void* scene, const float* m) {
    SceneMatrixStack& stack = Stack(scene);
    if (stack.depth == 0) {
        stack.depth = 1;
        std::memcpy(stack.matrices[0], m, 64);
        return;
    }
    int depth = ++stack.depth;
    MultiplyMatrix(stack.matrices[depth - 1], nullptr, stack.matrices[depth - 2], m);
}

void PopMatrix(void* scene) {
    SceneMatrixStack& stack = Stack(scene);
    if (stack.depth != 0) --stack.depth;
}

float* TopMatrix(void* scene) {
    SceneMatrixStack& stack = Stack(scene);
    return stack.matrices[stack.depth - 1];
}

// A 4x4 matrix at the object's or part's +0x54 from its orientation (+0x34) and position.
void BuildMatrix(CEngineObject* object) {
    auto* raw = reinterpret_cast<char*>(object);
    OrientationToMatrix(raw + 0x34, nullptr, reinterpret_cast<float*>(raw + 0x54));
    std::memcpy(raw + 0x60, raw + 0x1C, 4);
    std::memcpy(raw + 0x70, raw + 0x20, 4);
    std::memcpy(raw + 0x80, raw + 0x24, 4);
    At<std::uint32_t>(raw, 0x8C) = 0;
    At<std::uint32_t>(raw, 0x88) = 0;
    At<std::uint32_t>(raw, 0x84) = 0;
    At<float>(raw, 0x90) = 1.0f;
}

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
double Brightness(const CEngineObject* light) {
    double blue = static_cast<double>(At<float>(const_cast<CEngineObject*>(light), 0xA0)) * kBlue;
    double green = static_cast<double>(At<float>(const_cast<CEngineObject*>(light), 0x9C)) * kGreen;
    double red = static_cast<double>(At<float>(const_cast<CEngineObject*>(light), 0x98)) * kRed;
    return (blue + green) + red;
}

// A point light's share: from the light towards the object, scaled by its brightness and its
// falloff over the squared distance. False when the object is out of its range.
bool PointLightShare(const CObject3D* object, CEngineObject* light, float* out) {
    const float* at = &object->position.x;
    const float* from = &At<float>(light, 0x1C);
    double dx = static_cast<double>(at[0]) - from[0];
    double dy = static_cast<double>(at[1]) - from[1];
    double dz = static_cast<double>(at[2]) - from[2];
    float distanceSquared = static_cast<float>((dz * dz + dy * dy) + dx * dx);
    if (distanceSquared >= At<float>(light, 0x100)) return false;   // NaN counts as in range
    double falloff = kOne - static_cast<double>(At<float>(light, 0x104)) * distanceSquared;
    float intensity = static_cast<float>(Brightness(light) * falloff);
    float towards[3] = {static_cast<float>(static_cast<double>(at[0]) - from[0]),
                        static_cast<float>(static_cast<double>(at[1]) - from[1]),
                        static_cast<float>(static_cast<double>(at[2]) - from[2])};
    if (!(distanceSquared < 0.0f || distanceSquared > 0.0f)) {   // zero (or NaN)
        std::memcpy(towards, DefaultLight(), sizeof(towards));
    } else {
        double inverse = kOneDouble / x87::fsqrt(distanceSquared);
        towards[0] = static_cast<float>(towards[0] * inverse);
        towards[1] = static_cast<float>(towards[1] * inverse);
        towards[2] = static_cast<float>(towards[2] * inverse);
    }
    double scale = intensity;
    if (light->flags_ & kLightStrong) scale = scale * kStrongFactor;
    out[2] = static_cast<float>(towards[2] * scale);
    out[1] = static_cast<float>(towards[1] * scale);
    out[0] = static_cast<float>(towards[0] * scale);
    return true;
}

// A directional light's share: its direction (+0xD4) times its brightness.
void DirectionalShare(CEngineObject* light, float* out) {
    double brightness = Brightness(light);
    out[2] = static_cast<float>(brightness * At<float>(light, 0xDC));
    out[1] = static_cast<float>(brightness * At<float>(light, 0xD8));
    out[0] = static_cast<float>(brightness * At<float>(light, 0xD4));
}

// The normalised direction of `sum`, tilted down to at least 20 degrees below the horizontal.
void LightDirection(const float* sum, float* direction) {
    double lengthSquared = (static_cast<double>(sum[0]) * sum[0] + static_cast<double>(sum[2]) * sum[2]) +
                           static_cast<double>(sum[1]) * sum[1];
    std::memcpy(direction, sum, 12);
    if (lengthSquared < 0.0 || lengthSquared > 0.0) {
        double inverse = kOneDouble / x87::fsqrt(lengthSquared);
        direction[0] = static_cast<float>(sum[0] * inverse);
        direction[1] = static_cast<float>(inverse * direction[1]);
        direction[2] = static_cast<float>(inverse * direction[2]);
    }
    float horizontal = static_cast<float>(
        x87::fsqrt(static_cast<double>(direction[1]) * direction[1] + static_cast<double>(direction[0]) * direction[0]));
    float height = std::bit_cast<float>(std::bit_cast<std::uint32_t>(direction[2]) & 0x7FFFFFFFu);
    double elevation = Fpatan(height, horizontal);
    double minimum = g_pi * g_perDegree * g_minimumTilt;
    if (elevation >= minimum) return;   // NaN tilts
    double z = static_cast<double>(horizontal) * kTiltSlope;
    double inverse = kOne / x87::fsqrt((z * z + static_cast<double>(direction[1]) * direction[1]) +
                                       static_cast<double>(direction[0]) * direction[0]);
    direction[0] = static_cast<float>(inverse * direction[0]);
    direction[1] = static_cast<float>(inverse * direction[1]);
    direction[2] = static_cast<float>(inverse * z);
}

// The squared distance of a part from the object; parts beyond 1e10 are skipped (NaN is not).
bool PartTooFar(const FigurePart* part) {
    double x = part->position.x;
    double y = part->position.y;
    double z = part->position.z;
    return (x * x + y * y) + z * z > kFarSquared;
}

FigurePart* PartAt(const CObject3D* object, int i) { return static_cast<FigurePart*>(object->parts.data[i]); }

// The bits a part is rasterised into: unknown_00 is the callee's, the rest the target surface.
struct RasterTarget {
    std::uint8_t unknown_00[0x30];
    std::uint32_t colourKey;   // +0x30
    void* bits;                // +0x34
    int width;                 // +0x38 0x200
    int pitch;                 // +0x3C
};
static_assert(sizeof(RasterTarget) == 0x40);

// The surface description the lock fills (only the pitch and the bits are read).
struct SurfaceDescription {   // DDSURFACEDESC2
    std::uint8_t unknown_00[0x10];
    int pitch;                 // +0x10
    std::uint8_t unknown_14[0x24 - 0x14];
    void* bits;                // +0x24
    std::uint8_t unknown_28[0x54 - 0x28];
    int bitsPerPixel;          // +0x54 ddpfPixelFormat.dwRGBBitCount
    std::uint32_t redMask;     // +0x58
    std::uint32_t greenMask;   // +0x5C
    std::uint32_t blueMask;    // +0x60
    std::uint32_t alphaMask;   // +0x64
    std::uint8_t unknown_68[0x7C - 0x68];
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
bool RenderShadow(CObject3DClientSpecific* self, ShadowDrawContext* context, int* failure) {
    void* scene = context->scene;
    void* device = context->device;
    void* sceneObject = At<void*>(scene, 0xC);

    // The lights reaching the object.
    float sum[3];
    std::memcpy(sum, DefaultLight(), sizeof(sum));
    ObjectListIterator lights{nullptr, nullptr};
    for (CEngineObject* light = lights.First(LightList()); light; light = lights.Next()) {
        if (light->flags_ & kLightOff || light->flags_ & kLightRemoved) continue;
        float share[3];
        if (light->type_ == kTypePointLight) {
            if (!PointLightShare(self->object, light, share)) continue;
        } else {
            DirectionalShare(light, share);
        }
        sum[0] = static_cast<float>(static_cast<double>(share[0]) + sum[0]);
        sum[1] = static_cast<float>(static_cast<double>(share[1]) + sum[1]);
        sum[2] = static_cast<float>(static_cast<double>(share[2]) + sum[2]);
    }
    float direction[3];
    LightDirection(sum, direction);

    // Look from the light; the object turned to face it.
    float view[16];
    LightViewMatrix(scene, nullptr, view, direction);
    PushMatrix(scene, view);
    ResizeDiscarding(Points(), kPointBufferSize, AllocatePoints);
    ResizeDiscarding(Pixels(), kPointBufferSize, AllocatePixels);
    int count = 0;
    Quaternion turn{1.0f, 0.0f, 0.0f, 0.0f};
    LightRotation(scene, nullptr, direction, &turn);
    CObject3D* object = self->object;
    auto* orientation = &At<Quaternion>(object, 0x34);
    PreMultiplyAt(orientation, nullptr, &turn);
    BuildMatrix(object);
    turn.x = -turn.x;
    turn.y = -turn.y;
    turn.z = -turn.z;
    PreMultiplyAt(&At<Quaternion>(self->object, 0x34), nullptr, &turn);
    std::memcpy(view, &At<float>(self->object, 0x54), sizeof(view));
    std::memcpy(&self->bounds[0], &kFloatMax, 4);
    std::memcpy(&self->bounds[2], &kFloatLowest, 4);
    std::memcpy(&self->bounds[1], &kFloatMax, 4);
    std::memcpy(&self->bounds[3], &kFloatLowest, 4);
    view[11] = 0.0f;
    view[7] = 0.0f;
    view[3] = 0.0f;
    PushMatrix(scene, view);

    // Project the parts' points.
    for (int i = 0; i < self->object->parts.size; ++i) {
        FigurePart* part = PartAt(self->object, i);
        if (PartTooFar(part)) continue;
        if (part->flags_ & kPartHidden) continue;
        BuildMatrix(part);
        std::memcpy(view, &At<float>(PartAt(self->object, i), 0x54), sizeof(view));
        PushMatrix(scene, view);
        if (CEngineObject* figure = self->object->figure) {
            const char* name = ei::VCall<const char*>(PartAt(self->object, i), 7);
            stlp::string key(name);
            count += ei::VCall<int>(figure, 13, &key, reinterpret_cast<char*>(PartAt(self->object, i)) + 0x128,
                                    TopMatrix(scene), PointAt(count), self->bounds);
        } else {
            count += ProjectPart(PartAt(self->object, i), nullptr, scene, TopMatrix(scene), PointAt(count), self->bounds);
        }
        PopMatrix(scene);
    }
    PopMatrix(scene);
    PopMatrix(scene);
    ResizeDiscarding(Points(), count, AllocatePoints);

    // Points to texture pixels.
    float scaleX = static_cast<float>(kTextureScale / (static_cast<double>(self->bounds[2]) - self->bounds[0]));
    float scaleY = static_cast<float>(kTextureScale / (static_cast<double>(self->bounds[3]) - self->bounds[1]));
    float offsetX = static_cast<float>(-(static_cast<double>(scaleX) * self->bounds[0]));
    float offsetY = static_cast<float>(-(static_cast<double>(scaleY) * self->bounds[1]));
    float toPixels[16] = {};
    toPixels[0] = toPixels[5] = toPixels[10] = toPixels[15] = 1.0f;   // 0051E1C0
    toPixels[0] = scaleX;
    toPixels[12] = offsetX;
    toPixels[13] = offsetY;
    toPixels[5] = scaleY;
    for (int k = 0; k < Points().size; ++k) {
        float x = static_cast<float>(static_cast<double>(toPixels[0]) * PointAt(k)[0] + toPixels[12]);
        PixelAt(k)[0] = x87::fistp(x);
        float y = static_cast<float>(static_cast<double>(toPixels[5]) * PointAt(k)[1] + toPixels[13]);
        PixelAt(k)[1] = x87::fistp(y);
    }
    ResizeDiscarding(Pixels(), count, AllocatePixels);

    // Rasterise into the render surface.
    void* surface = RenderSurface(ei::VCall<void*>(self->ManagerContext(), 0x88 / 4));
    SurfaceDescription description;
    if (!LockSurface(surface, nullptr, &description)) {
        *failure = game::FatalError(reinterpret_cast<const char*>(0x0078F584), self->GetName());
        if (lights.manager) --reinterpret_cast<CEngineObject*>(lights.manager)->refCount_;
        return false;
    }
    RasterTarget target{};
    CEngineObject* material = self->material ? *self->material : nullptr;
    target.colourKey = material && (material->flags_ & kMaterialKeyed) ? At<std::uint32_t>(material, 0x60) : 0;
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
        keyedParts |= (At<std::uint32_t>(part, 0x188) >> 10) & 1;
        if (part->flags_ & kPartHidden) continue;
        if (CEngineObject* figure = self->object->figure) {
            const char* name = ei::VCall<const char*>(PartAt(self->object, i), 7);
            stlp::string key(name);
            count += ei::VCall<int>(figure, 14, &key, PixelAt(count), &target);
        } else {
            count += RasterisePart(part, nullptr, device, PixelAt(count), &target);
        }
    }
    if (ShadowMode() == 2) {
        ei::VCall<void>(device, 0x2C / 4, target.bits, kTextureSize, kTextureSize);
        ei::VCall<void>(device, 0x30 / 4, target.bits, kTextureSize, kTextureSize);
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
    float at[3];
    std::memcpy(at, &self->object->position, sizeof(at));
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
    float* m = view;
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
        quad[0] = static_cast<float>(static_cast<double>(at[0]) + x);
        quad[4] = static_cast<float>(static_cast<double>(at[1]) + y);
        quad[8] = at[2];
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
        quad[1 + c] = static_cast<float>(x + at[0]);
        quad[5 + c] = static_cast<float>(static_cast<double>(y) + at[1]);
        quad[9 + c] = at[2];
        quad[13 + c] = 1.0f;
    }

    // The point the shadow falls from: the object's offset point through its matrix.
    const char* o = reinterpret_cast<const char*>(self->object);
    auto f = [o](int offset) { return static_cast<double>(*reinterpret_cast<const float*>(o + offset)); };
    double pz = (f(0x78) * f(0x12C) + f(0x7C) * f(0x130)) + f(0x74) * f(0x128);
    float py = static_cast<float>((f(0x68) * f(0x12C) + f(0x6C) * f(0x130)) + f(0x64) * f(0x128));
    float px = static_cast<float>((f(0x58) * f(0x12C) + f(0x5C) * f(0x130)) + f(0x54) * f(0x128));
    float point[3] = {static_cast<float>(static_cast<double>(px) + at[0]),
                      static_cast<float>(static_cast<double>(py) + at[1]), static_cast<float>(pz + at[2])};
    ProjectShadow(ShadowProjector(), nullptr, direction, point, At<std::uint32_t>(self->object, 0x134), &Quads(),
                  &self->vertices, &self->indices, &self->texCoords, 0, keyedParts == 0);

    if (self->vertexCount != 0) {
        ei::VCall<void>(sceneObject, 7, &self->sphere, self->vertices, self->vertexCount);
        const float* position = &self->object->position.x;
        self->sphereCentre[0] = static_cast<float>(static_cast<double>(self->sphereCentre[0]) - position[0]);
        self->sphereCentre[1] = static_cast<float>(static_cast<double>(self->sphereCentre[1]) - position[1]);
        self->sphereCentre[2] = static_cast<float>(static_cast<double>(self->sphereCentre[2]) - position[2]);
    } else {
        self->sphere = 0;
    }
    self->flags_ &= ~kFlagRefresh;
    if (lights.manager) --reinterpret_cast<CEngineObject*>(lights.manager)->refCount_;
    return true;
}
} // namespace

int CObject3DClientSpecific::DrawShadow(ShadowDrawContext* context) {
    if (ShadowMode() <= 0) return 1;
    if (!ShadowSwitch(object->GetClassId())) return 1;
    if (object->flags_ & kObjectHidden) return 1;
    if (ei::VCall<bool>(object, 0x54 / 4)) return 1;   // invisible

    if (!texture) {
        AssignTexture(&texture, CreateShadowTexture(TextureManager()));
        if (!texture) return game::FatalError(reinterpret_cast<const char*>(0x0078F5B8));
        char name[0x100];
        gamecrt::sprintf(name, reinterpret_cast<const char*>(0x0078F5B0), object->id_);   // "SR%d"
        At<mfc::CString>(texture, 0x1C) = name;
        flags_ |= kFlagRefresh;
    }
    std::uint32_t objectFlags = object->flags_;
    if ((objectFlags & 0x600) == 0x200 || !(objectFlags & 0x200) || (flags_ & kFlagRefresh)) {
        int failure;
        if (!RenderShadow(this, context, &failure)) return failure;
    }

    if (vertexCount == 0) return 1;
    std::uint32_t colour = kDefaultColour;
    if (object->GetClassId() == kClassUnitObject) {
        CEngineObject* controller = At<CEngineObject*>(object, 0x240);
        float alpha = static_cast<float>(static_cast<double>(At<float>(controller, 0x78)) * kAlphaScale);
        colour = (static_cast<std::uint32_t>(x87::fistp(alpha)) << 24) | 0xFFFFFF;
    }
    SetTexture(TextureStages(), nullptr, 0, texture);
    std::uint8_t lit = IsLit(LightManager(), nullptr, &object->position.x);
    DrawGeometry(RenderDevice(), nullptr, vertices, indices, vertexCount, texCoords, primitiveCount, colour,
                 static_cast<std::uint32_t>(static_cast<std::uint8_t>(lit)) << 24, 0, kDrawDepth);
    return 1;
}
EI_REPLACE_METHOD(0x0051CE60, CObject3DClientSpecific::DrawShadow);
