#pragma once

// Shared by spell_effect_start.cpp (casting, 0067EF40-00681B00) and spell_effect_update.cpp
// (missile flight 00681BB0, lasting effects 00682650): forwarders, the x87 sequences the
// original repeats inline, and small accessors. See docs/notes/items_spells.md.

#include "game_strings.h"
#include "spell.h"

#include "combat.h"
#include "console.h"
#include "lib/mfc.h"
#include "map_object.h"
#include "unit.h"
#include "world.h"
#include "world_effect.h"

#include "ei/hook.h"
#include "originals_ai.h"
#include "originals_effects.h"
#include "originals_gameplay.h"
#include "originals_world.h"
#include "originals_units.h"
#include "originals_library.h"

#include <cstdint>
#include <cstring>

namespace spellfx {

// --- Classes and slots ---------------------------------------------------------------------------

constexpr int kClassSound = 0x48;
constexpr int kClassParticle = 0x4A;
constexpr int kClassPointLight = 0x4B;
constexpr int kClassLightning = 0x4D;
constexpr int kClassArrow = 0x4E;
constexpr int kClassWall = 0x5A;
constexpr int kClassShowVisibility = 0x5E;
constexpr int kClassSoundSequence = 99;
constexpr int kClassUnit = 0x50;

// The world's height slots the x87 helpers below call through (27 the grid height, 28 the ground).
constexpr int kWorldSlotGridHeight = 27;
constexpr int kWorldSlotGroundHeight = 28;

constexpr std::uint32_t kPointLightFlags = 0x840;
constexpr int kPtrListBlockSize = 10;

// Volumes and radii the casts pass as immediates.
constexpr float kVolume = 10.0f;
constexpr float kHearing = 50.0f;
constexpr float kLoud = 1000.0f;

// Constants the original reads from memory (Rebirth may patch data; read them as the original does).
inline float ConstAt(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
inline const float* ConstPtr(std::uintptr_t address) { return reinterpret_cast<const float*>(address); }
constexpr std::uintptr_t kOne = 0x0073B84C;             // 1.0
constexpr std::uintptr_t kAcidRayScale = 0x0073B868;    // 0.4
constexpr std::uintptr_t kFireballScale = 0x0073C54C;   // 0.3
constexpr std::uintptr_t kFireballNear = 0x0073CF50;    // 1.25
constexpr std::uintptr_t kStrikeDepth = 0x0073BAA4;     // 3.0
constexpr std::uintptr_t kStrikeHeight = 0x0073BAF0;    // 30.0
constexpr std::uintptr_t kStrikeWidth = 0x007439B0;     // -7.0
constexpr std::uintptr_t kLightAbove = 0x0073BF34;      // 5.0
constexpr std::uintptr_t kBoltWidthScale = 0x0073BF34;  // 5.0 (the same constant)
constexpr std::uintptr_t kTeleportDepth = 0x0073BAD4;   // 0.2
constexpr std::uintptr_t kBuffScale = 0x0073CA98;       // 16.0
constexpr std::uintptr_t kTargetHeight = 0x0073CDF4;    // 1.2
constexpr float kDefaultSpeed = 5.0f;                   // immediate in the fireball case
constexpr float kHalfBits = 0.5f;                       // immediate pushed to the footprint builder


// --- Forwarders ----------------------------------------------------------------------------------

// The world server's timed objects: WorldAddTimed (originals_world.h).
// Effect setters (world_effect*.cpp).
__declspec(naked) inline void __fastcall EffectSetVelocity(CEngineObject*, void*, const Vector3*) EI_ORIGINAL(0x00675D50)
__declspec(naked) inline void __fastcall ParticleSetTarget(CEngineObject*, void*, const Vector3*) EI_ORIGINAL(0x00679310)
__declspec(naked) inline void __fastcall WallSetDirection(CEngineObject*, void*, const Vector3*) EI_ORIGINAL(0x00679C30)
__declspec(naked) inline void __fastcall LightningSetEnds(CEngineObject*, void*, const Vector3*, const Vector3*, float /*width*/)
    EI_ORIGINAL(0x0067A6B0)
__declspec(naked) inline void __fastcall LightningSetObjects(CEngineObject*, void*, int /*from*/, int /*to*/) EI_ORIGINAL(0x0067A890)
__declspec(naked) inline void __fastcall LightningSetFlag(CEngineObject*, void*, int /*flag byte*/) EI_ORIGINAL(0x0067A8C0)
// 0067AC70 (not rewritten): the missile's target object id.
// 0067C640: CEffectShowVisibility's two object ids (+0x14C, +0x150).
__declspec(naked) inline void __fastcall ShowVisibilitySetObjects(CEngineObject*, void*, int, int) EI_ORIGINAL(0x0067C640)
// 00688510: MapEffect::Construct (spell_lifetime.cpp), through its address (fastcall; returns it).
__declspec(naked) inline MapEffect* __fastcall BuffRecordInit(MapEffect*) EI_ORIGINAL(0x00688510)
// The spell's own helpers (spell.cpp / spell_effect.cpp; through their addresses for the verify wrappers).
__declspec(naked) inline void __fastcall EffectHitUnit(CSpellEffect*, void*, DamageInfo*, CEngineObject*) EI_ORIGINAL(0x006835B0)
__declspec(naked) inline int __fastcall IsAreaSpellTarget(CEngineObject*) EI_ORIGINAL(0x00681B80)
// CPtrList constructor (block size) and destructor.
inline void* __fastcall PtrListConstruct(void* a0, void*, int a2) { return PtrListConstructOriginal(a0, nullptr, a2); }
inline void __fastcall PtrListDestruct(void* a0) { PtrListDestruct4C96F0Original(a0); }
// The unit controller (+0x240): 00552780 says remark `what`; 00551AE0 tries to be tamed by `tamer`
// with skill `skill` (0 when it failed).
// Footprints of the walls and clouds in the global cell set 007C8478 (thiscall on the set): along a
// direction (005B2A00) or a disc (005B2760); then stamped into / cleared from the AI map (ai_footprint.cpp).
__declspec(naked) inline void __fastcall FootprintAlong(void* /*set*/, void*, void* /*aiMap*/, const Vector3*, float /*width*/, const Vector3*)
    EI_ORIGINAL(0x005B2A00)
__declspec(naked) inline void __fastcall FootprintDisc(void* /*set*/, void*, void* /*aiMap*/, const Vector3*, float /*radius*/)
    EI_ORIGINAL(0x005B2760)
__declspec(naked) inline bool __fastcall AiMapStamp(void* /*aiMap*/, void*, int /*owner*/, void* /*set*/) EI_ORIGINAL(0x005B7820)
__declspec(naked) inline bool __fastcall AiMapClear(void* /*aiMap*/, void*, void* /*set*/) EI_ORIGINAL(0x005B78F0)
inline void* SpellFootprint() { return reinterpret_cast<void*>(0x007C8478); }   // a FootprintCellSet (ai_map.h)
// The later states' callees.
__declspec(naked) inline void __fastcall ParticleSetColors(CEngineObject*, void*, float /*a*/, float /*b*/) EI_ORIGINAL(0x006792E0)
// 005302F0 CUnit::ModeSpeed (unit.cpp); left unrounded in ST0.
__declspec(naked) inline void __fastcall EffectHitUnitsInRadius(CSpellEffect*) EI_ORIGINAL(0x00683600)
__declspec(naked) inline void __fastcall EffectHitUnitsAlong(CSpellEffect*, void*, const Vector3*) EI_ORIGINAL(0x00683730)
// 005C98C0 (world + 0x2838, the AI events): a noise of `loudness` and `kind` made by `source` at `at`.
inline void __fastcall AiNoise(void* a0, void*, void* a2, float a3, int a4, const Vector3* a5) { AiEventsNoiseOriginal(a0, nullptr, a2, a3, a4, (const void*)a5); }
// 0058D3D0 CWorldServer::WakeUnitsAround (world_tick.cpp): WorldWakeUnits (originals_world.h).
// 005CBBE0: the unit controller's AI memory (+0xA50) notices `point` at `level` / `kind`.
inline void __fastcall AiMemoryNotice(void* a0, void*, const Vector3* a2, int a3, int a4) { AiMemoryNoticeOriginal(a0, nullptr, (const void*)a2, a3, a4); }
// 004B2EF0 / 004B31D0 ScriptGroup::Insert / InsertAll (script_group.cpp).
__declspec(naked) inline void __fastcall GroupInsert(void*, void*, CEngineObject*) EI_ORIGINAL(0x004B2EF0)
__declspec(naked) inline void __fastcall GroupInsertAll(void*, void*, const void* /*list*/) EI_ORIGINAL(0x004B31D0)
// 005B7A40 CAIMap::IsPassableWithoutSelf (ai_map.cpp); 005C11D0 CSectorMap::Move: originals_ai.h.
__declspec(naked) inline bool __fastcall AiMapPassableWithoutSelf(void*, void*, CEngineObject*, const Vector3*) EI_ORIGINAL(0x005B7A40)
// 00554450: the unit controller re-places its unit after a teleport.
inline void __fastcall ControllerReposition(void* a0) { ControllerRepositionOriginal(a0); }

// --- x87 sequences ------------------------------------------------------------------------------

// The spell's power factor, repeated inline about twenty times in the original: 1 when the
// prototype's effect (+0x2C) compares equal to 0 (or is NaN), else
// pow(min(max(damage / effect, 1), 8), 0.33) - 0.2 written as FYL2X / F2XM1 / FSCALE. The two
// clamps select the float bits on C0 (1 < r or unordered; r < 8 or unordered), the first one
// stored and reloaded. ECX = &damage, EDX = &prototype effect; the factor is left in ST0.
// ClampPowCore is the part after the ratio: ECX = &ratio (a float).
__declspec(naked) inline void ClampPowCore() {
    __asm {
        sub esp, 8
        mov eax, dword ptr [ecx]
        mov dword ptr [esp], eax
        mov dword ptr [esp + 4], 3F800000h
        fld dword ptr [esp + 4]
        fcomp dword ptr [esp]
        fnstsw ax
        mov ecx, dword ptr [esp]
        shl eax, 17h
        sar eax, 1Fh
        and ecx, eax
        not eax
        and eax, dword ptr [esp + 4]
        or eax, ecx
        mov dword ptr [esp + 4], eax
        fld dword ptr [esp + 4]
        fstp dword ptr [esp]
        mov dword ptr [esp + 4], 41000000h
        fld dword ptr [esp]
        fcomp dword ptr [esp + 4]
        fnstsw ax
        mov ecx, dword ptr [esp]
        shl eax, 17h
        sar eax, 1Fh
        and ecx, eax
        not eax
        and eax, dword ptr [esp + 4]
        or eax, ecx
        mov dword ptr [esp + 4], eax
        fld dword ptr [esp + 4]
        fldln2
        fxch st(1)
        fyl2x
        fmul qword ptr ds:[007439A8h]
        fldl2e
        fmulp st(1), st
        fld st(0)
        frndint
        fxch st(1)
        fsub st, st(1)
        f2xm1
        fld1
        faddp st(1), st
        fscale
        fstp st(1)
        fsub qword ptr ds:[0073CCD0h]
        add esp, 8
        ret
    }
}

__declspec(naked) inline void PowerCore() {
    __asm {
        fld dword ptr [edx]
        fcom dword ptr ds:[0073B858h]
        fnstsw ax
        test ah, 40h
        jz ratio
        fstp st(0)
        fld dword ptr ds:[0073B84Ch]
        ret
    ratio:
        fld dword ptr [ecx]
        fdiv st, st(1)
        push ecx
        fstp dword ptr [esp]
        fstp st(0)
        mov ecx, esp
        call ClampPowCore
        pop ecx
        ret
    }
}

// The fireball explosion's size curve: pow(clamp((r * r * pi) / area, 1, 8), 0.33) - 0.2 with the
// ratio rounded to float, the result rounded to float into *out.
__declspec(naked) inline void __stdcall AreaCurve(const float* /*radius*/, const float* /*area*/, float* /*out*/) {
    __asm {
        mov eax, dword ptr [esp + 8]
        mov ecx, dword ptr [esp + 4]
        push ecx
        fld dword ptr [eax]
        fld dword ptr [ecx]
        fld st(0)
        fmul st, st(1)
        fmul dword ptr ds:[00743964h]
        fdiv st, st(2)
        fstp dword ptr [esp]
        fstp st(0)
        fstp st(0)
        mov ecx, esp
        call ClampPowCore
        pop ecx
        mov eax, dword ptr [esp + 0Ch]
        fstp dword ptr [eax]
        ret 0Ch
    }
}

// *out = (float)((*a / *b) * *c), the quotient unrounded.
__declspec(naked) inline void __stdcall DivideMultiply(const float* /*a*/, const float* /*b*/, const float* /*c*/, float* /*out*/) {
    __asm {
        mov eax, dword ptr [esp + 8]
        fld dword ptr [eax]
        mov eax, dword ptr [esp + 4]
        fdivr dword ptr [eax]
        mov eax, dword ptr [esp + 0Ch]
        fmul dword ptr [eax]
        mov eax, dword ptr [esp + 10h]
        fstp dword ptr [eax]
        ret 10h
    }
}

// The fireworks' fright level: _ftol(((float)timer / duration) * 250 + 50) with FILD / FIDIV,
// unrounded until the game's _ftol truncates it.
__declspec(naked) inline int __fastcall FrightLevel(const int* /*timer*/, const int* /*duration*/) {
    __asm {
        fild dword ptr [ecx]
        fidiv dword ptr [edx]
        fmul dword ptr ds:[0073BF48h]
        fadd dword ptr ds:[0074023Ch]
        mov eax, 006E853Ch
        jmp eax
    }
}

// *out = (float)(*a * *b + *c), unrounded in between.
__declspec(naked) inline void __stdcall MultiplyAdd(const float* /*a*/, const float* /*b*/, const float* /*c*/, float* /*out*/) {
    __asm {
        mov eax, dword ptr [esp + 4]
        fld dword ptr [eax]
        mov eax, dword ptr [esp + 8]
        fmul dword ptr [eax]
        mov eax, dword ptr [esp + 0Ch]
        fadd dword ptr [eax]
        mov eax, dword ptr [esp + 10h]
        fstp dword ptr [eax]
        ret 10h
    }
}

// The game's _ftol (006E853C) of a float constant in memory.
__declspec(naked) inline int __fastcall FtolOf(const float* /*value*/) {
    __asm {
        fld dword ptr [ecx]
        mov eax, 006E853Ch
        jmp eax
    }
}

// The factor rounded to float.
__declspec(naked) inline void __fastcall PowerToFloat(const float* /*damage*/, const float* /*effect*/, float* /*out*/) {
    __asm {
        call PowerCore
        mov eax, dword ptr [esp + 4]
        fstp dword ptr [eax]
        ret 4
    }
}

// The factor times *scale (unrounded product), rounded to float.
__declspec(naked) inline void __fastcall PowerScaledToFloat(const float*, const float*, const float* /*scale*/, float* /*out*/) {
    __asm {
        call PowerCore
        mov eax, dword ptr [esp + 4]
        fmul dword ptr [eax]
        mov eax, dword ptr [esp + 8]
        fstp dword ptr [eax]
        ret 8
    }
}

// The factor times *scale, truncated by the game's _ftol.
__declspec(naked) inline int __fastcall PowerScaledToInt(const float*, const float*, const float* /*scale*/) {
    __asm {
        call PowerCore
        mov eax, dword ptr [esp + 4]
        fmul dword ptr [eax]
        mov eax, 006E853Ch
        call eax
        ret 4
    }
}

// World slot `slot` (x, y): a ground height left in ST0; *out = (float)(height + *add).
__declspec(naked) inline void __stdcall HeightPlus(void* /*world*/, int /*slot*/, std::uint32_t /*x*/, std::uint32_t /*y*/,
                                            const float* /*add*/, float* /*out*/) {
    __asm {
        mov ecx, dword ptr [esp + 4]
        mov eax, dword ptr [ecx]
        mov edx, dword ptr [esp + 8]
        push dword ptr [esp + 10h]
        push dword ptr [esp + 10h]
        call dword ptr [eax + edx * 4]
        mov eax, dword ptr [esp + 14h]
        fadd dword ptr [eax]
        mov eax, dword ptr [esp + 18h]
        fstp dword ptr [eax]
        ret 18h
    }
}

// The same with height - *sub (FSUB).
__declspec(naked) inline void __stdcall HeightMinus(void*, int, std::uint32_t, std::uint32_t, const float* /*sub*/, float* /*out*/) {
    __asm {
        mov ecx, dword ptr [esp + 4]
        mov eax, dword ptr [ecx]
        mov edx, dword ptr [esp + 8]
        push dword ptr [esp + 10h]
        push dword ptr [esp + 10h]
        call dword ptr [eax + edx * 4]
        mov eax, dword ptr [esp + 14h]
        fsub dword ptr [eax]
        mov eax, dword ptr [esp + 18h]
        fstp dword ptr [eax]
        ret 18h
    }
}

// The lightning's end: *out = (float)(height + *a * *b).
__declspec(naked) inline void __stdcall HeightPlusProduct(void*, int, std::uint32_t, std::uint32_t, const float* /*a*/,
                                                   const float* /*b*/, float* /*out*/) {
    __asm {
        mov ecx, dword ptr [esp + 4]
        mov eax, dword ptr [ecx]
        mov edx, dword ptr [esp + 8]
        push dword ptr [esp + 10h]
        push dword ptr [esp + 10h]
        call dword ptr [eax + edx * 4]
        mov eax, dword ptr [esp + 14h]
        fld dword ptr [eax]
        mov eax, dword ptr [esp + 18h]
        fmul dword ptr [eax]
        faddp st(1), st
        mov eax, dword ptr [esp + 1Ch]
        fstp dword ptr [eax]
        ret 1Ch
    }
}

// *out = (float)sqrt(y*y + x*x).
__declspec(naked) inline void __fastcall PlanarLength(const Vector3* /*v*/, float* /*out*/) {
    __asm {
        fld dword ptr [ecx + 4]
        fmul dword ptr [ecx + 4]
        fld dword ptr [ecx]
        fmul dword ptr [ecx]
        faddp st(1), st
        fsqrt
        fstp dword ptr [edx]
        ret
    }
}

// The fireball's velocity: v *= (speed * 1/15) / sqrt((y*y + x*x) + z*z), the factor unrounded,
// each component rounded to float.
__declspec(naked) inline void __fastcall ScaleToSpeed(Vector3* /*v*/, const float* /*speed*/) {
    __asm {
        fld dword ptr [ecx + 4]
        fmul dword ptr [ecx + 4]
        fld dword ptr [ecx]
        fmul dword ptr [ecx]
        faddp st(1), st
        fld dword ptr [ecx + 8]
        fmul dword ptr [ecx + 8]
        faddp st(1), st
        fsqrt
        fld dword ptr [edx]
        fmul dword ptr ds:[00740544h]
        fdivrp st(1), st
        fld st(0)
        fmul dword ptr [ecx]
        fstp dword ptr [ecx]
        fld st(0)
        fmul dword ptr [ecx + 4]
        fstp dword ptr [ecx + 4]
        fmul dword ptr [ecx + 8]
        fstp dword ptr [ecx + 8]
        ret
    }
}

// FISTP((float)((distance / speed) * 15)): the missile's flight time in ticks.
__declspec(naked) inline int __fastcall FlightTicks(const float* /*distance*/, const float* /*speed*/) {
    __asm {
        sub esp, 4
        fld dword ptr [ecx]
        fdiv dword ptr [edx]
        fmul dword ptr ds:[0073C518h]
        fstp dword ptr [esp]
        fld dword ptr [esp]
        fistp dword ptr [esp]
        pop eax
        ret
    }
}

// The walls' extent: the normal (-d.y, d.x, 0) in the ground plane scaled to length `radius`:
// k = (float)(radius / sqrt(n.y*n.y + n.x*n.x)), then each component (float)(component * k).
__declspec(naked) inline void __stdcall WallSpan(const Vector3* /*direction*/, const float* /*radius*/, Vector3* /*out*/) {
    __asm {
        push ebx
        mov ecx, dword ptr [esp + 8]
        mov ebx, dword ptr [esp + 0Ch]
        mov edx, dword ptr [esp + 10h]
        sub esp, 8
        fld dword ptr [ecx + 4]
        fchs
        fst dword ptr [edx]
        mov eax, dword ptr [ecx]
        mov dword ptr [edx + 4], eax
        mov dword ptr [edx + 8], 0
        fld dword ptr [ebx]
        fld dword ptr [edx + 4]
        fmul dword ptr [edx + 4]
        fld st(2)
        fmul st, st(3)
        faddp st(1), st
        fsqrt
        fxch st(1)
        fdiv st, st(1)
        fstp dword ptr [esp]
        fstp st(0)
        fld dword ptr [esp]
        fmul st, st(1)
        fstp dword ptr [edx]
        fstp st(0)
        fld dword ptr [edx + 4]
        fmul dword ptr [esp]
        fstp dword ptr [edx + 4]
        fld dword ptr [edx + 8]
        fmul dword ptr [esp]
        fstp dword ptr [edx + 8]
        add esp, 8
        pop ebx
        ret 0Ch
    }
}

// The light wall's half extent from its lightning's ends (+0x14C from, +0x158 to): x keeps the
// difference unrounded before halving, y and z are rounded first.
__declspec(naked) inline void __fastcall HalfSpan(const CEngineObject* /*lightning*/, Vector3* /*out*/) {
    __asm {
        fld dword ptr [ecx + 158h]
        fsub dword ptr [ecx + 14Ch]
        fld dword ptr [ecx + 15Ch]
        fsub dword ptr [ecx + 150h]
        fstp dword ptr [edx + 4]
        fld dword ptr [ecx + 160h]
        fsub dword ptr [ecx + 154h]
        fstp dword ptr [edx + 8]
        fmul dword ptr ds:[0073B86Ch]
        fstp dword ptr [edx]
        fld dword ptr [edx + 4]
        fmul dword ptr ds:[0073B86Ch]
        fstp dword ptr [edx + 4]
        fld dword ptr [edx + 8]
        fmul dword ptr ds:[0073B86Ch]
        fstp dword ptr [edx + 8]
        ret
    }
}

// --- Small helpers ------------------------------------------------------------------------------

inline std::uint32_t Bits(const float& f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

inline bool IsUnit(CEngineObject* object) { return object && object->GetClassId() == kClassUnit; }
// Where the object sees from (slot 48) into `scratch`.
inline Vector3* PositionOf(CEngineObject* object, Vector3* scratch) { return ObjectSlots(object)->EyePoint(scratch); }
// A unit's server half (CUnit::controller).
inline CUnitServer* ControllerOf(CEngineObject* unit) { return static_cast<CUnitServer*>(static_cast<CUnit*>(unit)->controller); }

// A new object of class `classId` from the world's timed object manager (key -1).
inline CEngineObject* CreateObject(CSpellEffect* e, int classId) {
    return e->world->TimedObjectManager()->CreateObject(classId, -1);
}

// The counted-pointer assignment the helpers repeat: the old object leaves the world's timed
// objects and is released, the new one is counted.
inline void Assign(CSpellEffect* e, CEngineObject** slot, CEngineObject* object) {
    if (*slot) WorldRemoveTimed(e->world, nullptr, (*slot)->id_);
    CEngineObject* old = *slot;
    *slot = object;
    if (object) ++object->refCount_;
    if (old) old->Release();
}

inline void SetPosition(CEngineObject* object, const Vector3* position) {
    std::memcpy(&static_cast<CTransformObject*>(object)->localPosition_, position, 12);
}

// The fields of the spell prototype the casts read.
inline const char* Prototype(CSpellEffect* e) { return SpellGetPrototypeOriginal(e->Spell()); }
inline Vector3 PrototypeColor(CSpellEffect* e) {
    Vector3 color;
    std::memcpy(&color, Prototype(e) + 0x64, 12);
    return color;
}
inline float PrototypeRange(CSpellEffect* e) {
    float range;
    std::memcpy(&range, Prototype(e) + 0x70, 4);
    return range;
}

inline float Power(CSpellEffect* e) {
    float out;
    PowerToFloat(&e->damage, reinterpret_cast<const float*>(Prototype(e) + 0x2C), &out);
    return out;
}
inline float PowerScaled(CSpellEffect* e, std::uintptr_t scale) {
    float out;
    PowerScaledToFloat(&e->damage, reinterpret_cast<const float*>(Prototype(e) + 0x2C), ConstPtr(scale), &out);
    return out;
}

// The lights' rise speed: range / max(prototype +0x74, 1) (the larger picked on C0: 1 < v or NaN).
inline float LightRise(CSpellEffect* e) {
    float value;
    std::memcpy(&value, Prototype(e) + 0x74, 4);
    float range = PrototypeRange(e);
    float divisor = !(value <= 1.0f) ? value : 1.0f;
    return range / divisor;
}

// The same quotient left unrounded and multiplied by the constant at `scale`, then rounded.
inline float LightRiseScaled(CSpellEffect* e, std::uintptr_t scale) {
    float value;
    std::memcpy(&value, Prototype(e) + 0x74, 4);
    float range = PrototypeRange(e);
    float divisor = !(value <= 1.0f) ? value : 1.0f;
    float out;
    DivideMultiply(&range, &divisor, ConstPtr(scale), &out);
    return out;
}

// Every unit within the radius of the effect's position that IsSpellTarget accepts gets the
// effect's damage (a CPtrList of blocks of 10 on the stack, walked node by node).
inline void HitAreaTargets(CSpellEffect* e) {
    mfc::CTypedPtrList<CEngineObject> units;
    PtrListConstruct(&units, nullptr, kPtrListBlockSize);
    DamageInfo hit;
    SpellEffectGetDamageOriginal(e, nullptr, &hit);
    SectorMapFindUnitsInRadius(e->world->Sectors(), nullptr, &e->position, &units, e->radius);
    for (auto* node = units.head; node;) {
        CEngineObject* unit = node->data;
        node = node->next;
        if (static_cast<std::uint8_t>(IsAreaSpellTarget(unit))) EffectHitUnit(e, nullptr, &hit, unit);
    }
    PtrListDestruct(&units);
}

inline float GroundPlus(CSpellEffect* e, int slot, const Vector3& at, const float* add) {
    float out;
    HeightPlus(e->world, slot, Bits(at.x), Bits(at.y), add, &out);
    return out;
}
inline float GroundMinus(CSpellEffect* e, int slot, const Vector3& at, const float* sub) {
    float out;
    HeightMinus(e->world, slot, Bits(at.x), Bits(at.y), sub, &out);
    return out;
}

// The light every lasting spell puts at its point: the prototype's colour and range.
inline void PrototypeLight(CSpellEffect* e, const Vector3* at, float range, const Vector3* velocity) {
    Vector3 color = PrototypeColor(e);
    e->CreatePointLight(at, &color, range, velocity);
}

enum class Next { Done, Set, Leave };   // state 3; state set by the case; leave the state alone

inline const Vector3 kZeroVector{0.0f, 0.0f, 0.0f};

} // namespace spellfx
