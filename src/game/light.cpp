// Lights and the light manager (A-H23): CLight's lifetime and setters, CLightFactory
// ("CLightManager") lifetime and object creation. See spell.h and docs/notes/items_spells.md.
#include "spell.h"

#include "core_transform.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/vecmath.h"
#include "lib/nan_audit.h"

#include <cfloat>
#include <cmath>
#include <cstring>
#include <new>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {

constexpr int kLightDefault = 0x2E;
constexpr int kLightPoint = 0x31;
constexpr int kKindPoint = 1;
constexpr int kKindDirectional = 3;
constexpr std::uint32_t kFlagChanged = 0x08;
constexpr std::uint32_t kFlagCreatedLight = 0x80;   // cleared by the manager's CreateObject
constexpr std::uint32_t kFlagDestroyed = 0x20;   // cleared by Destroy

constexpr const char* kUnknownLightType = "CLightManager::CreateInstant.\n Request for light with unknown type %d.\n";

} // namespace

// --- CLight --------------------------------------------------------------------------------------

CLight::CLight() {
    ClearFields();
    localRotation_ = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    worldRotation_.SetIdentity();
    std::memset(&kind_, 0, offsetof(CLight, unused_FC) - offsetof(CLight, kind_));
    unused_FC = 0;
    unused_E8 = 1.0f;
    unused_E4 = 1.0f;
    // FSQRT of the double FLT_MAX; the main thread's 24-bit precision rounds the root to
    // a float, and its square from there (2^128 - 2^105, one below FLT_MAX).
    const float root = static_cast<float>(std::sqrt(static_cast<double>(FLT_MAX)));
    const double squared = static_cast<double>(root) * root;
    range_ = root;
    rangeSquared_ = static_cast<float>(squared);
    inverseRangeSquared_ = static_cast<float>(1.0 / static_cast<float>(squared));
}
EI_REWRITTEN_SPECIAL(0x0068CED0, CLight::CLight);

CLight::~CLight() = default;
EI_REWRITTEN_SPECIAL(0x0068CFE0, CLight::~CLight);
EI_REWRITTEN_SPECIAL(0x0068CFC0, CLight::~CLight);

BOOL CLight::SetPoint(const Vector3* color, const Vector3* position) {
    kind_ = kKindPoint;
    color_[0] = color->x;
    color_[1] = color->y;
    color_[2] = color->z;
    pointPosition_ = *position;
    localPosition_ = *position;
    flags_ |= kFlagChanged;
    return 1;
}
EI_REWRITTEN(0x0068D010, CLight::SetPoint);

BOOL CLight::SetDirectional(const Vector3* color, const Vector3* direction) {
    kind_ = kKindDirectional;
    color_[0] = color->x;
    color_[1] = color->y;
    color_[2] = color->z;
    direction_ = *direction;
    const Vector3 target = *direction;
    TiltTowards(target);
    flags_ |= kFlagChanged;
    return 1;
}
EI_REWRITTEN(0x0068D0A0, CLight::SetDirectional);

namespace {
// A channel with the light's share added, clamped at 1.
float Lit(float base, float amount, float color) {
    const float sum = amount * color + base;
    return sum > 1.0f ? 1.0f : sum;
}
} // namespace

void CLight::AddDirectional(Vector4* out, float scale, const Vector4* base, const Vector3* normal) const {
    const float facing = -vecmath::Dot<Sum3::ZYX>(direction_, *normal);
    EI_NAN_AUDIT(facing);
    if (facing <= 0.0f) {
        *out = *base;
        return;
    }
    const float amount = facing * scale;
    const float red = Lit(base->x, amount, color_[0]);
    const float green = Lit(base->y, amount, color_[1]);
    const float blue = Lit(base->z, amount, color_[2]);
    out->x = red;
    out->y = green;
    out->z = blue;
}
EI_REWRITTEN(0x0068D310, CLight::AddDirectional);

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
EI_REWRITTEN(0x0068D5A0, CLight::AddPoint);

int CLight::Create() { return 1; }
void CLight::Destroy() { flags_ &= ~kFlagDestroyed; }
const char* CLight::GetClassName() { return "CLight"; }
int CLight::GetClassId() { return 10; }
EI_REWRITTEN(0x0068CFF0, CLight::Create);
EI_REWRITTEN(0x0068D000, CLight::Destroy);
EI_REWRITTEN(0x0068CFA0, CLight::GetClassName);
EI_REWRITTEN(0x0068CFB0, CLight::GetClassId);

// --- CLightFactory -------------------------------------------------------------------------------

CLightFactory::CLightFactory() {
    held_ = nullptr;
    unused_CC = 0;
    auto* sentinel = static_cast<stlp::list<int>::Node*>(stlp::allocate(sizeof(stlp::list<int>::Node)));
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
    litIds_.head = sentinel;
    std::memset(unused_B0, 0, sizeof(unused_B0));
    unused_D0 = 1;
}
EI_REWRITTEN_SPECIAL(0x0068D850, CLightFactory::CLightFactory);

CLightFactory::~CLightFactory() {
    litIds_.ClearWith([](int*) {});
    stlp::deallocate(litIds_.head, sizeof(stlp::list<int>::Node));
    unused_CC = 0;
    if (held_) held_->Release();
}
EI_REWRITTEN_SPECIAL(0x0068D990, CLightFactory::~CLightFactory);
EI_REWRITTEN_SPECIAL(0x0068D970, CLightFactory::~CLightFactory);

CEngineObject* CLightFactory::NewObject(int type) {
    if (type != kLightDefault && type != kLightPoint) {
        game::DebugLog(kUnknownLightType, type);
        return nullptr;
    }
    // Allocated zeroed: the constructor leaves the transform's positions, and nothing sets a
    // directional light's localPosition_; the terrain's sun lighting reads its z (ShadeSun, under water), so
    // heap leftovers there (a NaN) darkened the shallow water's sun light by the heap's history.
    void* memory = gamecrt::operator_new_zeroed(sizeof(CLight));
    if (!memory) return nullptr;
    CLight* light = new (memory) CLight();
    light->type_ = type;
    return light;
}
EI_REWRITTEN(0x0068DA40, CLightFactory::NewObject);

CEngineObject* CLightFactory::CreateObject(int type, int id) {
    CEngineObject* object = CBaseManager::CreateObject(type, id);
    object->flags_ &= ~kFlagCreatedLight;
    return object;
}
EI_REWRITTEN(0x0068DAD0, CLightFactory::CreateObject);

const char* CLightFactory::GetClassName() { return "CLightManager"; }
int CLightFactory::GetClassId() { return 0x29; }
EI_REWRITTEN(0x0068D950, CLightFactory::GetClassName);
EI_REWRITTEN(0x0068D960, CLightFactory::GetClassId);
