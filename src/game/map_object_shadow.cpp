// The shadow of a 3D object on the client. See map_object_shadow.h and docs/notes/map_objects.md
// ("Shadows").
#include "figure_mesh.h"
#include "map_object_shadow.h"

#include "link_figure.h"        // CLinkMesh
#include "map_object.h"         // ObjectSlots
#include "ground_decal.h"        // DecalTexCoord
#include "dx_surface.h"          // SurfacePixels
#include "map_object_figures.h"
#include "model.h"
#include "spell.h"              // CLight
#include "terrain.h"
#include "unit.h"               // CUnit
#include "unit_client_models.h" // CUnitClientSpecific
#include "view_matrices.h"
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
#include "originals_core.h"
#include "originals_effects.h"
#include "originals_world.h"

#include <bit>
#include <cstring>
#include <vector>
#include "originals_render.h"
#include "game_data.h"

// The original keeps intermediate values on the FPU stack (double locals here) and rounds where
// it stores to a float (float locals); the order of every operation is the original's.

namespace {
// --- Globals ----------------------------------------------------------------------------------

int ShadowMode() { return ei::GameData<const int>(0x0079BD0C); }   // 0 off, 2 blurred
int ShadowSwitch(int classId) {
    if (classId == 0x50) return ei::GameData<const int>(0x0079BD14);   // units
    if (classId == 0x52) return ei::GameData<const int>(0x0079BD1C);
    return ei::GameData<const int>(0x0079BD18);
}
const Vector3* DefaultLight() { return ei::GamePointer<const Vector3>(0x007AF230); }
void* TextureManager() { return ei::GameData<void*>(0x007AF744); }
void* RenderDevice() { return ei::GameData<void*>(0x007AF080); }
void* ShadowProjector() { return ei::GamePointer<void>(0x007AF5B8); }
void* TextureStages() { return ei::GamePointer<void>(0x007AF060); }
// The work buffers: the parts' points projected into the light's view, their texture pixels, and
// the ground quad (one block of four corners).
using ShadowPoints = lib::AlignedArray<Vector2>;
using ShadowPixels = lib::AlignedArray<RasterTriangle::Point>;
using ShadowQuads = lib::AlignedArray<VertexPacket>;
ShadowPoints& Points() { return ei::GameData<ShadowPoints>(0x007AF1F8); }
ShadowPixels& Pixels() { return ei::GameData<ShadowPixels>(0x007AF1E0); }
ShadowQuads& Quads() { return ei::GameData<ShadowQuads>(0x007AF208); }

constexpr int kTypePointLight = 0x31;
constexpr int kClassUnitObject = 0x50;
constexpr std::uint32_t kFlagRefresh = 8;           // this->flags_: render the texture again
constexpr std::uint32_t kObjectHidden = 0x48;       // object->flags_
constexpr std::uint32_t kLightOff = 0x400;          // light->flags_
constexpr std::uint32_t kLightRemoved = 0x80;
constexpr std::uint32_t kLightStrong = 0x800;       // a point light counting ten times
constexpr std::uint32_t kPartHidden = 0x80;         // part->flags_
constexpr std::uint32_t kTextureKeyed = 0x2000;     // skin->flags_: block_ holds its key texels
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


// 00692140 / 00690FC0 / 00691030. The render surface, locked and unlocked.
// 0056C750 (CWorldClient::ProjectDecal). The shadow projector: lays the texture's quad on
// the ground.
// 00501AF0. Texture stages: binds a texture.
void __fastcall SetTexture(void* a0, void*, int a2, CEngineObject* a3) { static_cast<CSceneRenderer*>(a0)->SetTextureImpl(a2, (void*)a3); }
// 006CB020. Light manager: is the point lit, AL.
// 004EA4A0 (CTextureDevice::DrawFlatBlocks). Device: draws the shadow geometry.
// 004018A0 (PreMultiplyAt, originals_core.h). Rewritten elsewhere, through its game address: 005A0CD0
// CFigureMesh::DrawOffset.

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
bool PartTooFar(const CFigureMesh* part) { return LengthSquaredUnrounded<Sum3::XYZ, double>(part->localPosition_) > kFarSquared; }

CFigureMesh* PartAt(const CObject3D* object, int i) { return static_cast<CFigureMesh*>(object->parts.data[i]); }

// The bits a part is rasterised into: the callee's triangle corners and key texels, then the target
// surface.
struct RasterTarget : RasterTriangle {
    void* bits;                // +0x34
    int keyPitch;              // +0x38 the key texels' row pitch in bytes (only slot 9 reads it)
    int pitch;                 // +0x3C
};
static_assert(sizeof(RasterTarget) == 0x40);
constexpr int kKeyTexelPitch = 0x200;   // 256 texels of 2 bytes


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
    view::LightShearMatrix(view.m, &direction);   // 004F1440 (thiscall on the pipeline, not read)
    pipeline->PushMatrixProduct(view, kShadowStack);
    // The point and pixel buffers grow without keeping their contents (004545A0 / 004F65B0 allocate).
    Points().SetSizeDiscard(kPointBufferSize);
    Pixels().SetSizeDiscard(kPointBufferSize);
    int count = 0;
    Quaternion turn{1.0f, 0.0f, 0.0f, 0.0f};
    view::LightRotation(&direction, &turn);   // 004F15C0 (likewise)
    CObject3D* object = self->object;
    // The original turns the object back with the conjugate (0051D4E0 PreMultiply), which is not exact:
    // each re-rendered shadow moved the orientation by an ulp, and how often that ran followed the frame
    // timing. Ours keeps the orientation and puts it back (a determinism fix kept in the standalone; the
    // harness does the same in the original, harness_shadow_turn.h, SV7b4a).
    const Quaternion kept = object->orientation;
    PreMultiplyAt(&object->orientation, nullptr, &turn);
    ObjectWorldMatrix(&object->orientation, &object->position, &object->worldMatrix);
    turn.x = -turn.x;   // the turn back, still used for the projector below
    turn.y = -turn.y;
    turn.z = -turn.z;
    self->object->orientation = kept;
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
        CFigureMesh* part = PartAt(self->object, i);
        if (PartTooFar(part)) continue;
        if (part->flags_ & kPartHidden) continue;
        ObjectWorldMatrix(&part->localRotation_, &part->localPosition_, &part->worldMatrix_);
        view = PartAt(self->object, i)->worldMatrix_;
        pipeline->PushMatrixProduct(view, kShadowStack);
        if (CLinkMesh* figure = self->object->figure) {
            const char* name = PartAt(self->object, i)->GetName();
            stlp::string key(name);
            count += figure->ProjectPart(&key, &PartAt(self->object, i)->offsets_, TopMatrix(pipeline), &Points()[count].x,
                                         self->bounds);
        } else {
            count += PartAt(self->object, i)->DrawOffset(pipeline, const_cast<float*>(TopMatrix(pipeline)),
                                 &Points()[count].x, self->bounds);
        }
        pipeline->PopMatrix(kShadowStack);
    }
    pipeline->PopMatrix(kShadowStack);
    pipeline->PopMatrix(kShadowStack);
    Points().SetSizeDiscard(count);

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
        float x = static_cast<float>(static_cast<double>(t[0]) * Points()[k].x + t[12]);
        Pixels()[k].x = x87::fistp(x);
        float y = static_cast<float>(static_cast<double>(t[5]) * Points()[k].y + t[13]);
        Pixels()[k].y = x87::fistp(y);
    }
    Pixels().SetSizeDiscard(count);

    // Rasterise into the render surface.
    void* surface = static_cast<CWorld*>(self->ManagerContext())->TextureManager()->ShadowRenderSurface();
    SurfacePixels description;
    if (!static_cast<CTexture*>(surface)->LockPixels(&description)) {
        *failure = game::FatalError("Can't lock render surface for texture \"%s\"\n", self->GetName());
        if (lights.manager) --lights.manager->refCount_;
        return false;
    }
    RasterTarget target{};
    CTexture* skin = self->partSets[0] ? self->partSets[0]->texture : nullptr;
    target.keyTexels = skin && (skin->flags_ & kTextureKeyed) ? skin->block_ : nullptr;
    target.keyPitch = kKeyTexelPitch;
    target.pitch = description.pitch;
    // The shadow is rasterised into a CPU buffer of the surface's layout (the new renderer takes it
    // from there) and copied into the surface afterwards.
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(description.pitch) * kTextureSize);
    target.bits = pixels.data();
    std::memset(target.bits, 0, kTextureSize * kTextureSize * 2);
    count = 0;
    int keyedParts = 0;
    for (int i = 0; i < self->object->parts.size; ++i) {
        CFigureMesh* part = PartAt(self->object, i);
        if (PartTooFar(part)) continue;
        keyedParts |= (part->bounds_.flags >> 10) & 1;
        if (part->flags_ & kPartHidden) continue;
        if (CLinkMesh* figure = self->object->figure) {
            const char* name = PartAt(self->object, i)->GetName();
            stlp::string key(name);
            count += figure->RasterisePart(&key, &Pixels()[count].x, &target);
        } else {
            count += RasteriseFigureMesh(part, nullptr, static_cast<CPixelConverter*>(device), &Pixels()[count],
                                         &target);   // 005A0E80
        }
    }
    if (ShadowMode() == 2) {
        auto* converter = static_cast<CPixelConverter*>(device);
        converter->BlurShadow(target.bits, kTextureSize, kTextureSize);
        converter->BlurShadow2(target.bits, kTextureSize, kTextureSize);
    }
    std::memcpy(description.bits, pixels.data(), pixels.size());
    static_cast<CTexture*>(surface)->UnlockPixels();

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
    Quads().SetSize(1);   // keeps the contents when growing
    {
        float a = self->bounds[3];
        float b = self->bounds[0];
        float z = static_cast<float>(static_cast<double>(b) * m[8] + static_cast<double>(a) * m[9]);
        float y = CornerY(m, a, b);
        float x = static_cast<float>(CornerX(m, a, b));
        (void)z;   // computed for the corner's Vector3, never used
        VertexPacket* quad = Quads().data;   // rows x, y, z, w; lane 0
        quad->block[0][0] = static_cast<float>(static_cast<double>(at.x) + x);
        quad->block[1][0] = static_cast<float>(static_cast<double>(at.y) + y);
        quad->block[2][0] = at.z;
        quad->block[3][0] = 1.0f;
    }
    const float corners[3][2] = {{self->bounds[3], self->bounds[2]},
                                 {self->bounds[1], self->bounds[2]},
                                 {self->bounds[1], self->bounds[0]}};
    for (int c = 0; c < 3; ++c) {
        float a = corners[c][0];
        float b = corners[c][1];
        float y = CornerY(m, a, b);
        double x = CornerX(m, a, b);
        VertexPacket* quad = Quads().data;
        quad->block[0][1 + c] = static_cast<float>(x + at.x);
        quad->block[1][1 + c] = static_cast<float>(static_cast<double>(y) + at.y);
        quad->block[2][1 + c] = at.z;
        quad->block[3][1 + c] = 1.0f;
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
    static_cast<CWorldClient*>(ShadowProjector())->ProjectDecal(&direction, &point, self->object->radius, &Quads().data,
                               &self->vertices, &self->texCoords, &self->indices, static_cast<unsigned char>(0), static_cast<unsigned char>(keyedParts == 0));

    if (self->vertices.size != 0) {
        pipeline->transform_->FitBounds(&self->sphere, self->vertices.data, self->vertices.size);
        const Vector3& position = self->object->position;
        self->sphere.center.x = static_cast<float>(static_cast<double>(self->sphere.center.x) - position.x);
        self->sphere.center.y = static_cast<float>(static_cast<double>(self->sphere.center.y) - position.y);
        self->sphere.center.z = static_cast<float>(static_cast<double>(self->sphere.center.z) - position.z);
    } else {
        self->sphere.flags = 0;
    }
    self->flags_ &= ~kFlagRefresh;
    if (lights.manager) --lights.manager->refCount_;
    return true;
}
} // namespace

int CObject3DClientSpecific::DrawShadow(CSceneRenderer* context) {
    if (ShadowMode() <= 0) return 1;
    if (!ShadowSwitch(object->GetClassId())) return 1;
    if (object->flags_ & kObjectHidden) return 1;
    if (ObjectSlots(object)->InvisibleSlot()) return 1;   // invisible
    // The game shows this object's shadow: in the new renderer's shadow map its meshes cast (R16j),
    // a unit as strongly as it has faded in.
    float strength = 1.0f;
    if (object->GetClassId() == kClassUnitObject)
        strength = static_cast<CUnitClientSpecific*>(static_cast<CUnit*>(object)->controller)->fadeIn;
    render::CastsShadow(static_cast<const CEngineObject*>(object), strength);

    if (!texture) {
        AssignTexture(&texture, static_cast<CTextureManager*>(TextureManager())->NewShadowTexture());
        if (!texture) return game::FatalError("Can't create texture for shadow rendering!\n");
        char name[0x100];
        gamecrt::sprintf(name, "SR%d", object->id_);
        *static_cast<CTexture*>(texture)->Name() = name;
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
        auto* half = static_cast<CUnitClientSpecific*>(static_cast<CUnit*>(object)->controller);
        float alpha = static_cast<float>(static_cast<double>(half->fadeIn) * kAlphaScale);
        colour = (static_cast<std::uint32_t>(x87::fistp(alpha)) << 24) | 0xFFFFFF;
    }
    SetTexture(TextureStages(), nullptr, 0, texture);
    std::uint8_t lit = static_cast<Terrain*>(LightManager())->EdgeFade(&object->position);
    render::ClassicShadowDecal();   // drawn only with the classic shadows (the shadow map draws it)
    static_cast<CTextureDevice*>(RenderDevice())
        ->DrawFlatBlocks(vertices.data, reinterpret_cast<const char*>(texCoords.data), vertices.size, indices.data,
                         indices.size, colour, static_cast<std::uint32_t>(static_cast<std::uint8_t>(lit)) << 24, nullptr,
                         std::bit_cast<float>(kDrawDepth));
    return 1;
}
EI_REPLACE_METHOD(0x0051CE60, CObject3DClientSpecific::DrawShadow);
