// Checks for the torch's client half (src/game/torch_client_spec.cpp, A-U11) and the spec's network
// read (0052C390, torch_spec.cpp).
// - Constructor (0052D0D0): the original into a raw copy, ours on the object, bytes +0..+0x60 compared
//   (sound_ stays unwritten); neither allocates.
// - Slots 6 / 11 / 12 (0052D180 / 90 / A0): pure, both run.
// - Everything else makes or drops particles, lights and sounds: ours only, then the fields checked:
//   lifetime (no light, particles or sound), show / hide (the light's hidden bit, the sound), the
//   setters (the new source / light where the torch plus the offset puts it, CheckNear), the network
//   read (the effects exist on a client half afterwards).
// Reached by every zone with torches on the client (zone1's torches: the zone loader, the torch's
// network state each tick, lighting at night); the setters through the network read.
#include "verify.h"

#include "../../src/game/torch_client_spec.h"
#include "../../src/game/effects.h"
#include "../../src/game/map_object.h"
#include "../../src/game/spell.h"

#include <cstdint>
#include <cstring>

namespace {
void* s_unused;
constexpr std::uint32_t kLightHidden = 0x80;
constexpr std::size_t kConstructedBytes = 0x60;   // sound_ (+0x60) is not written

bool Dropped(const CTorchObjectClientSpec* self) { return !self->light_ && !self->particles_; }

bool NearAt(const char* name, const Vector3& actual, const CMapObject* torch, const float* offset) {
    if (!torch) return true;
    ei::verify::CheckNear(name, torch->position.x + offset[0], actual.x, "x");
    ei::verify::CheckNear(name, torch->position.y + offset[1], actual.y, "y");
    ei::verify::CheckNear(name, torch->position.z + offset[2], actual.z, "z");
    return true;
}
} // namespace

static CTorchObjectClientSpec*(__fastcall* s_construct)(void*);
static CTorchObjectClientSpec* __fastcall ConstructVerify(CTorchObjectClientSpec* self) {
    alignas(CTorchObjectClientSpec) unsigned char scratch[sizeof(CTorchObjectClientSpec)];
    std::memcpy(scratch, self, sizeof(scratch));
    void* expected = s_construct(scratch);
    CTorchObjectClientSpec* actual = self->Construct();
    bool ok = expected == scratch && actual == self && std::memcmp(scratch, self, kConstructedBytes) == 0;
    ei::verify::Check("CTorchObjectClientSpec::Construct", true, ok, "%p", static_cast<void*>(self));
    return actual;
}
EI_VERIFY(0x0052D0D0, ConstructVerify, 7, s_construct);

static int(__fastcall* s_update)(CTorchObjectClientSpec*);
static int __fastcall UpdateVerify(CTorchObjectClientSpec* self) {
    int expected = s_update(self);
    ei::verify::Check("CTorchObjectClientSpec::Update", expected, 1, "%d", expected);
    return 1;
}
EI_VERIFY(0x0052D180, UpdateVerify, 5, s_update);

static const char*(__fastcall* s_className)(CTorchObjectClientSpec*);
static const char* __fastcall ClassNameVerify(CTorchObjectClientSpec* self) {
    const char* expected = s_className(self);
    ei::verify::Check("CTorchObjectClientSpec::GetClassName", 0, std::strcmp(expected, "CTorchObjectClientSpecific"),
                      "%s", expected);
    return expected;
}
EI_VERIFY(0x0052D190, ClassNameVerify, 5, s_className);

static int(__fastcall* s_classId)(CTorchObjectClientSpec*);
static int __fastcall ClassIdVerify(CTorchObjectClientSpec* self) {
    int expected = s_classId(self);
    ei::verify::Check("CTorchObjectClientSpec::GetClassId", expected, 0x41, "%X", expected);
    return expected;
}
EI_VERIFY(0x0052D1A0, ClassIdVerify, 5, s_classId);

static void __fastcall DestructVerify(CTorchObjectClientSpec* self) {
    self->DestructClient();
    ei::verify::Check("CTorchObjectClientSpec::DestructClient", true, Dropped(self) && self->sound_ == 0, "");
}
EI_VERIFY(0x0052D1D0, DestructVerify, 0, s_unused);

static CTorchObjectClientSpec* __fastcall DeleteVerify(CTorchObjectClientSpec* self, void*, unsigned flags) {
    CTorchObjectClientSpec* result = self->DeleteClient(flags);
    ei::verify::Check("CTorchObjectClientSpec::DeleteClient", static_cast<void*>(self), static_cast<void*>(result), "");
    return result;
}
EI_VERIFY(0x0052D1B0, DeleteVerify, 0, s_unused);

static int __fastcall CreateVerify(CTorchObjectClientSpec* self) {
    int result = self->CreateClient();
    ei::verify::Check("CTorchObjectClientSpec::CreateClient", true, result == 1 && Dropped(self) && self->sound_ == 0,
                      "");
    return result;
}
EI_VERIFY(0x0052D2A0, CreateVerify, 0, s_unused);

static void __fastcall DestroyVerify(CTorchObjectClientSpec* self) {
    self->DestroyClient();
    ei::verify::Check("CTorchObjectClientSpec::DestroyClient", true, Dropped(self) && self->sound_ == 0, "");
}
EI_VERIFY(0x0052D2E0, DestroyVerify, 0, s_unused);

static void __fastcall ShowVerify(CTorchObjectClientSpec* self) {
    self->ShowEffects();
    bool ok = !self->light_ || !(self->light_->flags_ & kLightHidden);
    ei::verify::Check("CTorchObjectClientSpec::ShowEffects", true, ok, "sound %d", self->sound_);
}
EI_VERIFY(0x0052D580, ShowVerify, 0, s_unused);

static void __fastcall HideVerify(CTorchObjectClientSpec* self) {
    self->HideEffects();
    bool ok = self->sound_ == 0 && (!self->light_ || (self->light_->flags_ & kLightHidden));
    ei::verify::Check("CTorchObjectClientSpec::HideEffects", true, ok, "");
}
EI_VERIFY(0x0052D610, HideVerify, 0, s_unused);

static void __fastcall WarmUpVerify(CTorchObjectClientSpec* self) {
    CParticleSource* before = self->particles_;
    self->WarmUpParticles();
    ei::verify::Check("CTorchObjectClientSpec::WarmUpParticles", static_cast<void*>(before),
                      static_cast<void*>(self->particles_), "");
}
EI_VERIFY(0x0052D3F0, WarmUpVerify, 0, s_unused);

static void __fastcall SetParticlesVerify(CTorchObjectClientSpec* self, void*, std::uint32_t type, const float* offset,
                                          float scale) {
    float kept[3] = {offset[0], offset[1], offset[2]};
    self->SetParticles(type, offset, scale);
    ei::verify::Check("CTorchObjectClientSpec::SetParticles", true, self->particles_ != nullptr && self->lightKind_ == type,
                      "type %X", type);
    if (self->particles_) NearAt("CTorchObjectClientSpec::SetParticles position", self->particles_->position, self->torch_, kept);
}
EI_VERIFY(0x0052D330, SetParticlesVerify, 0, s_unused);

static void __fastcall SetLightVerify(CTorchObjectClientSpec* self, void*, const float* offset, float radius,
                                      const float* colour) {
    float kept[3] = {offset[0], offset[1], offset[2]};
    self->SetLightAt(offset, radius, colour);
    CLight* light = self->light_;
    bool ok = light && light->range_ == radius && !(light->flags_ & kLightHidden);
    ei::verify::Check("CTorchObjectClientSpec::SetLightAt", true, ok, "radius %g", radius);
    if (light) NearAt("CTorchObjectClientSpec::SetLightAt position", light->pointPosition_, self->torch_, kept);
}
EI_VERIFY(0x0052D410, SetLightVerify, 0, s_unused);

static void __fastcall SetSoundVerify(CTorchObjectClientSpec* self, void*, const char* name) {
    char kept[64] = {};
    if (name) {
        std::size_t n = std::strlen(name);
        std::memcpy(kept, name, n < sizeof(kept) - 1 ? n : sizeof(kept) - 1);
    }
    self->SetSound(name);
    ei::verify::Check("CTorchObjectClientSpec::SetSound", 0, std::strncmp(self->name_, kept, sizeof(kept) - 1), "%s",
                      kept);
}
EI_VERIFY(0x0052D520, SetSoundVerify, 0, s_unused);

static void __fastcall ReadNetVerify(CTorchObjectSpec* self, void*, DeltaField* field) {
    self->ReadNetState(field);
    bool client = *reinterpret_cast<const std::uintptr_t*>(self) == CTorchObjectClientSpec::kVtableAddress;
    bool ok = !client || static_cast<CTorchObjectClientSpec*>(self)->light_ != nullptr;
    ei::verify::Check("CTorchObjectSpec::ReadNetState", true, ok, "client %d", client ? 1 : 0);
}
EI_VERIFY(0x0052C390, ReadNetVerify, 0, s_unused);
