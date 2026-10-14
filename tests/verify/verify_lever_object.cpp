// Checks for the lever object (src/game/lever_object.cpp, A-U6).
// - Constants and readers (class name / id, slots 29, 38-40, 42, 43, 60): pure, both run (slot 29's
//   body is 3 bytes: ours only, against its constant).
// - SetUp, Enable, SetLock (they write the lever and MarkChanged): stateful; ours, the lever's bytes
//   kept and restored, the original, both results compared.
// - Save: both into capture streams, compared; the original's bytes go on to the real stream.
// - Constructor, Create, destructors, Load, the update, the network state and ApplyKey: ours only,
//   then the fields they set are checked.
// Reached by levers.txt (zone1's gate "VGate", id 1889: built and set up by the zone loader,
// switched, disabled and enabled, given a lock, saved and loaded) and every scenario that starts
// zone1 (constructor, Create, SetUp, the update and the network state each tick).
#include "verify.h"
#include "unit_capture_stream.h"

#include "../../src/game/lever.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {
using unitverify::CaptureStream;
void* s_unused;

struct LeverBytes {
    std::uint8_t bytes[sizeof(Lever)];
    void Save(const Lever* lever) { std::memcpy(bytes, lever, sizeof(bytes)); }
    void Restore(Lever* lever) const { std::memcpy(static_cast<void*>(lever), bytes, sizeof(bytes)); }
    int Difference(const Lever* lever) const {
        const auto* p = reinterpret_cast<const std::uint8_t*>(lever);
        for (int i = 0; i < static_cast<int>(sizeof(bytes)); ++i)
            if (p[i] != bytes[i]) return i;
        return -1;
    }
};

bool Reset(const Lever* lever) {
    return lever->lock.mask == 1 && lever->lock.id == 0 && lever->lock.level == 0 && lever->state == 0 &&
           lever->stateCount == 1 && lever->cycling == 0 && lever->flag260 == 0 && lever->keepsMapFlag == 0 &&
           lever->from == 0.0f && lever->to == 0.0f && lever->endTick == 0.0f && lever->rate == 0.0f;
}

constexpr std::uint32_t kDisabled = 0x80000;

using ClassNameFn = const char*(__fastcall*)(Lever*);
using IntFn = int(__fastcall*)(Lever*);
using BoolFn = bool(__fastcall*)(Lever*);
using UIntFn = std::uint32_t(__fastcall*)(Lever*);
using DoubleFn = double(__fastcall*)(Lever*);
using KeyFn = BOOL(__fastcall*)(Lever*, void*, const LockKey*);
using SetUpFn = std::uintptr_t(__fastcall*)(Lever*, void*, const LeverSetup*);
using EnableFn = std::uintptr_t(__fastcall*)(Lever*, void*, bool);
using SetLockFn = std::uintptr_t(__fastcall*)(Lever*, void*, const LockKey*);
using SaveFn = void(__fastcall*)(Lever*, void*, CStream*);
ClassNameFn s_className;
IntFn s_classId, s_flag260, s_needsKey;
BoolFn s_keepsMapFlag;
UIntFn s_keyId;
DoubleFn s_radius;
KeyFn s_keyOpens;
SetUpFn s_setUp;
EnableFn s_enable;
SetLockFn s_setLock;
SaveFn s_save;
} // namespace

// 0052B2E0 / 0052B2F0: MOV EAX,imm32 (5 bytes).
static const char* __fastcall LeverClassNameVerify(Lever* self) {
    const char* expected = s_className(self);
    ei::verify::Check("LeverClassName", expected, LeverClassName(self), "");
    return expected;
}
EI_VERIFY(0x0052B2E0, LeverClassNameVerify, 5, s_className);

static int __fastcall LeverClassIdVerify(Lever* self) {
    int expected = s_classId(self);
    ei::verify::Check("LeverClassId", expected, LeverClassId(self), "");
    return expected;
}
EI_VERIFY(0x0052B2F0, LeverClassIdVerify, 5, s_classId);

// 0052B2B0: MOV AL,1; RET (3 bytes): ours only.
static bool __fastcall Slot29Verify(Lever* self) {
    bool actual = self->Slot29True();
    ei::verify::Check("Lever::Slot29True", true, actual, "");
    return actual;
}
EI_VERIFY(0x0052B2B0, Slot29Verify, 0, s_unused);

// 0052B2D0: XOR EAX,EAX; MOV AL,[ECX+0x260] (8 bytes).
static int __fastcall Flag260Verify(Lever* self) {
    int expected = s_flag260(self);
    ei::verify::Check("Lever::Flag260Slot", expected, self->Flag260Slot(), "lever %d", self->id_);
    return expected;
}
EI_VERIFY(0x0052B2D0, Flag260Verify, 8, s_flag260);

// 0052B2C0: MOV AL,[ECX+0x261] (6 bytes).
static bool __fastcall KeepsMapFlagVerify(Lever* self) {
    bool expected = s_keepsMapFlag(self);
    ei::verify::Check("Lever::KeepsMapFlagSlot", expected, self->KeepsMapFlagSlot(), "lever %d", self->id_);
    return expected;
}
EI_VERIFY(0x0052B2C0, KeepsMapFlagVerify, 6, s_keepsMapFlag);

// 0052BB00: TEST dword ptr [ECX+8],0x80000 (7 bytes).
static int __fastcall NeedsKeyVerify(Lever* self) {
    int expected = s_needsKey(self);
    ei::verify::Check("Lever::NeedsKey", expected, self->NeedsKey(), "lever %d", self->id_);
    return expected;
}
EI_VERIFY(0x0052BB00, NeedsKeyVerify, 7, s_needsKey);

// 0052BBC0: TEST byte ptr [ECX+0x244],8 (7 bytes).
static std::uint32_t __fastcall KeyIdVerify(Lever* self) {
    std::uint32_t expected = s_keyId(self);
    ei::verify::Check("Lever::KeyId", expected, self->Lever::KeyId(), "lever %d", self->id_);
    return expected;
}
EI_VERIFY(0x0052BBC0, KeyIdVerify, 7, s_keyId);

// 0052B3E0: FLD dword ptr [ECX+0x134] (6 bytes).
static double __fastcall RadiusVerify(Lever* self) {
    double expected = s_radius(self);
    double actual = self->RadiusSlot();
    ei::verify::Check("Lever::RadiusSlot", true, std::memcmp(&expected, &actual, sizeof(double)) == 0 ||
                      (std::isnan(expected) && std::isnan(actual)), "lever %d", self->id_);
    return expected;
}
EI_VERIFY(0x0052B3E0, RadiusVerify, 6, s_radius);

// 0052BBA0: ADD ECX,0x244 (6 bytes). Both go through 00522A80 (pure).
static BOOL __fastcall KeyOpensVerify(Lever* self, void*, const LockKey* key) {
    BOOL expected = s_keyOpens(self, nullptr, key);
    ei::verify::Check("Lever::KeyOpens", expected, self->KeyOpens(key), "lever %d", self->id_);
    return expected;
}
EI_VERIFY(0x0052BBA0, KeyOpensVerify, 6, s_keyOpens);

// 0052B3F0: MOV EAX,[ESP+4]; PUSH EBX (5 bytes).
static std::uintptr_t __fastcall SetUpVerify(Lever* self, void*, const LeverSetup* setup) {
    LeverBytes before, ours;
    before.Save(self);
    std::uintptr_t actual = self->SetUp(setup);
    ours.Save(self);
    before.Restore(self);
    std::uintptr_t expected = s_setUp(self, nullptr, setup);
    int diff = ours.Difference(self);
    ei::verify::Check("Lever::SetUp", true, expected == actual && diff < 0, "lever %d: first difference at %#x",
                      self->id_, diff);
    return expected;
}
EI_VERIFY(0x0052B3F0, SetUpVerify, 5, s_setUp);

// 0052BAD0: MOV AL,[ESP+4]; TEST AL,AL (6 bytes).
static std::uintptr_t __fastcall EnableVerify(Lever* self, void*, bool on) {
    LeverBytes before, ours;
    before.Save(self);
    std::uintptr_t actual = self->Enable(on);
    ours.Save(self);
    before.Restore(self);
    std::uintptr_t expected = s_enable(self, nullptr, on);
    int diff = ours.Difference(self);
    ei::verify::Check("Lever::Enable", true, expected == actual && diff < 0, "lever %d on %d: first difference at %#x",
                      self->id_, on, diff);
    return expected;
}
EI_VERIFY(0x0052BAD0, EnableVerify, 6, s_enable);

// 0052BB20: MOV EDX,[ESP+4]; PUSH ESI (5 bytes).
static std::uintptr_t __fastcall SetLockVerify(Lever* self, void*, const LockKey* key) {
    LeverBytes before, ours;
    before.Save(self);
    std::uintptr_t actual = self->SetLock(key);
    ours.Save(self);
    before.Restore(self);
    std::uintptr_t expected = s_setLock(self, nullptr, key);
    int diff = ours.Difference(self);
    ei::verify::Check("Lever::SetLock", true, expected == actual && diff < 0, "lever %d: first difference at %#x",
                      self->id_, diff);
    return expected;
}
EI_VERIFY(0x0052BB20, SetLockVerify, 5, s_setLock);

// 0052BE40: PUSH ESI; MOV ESI,[ESP+8]; PUSH EDI (6 bytes).
static void __fastcall SaveVerify(Lever* self, void*, CStream* stream) {
    CaptureStream expected(stream), actual(stream);
    s_save(self, nullptr, &expected);
    self->SaveLever(&actual);
    ei::verify::Check("Lever::SaveLever", true, expected.Same(actual), "lever %d: %u vs %u bytes, first difference at %d",
                      self->id_, static_cast<unsigned>(expected.bytes.size()), static_cast<unsigned>(actual.bytes.size()),
                      expected.FirstDifference(actual));
    expected.Forward(stream);
}
EI_VERIFY(0x0052BE40, SaveVerify, 6, s_save);

// The rest: ours only.
static Lever* __fastcall ConstructVerify(Lever* self) {
    Lever* result = self->Construct();
    ei::verify::Check("Lever::Construct", true, result == self && Reset(self), "");
    return result;
}
EI_VERIFY(0x0052B1F0, ConstructVerify, 0, s_unused);

static int __fastcall CreateVerify(Lever* self) {
    int result = self->CreateLever();
    ei::verify::Check("Lever::CreateLever", true, result == 1 && Reset(self), "lever %d", self->id_);
    return result;
}
EI_VERIFY(0x0052B370, CreateVerify, 0, s_unused);

static void __fastcall DestructVerify(Lever* self) {
    self->DestructBody();
    ei::verify::Check("Lever::DestructBody", true, self->companion == nullptr, "");
}
EI_VERIFY(0x0052B320, DestructVerify, 0, s_unused);

static Lever* __fastcall DeleteVerify(Lever* self, void*, int flags) {
    Lever* result = self->DeletingDestruct(flags);
    ei::verify::Check("Lever::DeletingDestruct", static_cast<void*>(self), static_cast<void*>(result), "flags %d", flags);
    return result;
}
EI_VERIFY(0x0052B300, DeleteVerify, 0, s_unused);

static void __fastcall LoadVerify(Lever* self, void*, CStream* stream) {
    self->LoadLever(stream);
    ei::verify::Check("Lever::LoadLever", true, self->stateCount < 1 || (self->state >= 0 && self->state < self->stateCount),
                      "lever %d: state %d of %d", self->id_, self->state, self->stateCount);
}
EI_VERIFY(0x0052BDD0, LoadVerify, 0, s_unused);

// The update: 1 while a move runs, else 0.
static int __fastcall UpdateVerify(Lever* self) {
    int result = self->UpdateLever();
    bool ok = result == 0 || result == 1;
    ei::verify::Check("Lever::UpdateLever", true, ok, "lever %d: %d", self->id_, result);
    return result;
}
EI_VERIFY(0x0052B490, UpdateVerify, 0, s_unused);

// The writer may only end a finished move (to, rate): from, end tick and lock stay.
static void __fastcall WriteNetVerify(Lever* self) {
    float from = self->from, endTick = self->endTick;
    LockKey lock = self->lock;
    self->WriteLeverNetState();
    bool ok = std::memcmp(&from, &self->from, 4) == 0 && std::memcmp(&endTick, &self->endTick, 4) == 0 &&
              std::memcmp(&lock, &self->lock, sizeof(lock)) == 0;
    ei::verify::Check("Lever::WriteLeverNetState", true, ok, "lever %d", self->id_);
}
EI_VERIFY(0x0052B560, WriteNetVerify, 0, s_unused);

static std::uint32_t __fastcall ReadNetVerify(Lever* self, void*, int first) {
    std::uint32_t result = self->ReadLeverNetState(first);
    ei::verify::Check("Lever::ReadLeverNetState", true, result <= 1, "lever %d: %u", self->id_, result);
    return result;
}
EI_VERIFY(0x0052B650, ReadNetVerify, 0, s_unused);

// Opening disables the lever.
static bool __fastcall ApplyKeyVerify(Lever* self, void*, const LockKey* key, int unused) {
    bool result = self->ApplyKey(key, unused);
    ei::verify::Check("Lever::ApplyKey", true, !result || (self->flags_ & kDisabled) != 0, "lever %d", self->id_);
    return result;
}
EI_VERIFY(0x0052BB50, ApplyKeyVerify, 0, s_unused);

// A-U11. 00522A30 LockKey::WriteTo: both into capture streams, compared (PUSH ECX; PUSH ESI; MOV ESI,[ESP+0xC]:
// 6 bytes). 005229F0 LockKey::ReadFrom: ours only (three dwords read). The rewritten lever Load / Save call them
// directly, so these run only from original code (none left); Lever::SaveLever's compare covers the bytes.
static void(__fastcall* s_lockWrite)(const LockKey*, void*, CStream*);
static void __fastcall LockWriteVerify(const LockKey* self, void*, CStream* stream) {
    CaptureStream expected(stream), actual(stream);
    s_lockWrite(self, nullptr, &expected);
    self->WriteTo(&actual);
    ei::verify::Check("LockKey::WriteTo", true, expected.Same(actual), "first difference at %d",
                      expected.FirstDifference(actual));
    expected.Forward(stream);
}
EI_VERIFY(0x00522A30, LockWriteVerify, 6, s_lockWrite);

static void __fastcall LockReadVerify(LockKey* self, void*, CStream* stream) {
    self->ReadFrom(stream);
    ei::verify::Check("LockKey::ReadFrom", true, true, "");
}
EI_VERIFY(0x005229F0, LockReadVerify, 0, s_unused);
