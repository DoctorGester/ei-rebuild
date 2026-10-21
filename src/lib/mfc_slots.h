#pragma once

// The MFC collection functions only the exe's vtables point at (ticket S6a, docs/notes/standalone.md
// section 2): each CArray / CList / CMap instantiation's scalar deleting destructor (vtable slot 1)
// and the destructors they call. The bodies are mfc_array.h's and mfc_list.h's templates;
// mfc_slots.cpp registers the instantiations whose elements need nothing of the game,
// src/game/game_slots.cpp the others. Checked in game by verify_mfc_slots.cpp until S21d.

#include "mfc_archive.h"
#include "mfc_list.h"
#include "game/game_data.h"

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

// ---- Serialize (vtable slot 2, RET 4) --------------------------------------------------------------
//
// afxtempl.h's Serialize with the default SerializeElements: each element as its raw bytes (ar.Write
// / ar.Read of sizeof(TYPE)), whatever it holds. CObject::Serialize first is empty. `Abi` is the
// instantiation's (mfc_array.h, mfc_list.h): its element and how SetSize / NewNode build one.

// The count (loading: ReadCount, then SetSize(count, -1)), then the elements in one block.
template <typename Abi>
void __fastcall SerializeCArray(typename Abi::Array* self, void*, CArchive* ar) {
    if (!ar->IsLoading()) ar->WriteCount(static_cast<unsigned long>(self->size));
    else self->SetSize(static_cast<int>(ar->ReadCount()), -1);
    unsigned bytes = static_cast<unsigned>(self->size) * static_cast<unsigned>(sizeof(typename Abi::Element));
    if (!ar->IsLoading()) ar->Write(self->data, bytes);
    else ar->Read(self->data, bytes);
}

template <typename List>
struct ListElements;
template <typename T, typename E>
struct ListElements<CList<T, E>> {
    using Type = E;
};

// The count, then each element from the head. Loading reads each into a local (built as the
// element's constructor builds it; plain data is left as it was) and adds it at the tail (the node's
// element constructed, then assigned).
template <typename Abi>
void __fastcall SerializeCList(typename Abi::List* self, void*, CArchive* ar) {
    using T = typename Abi::Element;
    if (!ar->IsLoading()) {
        ar->WriteCount(static_cast<unsigned long>(self->count));
        for (auto* node = self->head; node; node = node->next) ar->Write(&node->data, sizeof(T));
    } else {
        for (unsigned long n = ar->ReadCount(); n--;) {
            T value;
            ListElements<typename Abi::List>::Type::Construct(&value, 1);
            ar->Read(&value, sizeof(T));
            self->AddTail(value);
        }
    }
}

// The same stored form; loading goes through a temporary as the instantiations whose element has an
// assignment of its own compile it (ticket S6d). `Through` is a ListTemporary: `Init` builds the
// temporary (before its bytes are read over it), the list's NewNode builds the tail node's element,
// `Assign` assigns it from the temporary, the node is linked, `Destroy` destroys the temporary. The
// temporary holds the stored bytes, so its pointers are whatever the archive held: the assignment
// shares them and the destruction releases them again.
template <typename ListAbiT, auto InitFn, auto AssignFn, auto DestroyFn>
struct ListTemporary {
    using Abi = ListAbiT;
    using T = typename Abi::Element;
    static void Init(T* p) { InitFn(p); }
    static void Assign(T* to, const T* from) { AssignFn(to, from); }
    static void Destroy(T* p) { DestroyFn(p); }
};

template <typename Through>
void __fastcall SerializeCListThrough(typename Through::Abi::List* self, void*, CArchive* ar) {
    using T = typename Through::T;
    if (!ar->IsLoading()) {
        ar->WriteCount(static_cast<unsigned long>(self->count));
        for (auto* node = self->head; node; node = node->next) ar->Write(&node->data, sizeof(T));
        return;
    }
    for (unsigned long n = ar->ReadCount(); n--;) {
        alignas(T) unsigned char storage[sizeof(T)];
        T* value = reinterpret_cast<T*>(storage);
        Through::Init(value);
        ar->Read(value, sizeof(T));
        auto* node = self->NewNode(self->tail, nullptr);
        Through::Assign(&node->data, value);
        if (self->tail) self->tail->next = node;
        else self->head = node;
        self->tail = node;
        Through::Destroy(value);
    }
}

// 007085AF SerializeElements<CString> (__stdcall, RET 0xC): each string stored (operator<<) or
// loaded (operator>>).
void __stdcall SerializeStrings(CArchive* ar, CString* strings, int count);
// 004C2700 CArray<CString>::Serialize: the count (loading: SetSize(count, -1)), then the strings.
void __fastcall SerializeStringArray(Strings::Array* self, void*, CArchive* ar);

// The count, then (when there are any) key and value of each association, bucket by bucket. Loading
// reads each pair and sets it (operator[]).
template <typename Abi>
void __fastcall SerializeCMap(typename Abi::Map* self, void*, CArchive* ar) {
    using Assoc = typename Abi::Assoc;
    using K = decltype(Assoc::key);
    using V = decltype(Assoc::value);
    if (!ar->IsLoading()) {
        ar->WriteCount(static_cast<unsigned long>(self->count));
        if (self->count == 0) return;
        for (std::uint32_t bucket = 0; bucket < self->hashSize; ++bucket)
            for (Assoc* assoc = self->table[bucket]; assoc; assoc = assoc->next) {
                ar->Write(&assoc->key, sizeof(K));
                ar->Write(&assoc->value, sizeof(V));
            }
    } else {
        for (unsigned long n = ar->ReadCount(); n--;) {
            K key;
            V value;
            ar->Read(&key, sizeof(K));
            ar->Read(&value, sizeof(V));
            *Abi::ElementAt(self, nullptr, &key) = value;
        }
    }
}

// ---- The instantiations' vtables (game data) -------------------------------------------------------

// Arrays whose elements need nothing.
struct NodeItemsAClass {   // inside the scene loader's 0x120-byte node records (00454F80)
    static void* Vtable() { return ei::GamePointer<void>(0x0073C1AC); }
};
struct NodeItemsBClass {
    static void* Vtable() { return ei::GamePointer<void>(0x0073C1B8); }
};
struct NodeItemsCClass {
    static void* Vtable() { return ei::GamePointer<void>(0x0073C138); }
};
struct TextureLinkArrayClass {   // the scene loader's {id, value} links
    static void* Vtable() { return ei::GamePointer<void>(0x0073C150); }
};
struct MeshCornerArrayClass {   // the scene meshes' corner records (IndexMeshCorners)
    static void* Vtable() { return ei::GamePointer<void>(0x0073C19C); }
};
struct Vector3ArrayClass {   // 12-byte points: animation node normals, use spots, terrain sector data
    static void* Vtable() { return ei::GamePointer<void>(0x0073C120); }
};
struct TrapCircleArrayClass {   // a trap's circles (map_spec, magic_trap)
    static void* Vtable() { return ei::GamePointer<void>(0x0073C698); }
};
struct TrapPointArrayClass {   // a trap's points
    static void* Vtable() { return ei::GamePointer<void>(0x0073C6A4); }
};
struct Array0498Class {   // constructed by 00498E60 only
    static void* Vtable() { return ei::GamePointer<void>(0x0073D024); }
};
struct ArgObjectsClass {   // a script command's object arguments, the derived class (script_copy)
    static void* Vtable() { return ei::GamePointer<void>(0x0073D484); }
};
struct ArgGroupsClass {   // a script command's group arguments, the derived class
    static void* Vtable() { return ei::GamePointer<void>(0x0073D478); }
};
struct ArgObjectsBaseClass {   // a script command's object arguments (script_destroy)
    static void* Vtable() { return ei::GamePointer<void>(0x0073D3F4); }
};
struct ArgGroupsBaseClass {   // a script command's group arguments
    static void* Vtable() { return ei::GamePointer<void>(0x0073D400); }
};
struct BindingsBaseClass {   // a script command's bindings
    static void* Vtable() { return ei::GamePointer<void>(0x0073D40C); }
};
struct EffectArrayClass {   // a map object's effects
    static void* Vtable() { return ei::GamePointer<void>(0x0073DBA0); }
};
struct EffectSoundArrayClass {   // a map object's effect sounds
    static void* Vtable() { return ei::GamePointer<void>(0x0073DC3C); }
};
struct FrameArrayClass {   // a 3D object's frames
    static void* Vtable() { return ei::GamePointer<void>(0x0073DE40); }
};
struct MotionWaypointBaseClass {   // a unit's motion waypoints once destroyed
    static void* Vtable() { return ei::GamePointer<void>(0x0073EBB4); }
};
struct CarriedArrayClass {   // a unit's carried objects
    static void* Vtable() { return ei::GamePointer<void>(0x0073EB84); }
};
struct IdArrayClass {   // CArray<int> of quest item ids (world_party)
    static void* Vtable() { return ei::GamePointer<void>(0x0073EBD4); }
};
struct SkinLayerArrayClass {   // a unit's skin layers (ComposeSkin)
    static void* Vtable() { return ei::GamePointer<void>(0x0073EEA0); }
};
struct CountdownArrayClass {   // the world's countdowns
    static void* Vtable() { return ei::GamePointer<void>(0x0073F234); }
};
struct PartyUnitArrayClass {   // the server's and interface's party unit arrays
    static void* Vtable() { return ei::GamePointer<void>(0x0073FAA4); }
};
struct SectorOccluderArrayClass {   // a sector's occluders
    static void* Vtable() { return ei::GamePointer<void>(0x0073FAE0); }
};
struct SectorCellArrayClass {   // constructed by 005C18C0
    static void* Vtable() { return ei::GamePointer<void>(0x0073FDEC); }
};
struct SpiralCellArrayClass {   // the sector spiral's cells
    static void* Vtable() { return ei::GamePointer<void>(0x0073FDE0); }
};
struct SmoothRowArrayClass {   // the path smoothing tables' rows
    static void* Vtable() { return ei::GamePointer<void>(0x0073FE34); }
};
struct SmoothEdgeArrayClass {
    static void* Vtable() { return ei::GamePointer<void>(0x0073FE28); }
};
struct SmoothCopyArrayClass {
    static void* Vtable() { return ei::GamePointer<void>(0x0073FE10); }
};
struct SmoothStepArrayClass {
    static void* Vtable() { return ei::GamePointer<void>(0x0073FE1C); }
};
struct ObjectiveExitArrayClass {   // the objectives window's exits
    static void* Vtable() { return ei::GamePointer<void>(0x00741264); }
};
struct ItemStripArrayClass {   // an item strip's CArray<char*>
    static void* Vtable() { return ei::GamePointer<void>(0x00741BC0); }
};
struct QuickEntryArrayClass {   // the HUD's quick item entries
    static void* Vtable() { return ei::GamePointer<void>(0x007428A0); }
};
struct TokenPtrArrayClass {   // the scroll text's token pointers
    static void* Vtable() { return ei::GamePointer<void>(0x00742F14); }
};
struct LinePtrArrayClass {   // the scroll text's line pointers
    static void* Vtable() { return ei::GamePointer<void>(0x00742F08); }
};
struct TerrainArrayAClass {   // the terrain builder's arrays (006CBE60)
    static void* Vtable() { return ei::GamePointer<void>(0x00744210); }
};
struct TerrainArrayBClass {   // (006CC2A0, 006CC3E0)
    static void* Vtable() { return ei::GamePointer<void>(0x0074421C); }
};
struct TerrainArrayCClass {   // (006CC530)
    static void* Vtable() { return ei::GamePointer<void>(0x00744228); }
};

// Lists whose elements need nothing.
struct BoneListClass {   // a figure bone's and an animation state's lists
    static void* Vtable() { return ei::GamePointer<void>(0x0073DA40); }
};
struct AnimationStateListClass {   // an animation state's second list
    static void* Vtable() { return ei::GamePointer<void>(0x0073DA80); }
};
struct Object3DListClass {   // a 3D object's list
    static void* Vtable() { return ei::GamePointer<void>(0x0073DE4C); }
};
struct ClientObjectListClass {   // the client world's and the sectors' object lists
    static void* Vtable() { return ei::GamePointer<void>(0x0073F530); }
};
struct ExperienceListClass {   // CWorldServer::AwardExperience's list
    static void* Vtable() { return ei::GamePointer<void>(0x0073FAB0); }
};
struct AttentionListClass {   // AIAttention::Choose's list
    static void* Vtable() { return ei::GamePointer<void>(0x0073FF34); }
};
struct PlayerListClass {   // a player's list
    static void* Vtable() { return ei::GamePointer<void>(0x00743138); }
};
struct TextureListClass {   // the texture manager's list
    static void* Vtable() { return ei::GamePointer<void>(0x00743D18); }
};
struct SoundListClass {   // the sound manager's list
    static void* Vtable() { return ei::GamePointer<void>(0x00744130); }
};

// Maps whose elements need nothing.
struct FigureRegistryMapClass {   // LoadFigureRegistry's map
    static void* Vtable() { return ei::GamePointer<void>(0x0073C16C); }
};
struct CornerMapAClass {   // IndexMeshCorners' maps
    static void* Vtable() { return ei::GamePointer<void>(0x0073C190); }
};
struct CornerMapBClass {
    static void* Vtable() { return ei::GamePointer<void>(0x0073C184); }
};
struct CornerMapCClass {
    static void* Vtable() { return ei::GamePointer<void>(0x0073C178); }
};

// Arrays whose elements own something.
struct FakeUnitBoneArraysClass {   // a fake unit's 20-byte CObject elements (FakeUnitConstruct)
    static void* Vtable() { return ei::GamePointer<void>(0x0073C0F0); }
};
struct FakeUnitPartArrayClass {   // a fake unit's 0x220-byte figure parts
    static void* Vtable() { return ei::GamePointer<void>(0x0073C108); }
};
struct FakeUnitSceneRecordArrayClass {   // a fake unit's 0x18C-byte scene records
    static void* Vtable() { return ei::GamePointer<void>(0x0073C0FC); }
};
struct FakeUnitRecord9CArrayClass {   // a fake unit's 0x9C-byte records
    static void* Vtable() { return ei::GamePointer<void>(0x0073C0E4); }
};
struct SceneObjectArrayClass {   // the scene loader's 0x1C-byte CObject elements (SceneInfo::Load)
    static void* Vtable() { return ei::GamePointer<void>(0x0073C80C); }
};
struct TerrainByteArraysClass {   // the terrain's CByteArray elements (Serialize 006C4790)
    static void* Vtable() { return ei::GamePointer<void>(0x007441A8); }
};
struct AlignedAt4ArrayClass {   // the scene loader's 20-byte records with an aligned block at +4
    static void* Vtable() { return ei::GamePointer<void>(0x0073C114); }
};
struct AlignedAt8ArrayClass {   // an animation node's 24-byte records with an aligned block at +8
    static void* Vtable() { return ei::GamePointer<void>(0x0073D9BC); }
};
struct TextureNameArrayClass {   // the scene loader's {id, CString}
    static void* Vtable() { return ei::GamePointer<void>(0x0073C144); }
};
struct MemoryRecordArrayClass {   // a unit's 0x18-byte memory records, a CString at +0x14
    static void* Vtable() { return ei::GamePointer<void>(0x0073F064); }
};
struct NamedRecordArrayClass {   // 12-byte records with a CString at +4 (004C9D90)
    static void* Vtable() { return ei::GamePointer<void>(0x0073D46C); }
};
struct GlobalMapPointArrayClass {   // the global map's 0x18-byte points, CStrings at +0x10, +0x14
    static void* Vtable() { return ei::GamePointer<void>(0x00740BF0); }
};
struct GlobalMapPathArrayClass {   // the same layout
    static void* Vtable() { return ei::GamePointer<void>(0x00740BE4); }
};
struct GlobalMapZoneArrayClass {   // the global map's 16-byte entries, CStrings at +8, +0xC
    static void* Vtable() { return ei::GamePointer<void>(0x00740BFC); }
};
struct PatrolArrayClass {   // patrols (32 bytes, a waypoint array at +0xC)
    static void* Vtable() { return ei::GamePointer<void>(0x0073C394); }
};
struct AllodArrayClass {   // the map database's allods (0x2C bytes: six CStrings, an id array)
    static void* Vtable() { return ei::GamePointer<void>(0x0073C3DC); }
};
struct ScriptAreaArrayClass {   // the script's areas (24-byte elements that are arrays)
    static void* Vtable() { return ei::GamePointer<void>(0x0073D3C4); }
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

// The CStrings at the given offsets set empty (the record zeroed first by EachElements).
template <std::size_t Size, std::size_t... At>
void InitStringsAt(Bytes<Size>* p) {
    ((*reinterpret_cast<char**>(p->bytes + At) = PchNil()), ...);
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

// Records whose CStrings SetSize sets empty and destroys (the global map's, 005FBF50 / 005FBD30 and
// the like; a unit memory's call records, 00558D60 / 00558D00).
template <std::size_t Size, std::size_t... At>
using BuiltStringsAt = ArrayAbi<Bytes<Size>, EachElements<Bytes<Size>, InitStringsAt<Size, At...>,
                                                          DestroyStringsAt<Bytes<Size>, At...>>>;
using GlobalMapZoneRecords = BuiltStringsAt<0x10, 0xC, 8>;
using GlobalMapPointRecords = BuiltStringsAt<0x18, 0x14, 0x10>;
using MemoryCallRecords = BuiltStringsAt<0x18, 0x14>;

// AIAttention::Choose's 12-byte records: -1, then 0 (005DA2B0).
struct Attention12 {
    int id;
    int value;
    int extra;
};
inline void InitAttention12(Attention12* p) {
    p->value = 0;
    p->id = -1;
}
using AttentionList = ListAbi<Attention12, InitElements<Attention12, InitAttention12>>;
using List30 = ListAbi<Bytes<0x30>>;

using Plain36 = ArrayAbi<Bytes<36>>;   // the 36-byte smoothing steps (SmoothStepArrayClass)
using Plain56 = ArrayAbi<Bytes<56>>;   // the frames' footprint parts (00519FC0 zeroes them twice)

struct TrapAreasArrayClass {   // a map trap record's areas (12-byte elements; the magic trap's circles)
    static void* Vtable() { return ei::GamePointer<void>(0x0073C68C); }
};
struct TrapTargetsArrayClass {   // a map trap record's targets (8-byte elements; the magic trap's points)
    static void* Vtable() { return ei::GamePointer<void>(0x0073C680); }
};
struct BehaviourPatrolsArrayClass {   // a unit record behaviour's patrols (mapspec::Patrol elements)
    static void* Vtable() { return ei::GamePointer<void>(0x0073C37C); }
};

// The scene's object records: CMap<DWORD, DWORD>s built by 00477380, destroyed through slot 1.
using DwordMap = CMap<std::uint32_t, std::uint32_t>;
using OwnedDwordMaps = ArrayAbi<DwordMap, EachElements<DwordMap, InitMap<DwordMapClass>, DeleteThroughObject<DwordMap>>>;
// {id, CString}: the string empty (004553F0); its destructor (0044C490).
using NamedStrings = ArrayAbi<WithString<8, 4>, EachElements<WithString<8, 4>, InitWithString<8, 4>,
                                                            DestroyStringsAt<WithString<8, 4>, 4>>>;

} // namespace mfc::impl
