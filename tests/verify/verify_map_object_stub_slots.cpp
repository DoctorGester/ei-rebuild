// Checks for map_object_stub_slots.cpp (A-U12).
// Side by side (pure; the original and ours both run): CompanionIsDead (0050E500: every target
// check on a tree, building or other plain object), MapObjectKeyId (0050F0E0) and
// MapObjectScaleEffectChanged (0050E960: an effect added to a plain object), MapObjectMoveSpeed /
// MapObjectRadius (0050E540 / 0050E550: units walking around plain objects, the AI's distances).
// Ours only:
// - The empty slots (results checked where there is one): plain objects ticking, drawn and hit
//   in any zone. The one-byte ones cannot be hooked and are never reached through a wrapper.
// - DeleteMapObject (0050E560): a plain object dropped (zone unload).
// - NotifyCompanion (0050E930): an object leaving the sector map.
// - TakePrototype (0050E9B0): a plain object given its prototype (zone load); it only runs (the
//   vitals it packs come from a local the original leaves unset as well).
// - WriteFullState / WriteChangedState / ReadState (0050EA40, 0050EA70, 0050EAB0): the server
//   sending objects to its players and the client taking them (every zone load, every change).
// - SetSecondName (0050F150): the client's new objects.
// - SaveTransformSlot / LoadTransformSlot (00516350 / 00516360): no scenario known (the objects'
//   own Save / Load write the transform themselves).
#include "verify.h"

#include "../../src/game/map_object.h"
#include "../../src/game/net_delta.h"
#include "../../src/game/unit.h"

#include <cstring>

int __fastcall MapObjectZero(CMapObject*);
int __fastcall MapObjectKeyId(CMapObject*);
float __fastcall MapObjectMoveSpeed(CMapObject*);
float __fastcall MapObjectRadius(CMapObject*);

namespace {
void* s_unused;
} // namespace

static void __fastcall IgnoreVerify(CMapObject* self, void*, int value) {
    self->IgnoreArgument(value);
    ei::verify::Check("CMapObject empty slot (one argument)", true, true, "");
}
EI_VERIFY(0x0050E410, IgnoreVerify, 0, s_unused);
EI_VERIFY(0x0050E480, IgnoreVerify, 0, s_unused);
EI_VERIFY(0x0050E4A0, IgnoreVerify, 0, s_unused);
EI_VERIFY(0x0050E510, IgnoreVerify, 0, s_unused);

static int __fastcall ZeroVerify(CMapObject* self) {
    int result = MapObjectZero(self);
    ei::verify::Check("CMapObject zero slot", 0, result, "");
    return result;
}
EI_VERIFY(0x0050F0C0, ZeroVerify, 0, s_unused);
EI_VERIFY(0x0050F0D0, ZeroVerify, 0, s_unused);
EI_VERIFY(0x0050E4F0, ZeroVerify, 0, s_unused);

static int __fastcall AcceptsKeyVerify(CMapObject* self, void*, const void* key) {
    int result = self->AcceptsNoKey(key);
    ei::verify::Check("CMapObject::AcceptsKey", 0, result, "");
    return result;
}
EI_VERIFY(0x0050F110, AcceptsKeyVerify, 0, s_unused);

static bool __fastcall AcceptsAnimationVerify(CMapObject* self, void*, const void* animation) {
    bool result = self->AcceptsAnyAnimation(animation);
    ei::verify::Check("CMapObject::AcceptsAnimation", true, result, "");
    return result;
}
EI_VERIFY(0x0050E400, AcceptsAnimationVerify, 0, s_unused);

// 0050E960: MOV EAX,1 (5 bytes).
static int(__fastcall* s_scaleEffectChanged)(CMapObject*, void*, int);
static int __fastcall ScaleEffectChangedVerify(CMapObject* self, void*, int scaleEffect) {
    int expected = s_scaleEffectChanged(self, nullptr, scaleEffect);
    ei::verify::Check("CMapObject::ScaleEffectChanged", expected, self->ScaleEffectChangedDefault(scaleEffect),
                      "");
    return expected;
}
EI_VERIFY(0x0050E960, ScaleEffectChangedVerify, 5, s_scaleEffectChanged);

// 0050F0E0: MOV EAX,0xFFFFFF85 (5 bytes).
static int(__fastcall* s_keyId)(CMapObject*);
static int __fastcall KeyIdVerify(CMapObject* self) {
    int expected = s_keyId(self);
    ei::verify::Check("CMapObject::KeyId", expected, MapObjectKeyId(self), "");
    return expected;
}
EI_VERIFY(0x0050F0E0, KeyIdVerify, 5, s_keyId);

// 0050E540 / 0050E550: FLD dword ptr [constant] (6 bytes).
static float(__fastcall* s_moveSpeed)(CMapObject*);
static float __fastcall MoveSpeedVerify(CMapObject* self) {
    float expected = s_moveSpeed(self);
    ei::verify::Check("CMapObject::MoveSpeed", expected, MapObjectMoveSpeed(self), "");
    return expected;
}
EI_VERIFY(0x0050E540, MoveSpeedVerify, 6, s_moveSpeed);

static float(__fastcall* s_radius)(CMapObject*);
static float __fastcall RadiusVerify(CMapObject* self) {
    float expected = s_radius(self);
    ei::verify::Check("CMapObject::Radius", expected, MapObjectRadius(self), "");
    return expected;
}
EI_VERIFY(0x0050E550, RadiusVerify, 6, s_radius);

static CMapObject* __fastcall DeleteVerify(CMapObject* self, void*, unsigned flags) {
    CMapObject* result = self->DeleteMapObject(flags);
    ei::verify::Check("CMapObject::DeleteMapObject", static_cast<void*>(self), static_cast<void*>(result), "flags %u",
                      flags);
    return result;
}
EI_VERIFY(0x0050E560, DeleteVerify, 0, s_unused);

// 0050E500: MOV ECX,[ECX+0x170] (6 bytes).
static int(__fastcall* s_companionIsDead)(CMapObject*);
static int __fastcall CompanionIsDeadVerify(CMapObject* self) {
    int expected = s_companionIsDead(self);
    ei::verify::Check("CMapObject::CompanionIsDead", expected, self->CompanionIsDead(), "");
    return expected;
}
EI_VERIFY(0x0050E500, CompanionIsDeadVerify, 6, s_companionIsDead);

static int __fastcall NotifyCompanionVerify(CMapObject* self) {
    int result = self->NotifyCompanion();
    ei::verify::Check("CMapObject::NotifyCompanion", 1, result, "");
    return result;
}
EI_VERIFY(0x0050E930, NotifyCompanionVerify, 0, s_unused);

static void __fastcall TakePrototypeVerify(CMapObject* self, void*, const char* prototype) {
    self->CMapObject::TakePrototype(prototype);
    ei::verify::Check("CMapObject::TakePrototype", true, true, "");
}
EI_VERIFY(0x0050E9B0, TakePrototypeVerify, 0, s_unused);

static void __fastcall WriteFullStateVerify(CMapObject* self) {
    self->WriteFullState();
    ei::verify::Check("CMapObject::WriteFullState", true,
                      self->NetState()->mode_ == DeltaState::kIdle && (self->flags_ & 2) == 0, "mode %d",
                      self->NetState()->mode_);
}
EI_VERIFY(0x0050EA40, WriteFullStateVerify, 0, s_unused);

static void __fastcall WriteChangedStateVerify(CMapObject* self) {
    self->WriteChangedState();
    ei::verify::Check("CMapObject::WriteChangedState", true,
                      self->NetState()->mode_ == DeltaState::kIdle && (self->flags_ & 2) == 0, "mode %d",
                      self->NetState()->mode_);
}
EI_VERIFY(0x0050EA70, WriteChangedStateVerify, 0, s_unused);

static std::uint32_t __fastcall ReadStateVerify(CMapObject* self, void*, int first, CMemoryStream* reader) {
    std::uint32_t result = self->ReadState(first, reader);
    ei::verify::Check("CMapObject::ReadState", static_cast<int>(DeltaState::kIdle), self->NetState()->mode_, "");
    return result;
}
EI_VERIFY(0x0050EAB0, ReadStateVerify, 0, s_unused);

static mfc::CString* __fastcall SetSecondNameVerify(CMapObject* self, void*, const char* text) {
    mfc::CString* result = self->SetSecondName(text);
    bool ok = static_cast<void*>(result) == static_cast<void*>(&self->name2) &&
              std::strcmp(self->name2, text ? text : "") == 0;
    ei::verify::Check("CMapObject::SetSecondName", true, ok, "%s", text ? text : "(null)");
    return result;
}
EI_VERIFY(0x0050F150, SetSecondNameVerify, 0, s_unused);

static void __fastcall SaveTransformVerify(CMapObject* self, void*, void* file) {
    self->SaveTransformSlot(file);
    ei::verify::Check("CMapObject::SaveTransformSlot", true, true, "");
}
EI_VERIFY(0x00516350, SaveTransformVerify, 0, s_unused);

static void __fastcall LoadTransformVerify(CMapObject* self, void*, void* file) {
    self->LoadTransformSlot(file);
    ei::verify::Check("CMapObject::LoadTransformSlot", true, true, "");
}
EI_VERIFY(0x00516360, LoadTransformVerify, 0, s_unused);
