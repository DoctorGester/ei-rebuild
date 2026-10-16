#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>
#include "lib/mfc.h"

class CEngineObject;
class IsaacRandom;
class CMappedResource;
struct DeltaField;
struct DeltaState;
struct ParticleElement;

__declspec(naked) inline void __fastcall DiplomacyOnAttack(void*, void*, CEngineObject*, CEngineObject*, int)
    EI_ORIGINAL(0x00455680)
__declspec(naked) inline char __fastcall QuitGame(int) EI_ORIGINAL(0x00455F20)
// 00455940 (rewritten as CEngineObject::ManagerContext): the object's manager context (+0x14 -> +0xAC, the world).
__declspec(naked) inline void* __fastcall ManagerContextOf(const void* /*object*/) EI_ORIGINAL(0x00455940)
__declspec(naked) inline void* __fastcall ObjectWorld(const void* /*object*/) EI_ORIGINAL(0x00455950)
__declspec(naked) inline std::uint8_t* __fastcall MapDatabaseFind(void*, void*, const char*) EI_ORIGINAL(0x0045E850)
__declspec(naked) inline void __fastcall VariableRemove(void*, void*, const char*) EI_ORIGINAL(0x00460DB0)
__declspec(naked) inline void __fastcall SessionPump(void*, void*, void*) EI_ORIGINAL(0x00479E30)
// Text tables (CTextManager): 004DEFC0 opens a section by key (RET 4, EAX 1 when found and non-empty, else 0);
// 004E0020 reads a string into a CString (RET 8, EAX 1 when found, else 0).
__declspec(naked) inline int __fastcall TextsOpenSection(void* /*texts*/, void*, const char* /*key*/)
    EI_ORIGINAL(0x004DEFC0)
__declspec(naked) inline int __fastcall TextsGetString(void* /*texts*/, void*, const char* /*key*/, mfc::CString* /*out*/)
    EI_ORIGINAL(0x004E0020)
__declspec(naked) inline double __fastcall RandomRange(IsaacRandom*, void*, float /*min*/, float /*max*/)
    EI_ORIGINAL(0x00488970)

// Named float table (RET 4 / RET 8): 00460AF0 Get (result in ST0), 00460BC0 Set (the value is the raw dword the
// original pushes, so no FPU load changes it on the way). 004DF8D0: the texts read the current line into a CString
// (RET 8, EAX 0/1).
__declspec(naked) inline float __fastcall NamedTableGet(void* /*table*/, void*, const char* /*name*/)
    EI_ORIGINAL(0x00460AF0)
__declspec(naked) inline void __fastcall NamedTableSet(void* /*table*/, void*, const char* /*name*/,
                                                       std::uint32_t /*valueBits*/) EI_ORIGINAL(0x00460BC0)
// The throw info of a thrown GameError* (dx_errors.h), 00759908; thrown with CxxThrowException (originals_library.h).
inline const void* GameErrorThrowInfo() { return reinterpret_cast<const void*>(0x00759908); }

// C3i. 004ABD00: spell name map operator[](key) (RET 4; inserts a null record for a missing name; EAX is the address of the
// value slot). 004ACC10: modifier map find_or_insert(pair) (RET 4; EAX is the address of the value slot, node + 4).
__declspec(naked) inline void** __fastcall SpellMapAtOriginal(void* /*map*/, void*, const void* /*key*/) EI_ORIGINAL(0x004ABD00)
__declspec(naked) inline void* __fastcall ModifierMapFindOrInsertOriginal(void* /*map*/, void*, const void* /*pair*/)
    EI_ORIGINAL(0x004ACC10)

// C3i part 2. 004A51C0: CMemoryStream reads a sized block from `from` into this stream (RET 4, EAX not a result).
// 004E5840: reads a CString from the stream into `out` (RET 4, EAX not a result). 004AB720 / 004AB820: the weapon / armour
// record by name (RET 4, record or null).
__declspec(naked) inline void __fastcall StreamReadSizedOriginal(void* /*stream*/, void*, void* /*to*/) EI_ORIGINAL(0x004A51C0)
__declspec(naked) inline void __fastcall StreamReadCStringOriginal(void* /*stream*/, void*, void* /*out*/)
    EI_ORIGINAL(0x004E5840)
__declspec(naked) inline void* __fastcall FindWeaponOriginal(void* /*map*/, void*, const char* /*name*/) EI_ORIGINAL(0x004AB720)
__declspec(naked) inline void* __fastcall FindArmorOriginal(void* /*map*/, void*, const char* /*name*/) EI_ORIGINAL(0x004AB820)

// C3j. 004849F0: particle source attaches to an object (RET 4, no result).
__declspec(naked) inline void __fastcall ParticleSourceAttachOriginal(void* /*source*/, void*, void* /*object*/)
    EI_ORIGINAL(0x004849F0)
// 004A5250: writes a stream with a size prefix into this stream (RET 4, no result).
__declspec(naked) inline void __fastcall StreamWriteSizedOriginal(void* /*to*/, void*, void* /*from*/) EI_ORIGINAL(0x004A5250)
// 004A5D50: copies a counted pointer (RET 4, returns this).
__declspec(naked) inline void* __fastcall CountedPtrCopyOriginal(void* /*to*/, void*, void* const* /*from*/) EI_ORIGINAL(0x004A5D50)
// 004D8E10: decodes the chunk text into `out` (RET 4, no result).
__declspec(naked) inline void __fastcall TextDecodeOriginal(void* /*decoder*/, void*, void* /*out CString*/) EI_ORIGINAL(0x004D8E10)

// C3k. 004ABBD0: the quick-item map's operator[](name) (RET 4: the key; EAX the address of the value slot, a new node is
// added when the name is missing). ECX is the map.
__declspec(naked) inline void** __fastcall QuickItemMapAtOriginal(void* /*map*/, void*, const void* /*key*/)
    EI_ORIGINAL(0x004ABBD0)

// C3l. 00581790: NamedFloatTable constructor (plain RET, returns `this`).
__declspec(naked) inline void* __fastcall NamedTableConstructOriginal(void* /*table*/) EI_ORIGINAL(0x00581790)
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
// A-E1. 00465090 (RET 0xC; rewritten as app::DeviceListArchive, app_device_list.cpp): the lever table's records as
// chunk `id` of the archive, one chunk 1 per record (the list emptied and resized when loading). The third argument
// is not read. Through its address so that its verify wrapper runs.
class ChunkArchive;
__declspec(naked) inline void __fastcall LeverRecordsArchiveOriginal(ChunkArchive* /*archive*/, void*, int /*id*/,
                                                                    void* /*records*/, std::uint32_t /*unused*/)
    EI_ORIGINAL(0x00465090)
// C3n. 004667F0 (rewritten as IsaacRandom::Next; thiscall on the generator, plain RET): the next 32-bit word. The game's
// generator is at 007A9D40.
__declspec(naked) inline std::uint32_t __fastcall RandomNext(IsaacRandom* /*random*/) EI_ORIGINAL(0x004667F0)

// C3n batch 4. CMappedResource at its addresses (the wrappers run; Rebirth patches some): 00671280 (rewritten as
// CMappedResource::Construct; plain RET) constructs it in `memory` and returns it.
__declspec(naked) inline CMappedResource* __fastcall MappedResourceConstructOriginal(void* /*memory*/) EI_ORIGINAL(0x00671280)
// 006721D0 (rewritten as CMMIOResource::Construct; plain RET): constructs a CMMIOResource (0x6C bytes; the archives, the
// figure models' .mod / .bon / .anm parts, the terrain's archive) in `memory` and returns it.
__declspec(naked) inline CMappedResource* __fastcall MmioResourceConstructOriginal(void* /*memory*/) EI_ORIGINAL(0x006721D0)
