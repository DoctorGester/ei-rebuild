// Checks for the map object base (map_object.cpp) and its companions (map_object_companion.cpp).
//
// Getters and functions that only write an output run both versions (the original into scratch
// memory) and compare bit for bit. Setters that touch nothing but the object run ours, snapshot
// what they can write, restore it, run the original and compare. The per-tick updates, ClearEffects,
// TakeHit and AttachCompanion free memory, play sounds, draw random numbers or release references,
// so only ours runs and the wrapper checks what must hold afterwards.
#include "verify.h"

#include "../../src/game/map_object.h"
#include "../../src/game/unit.h"

#include "ei/vcall.h"

#include <bit>
#include <cstring>

namespace {
constexpr int kTrackedSize = 0x1E0;   // MarkChanged writes nothing beyond +0x1DE
constexpr int kServerSize = 0x78;     // CMapObjectServer: header and the 0x58-byte block at +0x20
constexpr int kMaxEffects = 64;       // effect lists longer than this are not checked
constexpr int kMaxSounds = 32;

std::uint32_t Bits(float f) { return std::bit_cast<std::uint32_t>(f); }

// A copy of up to two memory regions, restorable.
struct Snapshot {
    unsigned char a[kTrackedSize];
    unsigned char b[kServerSize];
    void* pa;
    int na;
    void* pb;
    int nb;
    Snapshot(void* regionA, int sizeA, void* regionB = nullptr, int sizeB = 0)
        : pa(regionA), na(sizeA), pb(regionB), nb(sizeB) {
        std::memcpy(a, pa, na);
        if (pb) std::memcpy(b, pb, nb);
    }
    void Restore() const {
        std::memcpy(pa, a, na);
        if (pb) std::memcpy(pb, b, nb);
    }
    // Offset of the first byte that differs from the current memory, or -1 (+0x10000 for region B).
    int FirstDifference() const {
        for (int i = 0; i < na; ++i)
            if (a[i] != static_cast<unsigned char*>(pa)[i]) return i;
        if (pb)
            for (int i = 0; i < nb; ++i)
                if (b[i] != static_cast<unsigned char*>(pb)[i]) return 0x10000 + i;
        return -1;
    }
};

// Runs `ours`, snapshots the regions, restores them, runs `original`; reports whether both left
// the same bytes (and, with `compareResult`, the same EAX). Returns the original's result. The
// buffers are on the stack: the original may call other verified functions that do the same.
template <typename R, typename Ours, typename Original>
R SameWrites(const char* name, int id, void* regionA, int sizeA, void* regionB, int sizeB, bool compareResult, Ours ours,
             Original original) {
    Snapshot before(regionA, sizeA, regionB, sizeB);
    R actual = ours();
    Snapshot after(regionA, sizeA, regionB, sizeB);
    before.Restore();
    R expected = original();
    int firstDiff = after.FirstDifference();
    bool same = firstDiff < 0 && (!compareResult || expected == actual);
    ei::verify::Check(name, true, same, "object %d: result %X vs %X, first difference at +0x%X", id,
                      static_cast<unsigned>(expected), static_cast<unsigned>(actual), firstDiff);
    return expected;
}

CMapObject* OwnerOf(void* companion) { return CompanionOwner(static_cast<CEngineObject*>(companion)); }

void* VtableSlot(const void* object, int slot) { return (*reinterpret_cast<void* const* const*>(object))[slot]; }
} // namespace

// --- Per-tick update -------------------------------------------------------------------------

// 0050F1C0. Ours only (the companion's update runs once): returns 1.
static void* s_unusedUpdate;
static int __fastcall UpdateVerify(CMapObject* self) {
    int result = self->Update();
    ei::verify::Check("CMapObject::Update", 1, result, "object %d", self->id_);
    return result;
}
EI_VERIFY(0x0050F1C0, UpdateVerify, 0, s_unusedUpdate);

// 00515820: SUB ESP,8; PUSH EBP; PUSH ESI (5 bytes). When no effect runs out, the update only
// counts down (no calls besides 00688530), so both versions run: ours, the effect records and the
// owner's tracked bytes saved, restored, the original; compared. When an effect runs out (slots
// with side effects are called) only ours runs, and the list must end as the old one with every
// count one lower and the effects that reached 0 removed, in order.
static int(__fastcall* s_serverUpdate)(CMapObjectServer*);

// Ours only, checked against the predicted list.
static int ServerUpdateOursOnly(CMapObjectServer* self) {
    CMapObject* owner = OwnerOf(self);
    int before = owner->effectCount;
    bool checked = before >= 0 && before <= kMaxEffects;
    MapEffect expected[kMaxEffects];
    int expectedCount = 0;
    if (checked) {
        for (int i = 0; i < before; ++i) {
            MapEffect effect = owner->effects[i];
            effect.ticksLeft = static_cast<int>(static_cast<std::uint32_t>(effect.ticksLeft) - 1u);
            if (effect.ticksLeft > 0) expected[expectedCount++] = effect;
        }
    }
    int result = self->Update();
    owner = OwnerOf(self);
    bool ok = result == 0;
    if (checked)
        ok = ok && owner->effectCount == expectedCount &&
             (expectedCount == 0 || std::memcmp(owner->effects, expected, expectedCount * sizeof(MapEffect)) == 0);
    ei::verify::Check("CMapObjectServer::Update", true, ok, "object %d: effects %d -> %d (expected %d), result %d",
                      owner->id_, before, owner->effectCount, expectedCount, result);
    return result;
}

static int __fastcall ServerUpdateVerify(CMapObjectServer* self) {
    CMapObject* owner = OwnerOf(self);
    int count = owner->effectCount;
    bool expires = count < 0 || count > kMaxEffects;
    for (int i = 0; i < count && !expires; ++i) expires = owner->effects[i].ticksLeft <= 1;
    if (expires) return ServerUpdateOursOnly(self);

    MapEffect before[kMaxEffects];
    MapEffect after[kMaxEffects];
    std::size_t bytes = static_cast<std::size_t>(count) * sizeof(MapEffect);
    if (bytes) std::memcpy(before, owner->effects, bytes);
    int result = SameWrites<int>(
        "CMapObjectServer::Update", owner->id_, owner, kTrackedSize, nullptr, 0, true,
        [&] {
            int r = self->Update();
            if (bytes) {
                std::memcpy(after, owner->effects, bytes);
                std::memcpy(owner->effects, before, bytes);
            }
            return r;
        },
        [&] { return s_serverUpdate(self); });
    bool same = !bytes || std::memcmp(after, owner->effects, bytes) == 0;
    ei::verify::Check("CMapObjectServer::Update (effects)", true, same, "object %d: %d effects", owner->id_, count);
    return result;
}
EI_VERIFY(0x00515820, ServerUpdateVerify, 5, s_serverUpdate);

// 00510890. Ours only (sounds, random numbers). Every positive effect count is one lower, each
// stench timer one lower (16 after it went negative), and a shown light is either where it was
// or kLightHeight above the object.
namespace {
struct ClientSound {
    int kind;
    int unknown_04;
    int handle;
    std::uint32_t timer;
    std::uint32_t unknown_10;
};
static_assert(sizeof(ClientSound) == 0x14);
} // namespace

static void* s_unusedClientUpdate;
static int __fastcall ClientUpdateVerify(CMapObjectClientSpecific* self) {
    char* raw = reinterpret_cast<char*>(self);
    CMapObject* owner = OwnerOf(self);
    int effectCount = owner->effectCount;
    bool effectsChecked = effectCount >= 0 && effectCount <= kMaxEffects;
    int ticks[kMaxEffects];
    if (effectsChecked)
        for (int i = 0; i < effectCount; ++i) ticks[i] = owner->effects[i].ticksLeft;
    auto* sounds = *reinterpret_cast<ClientSound**>(raw + 0x40);
    int soundCount = *reinterpret_cast<int*>(raw + 0x44);
    bool soundsChecked = soundCount >= 0 && soundCount <= kMaxSounds;
    int kinds[kMaxSounds];
    std::uint32_t timers[kMaxSounds];
    if (soundsChecked)
        for (int i = 0; i < soundCount; ++i) {
            kinds[i] = sounds[i].kind;
            timers[i] = sounds[i].timer;
        }
    auto* light = *reinterpret_cast<CEngineObject**>(raw + 0x50);
    bool lightShown = light && !(light->flags_ & 0x80);
    float lightBefore[3] = {};
    if (light) std::memcpy(lightBefore, reinterpret_cast<char*>(light) + 0x1C, sizeof(lightBefore));

    int result = self->Update();

    owner = OwnerOf(self);
    bool ok = result == 1;
    int bad = -1;
    if (effectsChecked && owner->effectCount == effectCount) {
        for (int i = 0; i < effectCount && bad < 0; ++i)
            if (owner->effects[i].ticksLeft != (ticks[i] > 0 ? ticks[i] - 1 : ticks[i])) bad = i;
    } else if (effectsChecked) {
        bad = 0x100;
    }
    sounds = *reinterpret_cast<ClientSound**>(raw + 0x40);
    if (soundsChecked && *reinterpret_cast<int*>(raw + 0x44) == soundCount) {
        for (int i = 0; i < soundCount && bad < 0; ++i) {
            if (sounds[i].kind != kinds[i]) bad = 0x200 + i;
            else if (kinds[i] == 0x13) {
                std::uint32_t timer = timers[i] - 1u;
                if (timer & 0x80000000u) timer = 0x10;
                if (sounds[i].timer != timer) bad = 0x200 + i;
            } else if (sounds[i].timer != timers[i]) {
                bad = 0x200 + i;
            }
        }
    }
    if (lightShown && bad < 0) {
        const float* now = reinterpret_cast<const float*>(reinterpret_cast<char*>(light) + 0x1C);
        bool unchanged = std::memcmp(now, lightBefore, sizeof(lightBefore)) == 0;
        volatile float z = 2.0f + owner->positionZ;
        bool followed = Bits(now[0]) == Bits(owner->positionX) && Bits(now[1]) == Bits(owner->positionY) &&
                        Bits(now[2]) == Bits(z);
        if (!unchanged && !followed) bad = 0x300;
    }
    ok = ok && bad < 0;
    ei::verify::Check("CMapObjectClientSpecific::Update", true, ok, "object %d: result %d, first bad 0x%X", owner->id_,
                      result, bad);
    return result;
}
EI_VERIFY(0x00510890, ClientUpdateVerify, 0, s_unusedClientUpdate);

// --- Effects ---------------------------------------------------------------------------------

// 0050F5B0. Ours only (frees the array): no effects left, part 3 changed, the object dirty.
static void* s_unusedClearEffects;
static std::uintptr_t __fastcall ClearEffectsVerify(CMapObject* self) {
    int before = self->effectCount;
    std::uintptr_t result = self->ClearEffects();
    bool ok = !self->effects && self->effectCount == 0 && self->effectCapacity == 0 && (self->changedParts & 8) &&
              (self->flags_ & 2);
    ei::verify::Check("CMapObject::ClearEffects", true, ok, "object %d: effects %d -> %d", self->id_, before,
                      self->effectCount);
    return result;
}
EI_VERIFY(0x0050F5B0, ClearEffectsVerify, 0, s_unusedClearEffects);

// 0050F690: PUSH EBP; MOV EBP,ESP; SUB ESP,0x38 (6 bytes). Pure (float bits).
static float(__fastcall* s_effectValue)(CMapObject*, void*, std::uint32_t);
static float __fastcall EffectValueVerify(CMapObject* self, void*, std::uint32_t type) {
    float expected = s_effectValue(self, nullptr, type);
    float actual = self->EffectValue(type);
    ei::verify::Check("CMapObject::EffectValue", Bits(expected), Bits(actual), "object %d type %X: %g vs %g", self->id_,
                      type, expected, actual);
    return expected;
}
EI_VERIFY(0x0050F690, EffectValueVerify, 6, s_effectValue);

// --- Scale -----------------------------------------------------------------------------------

// 0050F2D0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x24 (6 bytes). Objects whose slot 30 is the plain copy
// (0050E440): ours, snapshot of the tracked bytes, restore, original; bytes and EAX compared.
// Units' slot 30 rebuilds the figures (00517700), which must not run twice: ours only, with the
// change marks checked.
static std::uintptr_t(__fastcall* s_recomputeScale)(CMapObject*);
static std::uintptr_t __fastcall RecomputeScaleVerify(CMapObject* self) {
    if (VtableSlot(self, mapslot::kResetScale) == reinterpret_cast<void*>(0x0050E440))
        return SameWrites<std::uintptr_t>("CMapObject::RecomputeScale", self->id_, self, kTrackedSize, nullptr, 0, true,
                                          [&] { return self->RecomputeScale(); },
                                          [&] { return s_recomputeScale(self); });
    std::uintptr_t result = self->RecomputeScale();
    bool ok = (self->changedParts & 2) && (self->flags_ & 2);
    ei::verify::Check("CMapObject::RecomputeScale", true, ok, "object %d (ours only): parts %X", self->id_,
                      self->changedParts);
    return result;
}
EI_VERIFY(0x0050F2D0, RecomputeScaleVerify, 6, s_recomputeScale);

// 0050E440: LEA EAX,[ECX+0x1AC] (6 bytes). Writes the scale only: snapshot compare, EAX too.
static std::uint32_t(__fastcall* s_resetScale)(CMapObject*);
static std::uint32_t __fastcall ResetScaleVerify(CMapObject* self) {
    return SameWrites<std::uint32_t>("CMapObject::ResetScale", self->id_, self, kTrackedSize, nullptr, 0, true,
                                     [&] { return self->ResetScale(); }, [&] { return s_resetScale(self); });
}
EI_VERIFY(0x0050E440, ResetScaleVerify, 6, s_resetScale);

// --- Position --------------------------------------------------------------------------------

// 0050E4B0 / 0050E4D0: MOV EAX,[ESP+4]; ADD ECX,0x1C (7 bytes). The original writes a scratch
// copy; the copies and the returned pointers (each its own `out`) are compared.
static float*(__fastcall* s_getPosition)(CMapObject*, void*, float*, int);
static float* __fastcall GetPositionVerify(CMapObject* self, void*, float* out, int unused) {
    float scratch[3];
    float* expected = s_getPosition(self, nullptr, scratch, unused);
    float* actual = self->GetPosition(out, unused);
    bool ok = expected == scratch && actual == out && std::memcmp(scratch, out, sizeof(scratch)) == 0;
    ei::verify::Check("CMapObject::GetPosition", true, ok, "object %d", self->id_);
    return out;
}
EI_VERIFY(0x0050E4B0, GetPositionVerify, 7, s_getPosition);

static float*(__fastcall* s_getPosition2)(CMapObject*, void*, float*);
static float* __fastcall GetPosition2Verify(CMapObject* self, void*, float* out) {
    float scratch[3];
    float* expected = s_getPosition2(self, nullptr, scratch);
    float* actual = self->GetPosition2(out);
    bool ok = expected == scratch && actual == out && std::memcmp(scratch, out, sizeof(scratch)) == 0;
    ei::verify::Check("CMapObject::GetPosition2", true, ok, "object %d", self->id_);
    return out;
}
EI_VERIFY(0x0050E4D0, GetPosition2Verify, 7, s_getPosition2);

// 0050F730: SUB ESP,0xC; FLD [ECX+0x1C] (6 bytes). Output only (the ground height is a query).
static float*(__fastcall* s_groundPoint)(CMapObject*, void*, float*);
static float* __fastcall GroundPointVerify(CMapObject* self, void*, float* out) {
    float scratch[3];
    float* expected = s_groundPoint(self, nullptr, scratch);
    float* actual = self->GroundPoint(out);
    bool ok = expected == scratch && actual == out && std::memcmp(scratch, out, sizeof(scratch)) == 0;
    ei::verify::Check("CMapObject::GroundPoint", true, ok, "object %d: %g %g %g vs %g %g %g", self->id_, scratch[0],
                      scratch[1], scratch[2], out[0], out[1], out[2]);
    return out;
}
EI_VERIFY(0x0050F730, GroundPointVerify, 6, s_groundPoint);

// 0050F020 begins PUSH ESI; MOV ESI,ECX; CALL 00455950: no 5-byte trampoline. The original runs
// through this stub, which repeats those instructions (the call through a register) and jumps
// past the hook's 5 bytes to 0050F028, which reads EAX (the world) and ESI.
__declspec(naked) static void __fastcall OriginalPlaceOnGround(CMapObject*, void*, float) {
    __asm {
        push esi
        mov esi, ecx
        mov eax, 0x00455950
        call eax
        mov ecx, 0x0050F028
        jmp ecx
    }
}

// Ours, z saved and restored, the original; z compared bit for bit.
static void* s_unusedPlaceOnGround;
static void __fastcall PlaceOnGroundVerify(CMapObject* self, void*, float offset) {
    float before = self->positionZ;
    self->PlaceOnGround(offset);
    float actual = self->positionZ;
    self->positionZ = before;
    OriginalPlaceOnGround(self, nullptr, offset);
    ei::verify::Check("CMapObject::PlaceOnGround", Bits(self->positionZ), Bits(actual), "object %d offset %g: %g vs %g",
                      self->id_, offset, self->positionZ, actual);
}
EI_VERIFY(0x0050F020, PlaceOnGroundVerify, 0, s_unusedPlaceOnGround);

// --- Companion -------------------------------------------------------------------------------

// 0050F050: PUSH ESI; MOV ESI,ECX; MOV ECX,[ESI+0x170] (9 bytes). Snapshot of the object's
// tracked bytes and the companion's first 0x78; bytes and EAX compared.
static std::uintptr_t(__fastcall* s_setServerData)(CMapObject*, void*, const void*);
static std::uintptr_t __fastcall SetServerDataVerify(CMapObject* self, void*, const void* data) {
    // Only a CMapObjectServer companion is written (and known to be at least 0x78 bytes).
    CEngineObject* server = self->companion && self->companion->GetClassId() == 0x39 ? self->companion : nullptr;
    return SameWrites<std::uintptr_t>("CMapObject::SetServerData", self->id_, self, kTrackedSize, server,
                                      server ? kServerSize : 0, true, [&] { return self->SetServerData(data); },
                                      [&] { return s_setServerData(self, nullptr, data); });
}
EI_VERIFY(0x0050F050, SetServerDataVerify, 9, s_setServerData);

// 00515950: MOV EAX,ECX; PUSH ESI; MOV ESI,[ESP+8] (7 bytes). Same snapshot (companion first).
static std::uintptr_t(__fastcall* s_serverSetData)(CMapObjectServer*, void*, const void*);
static std::uintptr_t __fastcall ServerSetDataVerify(CMapObjectServer* self, void*, const void* data) {
    CMapObject* owner = OwnerOf(self);
    return SameWrites<std::uintptr_t>("CMapObjectServer::SetData", owner->id_, owner, kTrackedSize, self, kServerSize,
                                      true, [&] { return self->SetData(data); },
                                      [&] { return s_serverSetData(self, nullptr, data); });
}
EI_VERIFY(0x00515950, ServerSetDataVerify, 7, s_serverSetData);

// 00515980. Ours only (applies damage, may clear the effects): returns 1; a dead object ends
// without effects and with flags_ bit 1.
static void* s_unusedTakeHit;
static BOOL __fastcall TakeHitVerify(CMapObjectServer* self, void*, const void* hit) {
    BOOL result = self->TakeHit(hit);
    CMapObject* owner = OwnerOf(self);
    bool dead = ei::VCall<BOOL>(self, 17) != 0;
    bool ok = result == 1 && (!dead || (owner->effectCount == 0 && (owner->flags_ & 1)));
    ei::verify::Check("CMapObjectServer::TakeHit", true, ok, "object %d: result %d, dead %d, effects %d", owner->id_,
                      result, dead, owner->effectCount);
    return result;
}
EI_VERIFY(0x00515980, TakeHitVerify, 0, s_unusedTakeHit);

// 0050E8F0. Ours only (releases the old companion): the new one is attached, points back, and
// (when it is not the old one) gained one reference.
static void* s_unusedAttach;
static void __fastcall AttachCompanionVerify(CMapObject* self, void*, CEngineObject* companion) {
    bool replaced = companion && companion != self->companion;
    int refs = companion ? companion->refCount_ : 0;
    self->AttachCompanion(companion);
    bool ok = self->companion == companion &&
              (!companion || (CompanionOwner(companion) == self && (!replaced || companion->refCount_ == refs + 1)));
    ei::verify::Check("CMapObject::AttachCompanion", true, ok, "object %d", self->id_);
}
EI_VERIFY(0x0050E8F0, AttachCompanionVerify, 0, s_unusedAttach);

// --- Flags (pure): MOV EAX,[ECX+8]; SHR EAX,n (6 bytes) ----------------------------------------

static std::uint32_t(__fastcall* s_isInvisible)(CMapObject*);
static std::uint32_t __fastcall IsInvisibleVerify(CMapObject* self) {
    std::uint32_t expected = s_isInvisible(self);
    ei::verify::Check("CMapObject::IsInvisible", expected, self->IsInvisible(), "flags %X", self->flags_);
    return expected;
}
EI_VERIFY(0x0050FA10, IsInvisibleVerify, 6, s_isInvisible);

static std::uint32_t(__fastcall* s_isStunned)(CMapObject*);
static std::uint32_t __fastcall IsStunnedVerify(CMapObject* self) {
    std::uint32_t expected = s_isStunned(self);
    ei::verify::Check("CMapObject::IsStunned", expected, self->IsStunned(), "flags %X", self->flags_);
    return expected;
}
EI_VERIFY(0x0050FA20, IsStunnedVerify, 6, s_isStunned);

static std::uint32_t(__fastcall* s_flag20000)(CMapObject*);
static std::uint32_t __fastcall Flag20000Verify(CMapObject* self) {
    std::uint32_t expected = s_flag20000(self);
    ei::verify::Check("CMapObject::Flag20000", expected, self->Flag20000(), "flags %X", self->flags_);
    return expected;
}
EI_VERIFY(0x0050E430, Flag20000Verify, 6, s_flag20000);
