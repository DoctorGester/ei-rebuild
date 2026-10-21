#pragma once

// Helpers shared by world_effect.cpp and world_effect_visual.cpp: forwarders to the original code
// the effects call (through their game addresses, so rewritten and verified versions run), the
// replicated-field operations, and the x87 details the effects repeat.

#include "world_effect.h"
#include "map_object.h"
#include "game_strings.h"
#include "world.h"
#include "world_data.h"

#include "ei/hook.h"
#include "originals_core.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_world.h"
#include "originals_script.h"
#include "game_data.h"

#include <concepts>
#include <cstdint>
#include <cstring>

namespace effect {

// --- Globals -------------------------------------------------------------------------------------

inline void* SoundSystem() { return ei::GameData<void*>(0x007AF784); }
// The light manager: ::LightManager (map_object.h).
inline CLightFactory* LightFactory() { return ei::GameData<CLightFactory*>(0x007AF748); }   // CreateObject makes lights
inline void* ParticleManager() { return ei::GameData<void*>(0x007AF754); }
inline CLightningManager* BoltFactory() { return ei::GameData<CLightningManager*>(0x007AF758); }   // CreateObject makes bolts
inline void* ObjectManager() { return ei::GameData<void*>(0x007AF5F8); }    // CBaseManager
inline void* CameraObject() { return ei::GameData<void*>(0x007AF7C8); }
inline int ChangeStamp() { return ei::GameData<const int>(0x007AFFC8); }
inline int GameTick() { return ei::GameData<const int>(0x007AF61C); }
inline std::uint8_t FrameCounter() { return ei::GameData<const std::uint8_t>(0x007AFFC4); }
// While unpacking: the snapshot the field is read against (delta::ReadSnapshot in net_delta.h).
inline BitStream* ReadSnapshot() { return ei::GameData<BitStream*>(0x007A9794); }

// Constants the original reads from memory (read the same way so nothing is folded).
inline float ConstAt(const float& constant) { return *reinterpret_cast<const volatile float*>(&constant); }
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
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> inline float FromBits(T) = delete;
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

// `min(value, 1.0)` the way the scorch code selects it: the value when it compares below 1.0 or
// is NaN (FCOMP sets C0), else 1.0.
inline float BelowOneOrOne(const float& value) {
    return !(value >= 1.0f) ? value : 1.0f;
}

// --- Engine objects ------------------------------------------------------------------------------

// 00455940 (originals_engine.h). The owning manager's context.
inline void* ManagerContext(const void* object) { return ManagerContextOf(object); }
// 0055FB00 (world). A unit by id, or null.
inline void* FindUnit(void* world, void*, int id) { return ::WorldFindUnit(world, nullptr, id); }
// 00403F40 (originals_core.h). An object of a manager by id, or null.
inline void* FindObject(void* manager, void*, int id) { return static_cast<CBaseManager*>(manager)->FindObject(id); }
// 00532860 (unit). A point of the unit (its effect origin); returns `out`.
__declspec(naked) inline Vector3* __fastcall UnitEffectPoint(void* /*unit*/, void*, Vector3* /*out*/) EI_ORIGINAL(0x00532860)

// The world an object belongs to (the effects' manager context is the world, too). The effects
// reach their own slots 18-23 through TimedSlots / EffectSlots (world_effect.h).
inline CWorld* WorldOf(const void* object) { return static_cast<CWorld*>(ObjectWorld(static_cast<const CEngineObject*>(object))); }
inline CWorld* ContextWorld(const void* object) { return static_cast<CWorld*>(ManagerContext(object)); }

// --- Replicated fields (net_delta.cpp, through the game addresses) --------------------------------

inline void __fastcall ResizeStream(BitStream* a0, void*, std::uint32_t a2) { static_cast<CMemoryStream*>((void*)a0)->Resize(a2); }

// The four streams of a field emptied (the original's 00675F70 or its inline copy).
inline void EmptyField(DeltaField* f) {
    ResizeStream(&f->snapshot[0], nullptr, 0);
    ResizeStream(&f->snapshot[1], nullptr, 0);
    ResizeStream(&f->out[0], nullptr, 0);
    ResizeStream(&f->out[1], nullptr, 0);
}

// Every Unpack ends by checking that the snapshot was read to its end (Position, then Size).
inline bool SnapshotReadToEnd() {
    BitStream* s = ReadSnapshot();
    std::uint32_t position = s->Position();
    return position == s->Size();
}

// --- Save streams (the effects' Load / Save slots take the CStream as void*) ----------------------

inline void Read(void* stream, void* data, std::uint32_t size) { static_cast<CStream*>(stream)->Read(data, size); }
inline void Write(void* stream, const void* data, std::uint32_t size) { static_cast<CStream*>(stream)->Write(data, size); }
inline int Version(const void* stream) { return static_cast<int>(static_cast<const CStream*>(stream)->version_); }
// A dword written through a stack copy, as the original writes most scalars.
inline void WriteCopy(void* stream, const void* field) {
    std::uint32_t copy;
    std::memcpy(&copy, field, 4);
    Write(stream, &copy, 4);
}

// --- MFC strings --------------------------------------------------------------------------------

// 004E5840: reads a CString from a save stream (thiscall on the stream).
inline void __fastcall ReadStreamCString(void* a0, void*, void* a2) { static_cast<CStream*>(a0)->ReadInto(static_cast<mfc::CString*>(a2)); }
inline const char* EmptyCString() { return game::EmptyText(); }

} // namespace effect
