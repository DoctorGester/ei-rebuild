// Checks for the torch object (src/game/torch_object.cpp, A-U6). Class name / id and the update
// are pure (both run); Save writes both into capture streams (compared, the original's bytes
// forwarded); the rest runs ours only and checks the spec link. Reached when a zone with torches
// (flame records) is built, saved and loaded: world_load.txt / zone scenarios, magic_traps.txt and
// levers.txt (save and load) if zone1 has any.
#include "verify.h"
#include "unit_capture_stream.h"

#include "../../src/game/server_specific_manager.h"
#include "../../src/game/torch.h"

namespace {
using unitverify::CaptureStream;
void* s_unused;
const char*(__fastcall* s_className)(CTorchObject*);
int(__fastcall* s_classId)(CTorchObject*);
int(__fastcall* s_update)(CTorchObject*);
void(__fastcall* s_save)(CTorchObject*, void*, UnitStream*);
CStream* AsStream(UnitStream* stream) { return reinterpret_cast<CStream*>(stream); }
} // namespace

// 0052BF20 / 0052BF30 / 0052BF10: MOV EAX,imm32 (5 bytes).
static const char* __fastcall TorchClassNameVerify(CTorchObject* self) {
    const char* expected = s_className(self);
    ei::verify::Check("TorchClassName", expected, TorchClassName(self), "");
    return expected;
}
EI_VERIFY(0x0052BF20, TorchClassNameVerify, 5, s_className);

static int __fastcall TorchClassIdVerify(CTorchObject* self) {
    int expected = s_classId(self);
    ei::verify::Check("TorchClassId", expected, TorchClassId(self), "");
    return expected;
}
EI_VERIFY(0x0052BF30, TorchClassIdVerify, 5, s_classId);

static int __fastcall TorchUpdateVerify(CTorchObject* self) {
    int expected = s_update(self);
    ei::verify::Check("CTorchObject::UpdateTorch", expected, self->UpdateTorch(), "");
    return expected;
}
EI_VERIFY(0x0052BF10, TorchUpdateVerify, 5, s_update);

// 0052BF00: MOV AL,1; RET (3 bytes): ours only.
static bool __fastcall TorchSlot29Verify(CTorchObject* self) {
    bool actual = self->Slot29True();
    ei::verify::Check("CTorchObject::Slot29True", true, actual, "");
    return actual;
}
EI_VERIFY(0x0052BF00, TorchSlot29Verify, 0, s_unused);

// 0052C2A0: PUSH ESI; PUSH EDI; MOV EDI,[ESP+0xC] (6 bytes).
static void __fastcall TorchSaveVerify(CTorchObject* self, void*, UnitStream* stream) {
    CaptureStream expected(stream), actual(stream);
    s_save(self, nullptr, &expected);
    self->SaveTorch(AsStream(&actual));
    ei::verify::Check("CTorchObject::SaveTorch", true, expected.Same(actual), "torch %d: %u vs %u bytes, first difference at %d",
                      self->id_, static_cast<unsigned>(expected.bytes.size()), static_cast<unsigned>(actual.bytes.size()),
                      expected.FirstDifference(actual));
    expected.Forward(stream);
}
EI_VERIFY(0x0052C2A0, TorchSaveVerify, 6, s_save);

static CTorchObject* __fastcall TorchConstructVerify(CTorchObject* self) {
    CTorchObject* result = self->Construct();
    ei::verify::Check("CTorchObject::Construct", true, result == self && !self->spec, "");
    return result;
}
EI_VERIFY(0x0052BEE0, TorchConstructVerify, 0, s_unused);

static void __fastcall TorchDestructVerify(CTorchObject* self) {
    self->DestructBody();
    ei::verify::Check("CTorchObject::DestructBody", true, true, "");
}
EI_VERIFY(0x0052BF60, TorchDestructVerify, 0, s_unused);

static CTorchObject* __fastcall TorchDeleteVerify(CTorchObject* self, void*, int flags) {
    CTorchObject* result = self->DeletingDestruct(flags);
    ei::verify::Check("CTorchObject::DeletingDestruct", static_cast<void*>(self), static_cast<void*>(result), "flags %d", flags);
    return result;
}
EI_VERIFY(0x0052BF40, TorchDeleteVerify, 0, s_unused);

static void __fastcall TorchDestroyVerify(CTorchObject* self) {
    self->DestroyTorch();
    ei::verify::Check("CTorchObject::DestroyTorch", true, !self->spec, "torch %d", self->id_);
}
EI_VERIFY(0x0052C150, TorchDestroyVerify, 0, s_unused);

static void __fastcall TorchLoadVerify(CTorchObject* self, void*, CStream* stream) {
    self->LoadTorch(stream);
    ei::verify::Check("CTorchObject::LoadTorch", true, self->spec != nullptr, "torch %d", self->id_);
}
EI_VERIFY(0x0052C280, TorchLoadVerify, 0, s_unused);

// A spec becomes the torch's and points back at it.
static std::uintptr_t __fastcall TorchAttachVerify(CTorchObject* self, void*, CEngineObject* object) {
    bool spec = object->type_ == 0x4B;
    std::uintptr_t result = self->AttachTorchObject(object);
    bool ok = !spec || (self->spec == static_cast<CEngineObject*>(object) &&
                        self->spec->unknown_1C == reinterpret_cast<std::uintptr_t>(self) &&
                        result == reinterpret_cast<std::uintptr_t>(object));
    ei::verify::Check("CTorchObject::AttachTorchObject", true, ok, "torch %d type %d", self->id_, object->type_);
    return result;
}
EI_VERIFY(0x0052C2C0, TorchAttachVerify, 0, s_unused);

static void __fastcall TorchWriteNetVerify(CTorchObject* self) {
    CTorchObjectSpec* spec = self->spec;
    self->WriteTorchNetState();
    ei::verify::Check("CTorchObject::WriteTorchNetState", true, self->spec == spec, "torch %d", self->id_);
}
EI_VERIFY(0x0052C180, TorchWriteNetVerify, 0, s_unused);

static std::uint32_t __fastcall TorchReadNetVerify(CTorchObject* self, void*, int first) {
    std::uint32_t result = self->ReadTorchNetState(first);
    ei::verify::Check("CTorchObject::ReadTorchNetState", true, result <= 1, "torch %d: %u", self->id_, result);
    return result;
}
EI_VERIFY(0x0052C220, TorchReadNetVerify, 0, s_unused);
