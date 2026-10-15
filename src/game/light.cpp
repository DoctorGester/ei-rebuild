// Lights and the light manager (A-H23): CLight's lifetime and setters, CLightFactory
// ("CLightManager") lifetime and object creation. See spell.h and docs/notes/items_spells.md.
#include "spell.h"

#include "core_transform.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/vecmath.h"

#include <cfloat>
#include <cmath>
#include <cstring>

namespace {

constexpr int kLightDefault = 0x2E;
constexpr int kLightPoint = 0x31;
constexpr int kKindPoint = 1;
constexpr int kKindDirectional = 3;
constexpr std::uint32_t kFlagChanged = 0x08;
constexpr std::uint32_t kFlagCreatedLight = 0x80;   // cleared by the manager's CreateObject
constexpr std::uint32_t kFlagDestroyed = 0x20;      // cleared by slot 3

constexpr std::uintptr_t kLightClassName = std::uintptr_t(0x00795A00);          // "CLight"
constexpr std::uintptr_t kLightManagerClassName = std::uintptr_t(0x00795A08);   // "CLightManager"
constexpr std::uintptr_t kUnknownLightType = std::uintptr_t(0x00795A18);        // "... unknown type %d.\n"

// The light constructor through its game address, so its verify wrapper sees the factory's calls.
__declspec(naked) CLight* __fastcall ConstructLightAt(void*) EI_ORIGINAL(0x0068CED0)

} // namespace

// --- CLight --------------------------------------------------------------------------------------

CLight* CLight::Construct() {
    ClearFields();
    localRotation_ = Quaternion{1.0f, 0.0f, 0.0f, 0.0f};
    worldRotation_.SetIdentity();
    ei::SetVtable(this, kVtableAddress);
    std::memset(&kind_, 0, offsetof(CLight, unused_FC) - offsetof(CLight, kind_));
    unused_FC = 0;
    unused_E8 = 1.0f;
    unused_E4 = 1.0f;
    // FSQRT of the double FLT_MAX (00743B48); the main thread's 24-bit precision rounds the root to
    // a float, and its square from there (2^128 - 2^105, one below FLT_MAX).
    const float root = RoundToFloat(std::sqrt(static_cast<double>(FLT_MAX)));
    const double squared = static_cast<double>(root) * root;
    range_ = root;
    rangeSquared_ = RoundToFloat(squared);
    inverseRangeSquared_ = static_cast<float>(1.0 / RoundToFloat(squared));
    return this;
}
EI_REPLACE_METHOD(0x0068CED0, CLight::Construct);

void CLight::Destruct() {
    ei::SetVtable(this, CTransformObject::kVtableAddress);
    DestructBase();
}
EI_REPLACE_METHOD(0x0068CFE0, CLight::Destruct);

CLight* CLight::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0068CFC0, CLight::DeletingDestruct);

BOOL CLight::SetPoint(const Vector3* color, const Vector3* position) {
    kind_ = kKindPoint;
    color_[0] = color->x;
    color_[1] = color->y;
    color_[2] = color->z;
    pointPosition_ = *position;
    position_ = *position;
    flags_ |= kFlagChanged;
    return 1;
}
EI_REPLACE_METHOD(0x0068D010, CLight::SetPoint);

BOOL CLight::SetDirectional(const Vector3* color, const Vector3* direction) {
    kind_ = kKindDirectional;
    color_[0] = color->x;
    color_[1] = color->y;
    color_[2] = color->z;
    direction_ = *direction;
    const Vector3 target = *direction;
    reinterpret_cast<CTransformObject*>(this)->TiltTowards(target);
    flags_ |= kFlagChanged;
    return 1;
}
EI_REPLACE_METHOD(0x0068D0A0, CLight::SetDirectional);

namespace {
// A channel with the light's share added, clamped at 1.
float Lit(float base, float amount, float color) {
    const float sum = amount * color + base;
    return sum > 1.0f ? 1.0f : sum;
}
} // namespace

void CLight::AddDirectional(Vector4* out, float scale, const Vector4* base, const Vector3* normal) const {
    const float facing = -vecmath::Dot<Sum3::ZYX>(direction_, *normal);
    if (!(facing > 0.0f)) {
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
EI_REPLACE_METHOD(0x0068D310, CLight::AddDirectional);

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
EI_REPLACE_METHOD(0x0068D5A0, CLight::AddPoint);

int __fastcall LightCreate(CLight*) { return 1; }
void __fastcall LightDestroy(CLight* light) { light->flags_ &= ~kFlagDestroyed; }
const char* __fastcall LightClassName(CLight*) { return reinterpret_cast<const char*>(kLightClassName); }
int __fastcall LightClassId(CLight*) { return 10; }
EI_REPLACE(0x0068CFF0, LightCreate);
EI_REPLACE(0x0068D000, LightDestroy);
EI_REPLACE(0x0068CFA0, LightClassName);
EI_REPLACE(0x0068CFB0, LightClassId);

// --- CLightFactory -------------------------------------------------------------------------------

CLightFactory* CLightFactory::Construct() {
    ConstructBase();
    held_ = nullptr;
    unused_CC = 0;
    auto* sentinel = static_cast<stlp::list<int>::Node*>(stlp::allocate(sizeof(stlp::list<int>::Node)));
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
    litIds_.head = sentinel;
    std::memset(unused_B0, 0, sizeof(unused_B0));
    ei::SetVtable(this, kVtableAddress);
    unused_D0 = 1;
    return this;
}
EI_REPLACE_METHOD(0x0068D850, CLightFactory::Construct);

void CLightFactory::Destruct() {
    ei::SetVtable(this, kVtableAddress);
    litIds_.ClearWith([](int*) {});
    stlp::deallocate(litIds_.head, sizeof(stlp::list<int>::Node));
    unused_CC = 0;
    if (held_) held_->Release();
    DestructBody();
}
EI_REPLACE_METHOD(0x0068D990, CLightFactory::Destruct);

CLightFactory* CLightFactory::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0068D970, CLightFactory::DeletingDestruct);

CEngineObject* CLightFactory::NewObject(int type) {
    if (type != kLightDefault && type != kLightPoint) {
        game::DebugLog(reinterpret_cast<const char*>(kUnknownLightType), type);
        return nullptr;
    }
    void* memory = gamecrt::operator_new(sizeof(CLight));
    CLight* light = memory ? ConstructLightAt(memory) : nullptr;
    if (!light) return nullptr;
    light->type_ = type;
    return light;
}

CEngineObject* CLightFactory::CreateObject(int type, int id) {
    CEngineObject* object = CBaseManager::CreateObjectImpl(type, id);
    object->flags_ &= ~kFlagCreatedLight;
    return object;
}

namespace {
CEngineObject* __fastcall LightManagerNewObjectHook(CLightFactory* self, void*, int type) {
    return self->CLightFactory::NewObject(type);
}
CEngineObject* __fastcall LightManagerCreateObjectHook(CLightFactory* self, void*, int type, int id) {
    return self->CLightFactory::CreateObject(type, id);
}
} // namespace
EI_REPLACE(0x0068DA40, LightManagerNewObjectHook);
EI_REPLACE(0x0068DAD0, LightManagerCreateObjectHook);

const char* __fastcall LightManagerClassName(CLightFactory*) {
    return reinterpret_cast<const char*>(kLightManagerClassName);
}
int __fastcall LightManagerClassId(CLightFactory*) { return 0x29; }
EI_REPLACE(0x0068D950, LightManagerClassName);
EI_REPLACE(0x0068D960, LightManagerClassId);
