#pragma once

// Helpers shared by world_effect.cpp and world_effect_visual.cpp: forwarders to the original code
// the effects call (through their game addresses, so rewritten and verified versions run), the
// replicated-field operations, and the x87 details the effects repeat.

#include "world_effect.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_core.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_world.h"

#include <cstdint>
#include <cstring>

namespace effect {

// --- Globals -------------------------------------------------------------------------------------

inline void* SoundSystem() { return *reinterpret_cast<void**>(0x007AF784); }
inline void* LightManager() { return *reinterpret_cast<void**>(0x007AF738); }
inline void* LightFactory() { return *reinterpret_cast<void**>(0x007AF748); }     // slot 23 creates lights
inline void* ParticleManager() { return *reinterpret_cast<void**>(0x007AF754); }
inline void* BoltFactory() { return *reinterpret_cast<void**>(0x007AF758); }      // slot 23 creates bolts
inline void* ObjectManager() { return *reinterpret_cast<void**>(0x007AF5F8); }    // CBaseManager
inline void* WorldClient() { return reinterpret_cast<void*>(0x007AF5B8); }        // scorch marks (0056D4C0)
inline void* CameraObject() { return *reinterpret_cast<void**>(0x007AF7C8); }
inline int ChangeStamp() { return *reinterpret_cast<const int*>(0x007AFFC8); }
inline int GameTick() { return *reinterpret_cast<const int*>(0x007AF61C); }
inline std::uint8_t FrameCounter() { return *reinterpret_cast<const std::uint8_t*>(0x007AFFC4); }
// While unpacking: the snapshot the field is read against (delta::ReadSnapshot in net_delta.h).
inline BitStream* ReadSnapshot() { return *reinterpret_cast<BitStream**>(0x007A9794); }

// Constants the original reads from memory (read the same way so nothing is folded).
inline float ConstAt(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
constexpr std::uintptr_t kZero = 0x0073B858;          // 0.0
constexpr std::uintptr_t kOne = 0x0073B84C;           // 1.0
constexpr std::uintptr_t kHalf = 0x0073B86C;          // 0.5
constexpr std::uintptr_t kRangeEpsilon = 0x0073CCF4;  // 0.001
constexpr std::uintptr_t kScorchAlpha = 0x0073CDE0;   // 0.6
constexpr std::uintptr_t kScorchMargin = 0x0073C54C;  // 0.3
constexpr std::uintptr_t kDefaultRadius = 0x007433AC; // 100.0
inline double AngleScale() { return *reinterpret_cast<const volatile double*>(0x0073FFD0); }  // ~2 pi / 2^32

// By reference: a float passed by value would be loaded and stored through the FPU.
inline std::uint32_t Bits(const float& f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
inline float FromBits(std::uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
inline float StoreFloat(float v) {
    volatile float r = v;
    return r;
}

// FCOMP against 0.0 then TEST AH,40h: "equal" also when the value is NaN (unordered sets C3).
inline bool EqualsZeroOrNaN(const float& v) {
    std::uint32_t u = Bits(v);
    if ((u & 0x7FFFFFFFu) == 0) return true;
    return (u & 0x7F800000u) == 0x7F800000u && (u & 0x007FFFFFu) != 0;
}
// The inline test after every velocity change: moving unless all three components compare equal
// to 0.0 (NaN compares equal here).
inline int IsMoving(const Vector3& v) {
    return EqualsZeroOrNaN(v.x) && EqualsZeroOrNaN(v.y) && EqualsZeroOrNaN(v.z) ? 0 : 1;
}

// A float copied with FLD / FSTP (a signalling NaN becomes quiet), as the original copies some.
inline void CopyThroughFpu(float* to, const float* from) {
    __asm {
        mov eax, from
        mov ecx, to
        fld dword ptr [eax]
        fstp dword ptr [ecx]
    }
}

// `min(value, 1.0)` the way the scorch code selects it: the value when it compares below 1.0 or
// is NaN (FCOMP sets C0), else 1.0. Works on the bits.
inline std::uint32_t BelowOneOrOne(const float& value) {
    return !(value >= 1.0f) ? Bits(value) : 0x3F800000u;
}

// --- Engine objects ------------------------------------------------------------------------------

// 00455940 (originals_engine.h). The owning manager's context.
inline void* ManagerContext(const void* object) { return ManagerContextOf(object); }
// 0055FB00 (world). A unit by id, or null.
inline void* FindUnit(void* world, void*, int id) { return ::WorldFindUnit(world, nullptr, id); }
// 00403F40 (originals_core.h). An object of a manager by id, or null.
inline void* FindObject(void* manager, void*, int id) { return ManagerFindObject(manager, nullptr, id); }
// 00532860 (unit). A point of the unit (its effect origin); returns `out`.
__declspec(naked) inline Vector3* __fastcall UnitEffectPoint(void* /*unit*/, void*, Vector3* /*out*/) EI_ORIGINAL(0x00532860)
// 004B3330 (rewritten in script_group.cpp). Whether the group holds the object.
__declspec(naked) inline int __fastcall GroupContains(void* /*group*/, void*, void* /*object*/) EI_ORIGINAL(0x004B3330)

// Virtual slots used on objects whose classes are not declared here.
inline void* ObjectById(void* holder, int id) { return ei::VCall<void*>(holder, 40, id); }       // slot 40 (+0xA0)
inline double GroundHeight(void* world, float x, float y) { return ei::VCall<double>(world, 28, x, y); }  // slot 28 (+0x70)
inline int IsClientSide(void* context) { return ei::VCall<int>(context, 26); }                // slot 26 (+0x68)
inline int ClassId(const void* object) { return ei::VCall<int>(object, 12); }                 // slot 12
inline void Release(void* object) { ei::VCall<void>(object, 4); }                            // slot 4

// --- The effect's own virtual slots --------------------------------------------------------------

inline void CallReset(CEffect* e) { ei::VCall<void>(e, 18); }
inline int CallUnpack(CEffect* e, int update, DeltaField* field) { return ei::VCall<int>(e, 21, update, field); }
inline void CallPack(CEffect* e, DeltaField* field) { ei::VCall<void>(e, 22, field); }
inline void CallStart(CEffect* e, int update) { ei::VCall<void>(e, 23, update); }

// --- Replicated fields (net_delta.cpp, through the game addresses) --------------------------------

__declspec(naked) inline DeltaField* __fastcall BeginWrite(DeltaField*, void*, int /*channel1Only*/) EI_ORIGINAL(0x0047FA40)
__declspec(naked) inline DeltaField* __fastcall BeginRead(DeltaField*, void*, int /*second*/, BitStream* /*reader*/) EI_ORIGINAL(0x0047FAA0)
__declspec(naked) inline void __fastcall EndField(DeltaField*) EI_ORIGINAL(0x0047FAF0)
inline void __fastcall ResizeStream(BitStream* a0, void*, std::uint32_t a2) { StreamResizeOriginal((void*)a0, nullptr, a2); }
__declspec(naked) inline void __fastcall WritePosition(DeltaField*, void*, const Vector3*) EI_ORIGINAL(0x0047E3A0)
__declspec(naked) inline void __fastcall WriteDelta(DeltaField*, void*, const void*, std::uint32_t) EI_ORIGINAL(0x0047DCA0)
__declspec(naked) inline void __fastcall WriteString(DeltaField*, void*, const char*) EI_ORIGINAL(0x0047E2D0)
__declspec(naked) inline void __fastcall WriteFloat16(DeltaField*, void*, std::uint32_t) EI_ORIGINAL(0x0047F1E0)
__declspec(naked) inline void __fastcall WriteFloat16x3(DeltaField*, void*, const void*) EI_ORIGINAL(0x0047F4D0)
__declspec(naked) inline void __fastcall ReadPosition(DeltaField*, void*, Vector3*) EI_ORIGINAL(0x0047E930)
__declspec(naked) inline void __fastcall ReadDelta(DeltaField*, void*, void*, std::uint32_t) EI_ORIGINAL(0x0047E000)
__declspec(naked) inline void __fastcall ReadText(DeltaField*, void*, char*, int /*maxLength*/) EI_ORIGINAL(0x0047E340)
__declspec(naked) inline void __fastcall ReadFloat16(DeltaField*, void*, void*) EI_ORIGINAL(0x0047F3D0)
__declspec(naked) inline void __fastcall ReadFloat16x3(DeltaField*, void*, void*) EI_ORIGINAL(0x0047F7F0)

// The four streams of a field emptied (the original's 00675F70 or its inline copy).
inline void EmptyField(DeltaField* f) {
    ResizeStream(&f->snapshot[0], nullptr, 0);
    ResizeStream(&f->snapshot[1], nullptr, 0);
    ResizeStream(&f->out[0], nullptr, 0);
    ResizeStream(&f->out[1], nullptr, 0);
}

// Every Unpack ends by checking that the snapshot was read to its end (slots 5 and 4).
inline bool SnapshotReadToEnd() {
    BitStream* s = ReadSnapshot();
    std::uint32_t position = ei::VCall<std::uint32_t>(s, 5);
    return position == ei::VCall<std::uint32_t>(s, 4);
}

// --- Save streams (slot 1 Read, slot 2 Write; +8 the save version) --------------------------------

inline void Read(void* stream, void* data, std::uint32_t size) { ei::VCall<std::uint32_t>(stream, 1, data, size); }
inline void Write(void* stream, const void* data, std::uint32_t size) { ei::VCall<void>(stream, 2, data, size); }
inline int Version(const void* stream) { return static_cast<const int*>(stream)[2]; }
// A dword written through a stack copy, as the original writes most scalars.
inline void WriteCopy(void* stream, const void* field) {
    std::uint32_t copy;
    std::memcpy(&copy, field, 4);
    Write(stream, &copy, 4);
}

// --- MFC strings --------------------------------------------------------------------------------

// 006773A0: reads a count and that many CStrings from a save stream, appending them (ECX array, EDX stream).
__declspec(naked) inline void __fastcall ReadStringArray(EffectStringArray*, void* /*stream*/) EI_ORIGINAL(0x006773A0)
// 004E5840: reads a CString from a save stream (thiscall on the stream).
__declspec(naked) inline void __fastcall ReadStreamCString(void* /*stream*/, void*, void* /*cstring*/) EI_ORIGINAL(0x004E5840)
inline const char* EmptyCString() { return reinterpret_cast<const char*>(0x0079B298); }

} // namespace effect
