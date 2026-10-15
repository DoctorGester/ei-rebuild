#pragma once

// The MFC collection functions only the exe's vtables point at (ticket S6a, docs/notes/standalone.md
// section 2): each CArray / CList / CMap instantiation's scalar deleting destructor (vtable slot 1)
// and the destructors they call. The bodies are mfc_array.h's and mfc_list.h's templates;
// mfc_slots.cpp registers the instantiations whose elements need nothing of the game,
// src/game/game_slots.cpp the others. Checked by tests/verify/verify_mfc_slots.cpp.

#include "mfc_list.h"

namespace mfc::impl {

// ---- The scalar deleting destructor ---------------------------------------------------------------

template <typename F>
struct DestructTarget;
template <typename T>
struct DestructTarget<void(__fastcall*)(T*)> {
    using Type = T;
};

// `Destruct` on the object, its memory freed when bit 0 of `flags` is set (RET 4); returns the object.
template <auto Destruct>
void* __fastcall ScalarDelete(void* self, void*, unsigned flags) {
    Destruct(static_cast<typename DestructTarget<decltype(Destruct)>::Type*>(self));
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}

// std::exception's destructor (006E7DA8), which the exception classes' own destructors jump to (0040C4A0,
// 0040C4D0, 0040C500, 0041F760).
void __fastcall ExceptionDestruct(libcrt::Exception* self);

// ---- The instantiations' vtables (game data) -------------------------------------------------------

// Arrays whose elements need nothing.
struct NodeItemsAClass {   // inside the scene loader's 0x120-byte node records (00454F80)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C1AC); }
};
struct NodeItemsBClass {
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C1B8); }
};
struct NodeItemsCClass {
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C138); }
};
struct TextureLinkArrayClass {   // the scene loader's {id, value} links
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C150); }
};
struct MeshCornerArrayClass {   // the scene meshes' corner records (IndexMeshCorners)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C19C); }
};
struct Vector3ArrayClass {   // 12-byte points: animation node normals, use spots, terrain sector data
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C120); }
};
struct TrapCircleArrayClass {   // a trap's circles (map_spec, magic_trap)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C698); }
};
struct TrapPointArrayClass {   // a trap's points
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C6A4); }
};
struct Array0498Class {   // constructed by 00498E60 only
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D024); }
};
struct ArgObjectsBaseClass {   // a script command's object arguments (script_destroy)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D3F4); }
};
struct ArgGroupsBaseClass {   // a script command's group arguments
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D400); }
};
struct BindingsBaseClass {   // a script command's bindings
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D40C); }
};
struct EffectArrayClass {   // a map object's effects
    static void* Vtable() { return reinterpret_cast<void*>(0x0073DBA0); }
};
struct EffectSoundArrayClass {   // a map object's effect sounds
    static void* Vtable() { return reinterpret_cast<void*>(0x0073DC3C); }
};
struct FrameArrayClass {   // a 3D object's frames
    static void* Vtable() { return reinterpret_cast<void*>(0x0073DE40); }
};
struct MotionWaypointBaseClass {   // a unit's motion waypoints once destroyed
    static void* Vtable() { return reinterpret_cast<void*>(0x0073EBB4); }
};
struct CarriedArrayClass {   // a unit's carried objects
    static void* Vtable() { return reinterpret_cast<void*>(0x0073EB84); }
};
struct IdArrayClass {   // CArray<int> of quest item ids (world_party)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073EBD4); }
};
struct SkinLayerArrayClass {   // a unit's skin layers (ComposeSkin)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073EEA0); }
};
struct CountdownArrayClass {   // the world's countdowns
    static void* Vtable() { return reinterpret_cast<void*>(0x0073F234); }
};
struct PartyUnitArrayClass {   // the server's and interface's party unit arrays
    static void* Vtable() { return reinterpret_cast<void*>(0x0073FAA4); }
};
struct SectorOccluderArrayClass {   // a sector's occluders
    static void* Vtable() { return reinterpret_cast<void*>(0x0073FAE0); }
};
struct SectorCellArrayClass {   // constructed by 005C18C0
    static void* Vtable() { return reinterpret_cast<void*>(0x0073FDEC); }
};
struct SpiralCellArrayClass {   // the sector spiral's cells
    static void* Vtable() { return reinterpret_cast<void*>(0x0073FDE0); }
};
struct SmoothRowArrayClass {   // the path smoothing tables' rows
    static void* Vtable() { return reinterpret_cast<void*>(0x0073FE34); }
};
struct SmoothEdgeArrayClass {
    static void* Vtable() { return reinterpret_cast<void*>(0x0073FE28); }
};
struct SmoothCopyArrayClass {
    static void* Vtable() { return reinterpret_cast<void*>(0x0073FE10); }
};
struct SmoothStepArrayClass {
    static void* Vtable() { return reinterpret_cast<void*>(0x0073FE1C); }
};
struct ObjectiveExitArrayClass {   // the objectives window's exits
    static void* Vtable() { return reinterpret_cast<void*>(0x00741264); }
};
struct ItemStripArrayClass {   // an item strip's CArray<char*>
    static void* Vtable() { return reinterpret_cast<void*>(0x00741BC0); }
};
struct QuickEntryArrayClass {   // the HUD's quick item entries
    static void* Vtable() { return reinterpret_cast<void*>(0x007428A0); }
};
struct TokenPtrArrayClass {   // the scroll text's token pointers
    static void* Vtable() { return reinterpret_cast<void*>(0x00742F14); }
};
struct LinePtrArrayClass {   // the scroll text's line pointers
    static void* Vtable() { return reinterpret_cast<void*>(0x00742F08); }
};
struct TerrainArrayAClass {   // the terrain builder's arrays (006CBE60)
    static void* Vtable() { return reinterpret_cast<void*>(0x00744210); }
};
struct TerrainArrayBClass {   // (006CC2A0, 006CC3E0)
    static void* Vtable() { return reinterpret_cast<void*>(0x0074421C); }
};
struct TerrainArrayCClass {   // (006CC530)
    static void* Vtable() { return reinterpret_cast<void*>(0x00744228); }
};

// Lists whose elements need nothing.
struct BoneListClass {   // a figure bone's and an animation state's lists
    static void* Vtable() { return reinterpret_cast<void*>(0x0073DA40); }
};
struct AnimationStateListClass {   // an animation state's second list
    static void* Vtable() { return reinterpret_cast<void*>(0x0073DA80); }
};
struct Object3DListClass {   // a 3D object's list
    static void* Vtable() { return reinterpret_cast<void*>(0x0073DE4C); }
};
struct ClientObjectListClass {   // the client world's and the sectors' object lists
    static void* Vtable() { return reinterpret_cast<void*>(0x0073F530); }
};
struct ExperienceListClass {   // CWorldServer::AwardExperience's list
    static void* Vtable() { return reinterpret_cast<void*>(0x0073FAB0); }
};
struct AttentionListClass {   // AIAttention::Choose's list
    static void* Vtable() { return reinterpret_cast<void*>(0x0073FF34); }
};
struct PlayerListClass {   // a player's list
    static void* Vtable() { return reinterpret_cast<void*>(0x00743138); }
};
struct TextureListClass {   // the texture manager's list
    static void* Vtable() { return reinterpret_cast<void*>(0x00743D18); }
};
struct SoundListClass {   // the sound manager's list
    static void* Vtable() { return reinterpret_cast<void*>(0x00744130); }
};

// Maps whose elements need nothing.
struct FigureRegistryMapClass {   // LoadFigureRegistry's map
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C16C); }
};
struct CornerMapAClass {   // IndexMeshCorners' maps
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C190); }
};
struct CornerMapBClass {
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C184); }
};
struct CornerMapCClass {
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C178); }
};

// Arrays whose elements own something.
struct FakeUnitBoneArraysClass {   // a fake unit's 20-byte CObject elements (FakeUnitConstruct)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C0F0); }
};
struct FakeUnitPartArrayClass {   // a fake unit's 0x220-byte figure parts
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C108); }
};
struct FakeUnitSceneRecordArrayClass {   // a fake unit's 0x18C-byte scene records
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C0FC); }
};
struct FakeUnitRecord9CArrayClass {   // a fake unit's 0x9C-byte records
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C0E4); }
};
struct SceneObjectArrayClass {   // the scene loader's 0x1C-byte CObject elements (SceneInfo::Load)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C80C); }
};
struct TerrainByteArraysClass {   // the terrain's CByteArray elements (Serialize 006C4790)
    static void* Vtable() { return reinterpret_cast<void*>(0x007441A8); }
};
struct AlignedAt4ArrayClass {   // the scene loader's 20-byte records with an aligned block at +4
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C114); }
};
struct AlignedAt8ArrayClass {   // an animation node's 24-byte records with an aligned block at +8
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D9BC); }
};
struct TextureNameArrayClass {   // the scene loader's {id, CString}
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C144); }
};
struct MemoryRecordArrayClass {   // a unit's 0x18-byte memory records, a CString at +0x14
    static void* Vtable() { return reinterpret_cast<void*>(0x0073F064); }
};
struct NamedRecordArrayClass {   // 12-byte records with a CString at +4 (004C9D90)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D46C); }
};
struct GlobalMapPointArrayClass {   // the global map's 0x18-byte points, CStrings at +0x10, +0x14
    static void* Vtable() { return reinterpret_cast<void*>(0x00740BF0); }
};
struct GlobalMapPathArrayClass {   // the same layout
    static void* Vtable() { return reinterpret_cast<void*>(0x00740BE4); }
};
struct GlobalMapZoneArrayClass {   // the global map's 16-byte entries, CStrings at +8, +0xC
    static void* Vtable() { return reinterpret_cast<void*>(0x00740BFC); }
};
struct PatrolArrayClass {   // patrols (32 bytes, a waypoint array at +0xC)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C394); }
};
struct AllodArrayClass {   // the map database's allods (0x2C bytes: six CStrings, an id array)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C3DC); }
};
struct ScriptAreaArrayClass {   // the script's areas (24-byte elements that are arrays)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D3C4); }
};

// ---- Element destructors -------------------------------------------------------------------------

// The CStrings at the given offsets destroyed, in the order given.
template <typename E, std::size_t... At>
void DestroyStringsAt(E* p) {
    (reinterpret_cast<CString*>(reinterpret_cast<std::uint8_t*>(p) + At)->~CString(), ...);
}
// The engine's aligned block whose pointer is at `At` freed (its allocation's start is the word
// before), the pointer left.
template <typename E, std::size_t At>
void FreeAlignedAt(E* p) {
    void* data = *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(p) + At);
    if (data) lib::FreeAligned32(data);
}

template <std::size_t Size>
using DeletedThroughObject = ArrayAbi<Bytes<Size>, EachElements<Bytes<Size>, nullptr, DeleteThroughObject<Bytes<Size>>>>;
template <std::size_t Size>
using DeletedThroughFirst = ArrayAbi<Bytes<Size>, EachElements<Bytes<Size>, nullptr, DeleteThroughFirst<Bytes<Size>>>>;
template <std::size_t Size, std::size_t At>
using AlignedBlockAt = ArrayAbi<Bytes<Size>, EachElements<Bytes<Size>, nullptr, FreeAlignedAt<Bytes<Size>, At>>>;
template <std::size_t Size, std::size_t... At>
using StringsAt = ArrayAbi<Bytes<Size>, EachElements<Bytes<Size>, nullptr, DestroyStringsAt<Bytes<Size>, At...>>>;

using SceneObjects = DeletedThroughObject<0x1C>;
using FakeUnitBoneArrays = DeletedThroughObject<0x14>;
using FakeUnitRecords9C = DeletedThroughFirst<0x9C>;
using AlignedAt4Records = AlignedBlockAt<0x14, 4>;
using AlignedAt8Records = AlignedBlockAt<0x18, 8>;
using TextureNames = StringsAt<8, 4>;          // {id, CString}
using ExitTargets = StringsAt<0x3C, 0x28>;     // zone exits: the target
using MemoryRecords = StringsAt<0x18, 0x14>;
using NamedRecords = StringsAt<0xC, 4>;
using GlobalMapPoints = StringsAt<0x18, 0x14, 0x10>;
using GlobalMapZones = StringsAt<0x10, 0xC, 8>;

} // namespace mfc::impl
