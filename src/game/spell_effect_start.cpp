// A spell effect's casting state (CSpellEffect::Update state 0, 0067F870) and the helpers that
// create its effect objects (0067EF40-0067F870). Every spell type starts differently: missiles
// get a CEffectArrow or a particle source and go to state 1, the lasting spells get particles,
// lights and sound sequences and go to state 2, the instant ones (lightning, the light strike,
// buffs, charm) deal or apply their effect here. See docs/notes/items_spells.md.
#include "spell.h"

#include "console.h"
#include "lib/mfc.h"
#include "world_effect.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstdint>
#include <cstring>

namespace {

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

constexpr int kSlotRelease = 4;
constexpr int kSlotClassId = 12;
constexpr int kSlotPosition = 48;       // Vector3* (Vector3* scratch)
constexpr int kSlotApplyBuff = 45;      // units: (BuffRecord*)
constexpr int kSlotBusyA = 54;          // units: nonzero refuses a link
constexpr int kSlotBusyB = 55;
constexpr int kWorldSlotFactory = 14;   // world server: the object factory (+0x48)
constexpr int kFactorySlotCreate = 23;  // (class id, key -1)
constexpr int kWorldSlotGridHeight = 27;
constexpr int kWorldSlotGroundHeight = 28;
constexpr int kControllerSlotHeal = 21; // unit controller (+0x240): (float amount)

constexpr int kUnitController = 0x240;
constexpr int kUnitPosition = 0x1C;

constexpr std::uint32_t kPointLightFlags = 0x840;
constexpr int kPtrListBlockSize = 10;

// Volumes and radii the casts pass as immediates.
constexpr float kVolume = 10.0f;
constexpr float kHearing = 50.0f;
constexpr float kLoud = 1000.0f;

// Constants the original reads from memory (Rebirth may patch data; read them as the original does).
float ConstAt(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
const float* ConstPtr(std::uintptr_t address) { return reinterpret_cast<const float*>(address); }
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

const char* GameString(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

// --- Forwarders ----------------------------------------------------------------------------------

// The world server's timed objects (world_server.cpp; thiscall on the world).
__declspec(naked) void __fastcall WorldAddTimed(void* /*world*/, void*, CEngineObject*) EI_ORIGINAL(0x0058E520)
__declspec(naked) void __fastcall WorldRemoveTimed(void* /*world*/, void*, int /*id*/) EI_ORIGINAL(0x0058E5D0)
// Effect setters (world_effect*.cpp).
__declspec(naked) void __fastcall EffectSetVelocity(CEngineObject*, void*, const Vector3*) EI_ORIGINAL(0x00675D50)
__declspec(naked) void __fastcall ParticleSetAttachedTo(CEngineObject*, void*, int /*id*/) EI_ORIGINAL(0x00679280)
__declspec(naked) void __fastcall ParticleSetTarget(CEngineObject*, void*, const Vector3*) EI_ORIGINAL(0x00679310)
__declspec(naked) void __fastcall WallSetDirection(CEngineObject*, void*, const Vector3*) EI_ORIGINAL(0x00679C30)
__declspec(naked) void __fastcall LightningSetEnds(CEngineObject*, void*, const Vector3*, const Vector3*, float /*width*/)
    EI_ORIGINAL(0x0067A6B0)
__declspec(naked) void __fastcall LightningSetObjects(CEngineObject*, void*, int /*from*/, int /*to*/) EI_ORIGINAL(0x0067A890)
__declspec(naked) void __fastcall LightningSetFlag(CEngineObject*, void*, int /*flag byte*/) EI_ORIGINAL(0x0067A8C0)
// 0067AC70 (not rewritten): the missile's target object id.
__declspec(naked) void __fastcall ArrowSetTarget(CEngineObject*, void*, int /*id*/) EI_ORIGINAL(0x0067AC70)
// 0067C640: CEffectShowVisibility's two object ids (+0x14C, +0x150).
__declspec(naked) void __fastcall ShowVisibilitySetObjects(CEngineObject*, void*, int, int) EI_ORIGINAL(0x0067C640)
// 00688510: the buff record's constructor (fastcall; returns it).
struct BuffRecord;
__declspec(naked) BuffRecord* __fastcall BuffRecordInit(BuffRecord*) EI_ORIGINAL(0x00688510)
// The spell's own helpers (spell.cpp / spell_effect.cpp; through their addresses for the verify wrappers).
__declspec(naked) char* __fastcall SpellPrototype(CSpell*) EI_ORIGINAL(0x0068BA20)
__declspec(naked) BOOL __fastcall EffectGetDamage(CSpellEffect*, void*, DamageInfo*) EI_ORIGINAL(0x00683950)
__declspec(naked) void __fastcall EffectHitUnit(CSpellEffect*, void*, DamageInfo*, CEngineObject*) EI_ORIGINAL(0x006835B0)
__declspec(naked) int __fastcall IsAreaSpellTarget(CEngineObject*) EI_ORIGINAL(0x00681B80)
// 005C0BD0 CSectorMap::FindUnitsInRadius (sector_map.cpp).
__declspec(naked) void __fastcall FindUnitsInRadius(void* /*sectors*/, void*, const Vector3*, void* /*list*/, float)
    EI_ORIGINAL(0x005C0BD0)
// CPtrList constructor (block size) and destructor.
__declspec(naked) void* __fastcall PtrListConstruct(void*, void*, int) EI_ORIGINAL(0x004C96A0)
__declspec(naked) void __fastcall PtrListDestruct(void*) EI_ORIGINAL(0x004C96F0)
// The unit controller (+0x240): 00552780 says remark `what`; 00551AE0 tries to be tamed by `tamer`
// with skill `skill` (0 when it failed).
__declspec(naked) void __fastcall ControllerShout(void*, void*, int /*what*/) EI_ORIGINAL(0x00552780)
__declspec(naked) int __fastcall ControllerTame(void*, void*, CEngineObject* /*tamer*/, float /*skill*/) EI_ORIGINAL(0x00551AE0)
// Footprints of the walls and clouds in the global cell set 007C8478 (thiscall on the set): along a
// direction (005B2A00) or a disc (005B2760); then stamped into / cleared from the AI map (ai_footprint.cpp).
__declspec(naked) void __fastcall FootprintAlong(void* /*set*/, void*, void* /*aiMap*/, const Vector3*, float /*width*/, const Vector3*)
    EI_ORIGINAL(0x005B2A00)
__declspec(naked) void __fastcall FootprintDisc(void* /*set*/, void*, void* /*aiMap*/, const Vector3*, float /*radius*/)
    EI_ORIGINAL(0x005B2760)
__declspec(naked) bool __fastcall AiMapStamp(void* /*aiMap*/, void*, int /*owner*/, void* /*set*/) EI_ORIGINAL(0x005B7820)
__declspec(naked) bool __fastcall AiMapClear(void* /*aiMap*/, void*, void* /*set*/) EI_ORIGINAL(0x005B78F0)
void* SpellFootprint() { return reinterpret_cast<void*>(0x007C8478); }

// --- x87 sequences ------------------------------------------------------------------------------

// The spell's power factor, repeated inline about twenty times in the original: 1 when the
// prototype's effect (+0x2C) compares equal to 0 (or is NaN), else
// pow(min(max(damage / effect, 1), 8), 0.33) - 0.2 written as FYL2X / F2XM1 / FSCALE. The two
// clamps select the float bits on C0 (1 < r or unordered; r < 8 or unordered), the first one
// stored and reloaded. ECX = &damage, EDX = &prototype effect; the factor is left in ST0.
__declspec(naked) void PowerCore() {
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
        sub esp, 8
        fstp dword ptr [esp]
        fstp st(0)
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

// The factor rounded to float.
__declspec(naked) void __fastcall PowerToFloat(const float* /*damage*/, const float* /*effect*/, float* /*out*/) {
    __asm {
        call PowerCore
        mov eax, dword ptr [esp + 4]
        fstp dword ptr [eax]
        ret 4
    }
}

// The factor times *scale (unrounded product), rounded to float.
__declspec(naked) void __fastcall PowerScaledToFloat(const float*, const float*, const float* /*scale*/, float* /*out*/) {
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
__declspec(naked) int __fastcall PowerScaledToInt(const float*, const float*, const float* /*scale*/) {
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
__declspec(naked) void __stdcall HeightPlus(void* /*world*/, int /*slot*/, std::uint32_t /*x*/, std::uint32_t /*y*/,
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
__declspec(naked) void __stdcall HeightMinus(void*, int, std::uint32_t, std::uint32_t, const float* /*sub*/, float* /*out*/) {
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
__declspec(naked) void __stdcall HeightPlusProduct(void*, int, std::uint32_t, std::uint32_t, const float* /*a*/,
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
__declspec(naked) void __fastcall PlanarLength(const Vector3* /*v*/, float* /*out*/) {
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
__declspec(naked) void __fastcall ScaleToSpeed(Vector3* /*v*/, const float* /*speed*/) {
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
__declspec(naked) int __fastcall FlightTicks(const float* /*distance*/, const float* /*speed*/) {
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
__declspec(naked) void __stdcall WallSpan(const Vector3* /*direction*/, const float* /*radius*/, Vector3* /*out*/) {
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
__declspec(naked) void __fastcall HalfSpan(const CEngineObject* /*lightning*/, Vector3* /*out*/) {
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

std::uint32_t Bits(const float& f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

int ClassId(const CEngineObject* object) { return ei::VCall<int>(object, kSlotClassId); }
bool IsUnit(const CEngineObject* object) { return object && ClassId(object) == kClassUnit; }
void Release(CEngineObject* object) { ei::VCall<void>(object, kSlotRelease); }
Vector3* PositionOf(CEngineObject* object, Vector3* scratch) { return ei::VCall<Vector3*>(object, kSlotPosition, scratch); }
void* ControllerOf(CEngineObject* unit) { return At<void*>(unit, kUnitController); }

CEngineObject* CreateObject(CSpellEffect* e, int classId) {
    void* factory = ei::VCall<void*>(e->world, kWorldSlotFactory);
    return ei::VCall<CEngineObject*>(factory, kFactorySlotCreate, classId, -1);
}

// The counted-pointer assignment the helpers repeat: the old object leaves the world's timed
// objects and is released, the new one is counted.
void Assign(CSpellEffect* e, CEngineObject** slot, CEngineObject* object) {
    if (*slot) WorldRemoveTimed(e->world, nullptr, (*slot)->id_);
    CEngineObject* old = *slot;
    *slot = object;
    if (object) ++object->refCount_;
    if (old) Release(old);
}

void SetPosition(CEngineObject* object, const Vector3* position) { std::memcpy(&At<Vector3>(object, 0x1C), position, 12); }

// The fields of the spell prototype the casts read.
const char* Prototype(CSpellEffect* e) { return SpellPrototype(e->Spell()); }
Vector3 PrototypeColor(CSpellEffect* e) {
    Vector3 color;
    std::memcpy(&color, Prototype(e) + 0x64, 12);
    return color;
}
float PrototypeRange(CSpellEffect* e) {
    float range;
    std::memcpy(&range, Prototype(e) + 0x70, 4);
    return range;
}

float Power(CSpellEffect* e) {
    float out;
    PowerToFloat(&e->damage, reinterpret_cast<const float*>(Prototype(e) + 0x2C), &out);
    return out;
}
float PowerScaled(CSpellEffect* e, std::uintptr_t scale) {
    float out;
    PowerScaledToFloat(&e->damage, reinterpret_cast<const float*>(Prototype(e) + 0x2C), ConstPtr(scale), &out);
    return out;
}

// The lights' rise speed: range / max(prototype +0x74, 1) (the larger picked on C0: 1 < v or NaN).
float LightRise(CSpellEffect* e) {
    float value;
    std::memcpy(&value, Prototype(e) + 0x74, 4);
    float range = PrototypeRange(e);
    float divisor = !(value <= 1.0f) ? value : 1.0f;
    return range / divisor;
}

float GroundPlus(CSpellEffect* e, int slot, const Vector3& at, const float* add) {
    float out;
    HeightPlus(e->world, slot, Bits(at.x), Bits(at.y), add, &out);
    return out;
}
float GroundMinus(CSpellEffect* e, int slot, const Vector3& at, const float* sub) {
    float out;
    HeightMinus(e->world, slot, Bits(at.x), Bits(at.y), sub, &out);
    return out;
}

// The light every lasting spell puts at its point: the prototype's colour and range.
void PrototypeLight(CSpellEffect* e, const Vector3* at, float range, const Vector3* velocity) {
    Vector3 color = PrototypeColor(e);
    e->CreatePointLight(at, &color, range, velocity);
}

// The record a buff spell hands to its unit (slot 45): the spell's bytes +0x1C..+0x2F with the
// strength byte replaced and the effect's +0xA4 flag.
struct BuffRecord {
    std::uint8_t index;
    std::uint8_t strength;
    std::uint16_t unknown_02;
    int duration;
    float effect;
    float radius;
    std::uint8_t flag;
    std::uint8_t unknown_11[3];
};
static_assert(sizeof(BuffRecord) == 0x14);

enum class Next { Done, Set, Leave };   // state 3; state set by the case; leave the state alone

const Vector3 kZeroVector{0.0f, 0.0f, 0.0f};

// --- The spell types ----------------------------------------------------------------------------

// 0 (0x2011) and 0x28 (0x204E): the fire arrow, a missile at a unit target.
Next StartFireArrow(CSpellEffect* e, int type, const Vector3& from) {
    if (!IsUnit(e->target)) return Next::Done;
    e->timer = 0x96;
    float range = PrototypeRange(e);
    Vector3 color = PrototypeColor(e);
    float power = Power(e);
    e->CreateArrow(&from, e->target, type == 0 ? 0x2011 : 0x204E, power, GameString(0x00795738), &color, range);
    e->CreateSound(0, GameString(0x0079571C), &e->position, &kZeroVector, kVolume, kHearing);
    e->state = 1;
    return Next::Set;
}

// 1 and 0x29: lightning from the caster to the target, hitting it at once.
Next StartLightning(CSpellEffect* e, int type, const Vector3& from) {
    float power = Power(e);
    e->CreateLightning(&from, e->target, e->caster, power, type == 0x29 ? 1 : 0);
    e->CreateSound(0, GameString(0x007956CC), &e->position, &kZeroVector, kVolume, kHearing);
    float range = PrototypeRange(e);
    Vector3 color = PrototypeColor(e);
    Vector3 scratch;
    e->CreatePointLight(PositionOf(e->target, &scratch), &color, range, &kZeroVector);   // no null check
    DamageInfo hit;
    EffectGetDamage(e, nullptr, &hit);
    if (IsUnit(e->target)) EffectHitUnit(e, nullptr, &hit, e->target);
    e->timer = 7;
    e->state = 2;
    return Next::Set;
}

// 2: the acid ray, a missile (power x 0.4).
Next StartAcidRay(CSpellEffect* e, const Vector3& from) {
    if (!IsUnit(e->target)) return Next::Done;
    e->timer = 0x96;
    float range = PrototypeRange(e);
    Vector3 color = PrototypeColor(e);
    float power = PowerScaled(e, kAcidRayScale);
    e->CreateArrow(&from, e->target, 0x2012, power, GameString(0x00795700), &color, range);
    e->CreateSound(0, GameString(0x007956E8), &e->position, &kZeroVector, kVolume, kHearing);
    e->state = 1;
    return Next::Set;
}

// 3: the fireball, a particle source flying at the target point; it explodes in state 1.
Next StartFireball(CSpellEffect* e, const Vector3& from) {
    float speed = e->speed;
    Vector3 velocity;
    velocity.x = e->targetPoint.x - e->position.x;
    velocity.y = e->targetPoint.y - e->position.y;
    velocity.z = e->targetPoint.z - e->position.z;
    velocity.z = 0.0f;
    float distance;
    PlanarLength(&velocity, &distance);
    if (!(speed > 0.0f)) speed = kDefaultSpeed;
    e->timer = FlightTicks(&distance, &speed) - 2;
    ScaleToSpeed(&velocity, &speed);

    float size = PowerScaled(e, kFireballScale);
    e->CreateParticle(1, 0x2000, &from, size, 1.0f, &velocity, nullptr);
    if (e->caster) At<int>(e->visual, 0x17C) = e->caster->id_;   // the particle's unit (no null check)
    e->targetPoint.z = GroundPlus(e, kWorldSlotGroundHeight, e->targetPoint, ConstPtr(kOne));
    ParticleSetTarget(e->visual, nullptr, &e->targetPoint);
    if (speed * ConstAt(kFireballNear) > distance) {
        e->CreateSound(0, GameString(0x007956B0), &e->position, &velocity, kVolume, kHearing);
    } else {
        e->CreateSoundSequence(&e->position, &velocity, GameString(0x00795678), GameString(0x00795694), -1, kVolume,
                               kHearing);
    }
    float range = PrototypeRange(e);
    Vector3 color = PrototypeColor(e);
    e->CreatePointLight(&from, &color, range, &velocity);
    e->state = 1;
    return Next::Set;
}

// 4: a lightning strike from 30 above the target point, hitting every unit in the radius.
Next StartLightStrike(CSpellEffect* e) {
    e->targetPoint.z = GroundMinus(e, kWorldSlotGroundHeight, e->targetPoint, ConstPtr(kStrikeDepth));
    e->position = e->targetPoint;
    Vector3 top = e->targetPoint;
    top.z = top.z + ConstAt(kStrikeHeight);
    float power = Power(e);
    CEngineObject* bolt = CreateObject(e, kClassLightning);
    LightningSetEnds(bolt, nullptr, &top, &e->position, power * ConstAt(kStrikeWidth));
    e->SetVisual(bolt);

    float alpha = Power(e);
    e->CreateParticle(0, 0x2030, &e->targetPoint, e->radius, alpha, &kZeroVector, nullptr);
    e->CreateSound(0, GameString(0x0079565C), &e->position, &kZeroVector, kLoud, kLoud);
    e->targetPoint.z = e->targetPoint.z + ConstAt(kLightAbove);
    Vector3 rise{0.0f, 0.0f, LightRise(e)};
    PrototypeLight(e, &e->targetPoint, PrototypeRange(e), &rise);

    mfc::CTypedPtrList<CEngineObject> units;
    PtrListConstruct(&units, nullptr, kPtrListBlockSize);
    DamageInfo hit;
    EffectGetDamage(e, nullptr, &hit);
    FindUnitsInRadius(e->world + 0x170 + 0x124, nullptr, &e->position, &units, e->radius);
    for (auto* node = units.head; node;) {
        CEngineObject* unit = node->data;
        node = node->next;
        if (static_cast<std::uint8_t>(IsAreaSpellTarget(unit))) EffectHitUnit(e, nullptr, &hit, unit);
    }
    PtrListDestruct(&units);
    e->timer = 7;
    e->state = 2;
    return Next::Set;
}

// 5: the acid column at the target point.
Next StartAcidColumn(CSpellEffect* e) {
    e->position = e->targetPoint;
    e->CreateParticle(0, 0x200C, &e->targetPoint, 1.0f, 1.0f, &kZeroVector, nullptr);
    e->CreateSound(0, GameString(0x00795640), &e->targetPoint, &kZeroVector, kVolume, kHearing);
    Vector3 rise{0.0f, 0.0f, LightRise(e)};
    PrototypeLight(e, &e->targetPoint, PrototypeRange(e), &rise);
    e->timer = 0x1E;
    e->state = 2;
    return Next::Set;
}

// The direction from the caster to the wall's centre; straight along x when it has no ground part
// (both compare equal to 0, NaN included).
Vector3 WallDirection(const Vector3& centre, const Vector3& from) {
    Vector3 d;
    d.x = centre.x - from.x;
    d.y = centre.y - from.y;
    d.z = centre.z - from.z;
    auto zeroOrNaN = [](float v) { return !(v < 0.0f) && !(v > 0.0f); };
    if (zeroOrNaN(d.x) && zeroOrNaN(d.y)) d.x = 1.0f;
    return d;
}

// 6: the fire wall across the line from the caster to the target point.
Next StartFireWall(CSpellEffect* e, const Vector3& from) {
    Vector3 centre = e->targetPoint;
    float alpha = Power(e);
    float size = Power(e);
    e->CreateParticle(1, 0x2010, &centre, size, alpha, &kZeroVector, nullptr);
    Vector3 direction = WallDirection(centre, from);
    Vector3 span;
    WallSpan(&direction, &e->radius, &span);
    if (e->visual) WallSetDirection(e->visual, nullptr, &span);
    e->position = centre;
    e->timer = e->targetCount;
    e->state = 2;
    centre.z = GroundPlus(e, kWorldSlotGridHeight, centre, ConstPtr(kOne));
    float radius = e->radius;
    float range = PrototypeRange(e);
    PrototypeLight(e, &centre, radius + range, &kZeroVector);
    e->CreateSoundSequence(&centre, &kZeroVector, GameString(0x00795608), GameString(0x00795624), -1, kVolume, kHearing);
    return Next::Set;
}

// 7: the light wall, a lightning between the two ends of the same span.
Next StartLightWall(CSpellEffect* e, const Vector3& from) {
    Vector3 centre = e->targetPoint;
    Vector3 direction = WallDirection(centre, from);
    Vector3 span;
    WallSpan(&direction, &e->radius, &span);
    Vector3 a, b;
    a.x = e->targetPoint.x - span.x;
    a.y = e->targetPoint.y - span.y;
    a.z = e->targetPoint.z - span.z;
    b.x = span.x + e->targetPoint.x;
    b.y = span.y + e->targetPoint.y;
    b.z = span.z + e->targetPoint.z;
    a.z = GroundPlus(e, kWorldSlotGroundHeight, a, ConstPtr(kOne));
    b.z = GroundPlus(e, kWorldSlotGroundHeight, b, ConstPtr(kOne));
    CEngineObject* bolt = CreateObject(e, kClassLightning);
    LightningSetEnds(bolt, nullptr, &a, &b, 0.0f);
    e->SetVisual(bolt);
    e->position = centre;
    e->timer = e->targetCount;
    e->state = 2;
    centre.z = GroundPlus(e, kWorldSlotGridHeight, centre, ConstPtr(kOne));
    float radius = e->radius;
    float range = PrototypeRange(e);
    PrototypeLight(e, &centre, radius + range, &kZeroVector);
    e->CreateSoundSequence(&centre, &kZeroVector, GameString(0x0079B298), GameString(0x007955EC), -1, kVolume, kHearing);
    return Next::Set;
}

// 8: the poison cloud at the target point.
Next StartPoisonCloud(CSpellEffect* e) {
    Vector3 centre = e->targetPoint;
    centre.z = GroundPlus(e, kWorldSlotGridHeight, centre, ConstPtr(kOne));
    float alpha = Power(e);
    e->CreateParticle(1, 0x2007, &centre, e->radius, alpha, &kZeroVector, nullptr);
    e->CreateSoundSequence(&centre, &kZeroVector, GameString(0x007955BC), GameString(0x007955D4), -1, kVolume, kHearing);
    float radius = e->radius;
    float range = PrototypeRange(e);
    PrototypeLight(e, &centre, radius + range, &kZeroVector);
    e->position = centre;
    return Next::Set;   // state 2, timer = duration (the shared tail)
}

// 9-0x11, 0x13, 0x19, 0x1A, 0x1D-0x22, 0x25, 0x26: buffs. The target gets a record of the spell
// with the strength trunc(power x 16); the effect itself ends.
Next ApplyBuff(CSpellEffect* e) {
    if (!e->target) return Next::Done;
    BuffRecord record;
    BuffRecordInit(&record);
    std::memcpy(&record, &e->unknown_50, sizeof(record));
    record.strength = static_cast<std::uint8_t>(
        PowerScaledToInt(&e->damage, reinterpret_cast<const float*>(Prototype(e) + 0x2C), ConstPtr(kBuffScale)));
    record.flag = e->unknown_A4;
    ei::VCall<void>(e->target, kSlotApplyBuff, &record);
    return Next::Done;
}

// 0x12: fireworks (five numbered sounds).
Next StartFirework(CSpellEffect* e) {
    Vector3 centre = e->targetPoint;
    centre.z = GroundPlus(e, kWorldSlotGridHeight, centre, ConstPtr(kOne));
    float size = Power(e);
    e->CreateParticle(1, 0x2009, &centre, size, 1.0f, &kZeroVector, nullptr);
    PrototypeLight(e, &centre, PrototypeRange(e), &kZeroVector);
    e->CreateSoundSequence(&centre, &kZeroVector, GameString(0x0079B298), GameString(0x007955A4), 5, kVolume, kHearing);
    e->position = centre;
    return Next::Set;
}

// 0x14: clairvoyance. The particle uses the target point as it was; the light, the ground point.
Next StartClairvoyance(CSpellEffect* e) {
    Vector3 centre = e->targetPoint;
    centre.z = GroundPlus(e, kWorldSlotGridHeight, centre, ConstPtr(kOne));
    e->position = centre;
    e->CreateParticle(0, 0x2018, &e->targetPoint, e->radius, 1.0f, &kZeroVector, nullptr);
    PrototypeLight(e, &centre, e->radius, &kZeroVector);
    e->CreateSound(0, GameString(0x00795584), &e->position, &kZeroVector, kVolume, kHearing);
    return Next::Set;
}

// 0x15: possession (only the sound here).
Next StartPossession(CSpellEffect* e) {
    if (!e->target) return Next::Done;
    Vector3 scratch;
    e->CreateSound(0, GameString(0x007954EC), PositionOf(e->target, &scratch), &kZeroVector, kVolume, kHearing);
    return Next::Set;
}

// 0x17: vision fog: a CEffectShowVisibility linking the caster and the target.
Next StartVisionFog(CSpellEffect* e) {
    if (!e->caster || !e->target) return Next::Done;
    int targetId = e->target->id_;
    int casterId = e->caster->id_;
    CEngineObject* shown = CreateObject(e, kClassShowVisibility);
    ShowVisibilitySetObjects(shown, nullptr, casterId, targetId);
    SetPosition(shown, &kZeroVector);
    e->SetVisual(shown);
    Vector3 scratch;
    e->CreateSound(0, GameString(0x007954D0), PositionOf(e->target, &scratch), &kZeroVector, kVolume, kHearing);
    return Next::Set;
}

// 0x18: healing. Heals the target's controller by the effect value; the sound and the light only
// when the target is not at full health (or +0xA4 is clear).
Next StartHealing(CSpellEffect* e) {
    if (ClassId(e->target) != kClassUnit) return Next::Done;   // no null check
    ei::VCall<void>(ControllerOf(e->target), kControllerSlotHeal, Bits(e->damage));
    CEngineObject* unit = e->target;
    float size = Power(e);
    e->CreateParticle(0, 0x2006, &kZeroVector, size, 1.0f, &kZeroVector, unit);
    bool full = At<std::uint16_t>(e->target, 0x17E) == At<std::uint16_t>(e->target, 0x180);
    if (e->unknown_A4 == 0 || !full) {
        e->CreateSound(0, GameString(0x00795520), &At<Vector3>(e->target, kUnitPosition), &kZeroVector, kVolume, kHearing);
        Vector3 rise{0.0f, 0.0f, LightRise(e)};
        float range = PrototypeRange(e);
        Vector3 color = PrototypeColor(e);
        e->CreatePointLight(&e->targetPoint, &color, -range, &rise);
    }
    e->timer = 0x1E;
    e->state = 2;
    return Next::Set;
}

// 0x1B: link between two different units that are both free; otherwise the caster complains (0x18).
Next StartLink(CSpellEffect* e) {
    if (!IsUnit(e->target) || !IsUnit(e->caster)) return Next::Done;
    bool free = ei::VCall<int>(e->target, kSlotBusyA) == 0 && ei::VCall<int>(e->caster, kSlotBusyA) == 0 &&
                ei::VCall<int>(e->target, kSlotBusyB) == 0 && e->target != e->caster;
    if (!free) {
        if (IsUnit(e->caster)) ControllerShout(ControllerOf(e->caster), nullptr, 0x18);
        return Next::Done;
    }
    Vector3 scratch;
    const Vector3* casterAt = PositionOf(e->caster, &scratch);
    Vector3 from = *casterAt;
    e->position = *casterAt;
    e->CreateParticle(1, 0x2014, &from, 1.0f, 1.0f, &kZeroVector, nullptr);
    if (e->visual) {
        const Vector3* to = PositionOf(e->target, &scratch);
        Vector3 direction;
        direction.x = to->x - from.x;
        direction.y = to->y - from.y;
        direction.z = to->z - from.z;
        WallSetDirection(e->visual, nullptr, &direction);
    }
    e->CreateSoundSequence(&from, &kZeroVector, GameString(0x00795554), GameString(0x0079556C), -1, kVolume, kHearing);
    return Next::Set;
}

// 0x1C: teleport: a particle where the caster stands and an inverted one at the target point.
Next StartTeleport(CSpellEffect* e) {
    const Vector3* source = e->caster ? &At<Vector3>(e->caster, kUnitPosition) : &e->position;
    Vector3 point = *source;
    point.z = GroundPlus(e, kWorldSlotGridHeight, point, &point.z);
    e->CreateParticle(1, 0x2019, &point, 1.0f, 1.0f, &kZeroVector, nullptr);
    point = e->targetPoint;
    point.z = GroundMinus(e, kWorldSlotGridHeight, point, ConstPtr(kTeleportDepth));
    e->CreateParticle(0, 0x2019, &point, -1.0f, 1.0f, &kZeroVector, nullptr);
    e->CreateSound(0, GameString(0x00795538), &point, &kZeroVector, kVolume, kHearing);
    PrototypeLight(e, &point, PrototypeRange(e), &kZeroVector);
    int duration = e->targetCount;
    if (duration < 1) duration = 1;
    e->state = 2;
    e->timer = duration + 0x1E;
    return Next::Set;
}

// 0x24: charm. The target's controller is tamed by the caster until their sides (controller
// +0x1C, +0x24C) agree; a failed attempt makes the caster complain (0x1A).
Next StartCharm(CSpellEffect* e) {
    if (ClassId(e->target) != kClassUnit) return Next::Leave;   // no null check
    if (!IsUnit(e->caster)) return Next::Leave;
    void* tamed = ControllerOf(e->target);
    void* tamer = ControllerOf(e->caster);
    float skill = At<float>(tamer, 0x200) + e->damage;
    auto side = [](void* controller) { return At<int>(At<void*>(controller, 0x1C), 0x24C); };
    while (side(tamed) != side(tamer)) {
        if (ControllerTame(tamed, nullptr, e->caster, skill) == 0) {
            if (IsUnit(e->caster)) ControllerShout(ControllerOf(e->caster), nullptr, 0x1A);
            return Next::Done;
        }
    }
    e->CreateSound(0, GameString(0x00795508), &At<Vector3>(e->target, kUnitPosition), &kZeroVector, kVolume, kHearing);
    return Next::Done;
}

// 0x27: the effect only moves to the ground point.
Next StartAtGround(CSpellEffect* e) {
    Vector3 centre = e->targetPoint;
    centre.z = GroundPlus(e, kWorldSlotGridHeight, centre, ConstPtr(kOne));
    e->position = centre;
    return Next::Set;
}

} // namespace

// --- CSpellEffect ---------------------------------------------------------------------------------

void CSpellEffect::Start(int type) {
    flags &= ~1u;
    Vector3 from;
    if (caster) {
        Vector3 scratch;
        from = *PositionOf(caster, &scratch);
    } else {
        from = position;
    }

    // The cases that end in the shared tail (state 2, timer = the spell's duration) return Set
    // without touching the state; the others set it themselves.
    Next next = Next::Done;
    bool durationTail = false;
    switch (type) {
    case 0x00:
    case 0x28: next = StartFireArrow(this, type, from); break;
    case 0x01:
    case 0x29: next = StartLightning(this, type, from); break;
    case 0x02: next = StartAcidRay(this, from); break;
    case 0x03: next = StartFireball(this, from); break;
    case 0x04: next = StartLightStrike(this); break;
    case 0x05: next = StartAcidColumn(this); break;
    case 0x06: next = StartFireWall(this, from); break;
    case 0x07: next = StartLightWall(this, from); break;
    case 0x08: next = StartPoisonCloud(this); durationTail = true; break;
    case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x11:
    case 0x13: case 0x19: case 0x1A: case 0x1D: case 0x1E: case 0x1F: case 0x20: case 0x21: case 0x22:
    case 0x25: case 0x26: next = ApplyBuff(this); break;
    case 0x12: next = StartFirework(this); durationTail = true; break;
    case 0x14: next = StartClairvoyance(this); durationTail = true; break;
    case 0x15: next = StartPossession(this); durationTail = true; break;
    case 0x17: next = StartVisionFog(this); durationTail = true; break;
    case 0x18: next = StartHealing(this); break;
    case 0x1B: next = StartLink(this); durationTail = true; break;
    case 0x1C: next = StartTeleport(this); break;
    case 0x24: next = StartCharm(this); break;
    case 0x27: next = StartAtGround(this); durationTail = true; break;
    default: break;   // 0x16, 0x23 and above 0x29
    }
    if (next == Next::Done) {
        state = 3;
    } else if (next == Next::Set && durationTail) {
        state = 2;
        timer = targetCount;
    }
    UpdateFootprint(type, 1);
}
EI_REPLACE_METHOD(0x0067F870, CSpellEffect::Start);

void CSpellEffect::SetVisual(CEngineObject* object) {
    Assign(this, &visual, object);
    WorldAddTimed(world, nullptr, visual);
}
EI_REPLACE_METHOD(0x0067EF40, CSpellEffect::SetVisual);

void CSpellEffect::UpdateFootprint(int type, int stamp) {
    if (!visual) return;
    void* aiMap = world + 0x170;
    switch (type) {
    case 6: {
        Vector3 direction;
        std::memcpy(&direction, reinterpret_cast<char*>(visual) + 0x180, sizeof(direction));
        FootprintAlong(SpellFootprint(), nullptr, aiMap, &position, kHalfBits, &direction);
        break;
    }
    case 7: {
        Vector3 half;
        HalfSpan(visual, &half);
        FootprintAlong(SpellFootprint(), nullptr, aiMap, &position, kHalfBits, &half);
        break;
    }
    case 8:
    case 0x27: FootprintDisc(SpellFootprint(), nullptr, aiMap, &position, radius); break;
    default: return;
    }
    if (stamp) {
        AiMapStamp(aiMap, nullptr, At<int>(world, 0x64), SpellFootprint());
        footprintStamped = 1;
    } else {
        AiMapClear(aiMap, nullptr, SpellFootprint());
        footprintStamped = 0;
    }
}
EI_REPLACE_METHOD(0x0067EFC0, CSpellEffect::UpdateFootprint);

void CSpellEffect::CreateArrow(const Vector3* from, CEngineObject* missileTarget, int particleType, float power,
                               const char* soundName, const Vector3* color, float range) {
    CEngineObject* arrow = CreateObject(this, kClassArrow);
    if (!arrow) return;
    std::memcpy(&At<Vector3>(arrow, 0x6A4), color, 12);
    std::memcpy(&At<float>(arrow, 0x6B0), &range, 4);
    SetPosition(arrow, from);
    ArrowSetTarget(arrow, nullptr, missileTarget->id_);
    At<int>(arrow, 0x6B4) = particleType;
    std::memcpy(&At<float>(arrow, 0x6B8), &power, 4);
    At<mfc::CString>(arrow, 0x6A0) = soundName;
    if (caster) {
        At<std::uint8_t>(arrow, 0x154) = 1;
        At<int>(arrow, 0x6BC) = caster->id_;
    }
    Assign(this, &visual, arrow);
    WorldAddTimed(world, nullptr, visual);
}
EI_REPLACE_METHOD(0x0067F170, CSpellEffect::CreateArrow);

CEngineObject* CSpellEffect::CreateParticle(int keep, int particleType, const Vector3* at, float size, float alpha,
                                            const Vector3* velocity, CEngineObject* attachTo) {
    int classId = (particleType == 0x2010 || particleType == 0x2014) ? kClassWall : kClassParticle;
    CEngineObject* particle = CreateObject(this, classId);
    At<int>(particle, 0x158) = particleType;
    SetPosition(particle, at);
    std::memcpy(&At<float>(particle, 0x15C), &size, 4);
    EffectSetVelocity(particle, nullptr, velocity);
    std::memcpy(&At<float>(particle, 0x160), &alpha, 4);
    if (attachTo) ParticleSetAttachedTo(particle, nullptr, attachTo->id_);
    if (keep) {
        Assign(this, &visual, particle);
        WorldAddTimed(world, nullptr, visual);
    } else {
        WorldAddTimed(world, nullptr, particle);
    }
    return particle;
}
EI_REPLACE_METHOD(0x0067F260, CSpellEffect::CreateParticle);

void CSpellEffect::CreateLightning(const Vector3* from, CEngineObject* boltTarget, CEngineObject* source, float power,
                                   int flag) {
    if (!boltTarget) return;
    Vector3 to = At<Vector3>(boltTarget, kUnitPosition);
    HeightPlusProduct(world, kWorldSlotGridHeight, Bits(to.x), Bits(to.y), &At<float>(boltTarget, 0x14C),
                      ConstPtr(kTargetHeight), &to.z);
    CEngineObject* bolt = CreateObject(this, kClassLightning);
    LightningSetEnds(bolt, nullptr, from, &to, power * ConstAt(kBoltWidthScale));
    LightningSetObjects(bolt, nullptr, source ? source->id_ : -1, boltTarget->id_);
    LightningSetFlag(bolt, nullptr, flag);
    Assign(this, &visual, bolt);
    WorldAddTimed(world, nullptr, visual);
}
EI_REPLACE_METHOD(0x0067F410, CSpellEffect::CreateLightning);

void CSpellEffect::CreateSound(int keep, const char* name, const Vector3* at, const Vector3* velocity, float volume,
                               float hearing) {
    CEngineObject* effect = CreateObject(this, kClassSound);
    SetPosition(effect, at);
    At<std::uint8_t>(effect, 0x258) = 1;
    std::memcpy(&At<float>(effect, 0x250), &hearing, 4);
    std::memcpy(&At<float>(effect, 0x24C), &volume, 4);
    std::memcpy(&At<char>(effect, 0x14C), name, std::strlen(name) + 1);
    EffectSetVelocity(effect, nullptr, velocity);
    At<int>(effect, 0x254) = keep;
    if (keep) Assign(this, &sound, effect);
    WorldAddTimed(world, nullptr, effect);
}
EI_REPLACE_METHOD(0x0067F580, CSpellEffect::CreateSound);

void CSpellEffect::CreateSoundSequence(const Vector3* at, const Vector3* velocity, const char* first,
                                       const char* pattern, int count, float volume, float hearing) {
    mfc::CString name;
    CEngineObject* effect = CreateObject(this, kClassSoundSequence);
    SetPosition(effect, at);
    EffectSetVelocity(effect, nullptr, velocity);
    std::memcpy(&At<float>(effect, 0x168), &hearing, 4);
    std::memcpy(&At<float>(effect, 0x164), &volume, 4);
    At<std::uint8_t>(effect, 0x16C) = 1;
    At<mfc::CString>(effect, 0x14C) = first;
    auto& names = At<console::CStringArray>(effect, 0x150);
    if (count < 1) {
        names.SetAtGrow(names.size_, pattern);
    } else {
        for (int i = 1; i <= count; ++i) {
            name.Format(pattern, i);
            names.SetAtGrow(names.size_, name);
        }
    }
    Assign(this, &sound, effect);
    WorldAddTimed(world, nullptr, effect);
}
EI_REPLACE_METHOD(0x0067F660, CSpellEffect::CreateSoundSequence);

void CSpellEffect::CreatePointLight(const Vector3* at, const Vector3* color, float range, const Vector3* velocity) {
    CEngineObject* effect = CreateObject(this, kClassPointLight);
    std::memcpy(&At<Vector3>(effect, 0x14C), color, 12);
    SetPosition(effect, at);
    std::memcpy(&At<float>(effect, 0x158), &range, 4);
    EffectSetVelocity(effect, nullptr, velocity);
    At<std::uint32_t>(effect, 0x160) = kPointLightFlags;
    Assign(this, &light, effect);
    WorldAddTimed(world, nullptr, effect);
}
EI_REPLACE_METHOD(0x0067F7C0, CSpellEffect::CreatePointLight);
