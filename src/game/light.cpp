// Lights and the light manager: CLight's lifetime and setters, LightFactory ("CLightManager")
// lifetime and object creation. See spell.h and docs/notes/items_spells.md.
#include "core_transform.h"
#include "spell.h"

#include "lib/crt.h"
#include "lib/vecmath.h"

#include <cfloat>
#include <cmath>
#include <cstring>
#include <list>
#include <new>

namespace {

constexpr int kLightDefault = 0x2E;
constexpr int kLightPoint = 0x31;
constexpr int kKindPoint = 1;
constexpr int kKindDirectional = 3;

constexpr const char* kUnknownLightType = "CLightManager::CreateInstant.\n Request for light with unknown type %d.\n";

float Lit(float base, float amount, float color) {
    const float sum = amount * color + base;
    return sum > 1.0f ? 1.0f : sum;
}

} // namespace

// --- CLight --------------------------------------------------------------------------------------

CLight::CLight() {
    localRotation = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    worldRotation.SetIdentity();
    std::memset(&kind, 0, offsetof(CLight, rangeSquared) - offsetof(CLight, kind));
    // The root of the double FLT_MAX: the main thread's 24-bit precision rounds the root to
    // a float, and its square from there (2^128 - 2^105, one below FLT_MAX).
    const float root = static_cast<float>(std::sqrt(static_cast<double>(FLT_MAX)));
    const double squared = static_cast<double>(root) * root;
    range = root;
    rangeSquared = static_cast<float>(squared);
    inverseRangeSquared = static_cast<float>(1.0 / static_cast<float>(squared));
}

CLight::~CLight() = default;

void CLight::SetPoint(const Vector3& newColor, const Vector3& position) {
    kind = kKindPoint;
    color[0] = newColor.x;
    color[1] = newColor.y;
    color[2] = newColor.z;
    pointPosition = position;
    localPosition = position;
    flags |= LightFlag::Changed;
}

void CLight::SetDirectional(const Vector3& newColor, const Vector3& newDirection) {
    kind = kKindDirectional;
    color[0] = newColor.x;
    color[1] = newColor.y;
    color[2] = newColor.z;
    direction = newDirection;
    const Vector3 target = newDirection;
    TiltTowards(target);
    flags |= LightFlag::Changed;
}

void CLight::AddPoint(Vector4* out, float scale, const Vector4* base, const Vector3& normal, const Vector3& toLight,
                      float distance) const {
    const float falloff = 1.0f - distance * inverseRangeSquared * distance;
    if (falloff < 0.0f) {
        *out = *base;
        return;
    }
    const float cosine = vecmath::Dot<Sum3::ZYX>(normal, toLight);
    const float facing = cosine <= 0.0f ? cosine + 1.0f : 1.0f;
    const float amount = facing * falloff * scale;
    const float red = Lit(base->x, amount, color[0]);
    const float green = Lit(base->y, amount, color[1]);
    const float blue = Lit(base->z, amount, color[2]);
    out->x = red;
    out->y = green;
    out->z = blue;
}

void CLight::Create() {}
void CLight::Destroy() { flags &= ~LightFlag::Active; }
const char* CLight::ClassName() { return "CLight"; }
ClassId CLight::GetClassId() { return ClassId::Light; }

// --- LightFactory -------------------------------------------------------------------------------

LightFactory::LightFactory() {
    held = nullptr;
}

LightFactory::~LightFactory() {
    if (held) held->Release();
}

EngineObject* LightFactory::NewObject(int objectType) {
    if (objectType != kLightDefault && objectType != kLightPoint) {
        game::DebugLog(kUnknownLightType, objectType);
        return nullptr;
    }
    // Allocated zeroed: the constructor leaves the transform's positions, and nothing sets a
    // directional light's localPosition (the game's terrain lighting read its z under water, so heap
    // leftovers there, a NaN, darkened the shallow water's sun light by the heap's history).
    auto* light = new (kZeroed) CLight();
    light->type = objectType;
    return light;
}

EngineObject* LightFactory::CreateObject(int newType, int newId) {
    EngineObject* object = BaseManager::CreateObject(newType, newId);
    object->flags &= ~LightFlag::Hidden;
    return object;
}

const char* LightFactory::ClassName() { return "CLightManager"; }
ClassId LightFactory::GetClassId() { return ClassId::LightFactory; }
