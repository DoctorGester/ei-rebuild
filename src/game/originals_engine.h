#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>
#include "lib/mfc.h"
#include "game_data.h"

class CEngineObject;
class IsaacRandom;
class CMappedResource;
struct DeltaField;
struct DeltaState;
struct ParticleElement;

// 00455940 (rewritten as CEngineObject::ManagerContext): the object's manager context (+0x14 -> +0xAC, the world).
__declspec(naked) inline void* __fastcall ManagerContextOf(const void* /*object*/) EI_ORIGINAL(0x00455940)
// Text tables (CTextManager): 004DEFC0 opens a section by key (RET 4, EAX 1 when found and non-empty, else 0);
// 004E0020 reads a string into a CString (RET 8, EAX 1 when found, else 0).
__declspec(naked) inline int __fastcall TextsOpenSection(void* /*texts*/, void*, const char* /*key*/)
    EI_ORIGINAL(0x004DEFC0)
__declspec(naked) inline int __fastcall TextsGetString(void* /*texts*/, void*, const char* /*key*/, mfc::CString* /*out*/)
    EI_ORIGINAL(0x004E0020)
__declspec(naked) inline double __fastcall RandomRange(IsaacRandom*, void*, float /*min*/, float /*max*/)
    EI_ORIGINAL(0x00488970)

// The throw info of a thrown GameError* (dx_errors.h), 00759908; thrown with CxxThrowException (originals_library.h).
inline const void* GameErrorThrowInfo() { return ei::GamePointer<const void>(0x00759908); }

// C3j. 004849F0: particle source attaches to an object (RET 4, no result).
__declspec(naked) inline void __fastcall ParticleSourceAttachOriginal(void* /*source*/, void*, void* /*object*/)
    EI_ORIGINAL(0x004849F0)

// 006714A0: CMappedResource's destructor body at its original address. Rebirth patches it (it then runs Rebirth's
// version); in the vanilla launch the address leads to CMappedResource::Destroy.
__declspec(naked) inline void __fastcall MappedResourceDestroyOriginal(CMappedResource*) EI_ORIGINAL(0x006714A0)
// 00671730: CMappedResource::OpenResourceImpl (RET 8) at its address: Rebirth patches it (rule 9).
__declspec(naked) inline int __fastcall MappedResourceOpenOriginal(CMappedResource*, void*, const char* /*path*/, int /*writable*/)
    EI_ORIGINAL(0x00671730)
class ChunkArchive;
// A-W13. 0040ECD0 (RET 8; Rebirth-patched, so always through this forwarder; rewritten as CCamera::LoadPath,
// camera_path_load.cpp, A-E1): the camera loads the recorded path file `name` into its keys (+0x2F0, count +0x2F4);
// 0 when it fails. The glue passes `flag` on (0 from 00578880).
class CCamera;
__declspec(naked) inline int __fastcall CameraLoadPathOriginal(CCamera* /*camera*/, void*, const char* /*name*/, int /*flag*/)
    EI_ORIGINAL(0x0040ECD0)
// A-E1. 00468A00 (cdecl, no arguments; Rebirth-patched, so always through this forwarder; rewritten as
// LoadLightningNoise, lightning_load.cpp): loads the lightning noise table. Its result is not read.
// Its only caller is the lightning manager's Create (004689D0).
__declspec(naked) inline void __cdecl LoadLightningNoiseOriginal() EI_ORIGINAL(0x00468A00)
// C3n. 004667F0 (rewritten as IsaacRandom::Next; thiscall on the generator, plain RET): the next 32-bit word. The game's
// generator is at 007A9D40.
__declspec(naked) inline std::uint32_t __fastcall RandomNext(IsaacRandom* /*random*/) EI_ORIGINAL(0x004667F0)

