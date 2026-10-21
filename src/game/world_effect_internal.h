#pragma once

// Helpers shared by world_effect.cpp and world_effect_visual.cpp: forwarders to the original code
// the effects call (through their game addresses, so rewritten and verified versions run), the
// replicated-field operations, and the x87 details the effects repeat.

#include "world_effect.h"
#include "map_object.h"
#include "game_strings.h"
#include "world.h"
#include "world_data.h"
#include "game_singletons.h"

#include "ei/hook.h"
#include "originals_core.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_world.h"
#include "originals_script.h"
#include "game_data.h"
#include "lib/nan_audit.h"

#include <concepts>
#include <cstdint>
#include <cstring>

namespace effect {

// --- Globals -------------------------------------------------------------------------------------

// The light manager: ::LightManager (map_object.h).
inline CLightFactory* LightFactory() { return ei::GameData<CLightFactory*>(0x007AF748); }   // CreateObject makes lights
inline void* ParticleManager() { return ei::GameData<void*>(0x007AF754); }
inline CLightningManager* BoltFactory() { return ei::GameData<CLightningManager*>(0x007AF758); }   // CreateObject makes bolts
inline void* CameraObject() { return ei::GameData<void*>(0x007AF7C8); }
inline int ChangeStamp() { return ei::GameData<const int>(0x007AFFC8); }
inline std::uint8_t FrameCounter() { return static_cast<std::uint8_t>(CurrentWorldTick()); }
// While unpacking: the snapshot the field is read against (delta::ReadSnapshot in net_delta.h).
inline BitStream* ReadSnapshot() { return ei::GameData<BitStream*>(0x007A9794); }

// Constants the original reads from memory (read the same way so nothing is folded).
inline float ConstAt(const float& constant) { return *reinterpret_cast<const volatile float*>(&constant); }
inline double AngleScale() { return 1.4668330997698233e-9 /*0073FFD0*/; }  // ~2 pi / 2^32

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
// The inline test after every velocity change: moving unless all three components are 0.
inline int IsMoving(const Vector3& v) {
    EI_NAN_AUDIT(v.x, v.y, v.z);
    return v.x == 0.0f && v.y == 0.0f && v.z == 0.0f ? 0 : 1;
}

// `min(value, 1.0)` the way the scorch code selects it: the value when it compares below 1.0 or
// is NaN, else 1.0.
inline float BelowOneOrOne(const float& value) {
    return value >= 1.0f ? 1.0f : value;
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
inline CWorld* WorldOf(const void* object) { return ObjectWorld(static_cast<const CEngineObject*>(object)); }
inline CWorld* ContextWorld(const void* object) { return static_cast<CWorld*>(ManagerContext(object)); }

// --- Replicated fields (net_delta.cpp, through the game addresses) --------------------------------

// The four streams of a field emptied (the original's 00675F70 or its inline copy).
inline void EmptyField(DeltaField* f) {
    static_cast<CMemoryStream*>((void*)&f->snapshot[0])->Resize(0);
    static_cast<CMemoryStream*>((void*)&f->snapshot[1])->Resize(0);
    static_cast<CMemoryStream*>((void*)&f->out[0])->Resize(0);
    static_cast<CMemoryStream*>((void*)&f->out[1])->Resize(0);
}

// Every Unpack ends by checking that the snapshot was read to its end (Position, then Size).
inline bool SnapshotReadToEnd() {
    BitStream* s = ReadSnapshot();
    std::uint32_t position = s->Position();
    return position == s->Size();
}

// --- Save streams (the effects' Load / Save slots) -------------------------------------------------

inline void Read(CStream* stream, void* data, std::uint32_t size) { stream->Read(data, size); }
inline void Write(CStream* stream, const void* data, std::uint32_t size) { stream->Write(data, size); }
inline int Version(const CStream* stream) { return static_cast<int>(stream->version_); }
// A dword written through a stack copy, as the original writes most scalars.
inline void WriteCopy(CStream* stream, const void* field) {
    std::uint32_t copy;
    std::memcpy(&copy, field, 4);
    Write(stream, &copy, 4);
}

// --- MFC strings --------------------------------------------------------------------------------

inline const char* EmptyCString() { return game::EmptyText(); }

} // namespace effect
