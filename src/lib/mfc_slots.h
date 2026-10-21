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

// ---- Compiler-made vtables (C19) -------------------------------------------------------------------
//
// The collection templates keep the vtable as a plain field (mfc.h): an instantiation's Class tag names the table
// its constructor and destructor store (`Class::Vtable()`). A tag that is a CompiledCollection is a C++ class whose
// vtable the compiler makes in place of game.exe's table for the instantiation (docs/notes/standalone.md,
// "Compiler-made vtables"): CObject's three slots over the header's layout (the CArray / CList whose first field
// holds the table). Slot 0 is CObject's GetRuntimeClass (007005E3), slot 1 the destructor `Destroy` (the
// instantiation's own: the table written, the elements destroyed, the storage freed; the compiler's scalar deleting
// destructor frees the object into the game's heap), slot 2 Serialize (`Store`). Our code calls the header's
// functions by name; the slots are what the original's dispatch through the table reaches (DestructElements' slot 1
// on an element that is a collection, CArchive's slot 2). A tag derived from another is the derived
// instantiation's (its destructor ends in the base's, which stores the base's table, as the original's does).

// CObject::GetRuntimeClass (007005E3): CObject's record (mfc_runtime_class.h's kClassCObject; mfc_coll.cpp).
struct CRuntimeClass;
const CRuntimeClass* ObjectRuntimeClass();

// The table a T's constructor stores in its first dword (T built in a buffer and left: it holds nothing to destroy).
template <typename T>
void* CompiledVtable() {
    static_assert(std::is_polymorphic_v<T>);
    alignas(T) unsigned char storage[sizeof(T)];
    return *reinterpret_cast<void**>(::new (static_cast<void*>(storage)) T);
}

template <typename F>
struct SerializeTarget;
template <typename T>
struct SerializeTarget<void(__fastcall*)(T*, void*, CArchive*)> {
    using Type = T;
};

template <typename Self, auto Destroy, auto Store>
class CompiledCollection {
public:
    static void* Vtable() { return CompiledVtable<Self>(); }

    virtual const CRuntimeClass* GetRuntimeClass() const { return ObjectRuntimeClass(); }   // 0
    virtual ~CompiledCollection() {                                                         // 1
        Destroy(reinterpret_cast<typename DestructTarget<decltype(Destroy)>::Type*>(this));
    }
    virtual void Serialize(CArchive& archive) {                                             // 2
        Store(reinterpret_cast<typename SerializeTarget<decltype(Store)>::Type*>(this), nullptr, &archive);
    }

    static void operator delete(void* p) { gamecrt::operator_delete(p); }

protected:
    CompiledCollection() = default;
    CompiledCollection(const CompiledCollection&) = delete;
    CompiledCollection& operator=(const CompiledCollection&) = delete;
};

// ---- The instantiations' vtables (game data) -------------------------------------------------------

// Arrays whose elements need nothing.
// Inside the scene loader's 0x120-byte node records (00454F80).
struct NodeItemsAClass final
    : CompiledCollection<NodeItemsAClass, &ArrayHeader<NodeItemsAClass>::Destruct, &SerializeCArray<Plain20>> {};
struct NodeItemsBClass final
    : CompiledCollection<NodeItemsBClass, &ArrayHeader<NodeItemsBClass>::Destruct, &SerializeCArray<Plain24>> {};
struct NodeItemsCClass final
    : CompiledCollection<NodeItemsCClass, &ArrayHeader<NodeItemsCClass>::Destruct, &SerializeCArray<Plain4>> {};
// The scene loader's {id, value} links.
struct TextureLinkArrayClass final
    : CompiledCollection<TextureLinkArrayClass, &ArrayHeader<TextureLinkArrayClass>::Destruct, &SerializeCArray<Plain8>> {};
// The scene meshes' corner records (IndexMeshCorners).
struct MeshCornerArrayClass final
    : CompiledCollection<MeshCornerArrayClass, &ArrayHeader<MeshCornerArrayClass>::Destruct, &SerializeCArray<Plain16>> {};
// 12-byte points: animation node normals, use spots, terrain sector data.
struct Vector3ArrayClass final
    : CompiledCollection<Vector3ArrayClass, &ArrayHeader<Vector3ArrayClass>::Destruct, &SerializeCArray<Plain12>> {};
// A trap's circles (map_spec, magic_trap).
struct TrapCircleArrayClass
    : CompiledCollection<TrapCircleArrayClass, &ArrayHeader<TrapCircleArrayClass>::Destruct, &SerializeCArray<Plain12>> {};
// A trap's points.
struct TrapPointArrayClass
    : CompiledCollection<TrapPointArrayClass, &ArrayHeader<TrapPointArrayClass>::Destruct, &SerializeCArray<Plain8>> {};
struct Array0498Class {   // constructed by 00498E60 only
    static void* Vtable() { return ei::GamePointer<void>(0x0073D024); }
};
// A script command's object arguments (script_destroy).
struct ArgObjectsBaseClass
    : CompiledCollection<ArgObjectsBaseClass, &ArrayHeader<ArgObjectsBaseClass>::Destruct, &SerializeCArray<Plain4>> {};
// A script command's object arguments, the derived class (script_copy).
struct ArgObjectsClass final : ArgObjectsBaseClass {
    static void* Vtable() { return CompiledVtable<ArgObjectsClass>(); }
};
// A script command's group arguments.
struct ArgGroupsBaseClass
    : CompiledCollection<ArgGroupsBaseClass, &ArrayHeader<ArgGroupsBaseClass>::Destruct, &SerializeCArray<Plain4>> {};
// A script command's group arguments, the derived class.
struct ArgGroupsClass final : ArgGroupsBaseClass {
    static void* Vtable() { return CompiledVtable<ArgGroupsClass>(); }
};
// A script command's bindings.
struct BindingsBaseClass
    : CompiledCollection<BindingsBaseClass, &ArrayHeader<BindingsBaseClass>::Destruct, &SerializeCArray<Plain12>> {};
struct EffectArrayClass {   // a map object's effects
    static void* Vtable() { return ei::GamePointer<void>(0x0073DBA0); }
};
struct EffectSoundArrayClass {   // a map object's effect sounds
    static void* Vtable() { return ei::GamePointer<void>(0x0073DC3C); }
};
struct FrameArrayClass {   // a 3D object's frames
    static void* Vtable() { return ei::GamePointer<void>(0x0073DE40); }
};
// A unit's motion (UnitMotion's points and waypoints): the arrays the unit's constructor builds are derived
// instantiations of the ones they are destroyed as (game.exe's 0073EB9C / 0073EB90 over 0073EBA8 / 0073EBB4).
struct MotionPointArrayClass
    : CompiledCollection<MotionPointArrayClass, &ArrayHeader<MotionPointArrayClass>::Destruct, &SerializeCArray<Plain28>> {};
struct MotionPointsClass final : MotionPointArrayClass {
    static void* Vtable() { return CompiledVtable<MotionPointsClass>(); }
};
struct MotionWaypointBaseClass
    : CompiledCollection<MotionWaypointBaseClass, &ArrayHeader<MotionWaypointBaseClass>::Destruct, &SerializeCArray<Plain12>> {};
struct MotionWaypointArrayClass final : MotionWaypointBaseClass {
    static void* Vtable() { return CompiledVtable<MotionWaypointArrayClass>(); }
};
// A unit's carried objects (CUnit +0x49C).
struct CarriedArrayClass final
    : CompiledCollection<CarriedArrayClass, &ArrayHeader<CarriedArrayClass>::Destruct, &SerializeCArray<Plain4>> {};
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
// A figure bone's children and an animation state's bones.
struct BoneListClass final : CompiledCollection<BoneListClass, &PtrList::Destruct<BoneListClass>, &SerializeCList<PtrList>> {};
// An animation state's queue.
struct AnimationStateListClass final
    : CompiledCollection<AnimationStateListClass, &PtrList::Destruct<AnimationStateListClass>, &SerializeCList<PtrList>> {};
// A 3D object's effect sources.
struct Object3DListClass final
    : CompiledCollection<Object3DListClass, &PtrList::Destruct<Object3DListClass>, &SerializeCList<PtrList>> {};
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
// LoadFigureRegistry's map.
struct FigureRegistryMapClass final
    : CompiledCollection<FigureRegistryMapClass, &Map32::Destruct<FigureRegistryMapClass>, &SerializeCMap<Map32>> {};
// IndexMeshCorners' maps.
struct CornerMapAClass final
    : CompiledCollection<CornerMapAClass, &Map32::Destruct<CornerMapAClass>, &SerializeCMap<Map64x64>> {};
struct CornerMapBClass final
    : CompiledCollection<CornerMapBClass, &Map32::Destruct<CornerMapBClass>, &SerializeCMap<Map64x32>> {};
struct CornerMapCClass final
    : CompiledCollection<CornerMapCClass, &Map32::Destruct<CornerMapCClass>, &SerializeCMap<Map32>> {};

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

// A map trap record's areas (12-byte elements; the magic trap's circles).
struct TrapAreasArrayClass final : TrapCircleArrayClass {
    static void* Vtable() { return CompiledVtable<TrapAreasArrayClass>(); }
};
// A map trap record's targets (8-byte elements; the magic trap's points).
struct TrapTargetsArrayClass final : TrapPointArrayClass {
    static void* Vtable() { return CompiledVtable<TrapTargetsArrayClass>(); }
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
