// Checks for the placed traps and the magic trap's slots (src/game/trap_object.cpp, A-U6).
// - Constants (class names / ids, slot 38, 00522AD0): pure, both run.
// - Arm and Disarm (they only change flags_): stateful; ours, flags_ kept and restored, the
//   original, results and flags_ compared.
// - Save: the original and ours into capture streams, compared.
// - Constructor, Create, destructors, Load, the update (it may run a world script), slots 29 / 42
//   (bodies too short or starting with a call): ours only.
// The class slots, constructors and destructors run when a zone with these objects is built or
// left (magic_traps.txt for the magic trap); the placed trap only when a trap item is used (no
// scenario yet).
#include "verify.h"
#include "unit_capture_stream.h"

#include "../../src/game/magic_trap.h"
#include "../../src/game/trap_object.h"
#include "../../src/game/unit.h"

namespace {
using unitverify::CaptureStream;
void* s_unused;
using NameFn = const char*(__fastcall*)(CMapObject*);
using IdFn = int(__fastcall*)(CMapObject*);
NameFn s_trapName, s_magicName;
IdFn s_trapId, s_magicId;
int(__fastcall* s_slot38)(CTrapObject*);
BOOL(__fastcall* s_accepted)(const LockKey*);
void(__fastcall* s_arm)(CTrapObject*);
bool(__fastcall* s_disarm)(CTrapObject*, void*, const LockKey*, int);
void(__fastcall* s_save)(CTrapObject*, void*, CStream*);
constexpr std::uint32_t kArmed = 0x80000;
} // namespace

static const char* __fastcall TrapNameVerify(CMapObject* s) {
    const char* e = s_trapName(s);
    ei::verify::Check("TrapClassName", e, TrapClassName(s), "");
    return e;
}
EI_VERIFY(0x00529B00, TrapNameVerify, 5, s_trapName);
static int __fastcall TrapIdVerify(CMapObject* s) {
    int e = s_trapId(s);
    ei::verify::Check("TrapClassId", e, TrapClassId(s), "");
    return e;
}
EI_VERIFY(0x00529B10, TrapIdVerify, 5, s_trapId);
static const char* __fastcall MagicNameVerify(CMapObject* s) {
    const char* e = s_magicName(s);
    ei::verify::Check("MagicTrapClassName", e, MagicTrapClassName(s), "");
    return e;
}
EI_VERIFY(0x00529F50, MagicNameVerify, 5, s_magicName);
static int __fastcall MagicIdVerify(CMapObject* s) {
    int e = s_magicId(s);
    ei::verify::Check("MagicTrapClassId", e, MagicTrapClassId(s), "");
    return e;
}
EI_VERIFY(0x00529F60, MagicIdVerify, 5, s_magicId);

// 00529AF0 / 00522AD0: MOV EAX,1 (5 bytes).
static int __fastcall Slot38Verify(CTrapObject* self) {
    int e = s_slot38(self);
    ei::verify::Check("CTrapObject::Slot38One", e, self->Slot38One(), "");
    return e;
}
EI_VERIFY(0x00529AF0, Slot38Verify, 5, s_slot38);
static BOOL __fastcall AcceptedVerify(const LockKey* key) {
    BOOL e = s_accepted(key);
    ei::verify::Check("LockKey::AcceptedByTrap", e, key->AcceptedByTrap(), "");
    return e;
}
EI_VERIFY(0x00522AD0, AcceptedVerify, 5, s_accepted);

// 00529D20: OR dword ptr [ECX+8],0x180000 (7 bytes).
static void __fastcall ArmVerify(CTrapObject* self) {
    std::uint32_t before = self->flags_;
    self->Arm();
    std::uint32_t ours = self->flags_;
    self->flags_ = before;
    s_arm(self);
    ei::verify::Check("CTrapObject::Arm", self->flags_, ours, "");
}
EI_VERIFY(0x00529D20, ArmVerify, 7, s_arm);

// 00529D30: PUSH ESI; MOV ESI,ECX; TEST dword ptr [ESI+8],0x80000 (10 bytes).
static bool __fastcall DisarmVerify(CTrapObject* self, void*, const LockKey* key, int unused) {
    std::uint32_t before = self->flags_;
    bool actual = self->Disarm(key, unused);
    std::uint32_t ours = self->flags_;
    self->flags_ = before;
    bool expected = s_disarm(self, nullptr, key, unused);
    ei::verify::Check("CTrapObject::Disarm", true, expected == actual && self->flags_ == ours, "flags %08X", before);
    return expected;
}
EI_VERIFY(0x00529D30, DisarmVerify, 10, s_disarm);

// 00529DA0: PUSH ESI; PUSH EDI; MOV EDI,[ESP+0xC] (6 bytes).
static void __fastcall TrapSaveVerify(CTrapObject* self, void*, CStream* stream) {
    CaptureStream expected(stream), actual(stream);
    s_save(self, nullptr, &expected);
    self->SaveTrapObject(&actual);
    ei::verify::Check("CTrapObject::SaveTrapObject", true, expected.Same(actual), "first difference at %d",
                      expected.FirstDifference(actual));
    expected.Forward(stream);
}
EI_VERIFY(0x00529DA0, TrapSaveVerify, 6, s_save);

static CTrapObject* __fastcall TrapConstructVerify(CTrapObject* self) {
    CTrapObject* result = self->Construct();
    ei::verify::Check("CTrapObject::Construct", true, result == self && !(self->flags_ & 0x40), "");
    return result;
}
EI_VERIFY(0x00529A80, TrapConstructVerify, 0, s_unused);

static int __fastcall TrapCreateVerify(CTrapObject* self) {
    int result = self->CreateTrap();
    ei::verify::Check("CTrapObject::CreateTrap", true, result == 1 && !(self->flags_ & 0x40), "");
    return result;
}
EI_VERIFY(0x00529B60, TrapCreateVerify, 0, s_unused);

static CTrapObject* __fastcall TrapDeleteVerify(CTrapObject* self, void*, int flags) {
    CTrapObject* result = self->DeletingDestruct(flags);
    ei::verify::Check("CTrapObject::DeletingDestruct", static_cast<void*>(self), static_cast<void*>(result), "");
    return result;
}
EI_VERIFY(0x00529B30, TrapDeleteVerify, 0, s_unused);

static MagicTrap* __fastcall MagicDeleteVerify(MagicTrap* self, void*, int flags) {
    MagicTrap* result = self->DeletingDestruct(flags);
    ei::verify::Check("MagicTrap::DeletingDestruct", static_cast<void*>(self), static_cast<void*>(result), "");
    return result;
}
EI_VERIFY(0x00529F70, MagicDeleteVerify, 0, s_unused);

static void __fastcall TrapLoadVerify(CTrapObject* self, void*, CStream* stream) {
    self->LoadTrapObject(stream);
    ei::verify::Check("CTrapObject::LoadTrapObject", true, true, "");
}
EI_VERIFY(0x00529D70, TrapLoadVerify, 0, s_unused);

// The update returns 0; an armed trap that was set off is disarmed.
static int __fastcall TrapUpdateVerify(CTrapObject* self) {
    int result = self->UpdateTrap();
    ei::verify::Check("CTrapObject::UpdateTrap", 0, result, "");
    return result;
}
EI_VERIFY(0x00529B80, TrapUpdateVerify, 0, s_unused);

static bool __fastcall TrapSlot29Verify(CTrapObject* self) {
    bool r = self->Slot29True();
    ei::verify::Check("CTrapObject::Slot29True", true, r, "");
    return r;
}
EI_VERIFY(0x00529AE0, TrapSlot29Verify, 0, s_unused);

static int __fastcall TrapKeyFitsVerify(CTrapObject* self, void*, const LockKey* key) {
    int r = self->KeyFits(key);
    ei::verify::Check("CTrapObject::KeyFits", 1, r, "");
    return r;
}
EI_VERIFY(0x00529D60, TrapKeyFitsVerify, 0, s_unused);
