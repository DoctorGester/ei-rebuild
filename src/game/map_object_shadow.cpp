// The shadow of a 3D object on the client: the renderer's shadow map draws it (CastsShadow); the game keeps a
// sphere around the object and its shadow on the ground, which decides the shadow's visibility out of view
// (ShadowInView). See map_object_shadow.h and docs/notes/map_objects.md ("Shadows").
#include "map_object_shadow.h"

#include "game_singletons.h"
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
#include "world.h"   // g_clientWorld

#include "lib/float_select.h"
#include "lib/gameapi.h"
#include "lib/nan_audit.h"
#include "lib/vecmath.h"

#include "ei/x87.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

// The lights' direction is the original's: it keeps intermediate values on the FPU stack (double locals here)
// and rounds where it stores to a float (float locals); the order of every operation is the original's.

namespace {

int ShadowSwitch(ClassId classId) {
    if (classId == ClassId::Unit) return options::g_options.unitShadow;
    if (classId == ClassId::Tree) return options::g_options.floraShadow;
    return options::g_options.buildingShadow;
}
constexpr Vector3 kDefaultLight = /*007AF230*/ {};   // a static initialiser: zeros
constexpr int kTypePointLight = 0x31;
constexpr std::uint32_t kFlagRefresh = 8;           // this->flags: fit the shadow's sphere again
// The least downward share of a direction LightDirection gives (sin 20 degrees is 0.342); below it (only NaN)
// the sweep goes straight down.
constexpr double kLeastDown = 0.3;

constexpr float kRed = 0.3f;
static_assert(std::bit_cast<std::uint32_t>(kRed) == 0x3E99999Au);
constexpr float kGreen = 0.6f;
static_assert(std::bit_cast<std::uint32_t>(kGreen) == 0x3F19999Au);
constexpr float kBlue = 0.1f;
static_assert(std::bit_cast<std::uint32_t>(kBlue) == 0x3DCCCCCDu);
constexpr float kStrongFactor = 10.0f;
constexpr float kTiltSlope = -0.36397f;  // -tan(20 degrees)
static_assert(std::bit_cast<std::uint32_t>(kTiltSlope) == 0xBEBA5A47u);

// pi, 1/180 and 20: volatile, so the product is formed at run time with the FPU's precision like the
// original's.
volatile double g_pi = 3.141592653589793;
volatile double g_perDegree = std::bit_cast<double>(0x3F76C16C16C16C17ull);
volatile double g_minimumTilt = 20.0;

// The light's brightness: 0.1 blue + 0.6 green + 0.3 red, unrounded.
double Brightness(const CLight* light) {
    double blue = static_cast<double>(light->color[2]) * kBlue;
    double green = static_cast<double>(light->color[1]) * kGreen;
    double red = static_cast<double>(light->color[0]) * kRed;
    return (blue + green) + red;
}

// A point light's share: from the light towards the object, scaled by its brightness and its
// falloff over the squared distance. False when the object is out of its range.
bool PointLightShare(const CObject3D* object, CLight* light, Vector3* out) {
    const Vector3& at = object->localPosition;
    const Vector3& from = light->localPosition;
    double dx = static_cast<double>(at.x) - from.x;
    double dy = static_cast<double>(at.y) - from.y;
    double dz = static_cast<double>(at.z) - from.z;
    float distanceSquared = static_cast<float>(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz));
    if (distanceSquared >= light->rangeSquared) return false;    // NaN counts as in range
    double falloff = 1.0f - static_cast<double>(light->inverseRangeSquared) * distanceSquared;
    float intensity = static_cast<float>(Brightness(light) * falloff);
    Vector3 towards = {static_cast<float>(static_cast<double>(at.x) - from.x),
                       static_cast<float>(static_cast<double>(at.y) - from.y),
                       static_cast<float>(static_cast<double>(at.z) - from.z)};
    EI_NAN_AUDIT(distanceSquared);
    if (distanceSquared == 0.0f) {
        towards = kDefaultLight;
    } else {
        double inverse = 1.0 / std::sqrt(distanceSquared);
        towards.x = static_cast<float>(towards.x * inverse);
        towards.y = static_cast<float>(towards.y * inverse);
        towards.z = static_cast<float>(towards.z * inverse);
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
    double elevation = x87::fpatan(height, horizontal);
    double minimum = g_pi * g_perDegree * g_minimumTilt;
    if (elevation >= minimum) return;   // NaN tilts
    double z = static_cast<double>(horizontal) * kTiltSlope;
    double inverse = 1.0f / std::sqrt(LengthSquaredUnrounded<Sum3::ZYX, double>(direction->x, direction->y, z));
    direction->x = static_cast<float>(inverse * direction->x);
    direction->y = static_cast<float>(inverse * direction->y);
    direction->z = static_cast<float>(inverse * z);
}

// The shadow's sphere about the object (the original projected the parts from the lights' direction and
// the square of their bounds onto the ground, and took the projected decal's sphere): the object's bounding
// sphere in the world swept along the lights' direction down to the ground, taken as level at the lower of the
// object's origin and its sphere's bottom; a box around the sweep and the sphere around the box, the centre
// relative to the object. Only ShadowInView reads it; plain double maths.
void FitShadowSphere(Object3DClientSpecific* self) {
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
        sum.x = static_cast<float>(static_cast<double>(share.x) + sum.x);
        sum.y = static_cast<float>(static_cast<double>(share.y) + sum.y);
        sum.z = static_cast<float>(static_cast<double>(share.z) + sum.z);
    }
    if (lights.manager) lights.manager->ForgetReference();
    Vector3 direction;
    LightDirection(sum, &direction);
    self->flags &= ~kFlagRefresh;

    const CObject3D* object = self->object;
    const ClipBounds& bounds = object->bounds;
    ClipBounds& sphere = self->sphere;
    if (!(bounds.flags & static_cast<std::uint32_t>(BoundsFlag::Sphere))) {
        sphere.flags = 0;   // no bounds: ClipTest calls it crossing the view
        return;
    }
    // The sweep goes down whichever way the direction's height points.
    double dx = direction.x, dy = direction.y, down = std::fabs(static_cast<double>(direction.z));
    if (!(down >= kLeastDown)) {
        dx = 0.0;
        dy = 0.0;
        down = 1.0;
    }
    Matrix4 placement;
    ObjectWorldMatrix(&object->localRotation, object->localPosition, &placement);
    const Vector3& scale = self->figureScale;
    const double local[3] = {static_cast<double>(bounds.center.x) * scale.x, static_cast<double>(bounds.center.y) * scale.y,
                             static_cast<double>(bounds.center.z) * scale.z};
    double centre[3];
    for (int i = 0; i < 3; ++i) {
        const float* row = placement.m + i * 4;
        centre[i] = DotUnrounded<Sum3::XYZ, double>(row[0], row[1], row[2], local[0], local[1], local[2]) + row[3];
    }
    const double radius = bounds.radius * (std::max)({std::fabs(static_cast<double>(scale.x)), std::fabs(static_cast<double>(scale.y)),
                                                      std::fabs(static_cast<double>(scale.z))});
    const Vector3& at = object->localPosition;
    const double ground = (std::min)(static_cast<double>(at.z), centre[2] - radius);
    const double length = (centre[2] - ground + radius) / down;
    const double end[3] = {centre[0] + dx * length, centre[1] + dy * length, centre[2] - down * length};
    const double origin[3] = {at.x, at.y, at.z};
    double middle[3], half[3];
    for (int i = 0; i < 3; ++i) {
        const double low = (std::min)(centre[i], end[i]) - radius;
        const double high = (std::max)(centre[i], end[i]) + radius;
        middle[i] = (low + high) * 0.5 - origin[i];
        half[i] = (high - low) * 0.5;
    }
    const double diagonal = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ, double>(half[0], half[1], half[2]));
    if (!std::isfinite(diagonal) || !std::isfinite(middle[0]) || !std::isfinite(middle[1]) || !std::isfinite(middle[2])) {
        sphere.flags = 0;
        return;
    }
    sphere.center = {static_cast<float>(middle[0]), static_cast<float>(middle[1]), static_cast<float>(middle[2])};
    sphere.boxMax = {static_cast<float>(half[0]), static_cast<float>(half[1]), static_cast<float>(half[2])};
    sphere.boxMin = {-sphere.boxMax.x, -sphere.boxMax.y, -sphere.boxMax.z};
    sphere.radius = static_cast<float>(diagonal);
    sphere.flags = static_cast<std::uint32_t>(BoundsFlag::Sphere | BoundsFlag::Box);
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

int Object3DClientSpecific::DrawShadow() {
    // The game shows this object's shadow: in the renderer's shadow map its meshes cast, a unit as strongly as it
    // has faded in. No shadow texture or decal is drawn.
    float strength = 1.0f;
    if (!ShadowCastStrength(object, &strength)) return 1;
    render::CastsShadow(object, strength);

    // The shadow's sphere decides the object's shadow visibility (ShadowInView). Fitted the first time after
    // Create / ClearShadow, for an object not drawn static and when the half asks (flag 8).
    const bool drawnStatic = lib::Has(object->flags, MapObjectFlag::Stationary) && lib::Has(object->flags, MapObjectFlag::Static);
    const bool gameBuilds = !shadowMade || !drawnStatic || (flags & kFlagRefresh);
    shadowMade = true;
    if (gameBuilds) {
        render::BeginShadowBuildTime();
        FitShadowSphere(this);
        render::EndShadowBuildTime();
    }
    return 1;
}
