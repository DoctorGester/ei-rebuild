// Checks for the torch spec's slots (src/game/torch_spec.cpp, A-U6). The setters 16 / 17 only write
// fields: stateful (ours, the spec's bytes kept and restored, the original, compared). Save writes
// both into capture streams (compared, the original's bytes forwarded). Create, Destroy, Load and
// SetName (string allocation) run ours only and check the fields. Reached when a zone with torches
// is built (Create, the setters from the zone loader), saved and loaded.
#include "verify.h"
#include "unit_capture_stream.h"

#include "../../src/game/server_specific_manager.h"

#include <cstring>

namespace {
using unitverify::CaptureStream;
void* s_unused;
void(__fastcall* s_setLight)(CTorchObjectSpec*, void*, std::uint32_t, const float*, float);
void(__fastcall* s_setFlame)(CTorchObjectSpec*, void*, const float*, float, const float*);
void(__fastcall* s_save)(CTorchObjectSpec*, void*, CStream*);

struct SpecBytes {
    unsigned char bytes[sizeof(CTorchObjectSpec)];
    void Save(const CTorchObjectSpec* spec) { std::memcpy(bytes, spec, sizeof(bytes)); }
    void Restore(CTorchObjectSpec* spec) const { std::memcpy(static_cast<void*>(spec), bytes, sizeof(bytes)); }
    bool Same(const CTorchObjectSpec* spec) const { return std::memcmp(bytes, spec, sizeof(bytes)) == 0; }
};
} // namespace

// 0052C590: MOV EAX,[ESP+4]; PUSH ESI (5 bytes).
static void __fastcall SetLightVerify(CTorchObjectSpec* self, void*, std::uint32_t kind, const float* colour, float value) {
    SpecBytes before, ours;
    before.Save(self);
    self->SetLight(kind, colour, value);
    ours.Save(self);
    before.Restore(self);
    s_setLight(self, nullptr, kind, colour, value);
    ei::verify::Check("CTorchObjectSpec::SetLight", true, ours.Same(self), "kind %u", kind);
}
EI_VERIFY(0x0052C590, SetLightVerify, 5, s_setLight);

// 0052C5C0: MOV EDX,[ESP+4]; PUSH ESI (5 bytes).
static void __fastcall SetFlameVerify(CTorchObjectSpec* self, void*, const float* colour, float value, const float* values) {
    SpecBytes before, ours;
    before.Save(self);
    self->SetFlame(colour, value, values);
    ours.Save(self);
    before.Restore(self);
    s_setFlame(self, nullptr, colour, value, values);
    ei::verify::Check("CTorchObjectSpec::SetFlame", true, ours.Same(self), "");
}
EI_VERIFY(0x0052C5C0, SetFlameVerify, 5, s_setFlame);

// 0052C500: PUSH EBX; PUSH EBP; MOV EBX,ECX; PUSH ESI (5 bytes).
static void __fastcall SpecSaveVerify(CTorchObjectSpec* self, void*, CStream* stream) {
    CaptureStream expected(stream), actual(stream);
    s_save(self, nullptr, &expected);
    self->SaveSpec(&actual);
    ei::verify::Check("CTorchObjectSpec::SaveSpec", true, expected.Same(actual), "%u vs %u bytes, first difference at %d",
                      static_cast<unsigned>(expected.bytes.size()), static_cast<unsigned>(actual.bytes.size()),
                      expected.FirstDifference(actual));
    expected.Forward(stream);
}
EI_VERIFY(0x0052C500, SpecSaveVerify, 5, s_save);

static int __fastcall SpecCreateVerify(CTorchObjectSpec* self) {
    int result = self->CreateSpec();
    bool ok = result == 1 && self->torch_ == nullptr && self->lightKind_ == 0 && static_cast<const char*>(self->name_)[0] == 0 &&
              self->values_[1] == 10.0f && self->values_[0] == 1.0f && self->values_[5] == 1.0f;
    ei::verify::Check("CTorchObjectSpec::CreateSpec", true, ok, "");
    return result;
}
EI_VERIFY(0x0052C610, SpecCreateVerify, 0, s_unused);

static void __fastcall SpecDestroyVerify(CTorchObjectSpec* self) {
    self->DestroySpec();
    ei::verify::Check("CTorchObjectSpec::DestroySpec", true, self->torch_ == nullptr && static_cast<const char*>(self->name_)[0] == 0, "");
}
EI_VERIFY(0x0052C680, SpecDestroyVerify, 0, s_unused);

static void __fastcall SpecLoadVerify(CTorchObjectSpec* self, void*, CStream* stream) {
    self->LoadSpec(stream);
    ei::verify::Check("CTorchObjectSpec::LoadSpec", true, true, "");
}
EI_VERIFY(0x0052C490, SpecLoadVerify, 0, s_unused);

static void __fastcall SpecSetNameVerify(CTorchObjectSpec* self, void*, const char* name) {
    self->SetName(name);
    const char* now = self->name_;
    ei::verify::Check("CTorchObjectSpec::SetName", true, std::strcmp(now, name) == 0, "");
}
EI_VERIFY(0x0052C600, SpecSetNameVerify, 0, s_unused);
