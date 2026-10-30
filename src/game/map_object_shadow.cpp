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
#include "spell.h"                // Light
#include "unit.h"                 // Unit
#include "unit_client_models.h"   // UnitClientSpecific
#include "world.h"   // g_clientWorld

#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "lib/vecmath.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>


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
constexpr float kLeastDown = 0.3f;

constexpr float kRed = 0.3f;
static_assert(std::bit_cast<std::uint32_t>(kRed) == 0x3E99999Au);
constexpr float kGreen = 0.6f;
static_assert(std::bit_cast<std::uint32_t>(kGreen) == 0x3F19999Au);
constexpr float kBlue = 0.1f;
static_assert(std::bit_cast<std::uint32_t>(kBlue) == 0x3DCCCCCDu);
constexpr float kStrongFactor = 10.0f;
constexpr float kTiltSlope = -0.36397f;  // -tan(20 degrees)
static_assert(std::bit_cast<std::uint32_t>(kTiltSlope) == 0xBEBA5A47u);

constexpr float kMinimumTilt = kPi / 180.0f * 20.0f;   // radians

// The light's brightness: 0.1 blue + 0.6 green + 0.3 red.
float Brightness(const Light* light) {
    float blue = light->color[2] * kBlue;
    float green = light->color[1] * kGreen;
    float red = light->color[0] * kRed;
    return (blue + green) + red;
}

// A point light's share: from the light towards the object, scaled by its brightness and its
// falloff over the squared distance. False when the object is out of its range.
bool PointLightShare(const Object3D* object, Light* light, Vector3* out) {
    const Vector3& at = object->localPosition;
    const Vector3& from = light->localPosition;
    float dx = at.x - from.x;
    float dy = at.y - from.y;
    float dz = at.z - from.z;
    float distanceSquared = LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz);
    if (distanceSquared >= light->rangeSquared) return false;    // NaN counts as in range
    float falloff = 1.0f - light->inverseRangeSquared * distanceSquared;
    float intensity = Brightness(light) * falloff;
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
    float scale = intensity;
    if (light->flags & LightFlag::Strong) scale = scale * kStrongFactor;
    out->z = towards.z * scale;
    out->y = towards.y * scale;
    out->x = towards.x * scale;
    return true;
}

// A directional light's share: its direction times its brightness.
void DirectionalShare(Light* light, Vector3* out) {
    float brightness = Brightness(light);
    out->z = brightness * light->direction.z;
    out->y = brightness * light->direction.y;
    out->x = brightness * light->direction.x;
}

// The normalised direction of `sum`, tilted down to at least 20 degrees below the horizontal.
void LightDirection(const Vector3& sum, Vector3* direction) {
    float lengthSquared = LengthSquaredUnrounded<Sum3::ZXY>(sum);   // (x + z) + y
    *direction = sum;
    EI_NAN_AUDIT(lengthSquared);
    if (lengthSquared != 0.0f) {
        float inverse = 1.0f / std::sqrt(lengthSquared);
        direction->x = sum.x * inverse;
        direction->y = inverse * direction->y;
        direction->z = inverse * direction->z;
    }
    float horizontal = std::sqrt(LengthSquared2DUnrounded(direction->y, direction->x));
    float height = AbsBits(direction->z);
    float elevation = std::atan2(height, horizontal);
    if (elevation >= kMinimumTilt) return;   // NaN tilts
    float z = horizontal * kTiltSlope;
    float inverse = 1.0f / std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(direction->x, direction->y, z));
    direction->x = inverse * direction->x;
    direction->y = inverse * direction->y;
    direction->z = inverse * z;
}

// The shadow's sphere about the object (the original projected the parts from the lights' direction and
// the square of their bounds onto the ground, and took the projected decal's sphere): the object's bounding
// sphere in the world swept along the lights' direction down to the ground, taken as level at the lower of the
// object's origin and its sphere's bottom; a box around the sweep and the sphere around the box, the centre
// relative to the object. Only ShadowInView reads it.
void FitShadowSphere(Object3DClientSpecific* self) {
    // The lights reaching the object.
    Vector3 sum;
    sum = kDefaultLight;
    ObjectListIteratorOf<Light> lights{};
    for (Light* light = lights.First(g_clientWorld.lightFactory); light; light = lights.Next()) {
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
    if (lights.manager) lights.manager->ForgetReference();
    Vector3 direction;
    LightDirection(sum, &direction);
    self->flags &= ~kFlagRefresh;

    const Object3D* object = self->object;
    const ClipBounds& bounds = object->bounds;
    ClipBounds& sphere = self->sphere;
    if (!(bounds.flags & static_cast<std::uint32_t>(BoundsFlag::Sphere))) {
        sphere.flags = 0;   // no bounds: ClipTest calls it crossing the view
        return;
    }
    // The sweep goes down whichever way the direction's height points.
    float dx = direction.x, dy = direction.y, down = std::fabs(direction.z);
    if (!(down >= kLeastDown)) {
        dx = 0.0f;
        dy = 0.0f;
        down = 1.0f;
    }
    Matrix4 placement;
    ObjectWorldMatrix(&object->localRotation, object->localPosition, &placement);
    const Vector3& scale = self->figureScale;
    const Vector3 local = {bounds.center.x * scale.x, bounds.center.y * scale.y, bounds.center.z * scale.z};
    const Vector3 centre = TransformColumn<Sum3::XYZ>(placement, local);
    const float radius = bounds.radius * (std::max)({std::fabs(scale.x), std::fabs(scale.y), std::fabs(scale.z)});
    const Vector3& at = object->localPosition;
    const float ground = (std::min)(at.z, centre.z - radius);
    const float length = (centre.z - ground + radius) / down;
    const Vector3 end = {centre.x + dx * length, centre.y + dy * length, centre.z - down * length};
    const Vector3 grow = {radius, radius, radius};
    const Vector3 low =
        Vector3{(std::min)(centre.x, end.x), (std::min)(centre.y, end.y), (std::min)(centre.z, end.z)} - grow;
    const Vector3 high =
        Vector3{(std::max)(centre.x, end.x), (std::max)(centre.y, end.y), (std::max)(centre.z, end.z)} + grow;
    const Vector3 middle = (low + high) * 0.5f - at;
    const Vector3 half = (high - low) * 0.5f;
    const float diagonal = LengthXYZ(half);
    if (!std::isfinite(diagonal) || !std::isfinite(middle.x) || !std::isfinite(middle.y) || !std::isfinite(middle.z)) {
        sphere.flags = 0;
        return;
    }
    sphere.center = middle;
    sphere.boxMax = half;
    sphere.boxMin = -half;
    sphere.radius = diagonal;
    sphere.flags = static_cast<std::uint32_t>(BoundsFlag::Sphere | BoundsFlag::Box);
}

} // namespace

// --- The shadow -------------------------------------------------------------------------------

std::optional<float> ShadowCastStrength(Object3D* object) {
    if (options::g_options.shadowQuality <= 0) return std::nullopt;
    if (!ShadowSwitch(object->GetClassId())) return std::nullopt;
    if (object->flags & (MapObjectFlag::Hidden | MapObjectFlag::NotDrawn)) return std::nullopt;
    if (object->InvisibleSlot()) return std::nullopt;
    if (Unit* unit = UnitOrNull(object)) return unit->ClientController()->fadeIn;
    return 1.0f;
}

int Object3DClientSpecific::DrawShadow() {
    // The game shows this object's shadow: in the renderer's shadow map its meshes cast, a unit as strongly as it
    // has faded in. No shadow texture or decal is drawn.
    const std::optional<float> strength = ShadowCastStrength(object);
    if (!strength) return 1;
    render::CastsShadow(object, *strength);

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
