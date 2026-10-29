// Lights and the light manager: CLight's lifetime and setters, CLightFactory ("CLightManager")
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
constexpr std::uint32_t kFlagChanged = 0x08;
constexpr std::uint32_t kFlagCreatedLight = 0x80;   // cleared by the manager's CreateObject
constexpr std::uint32_t kFlagDestroyed = 0x20;   // cleared by Destroy

constexpr const char* kUnknownLightType = "CLightManager::CreateInstant.\n Request for light with unknown type %d.\n";

float Lit(float base, float amount, float color) {
    const float sum = amount * color + base;
    return sum > 1.0f ? 1.0f : sum;
}

} // namespace

// --- CLight --------------------------------------------------------------------------------------

CLight::CLight() {
    ClearFields();
    localRotation_ = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    worldRotation_.SetIdentity();
    std::memset(&kind_, 0, offsetof(CLight, rangeSquared_) - offsetof(CLight, kind_));
    // The root of the double FLT_MAX: the main thread's 24-bit precision rounds the root to
    // a float, and its square from there (2^128 - 2^105, one below FLT_MAX).
    const float root = static_cast<float>(std::sqrt(static_cast<double>(FLT_MAX)));
    const double squared = static_cast<double>(root) * root;
    range_ = root;
    rangeSquared_ = static_cast<float>(squared);
    inverseRangeSquared_ = static_cast<float>(1.0 / static_cast<float>(squared));
}

CLight::~CLight() = default;

void CLight::SetPoint(const Vector3* color, const Vector3* position) {
    kind_ = kKindPoint;
    color_[0] = color->x;
    color_[1] = color->y;
    color_[2] = color->z;
    pointPosition_ = *position;
    localPosition_ = *position;
    flags_ |= kFlagChanged;
}

void CLight::SetDirectional(const Vector3* color, const Vector3* direction) {
    kind_ = kKindDirectional;
    color_[0] = color->x;
    color_[1] = color->y;
    color_[2] = color->z;
    direction_ = *direction;
    const Vector3 target = *direction;
    TiltTowards(target);
    flags_ |= kFlagChanged;
}

void CLight::AddPoint(Vector4* out, float scale, const Vector4* base, const Vector3* normal, const Vector3* toLight,
                      float distance) const {
    const float falloff = 1.0f - distance * inverseRangeSquared_ * distance;
    if (falloff < 0.0f) {
        *out = *base;
        return;
    }
    const float cosine = vecmath::Dot<Sum3::ZYX>(*normal, *toLight);
    const float facing = cosine <= 0.0f ? cosine + 1.0f : 1.0f;
    const float amount = facing * falloff * scale;
    const float red = Lit(base->x, amount, color_[0]);
    const float green = Lit(base->y, amount, color_[1]);
    const float blue = Lit(base->z, amount, color_[2]);
    out->x = red;
    out->y = green;
    out->z = blue;
}

int CLight::Create() { return 1; }
void CLight::Destroy() { flags_ &= ~kFlagDestroyed; }
const char* CLight::ClassName() { return "CLight"; }
ClassId CLight::GetClassId() { return ClassId::Light; }

// --- CLightFactory -------------------------------------------------------------------------------

CLightFactory::CLightFactory() {
    held_ = nullptr;
}

CLightFactory::~CLightFactory() {
    if (held_) held_->Release();
}

CEngineObject* CLightFactory::NewObject(int type) {
    if (type != kLightDefault && type != kLightPoint) {
        game::DebugLog(kUnknownLightType, type);
        return nullptr;
    }
    // Allocated zeroed: the constructor leaves the transform's positions, and nothing sets a
    // directional light's localPosition_; the terrain's sun lighting reads its z (ShadeSun, under water), so
    // heap leftovers there (a NaN) darkened the shallow water's sun light by the heap's history.
    auto* light = new (kZeroed) CLight();
    light->type_ = type;
    return light;
}

CEngineObject* CLightFactory::CreateObject(int type, int id) {
    CEngineObject* object = CBaseManager::CreateObject(type, id);
    object->flags_ &= ~kFlagCreatedLight;
    return object;
}

const char* CLightFactory::ClassName() { return "CLightManager"; }
ClassId CLightFactory::GetClassId() { return ClassId::LightFactory; }
