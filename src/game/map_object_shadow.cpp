// The shadow of a 3D object on the client: the renderer's shadow map draws it (CastsShadow); the game's own
// build still projects the decal, whose sphere decides the shadow's visibility out of view (ShadowInView). See
// map_object_shadow.h and docs/notes/map_objects.md ("Shadows").
#include "map_object_shadow.h"

#include "figure_mesh.h"
#include "game_singletons.h"
#include "link_figure.h"   // LinkMesh
#include "map_object.h"
#include "map_object_figures.h"
#include "maths.h"
#include "model.h"
#include "object.h"
#include "options.h"
#include "render/render.h"
#include "spell.h"                // CLight
#include "unit.h"                 // CUnit
#include "unit_client_models.h"   // UnitClientSpecific
#include "view_matrices.h"
#include "world.h"   // g_clientWorld

#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/gameapi.h"
#include "lib/nan_audit.h"
#include "lib/vecmath.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// The original keeps intermediate values on the FPU stack (double locals here) and rounds where it stores to a
// float (float locals); the order of every operation is the original's.

namespace {

int ShadowSwitch(ClassId classId) {
    if (classId == ClassId::Unit) return options::g_options.unitShadow;
    if (classId == ClassId::Tree) return options::g_options.floraShadow;
    return options::g_options.buildingShadow;
}
constexpr Vector3 kDefaultLight = /*007AF230*/ {};   // a static initialiser: zeros
// The work buffers: the parts' points projected into the light's view and the ground quad (one block of four
// corners).
std::vector<Vector2> g_points;
std::vector<VertexPacket> g_quads;
constexpr int kTypePointLight = 0x31;
constexpr std::uint32_t kFlagRefresh = 8;           // this->flags: build the shadow again
constexpr int kPointBufferSize = 0x1000;
constexpr std::uint32_t kFloatMax = 0x7F7FFFFF;
constexpr std::uint32_t kFloatLowest = 0xFF7FFFFF;

constexpr float kRed = 0.3f;
static_assert(std::bit_cast<std::uint32_t>(kRed) == 0x3E99999Au);
constexpr float kGreen = 0.6f;
static_assert(std::bit_cast<std::uint32_t>(kGreen) == 0x3F19999Au);
constexpr float kBlue = 0.1f;
static_assert(std::bit_cast<std::uint32_t>(kBlue) == 0x3DCCCCCDu);
constexpr float kStrongFactor = 10.0f;
constexpr float kTiltSlope = -0.36397f;  // -tan(20 degrees)
static_assert(std::bit_cast<std::uint32_t>(kTiltSlope) == 0xBEBA5A47u);
constexpr float kFarSquared = 1e10f;

// pi, 1/180 and 20: volatile, so the product is formed at run time with the FPU's precision like the
// original's.
volatile double g_pi = 3.141592653589793;
volatile double g_perDegree = std::bit_cast<double>(0x3F76C16C16C16C17ull);
volatile double g_minimumTilt = 20.0;


// The pipeline's stack the shadow is rendered with.
constexpr int kShadowStack = 10;

const Matrix4& TopMatrix(const TransformPipeline* pipeline) { return *pipeline->Top(kShadowStack); }

// The light's brightness: 0.1 blue + 0.6 green + 0.3 red, unrounded.
double Brightness(const CLight* light) {
    float blue = light->color[2] * kBlue;
    float green = light->color[1] * kGreen;
    float red = light->color[0] * kRed;
    return (blue + green) + red;
}

// A point light's share: from the light towards the object, scaled by its brightness and its
// falloff over the squared distance. False when the object is out of its range.
bool PointLightShare(const CObject3D* object, CLight* light, Vector3* out) {
    const Vector3& at = object->localPosition;
    const Vector3& from = light->localPosition;
    float dx = at.x - from.x;
    float dy = at.y - from.y;
    float dz = at.z - from.z;
    float distanceSquared = LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz);
    if (distanceSquared >= light->rangeSquared) return false;    // NaN counts as in range
    float falloff = 1.0f - light->inverseRangeSquared * distanceSquared;
    float intensity = static_cast<float>(Brightness(light) * falloff);
    Vector3 towards = {at.x - from.x,
                       at.y - from.y,
                       at.z - from.z};
    EI_NAN_AUDIT(distanceSquared);
    if (distanceSquared == 0.0f) {
        towards = kDefaultLight;
    } else {
        float inverse = 1.0f / std::sqrt(distanceSquared);
        towards.x = towards.x * inverse;
        towards.y = towards.y * inverse;
        towards.z = towards.z * inverse;
    }
    double scale = intensity;
    if (light->flags & LightFlag::Strong) scale = scale * kStrongFactor;
    out->z = static_cast<float>(towards.z * scale);
    out->y = static_cast<float>(towards.y * scale);
    out->x = static_cast<float>(towards.x * scale);
    return true;
}

// A directional light's share: its direction times its brightness.
void DirectionalShare(CLight* light, Vector3* out) {
    double brightness = Brightness(light);
    out->z = static_cast<float>(brightness * light->direction.z);
    out->y = static_cast<float>(brightness * light->direction.y);
    out->x = static_cast<float>(brightness * light->direction.x);
}

// The normalised direction of `sum`, tilted down to at least 20 degrees below the horizontal.
void LightDirection(const Vector3& sum, Vector3* direction) {
    double lengthSquared = LengthSquaredUnrounded<Sum3::ZXY, double>(sum);   // (x + z) + y
    *direction = sum;
    EI_NAN_AUDIT(lengthSquared);
    if (lengthSquared != 0.0) {
        double inverse = 1.0 / std::sqrt(lengthSquared);
        direction->x = static_cast<float>(sum.x * inverse);
        direction->y = static_cast<float>(inverse * direction->y);
        direction->z = static_cast<float>(inverse * direction->z);
    }
    float horizontal = static_cast<float>(std::sqrt(LengthSquared2DUnrounded<double>(direction->y, direction->x)));
    float height = AbsBits(direction->z);
    float elevation = std::atan2(height, horizontal);
    double minimum = g_pi * g_perDegree * g_minimumTilt;
    if (elevation >= minimum) return;   // NaN tilts
    float z = horizontal * kTiltSlope;
    double inverse = 1.0f / std::sqrt(LengthSquaredUnrounded<Sum3::ZYX, double>(direction->x, direction->y, z));
    direction->x = static_cast<float>(inverse * direction->x);
    direction->y = static_cast<float>(inverse * direction->y);
    direction->z = static_cast<float>(inverse * z);
}

// Parts beyond 1e10 from the object are skipped (NaN is not).
bool PartTooFar(const FigureMesh* part) { return LengthSquaredUnrounded<Sum3::XYZ, double>(part->localPosition) > kFarSquared; }

FigureMesh* PartAt(const CObject3D* object, int i) { return object->parts[i]; }


// One corner's x and y in the light's rotation `m` (3x4 rows of 4).
double CornerX(const float* m, float a, float b) {
    return static_cast<double>(m[1]) * a + static_cast<double>(b) * m[0];
}
float CornerY(const float* m, float a, float b) {
    return a * m[5] + m[4] * b;
}

// Whether any of the object's parts (but those too far, hidden or not) has keyed texels: the decal then skips
// the platforms (the original reads it in the raster's loop; the raster changes no flag).
int KeyedParts(const CObject3D* object) {
    int keyedParts = 0;
    for (int i = 0; i < std::ssize(object->parts); ++i) {
        const FigureMesh* part = PartAt(object, i);
        if (PartTooFar(part)) continue;
        keyedParts |= lib::Has(part->box.bounds.flags, BoundsFlag::Base) ? 1 : 0;
    }
    return keyedParts;
}

// The shadow built again: the lights' direction, the parts' points from there (their bounds in the light's view),
// the bounds' square on the ground and the decal's projection of it (its sphere). No shadow texture is rasterised:
// the shadow map draws the shadow (docs/notes/map_objects.md, "Shadows").
void RenderShadow(Object3DClientSpecific* self, SceneRenderer* context) {
    TransformPipeline* pipeline = context->pipeline;

    // The lights reaching the object.
    Vector3 sum;
    sum = kDefaultLight;
    ObjectListIteratorOf<CLight> lights{};
    for (CLight* light = lights.First(g_clientWorld.lightFactory); light; light = lights.Next()) {
        if ((light->flags & LightFlag::Off) || (light->flags & LightFlag::Hidden)) continue;
        Vector3 share;
        if (light->type == kTypePointLight) {
            if (!PointLightShare(self->object, light, &share)) continue;
        } else {
            DirectionalShare(light, &share);
        }
        sum.x = share.x + sum.x;
        sum.y = share.y + sum.y;
        sum.z = share.z + sum.z;
    }
    Vector3 direction;
    LightDirection(sum, &direction);

    // Look from the light; the object turned to face it.
    Matrix4 view;
    view::LightShearMatrix(view.m, direction);   // (on the pipeline, not read)
    pipeline->PushMatrixProduct(view, kShadowStack);
    // The buffer grows without keeping its contents.
    g_points.resize(kPointBufferSize);
    int count = 0;
    Quaternion turn{1.0f, 0.0f, 0.0f, 0.0f};
    view::LightRotation(direction, &turn);   // (likewise)
    CObject3D* object = self->object;
    // The original turns the object back with the conjugate (PreMultiply), which is not exact: each re-rendered
    // shadow moved the orientation by an ulp, as often as the frame timing ran it. Ours keeps the orientation
    // and puts it back (a determinism fix; harness_shadow_turn.h does the same in the original).
    const Quaternion kept = object->localRotation;
    object->localRotation.PreMultiply(turn);
    ObjectWorldMatrix(&object->localRotation, object->localPosition, &object->worldMatrix);
    turn.x = -turn.x;   // the turn back, still used for the projector below
    turn.y = -turn.y;
    turn.z = -turn.z;
    self->object->localRotation = kept;
    view = self->object->worldMatrix;
    self->bounds[0] = std::bit_cast<float>(kFloatMax);
    self->bounds[2] = std::bit_cast<float>(kFloatLowest);
    self->bounds[1] = std::bit_cast<float>(kFloatMax);
    self->bounds[3] = std::bit_cast<float>(kFloatLowest);
    view.m[11] = 0.0f;
    view.m[7] = 0.0f;
    view.m[3] = 0.0f;
    pipeline->PushMatrixProduct(view, kShadowStack);

    // Project the parts' points.
    for (int i = 0; i < std::ssize(self->object->parts); ++i) {
        FigureMesh* part = PartAt(self->object, i);
        if (PartTooFar(part)) continue;
        if (part->flags & FigureMeshFlag::Hidden) continue;
        ObjectWorldMatrix(&part->localRotation, part->localPosition, &part->worldMatrix);
        view = PartAt(self->object, i)->worldMatrix;
        pipeline->PushMatrixProduct(view, kShadowStack);
        if (LinkMesh* figure = self->object->figure) {
            const char* name = PartAt(self->object, i)->GetName();
            std::string key(name);
            count += figure->ProjectPart(&key, &PartAt(self->object, i)->offsets, TopMatrix(pipeline), &g_points[count],
                                         self->bounds);
        } else {
            count += PartAt(self->object, i)->DrawOffset(TopMatrix(pipeline), &g_points[count], self->bounds);
        }
        pipeline->PopMatrix(kShadowStack);
    }
    pipeline->PopMatrix(kShadowStack);
    pipeline->PopMatrix(kShadowStack);
    g_points.resize(count);

    // Whether any part is keyed: the decal then skips the platforms.
    const int keyedParts = KeyedParts(self->object);

    // The light's rotation as a matrix.
    float x2 = turn.x + turn.x;
    float y2 = turn.y + turn.y;
    float z2 = turn.z + turn.z;
    Vector3 at;
    at = self->object->localPosition;
    float wx = x2 * turn.w;
    float wy = y2 * turn.w;
    float wz = z2 * turn.w;
    float xx = x2 * turn.x;
    float xy = y2 * turn.x;
    float xz = z2 * turn.x;
    float yy = y2 * turn.y;
    float yz = z2 * turn.y;
    float zzExact = z2 * turn.z;
    float zz = zzExact;
    float* m = view.m;
    m[0] = 1.0f - (zzExact + yy);
    m[1] = xy - wz;
    m[2] = xz + wy;
    m[4] = xy + wz;
    m[5] = 1.0f - (zz + xx);
    m[6] = yz - wx;
    m[8] = xz - wy;
    m[9] = yz + wx;
    m[10] = 1.0f - (yy + xx);

    // The bounds' square on the ground: corner 0 at (minimum x, maximum y), then clockwise.
    g_quads.resize(1);   // keeps the contents when growing
    {
        float a = self->bounds[3];
        float b = self->bounds[0];
        float z = b * m[8] + a * m[9];
        float y = CornerY(m, a, b);
        float x = static_cast<float>(CornerX(m, a, b));
        (void)z;   // computed for the corner's Vector3, never used
        VertexPacket* quad = g_quads.data();   // rows x, y, z, w; lane 0
        quad->block[0][0] = at.x + x;
        quad->block[1][0] = at.y + y;
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
        VertexPacket* quad = g_quads.data();
        quad->block[0][1 + c] = static_cast<float>(x + at.x);
        quad->block[1][1 + c] = y + at.y;
        quad->block[2][1 + c] = at.z;
        quad->block[3][1 + c] = 1.0f;
    }

    // The point the shadow falls from: the object's offset point through its matrix.
    // Each row of the world matrix times the bounds' centre: (y + z) + x, unrounded.
    const float* w = self->object->worldMatrix.m;
    const Vector3& c = self->object->bounds.center;
    auto product = [](float a, float b) { return static_cast<double>(a) * b; };
    double pz = (product(w[9], c.y) + product(w[10], c.z)) + product(w[8], c.x);
    float py = static_cast<float>((product(w[5], c.y) + product(w[6], c.z)) + product(w[4], c.x));
    float px = static_cast<float>((product(w[1], c.y) + product(w[2], c.z)) + product(w[0], c.x));
    Vector3 point = {px + at.x,
                     py + at.y, static_cast<float>(pz + at.z)};
    g_clientWorld.ProjectDecal(direction, point, self->object->bounds.radius, g_quads[0],
                               &self->vertices, &self->texCoords, &self->indices, static_cast<unsigned char>(0), static_cast<unsigned char>(keyedParts == 0));

    if (!self->vertices.empty()) {
        self->sphere.Fit(self->vertices.data(), std::ssize(self->vertices));
        const Vector3& position = self->object->localPosition;
        self->sphere.center.x = self->sphere.center.x - position.x;
        self->sphere.center.y = self->sphere.center.y - position.y;
        self->sphere.center.z = self->sphere.center.z - position.z;
    } else {
        self->sphere.flags = 0;
    }
    self->flags &= ~kFlagRefresh;
    if (lights.manager) lights.manager->ForgetReference();
}

} // namespace

// --- The shadow -------------------------------------------------------------------------------

bool ShadowCastStrength(CObject3D* object, float* strength) {
    if (options::g_options.shadowQuality <= 0) return false;
    if (!ShadowSwitch(object->GetClassId())) return false;
    if (object->flags & (MapObjectFlag::Hidden | MapObjectFlag::NotDrawn)) return false;
    if (object->InvisibleSlot()) return false;
    *strength = 1.0f;
    if (CUnit* unit = UnitOrNull(object)) *strength = unit->ClientController()->fadeIn;
    return true;
}

int Object3DClientSpecific::DrawShadow(SceneRenderer* context) {
    // The game shows this object's shadow: in the renderer's shadow map its meshes cast, a unit as strongly as it
    // has faded in. No shadow texture or decal is drawn.
    float strength = 1.0f;
    if (!ShadowCastStrength(object, &strength)) return 1;
    render::CastsShadow(object, strength);

    // The build: its decal's sphere decides the object's shadow visibility (ShadowInView). Built the first time
    // after Create / ClearShadow, for an object not drawn static and when the half asks (flag 8).
    const bool drawnStatic = lib::Has(object->flags, MapObjectFlag::Stationary) && lib::Has(object->flags, MapObjectFlag::Static);
    const bool gameBuilds = !shadowMade || !drawnStatic || (flags & kFlagRefresh);
    shadowMade = true;
    if (gameBuilds) {
        render::BeginShadowBuildTime();
        RenderShadow(this, context);
        render::EndShadowBuildTime();
    }
    return 1;
}
