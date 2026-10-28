#pragma once

// The MFC collection functions only the exe's vtables point at (docs/notes/standalone.md section 2): each
// CArray / CList / CMap instantiation's scalar deleting destructor and the destructors they call. The bodies
// are mfc_array.h's and mfc_list.h's templates.

#include "compiled_vtable.h"
#include "mfc_archive.h"
#include "mfc_list.h"
#include "game/game_data.h"

namespace mfc::impl {

// ---- The scalar deleting destructor ---------------------------------------------------------------

template <typename F>
struct DestructTarget;
template <typename T>
struct DestructTarget<void(*)(T*)> {
    using Type = T;
};

// `Destruct` on the object, its memory freed when bit 0 of `flags` is set; returns the object.
// ScalarDeleting<&Destruct>::Delete: `this` is the object, of Destruct's parameter type.
template <auto Destruct>
struct ScalarDeleting {
    void* Delete(unsigned flags) {
        void* self = this;
        Destruct(static_cast<typename DestructTarget<decltype(Destruct)>::Type*>(self));
        if (flags & 1) gamecrt::operator_delete(self);
        return self;
    }
};

// ---- Serialize (vtable slot 2) --------------------------------------------------------------
// afxtempl.h's Serialize with the default SerializeElements: each element as its raw bytes, whatever it
// holds. CObject::Serialize first is empty. `Abi` is the instantiation's: its element and how SetSize /
// NewNode build one.

// The count (loading: ReadCount, then SetSize(count, -1)), then the elements in one block.
template <typename T, typename E>
void ArrayAbi<T, E>::Serialize(CArchive* ar) {
    if (!ar->IsLoading()) ar->WriteCount(static_cast<unsigned long>(this->size));
    else this->SetSize(static_cast<int>(ar->ReadCount()), -1);
    unsigned bytes = static_cast<unsigned>(this->size) * static_cast<unsigned>(sizeof(T));
    if (!ar->IsLoading()) ar->Write(this->data, bytes);
    else ar->Read(this->data, bytes);
}

// The count, then each element from the head. Loading reads each into a local (plain data is left as it was)
// and adds it at the tail (the node's element constructed, then assigned).
template <typename T, typename E>
void ListAbi<T, E>::Serialize(CArchive* ar) {
    if (!ar->IsLoading()) {
        ar->WriteCount(static_cast<unsigned long>(this->count));
        for (auto* node = this->head; node; node = node->next) ar->Write(&node->data, sizeof(T));
    } else {
        for (unsigned long n = ar->ReadCount(); n--;) {
            T value;
            E::Construct(&value, 1);
            ar->Read(&value, sizeof(T));
            this->AddTail(value);
        }
    }
}

// The same stored form; loading goes through a temporary as the instantiations whose element has an
// assignment of its own compile it. `Through` is a ListTemporary: `Init` builds the temporary (before its
// bytes are read over it), the list's NewNode builds the tail node's element, `Assign` assigns it from the
// temporary, `Destroy` destroys it. The temporary holds the stored bytes, so its pointers are whatever the
// archive held: the assignment shares them and the destruction releases them again.
template <typename ListAbiT, auto InitFn, auto AssignFn, auto DestroyFn>
struct ListTemporary {
    using Abi = ListAbiT;
    using T = typename Abi::Element;
    static void Init(T* p) { InitFn(p); }
    static void Assign(T* to, const T* from) { AssignFn(to, from); }
    static void Destroy(T* p) { DestroyFn(p); }
};

template <typename T, typename E>
template <typename Through>
void ListAbi<T, E>::SerializeThrough(CArchive* ar) {
    if (!ar->IsLoading()) {
        ar->WriteCount(static_cast<unsigned long>(this->count));
        for (auto* node = this->head; node; node = node->next) ar->Write(&node->data, sizeof(T));
        return;
    }
    for (unsigned long n = ar->ReadCount(); n--;) {
        alignas(T) unsigned char storage[sizeof(T)];
        T* value = reinterpret_cast<T*>(storage);
        Through::Init(value);
        ar->Read(value, sizeof(T));
        auto* node = this->NewNode(this->tail, nullptr);
        Through::Assign(&node->data, value);
        if (this->tail) this->tail->next = node;
        else this->head = node;
        this->tail = node;
        Through::Destroy(value);
    }
}

// The script's string arrays' Serialize: the count, then the strings.
template <>
void Strings::Serialize(CArchive* ar);

// The count, then (when there are any) key and value of each association, bucket by bucket. Loading
// reads each pair and sets it (operator[]).
template <typename K, typename V>
void MapAbi<K, V>::Serialize(CArchive* ar) {
    if (!ar->IsLoading()) {
        ar->WriteCount(static_cast<unsigned long>(this->count));
        if (this->count == 0) return;
        for (std::uint32_t bucket = 0; bucket < this->hashSize; ++bucket)
            for (Assoc* assoc = this->table[bucket]; assoc; assoc = assoc->next) {
                ar->Write(&assoc->key, sizeof(K));
                ar->Write(&assoc->value, sizeof(V));
            }
    } else {
        for (unsigned long n = ar->ReadCount(); n--;) {
            K key;
            V value;
            ar->Read(&key, sizeof(K));
            ar->Read(&value, sizeof(V));
            this->ElementAt(key) = value;
        }
    }
}

// ---- Compiler-made vtables -------------------------------------------------------------------
// An instantiation's Class tag names the table its constructor and destructor store. See
// docs/notes/standalone.md, "The image's source files".

// CObject::GetRuntimeClass: CObject's record (mfc_runtime_class.h's kClassCObject; mfc_coll.cpp).
struct CRuntimeClass;
const CRuntimeClass* ObjectRuntimeClass();

using ei::CompiledVtable;

template <typename F>
struct SerializeTarget;
template <typename T>
struct SerializeTarget<void (T::*)(CArchive*)> {
    using Type = T;
};

template <typename Self, auto Destroy, auto Store>
class CompiledCollection : public ei::CompiledTag<Self> {
public:

    virtual const CRuntimeClass* GetRuntimeClass() const { return ObjectRuntimeClass(); }   // 0
    virtual ~CompiledCollection() {                                                         // 1
        Destroy(this->template ObjectAs<typename DestructTarget<decltype(Destroy)>::Type>());
    }
    virtual void Serialize(CArchive& archive) {                                             // 2
        (this->template ObjectAs<typename SerializeTarget<decltype(Store)>::Type>()->*Store)(&archive);
    }

    static void operator delete(void* p) { gamecrt::operator_delete(p); }

protected:
    CompiledCollection() = default;
    CompiledCollection(const CompiledCollection&) = delete;
    CompiledCollection& operator=(const CompiledCollection&) = delete;
};

using Plain36 = ArrayAbi<Bytes<36>>;   // the 36-byte smoothing steps (SmoothStepArrayClass)
using Plain56 = ArrayAbi<Bytes<56>>;   // the frames' footprint parts (zeroes them twice)

// AIAttention::Choose's 12-byte records: -1, then 0.
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

// The container instantiations of mfc_array.h and mfc_list.h: their tags are declared there and defined
// here, one class per vtable. A tag that is a base of another is not final (the base is defined first); the
// derived tag's table calls the base's destructor.

using List30 = ListAbi<Bytes<0x30>>;

// CArray<int> / CDWordArray-like: the zone and quest id lists, the climb tables
struct IntArrayClass final
    : CompiledCollection<IntArrayClass, &ArrayHeader<IntArrayClass>::Destruct, &Plain4::Serialize> {};
// The interface's unit pointer arrays
struct UnitPtrArrayClass final
    : CompiledCollection<UnitPtrArrayClass, &ArrayHeader<UnitPtrArrayClass>::Destruct, &Plain4::Serialize> {};
// CArray<float> (the motions' step speeds, the radius tree's keys, the terrain's level arrays)
struct FloatArrayClass final
    : CompiledCollection<FloatArrayClass, &ArrayHeader<FloatArrayClass>::Destruct, &Plain4::Serialize> {};
// The script commands' float arguments (a CArray<float> subclass)
struct ArgFloatArrayClass final
    : CompiledCollection<ArgFloatArrayClass, &ArrayHeader<FloatArrayClass>::Destruct, &Plain4::Serialize> {};
// The script's float variables
struct ScriptFloatsClass
    : CompiledCollection<ScriptFloatsClass, &ArrayHeader<ScriptFloatsClass>::Destruct, &Plain4::Serialize> {};
// The float values of the script's variables (ScriptVars, ScriptNamedValues<float>; base ScriptFloatsClass)
struct ScriptFloatValuesClass final : ScriptFloatsClass {
    static void* Vtable() { return CompiledVtable<ScriptFloatValuesClass>(); }
};
// A script area's parts
struct ScriptAreaClass
    : CompiledCollection<ScriptAreaClass, &ArrayHeader<ScriptAreaClass>::Destruct, &Plain20::Serialize> {};
// The array a script area element holds (base ScriptAreaClass)
struct ScriptAreaPartsClass final : ScriptAreaClass {
    static void* Vtable() { return CompiledVtable<ScriptAreaPartsClass>(); }
};
// The script's if-then blocks
struct ScriptIfThenArrayClass
    : CompiledCollection<ScriptIfThenArrayClass, &ArrayHeader<ScriptIfThenArrayClass>::Destruct, &Plain8::Serialize> {};
// A definition's if-then pairs as the parser builds them (ScriptIfThens; base ScriptIfThenArrayClass)
struct ScriptIfThenPairsClass final : ScriptIfThenArrayClass {
    static void* Vtable() { return CompiledVtable<ScriptIfThenPairsClass>(); }
};
// The script's party slots
struct PartySlotArrayClass final
    : CompiledCollection<PartySlotArrayClass, &ArrayHeader<PartySlotArrayClass>::Destruct, &Placements::Serialize> {};
// The AI's look records
struct AiLookArrayClass
    : CompiledCollection<AiLookArrayClass, &ArrayHeader<AiLookArrayClass>::Destruct, &Plain16::Serialize> {};
// The array inside the AI's 32-byte look records (base AiLookArrayClass)
struct AiLookRecordArrayClass final : AiLookArrayClass {
    static void* Vtable() { return CompiledVtable<AiLookRecordArrayClass>(); }
};
// The weather and lightning sprites
struct SpriteArrayClass final
    : CompiledCollection<SpriteArrayClass, &ArrayHeader<SpriteArrayClass>::Destruct, &Plain8::Serialize> {};
// A figure's bone links
struct BoneLinkArrayClass final
    : CompiledCollection<BoneLinkArrayClass, &ArrayHeader<BoneLinkArrayClass>::Destruct, &KeyFrames::Serialize> {};
// The base vtable the element arrays' destructors restore (patrols, motion records)
struct ArrayBaseClass
    : CompiledCollection<ArrayBaseClass, &ArrayHeader<ArrayBaseClass>::Destruct, &Plain20::Serialize> {};
// A patrol's waypoints
struct WaypointArrayClass final : ArrayBaseClass {
    static void* Vtable() { return CompiledVtable<WaypointArrayClass>(); }
};
// A unit's point lists
struct PointArrayClass final
    : CompiledCollection<PointArrayClass, &ArrayHeader<PointArrayClass>::Destruct, &Plain8::Serialize> {};
// A unit's attack options
struct AttackOptionArrayClass final
    : CompiledCollection<AttackOptionArrayClass, &ArrayHeader<AttackOptionArrayClass>::Destruct, &Plain40::Serialize> {};
// A unit's animation steps
struct AnimationStepsClass
    : CompiledCollection<AnimationStepsClass, &ArrayHeader<AnimationStepsClass>::Destruct, &AnimationSteps::Serialize> {};
// The animation steps a motion copies
struct AnimationStepsCopyClass final : AnimationStepsClass {
    static void* Vtable() { return CompiledVtable<AnimationStepsCopyClass>(); }
};
// The AI map's cell rows
struct CellRowArrayClass final
    : CompiledCollection<CellRowArrayClass, &ArrayHeader<CellRowArrayClass>::Destruct, &Plain4::Serialize> {};
// The AI map's area lists (CArray<{int, int}>)
struct AreaListClass final
    : CompiledCollection<AreaListClass, &ArrayHeader<AreaListClass>::Destruct, &Plain8::Serialize> {};
// The script's string arrays (game.exe's CArray<CString>; std::string elements)
struct StringArrayClass final
    : CompiledCollection<StringArrayClass, &Strings::Destruct<StringArrayClass>, &Strings::Serialize> {};
// CPtrList (the lists the code builds on the stack; blocks of 10)
struct PtrListClass final
    : CompiledCollection<PtrListClass, &PtrList::Destruct<PtrListClass>, &PtrList::Serialize> {};
// The item and object pointer lists (CTypedPtrList)
struct ItemListClass final
    : CompiledCollection<ItemListClass, &PtrList::Destruct<ItemListClass>, &PtrList::Serialize> {};
// A pointer list built around each item creation (world quests)
struct TempListClass final
    : CompiledCollection<TempListClass, &PtrList::Destruct<TempListClass>, &PtrList::Serialize> {};
// The units' path cell lists
struct CellListClass final
    : CompiledCollection<CellListClass, &PtrList::Destruct<CellListClass>, &List8::Serialize> {};
// The loot lists
struct LootListClass final
    : CompiledCollection<LootListClass, &PtrList::Destruct<LootListClass>, &PtrList::Serialize> {};
// The client maps' platform lists
struct PlatformListClass final
    : CompiledCollection<PlatformListClass, &PtrList::Destruct<PlatformListClass>, &List4C::Serialize> {};
// The client map's lists (world_client_maps)
struct ClientListClass final
    : CompiledCollection<ClientListClass, &PtrList::Destruct<ClientListClass>, &PtrList::Serialize> {};
// The client's second list
struct ClientList2Class final
    : CompiledCollection<ClientList2Class, &PtrList::Destruct<ClientList2Class>, &PtrList::Serialize> {};
// The client's third list
struct ClientList3Class final
    : CompiledCollection<ClientList3Class, &PtrList::Destruct<ClientList3Class>, &PtrList::Serialize> {};
// The AI map's node lists
struct NodeListClass final
    : CompiledCollection<NodeListClass, &PtrList::Destruct<NodeListClass>, &List1C::Serialize> {};
// The AI's block cost lists
struct BlockCostListClass final
    : CompiledCollection<BlockCostListClass, &PtrList::Destruct<BlockCostListClass>, &List12::Serialize> {};
// The AI map's lists
struct AiMapListClass final
    : CompiledCollection<AiMapListClass, &PtrList::Destruct<AiMapListClass>, &List8::Serialize> {};
// The AI plan's waypoint lists
struct WaypointListClass final
    : CompiledCollection<WaypointListClass, &PtrList::Destruct<WaypointListClass>, &List12::Serialize> {};
// The list a static initialiser builds
struct StaticListClass final
    : CompiledCollection<StaticListClass, &PtrList::Destruct<StaticListClass>, &List30::Serialize> {};
// The scene loader's CMap<DWORD, DWORD> (and the maps a node record holds)
struct DwordMapClass final
    : CompiledCollection<DwordMapClass, &Map32::Destruct<DwordMapClass>, &Map32::Serialize> {};

// The animation list's copy constructor of the steps' array alone: the copy tag's table written first.
inline AnimationStepListAbi::Array* AnimationStepListAbi::CopyConstructSteps(const Array* source) {
    Array::Construct(AnimationStepsCopyClass::Vtable());
    Array::Copy(*source);
    return this;
}

// ---- Element policies of the owned-element arrays ------------------------------------------------

template <std::size_t Size>
using DeletedThroughObject = ArrayAbi<Bytes<Size>, EachElements<Bytes<Size>, nullptr, DeleteThroughObject<Bytes<Size>>>>;

using SceneObjects = DeletedThroughObject<0x1C>;
using FakeUnitBoneArrays = DeletedThroughObject<0x14>;

// The scene's object records: CMap<DWORD, DWORD>s built, destroyed through slot 1.
using DwordMap = CMap<std::uint32_t, std::uint32_t>;
using OwnedDwordMaps = ArrayAbi<DwordMap, EachElements<DwordMap, InitMap<DwordMapClass>, DeleteThroughObject<DwordMap>>>;

// ---- The instantiations' vtables (game data) -------------------------------------------------------

// Arrays whose elements need nothing.
// Inside the scene loader's 0x120-byte node records.
struct NodeItemsAClass final
    : CompiledCollection<NodeItemsAClass, &ArrayHeader<NodeItemsAClass>::Destruct, &Plain20::Serialize> {};
struct NodeItemsBClass final
    : CompiledCollection<NodeItemsBClass, &ArrayHeader<NodeItemsBClass>::Destruct, &Plain24::Serialize> {};
struct NodeItemsCClass final
    : CompiledCollection<NodeItemsCClass, &ArrayHeader<NodeItemsCClass>::Destruct, &Plain4::Serialize> {};
// The scene loader's {id, value} links.
struct TextureLinkArrayClass final
    : CompiledCollection<TextureLinkArrayClass, &ArrayHeader<TextureLinkArrayClass>::Destruct, &Plain8::Serialize> {};
// The scene meshes' corner records (IndexMeshCorners).
struct MeshCornerArrayClass final
    : CompiledCollection<MeshCornerArrayClass, &ArrayHeader<MeshCornerArrayClass>::Destruct, &Plain16::Serialize> {};
// 12-byte points: animation node normals, use spots, terrain sector data.
struct Vector3ArrayClass final
    : CompiledCollection<Vector3ArrayClass, &ArrayHeader<Vector3ArrayClass>::Destruct, &Plain12::Serialize> {};
// A trap's circles (map_spec, magic_trap).
struct TrapCircleArrayClass
    : CompiledCollection<TrapCircleArrayClass, &ArrayHeader<TrapCircleArrayClass>::Destruct, &Plain12::Serialize> {};
// A trap's points.
struct TrapPointArrayClass
    : CompiledCollection<TrapPointArrayClass, &ArrayHeader<TrapPointArrayClass>::Destruct, &Plain8::Serialize> {};
// A script command's object arguments (script_destroy).
struct ArgObjectsBaseClass
    : CompiledCollection<ArgObjectsBaseClass, &ArrayHeader<ArgObjectsBaseClass>::Destruct, &Plain4::Serialize> {};
// A script command's object arguments, the derived class (script_copy).
struct ArgObjectsClass final : ArgObjectsBaseClass {
    static void* Vtable() { return CompiledVtable<ArgObjectsClass>(); }
};
// A script command's group arguments.
struct ArgGroupsBaseClass
    : CompiledCollection<ArgGroupsBaseClass, &ArrayHeader<ArgGroupsBaseClass>::Destruct, &Plain4::Serialize> {};
// A script command's group arguments, the derived class.
struct ArgGroupsClass final : ArgGroupsBaseClass {
    static void* Vtable() { return CompiledVtable<ArgGroupsClass>(); }
};
// A script command's bindings.
struct BindingsBaseClass
    : CompiledCollection<BindingsBaseClass, &ArrayHeader<BindingsBaseClass>::Destruct, &Plain12::Serialize> {};
// A map object's effects.
struct EffectArrayClass final
    : CompiledCollection<EffectArrayClass, &ArrayHeader<EffectArrayClass>::Destruct, &Plain20::Serialize> {};
// A map object's effect sounds.
struct EffectSoundArrayClass final
    : CompiledCollection<EffectSoundArrayClass, &ArrayHeader<EffectSoundArrayClass>::Destruct, &SoundRecords::Serialize> {};
// A 3D object's frames.
struct FrameArrayClass final
    : CompiledCollection<FrameArrayClass, &ArrayHeader<FrameArrayClass>::Destruct, &Plain56::Serialize> {};
// A unit's motion (UnitMotion's points and waypoints): the arrays the unit's constructor builds are derived
// instantiations of the ones they are destroyed as (game.exe's over).
struct MotionPointArrayClass
    : CompiledCollection<MotionPointArrayClass, &ArrayHeader<MotionPointArrayClass>::Destruct, &Plain28::Serialize> {};
struct MotionPointsClass final : MotionPointArrayClass {
    static void* Vtable() { return CompiledVtable<MotionPointsClass>(); }
};
struct MotionWaypointBaseClass
    : CompiledCollection<MotionWaypointBaseClass, &ArrayHeader<MotionWaypointBaseClass>::Destruct, &Plain12::Serialize> {};
struct MotionWaypointArrayClass final : MotionWaypointBaseClass {
    static void* Vtable() { return CompiledVtable<MotionWaypointArrayClass>(); }
};
// A unit's carried objects.
struct CarriedArrayClass final
    : CompiledCollection<CarriedArrayClass, &ArrayHeader<CarriedArrayClass>::Destruct, &Plain4::Serialize> {};
// CArray<int> of quest item ids (world_party).
struct IdArrayClass final
    : CompiledCollection<IdArrayClass, &ArrayHeader<IdArrayClass>::Destruct, &Plain4::Serialize> {};
// A unit's skin layers (ComposeSkin).
struct SkinLayerArrayClass final
    : CompiledCollection<SkinLayerArrayClass, &ArrayHeader<SkinLayerArrayClass>::Destruct, &PathRecords::Serialize> {};
// The world's countdowns.
struct CountdownArrayClass final
    : CompiledCollection<CountdownArrayClass, &ArrayHeader<CountdownArrayClass>::Destruct, &Plain16::Serialize> {};
// The server's and interface's party unit arrays.
struct PartyUnitArrayClass final
    : CompiledCollection<PartyUnitArrayClass, &ArrayHeader<PartyUnitArrayClass>::Destruct, &Plain4::Serialize> {};
// A sector's occluders.
struct SectorOccluderArrayClass final
    : CompiledCollection<SectorOccluderArrayClass, &ArrayHeader<SectorOccluderArrayClass>::Destruct, &Plain8::Serialize> {};
// The sector spiral's cells.
struct SpiralCellArrayClass final
    : CompiledCollection<SpiralCellArrayClass, &ArrayHeader<SpiralCellArrayClass>::Destruct, &Plain8::Serialize> {};
struct SmoothEdgeArrayClass final
    : CompiledCollection<SmoothEdgeArrayClass, &ArrayHeader<SmoothEdgeArrayClass>::Destruct, &Plain20::Serialize> {};
struct SmoothCopyArrayClass final
    : CompiledCollection<SmoothCopyArrayClass, &ArrayHeader<SmoothCopyArrayClass>::Destruct, &Plain8::Serialize> {};
struct SmoothStepArrayClass final
    : CompiledCollection<SmoothStepArrayClass, &ArrayHeader<SmoothStepArrayClass>::Destruct, &Plain36::Serialize> {};
// The objectives window's exits.
struct ObjectiveExitArrayClass final
    : CompiledCollection<ObjectiveExitArrayClass, &ArrayHeader<ObjectiveExitArrayClass>::Destruct, &Plain28::Serialize> {};
// An item strip's CArray<char*>.
struct ItemStripArrayClass final
    : CompiledCollection<ItemStripArrayClass, &ArrayHeader<ItemStripArrayClass>::Destruct, &Plain4::Serialize> {};
// The HUD's quick item entries.
struct QuickEntryArrayClass final
    : CompiledCollection<QuickEntryArrayClass, &ArrayHeader<QuickEntryArrayClass>::Destruct, &Plain12::Serialize> {};
// The scroll text's token pointers.
struct TokenPtrArrayClass final
    : CompiledCollection<TokenPtrArrayClass, &ArrayHeader<TokenPtrArrayClass>::Destruct, &Plain4::Serialize> {};
// The scroll text's line pointers.
struct LinePtrArrayClass final
    : CompiledCollection<LinePtrArrayClass, &ArrayHeader<LinePtrArrayClass>::Destruct, &Plain4::Serialize> {};
// The terrain builder's arrays.
struct TerrainArrayAClass final
    : CompiledCollection<TerrainArrayAClass, &ArrayHeader<TerrainArrayAClass>::Destruct, &Plain4::Serialize> {};
struct TerrainArrayBClass final
    : CompiledCollection<TerrainArrayBClass, &ArrayHeader<TerrainArrayBClass>::Destruct, &Plain16::Serialize> {};
struct TerrainArrayCClass final
    : CompiledCollection<TerrainArrayCClass, &ArrayHeader<TerrainArrayCClass>::Destruct, &Plain4::Serialize> {};

// A model or animation file's arrays (the loader's).
struct RotKeyArrayClass final
    : CompiledCollection<RotKeyArrayClass, &ArrayHeader<RotKeyArrayClass>::Destruct, &Plain20::Serialize> {};
struct PosKeyArrayClass final
    : CompiledCollection<PosKeyArrayClass, &ArrayHeader<PosKeyArrayClass>::Destruct, &Plain24::Serialize> {};
struct MorphHeadArrayClass final
    : CompiledCollection<MorphHeadArrayClass, &ArrayHeader<MorphHeadArrayClass>::Destruct, &Plain8::Serialize> {};
struct NormalArrayClass final
    : CompiledCollection<NormalArrayClass, &ArrayHeader<NormalArrayClass>::Destruct, &Plain20::Serialize> {};
struct FilePointArrayClass final
    : CompiledCollection<FilePointArrayClass, &ArrayHeader<FilePointArrayClass>::Destruct, &Plain12::Serialize> {};
struct FileUvArrayClass final
    : CompiledCollection<FileUvArrayClass, &ArrayHeader<FileUvArrayClass>::Destruct, &Plain12::Serialize> {};
struct FileFaceArrayClass final
    : CompiledCollection<FileFaceArrayClass, &ArrayHeader<FileFaceArrayClass>::Destruct, &Plain28::Serialize> {};
struct IndexArrayClass final
    : CompiledCollection<IndexArrayClass, &ArrayHeader<IndexArrayClass>::Destruct, &Plain4::Serialize> {};

// Lists whose elements need nothing.
// A figure bone's children and an animation state's bones.
struct BoneListClass final : CompiledCollection<BoneListClass, &PtrList::Destruct<BoneListClass>, &PtrList::Serialize> {};
// An animation state's queue.
struct AnimationStateListClass final
    : CompiledCollection<AnimationStateListClass, &PtrList::Destruct<AnimationStateListClass>, &PtrList::Serialize> {};
// A 3D object's effect sources.
struct Object3DListClass final
    : CompiledCollection<Object3DListClass, &PtrList::Destruct<Object3DListClass>, &PtrList::Serialize> {};
// The client world's and the sectors' object lists.
struct ClientObjectListClass final
    : CompiledCollection<ClientObjectListClass, &PtrList::Destruct<ClientObjectListClass>, &PtrList::Serialize> {};
// CWorldServer::AwardExperience's list.
struct ExperienceListClass final
    : CompiledCollection<ExperienceListClass, &PtrList::Destruct<ExperienceListClass>, &List8::Serialize> {};
// AIAttention::Choose's list.
struct AttentionListClass final
    : CompiledCollection<AttentionListClass, &PtrList::Destruct<AttentionListClass>, &AttentionList::Serialize> {};
// A player's list.
struct PlayerListClass final
    : CompiledCollection<PlayerListClass, &PtrList::Destruct<PlayerListClass>, &PtrList::Serialize> {};
// The texture manager's list.
struct TextureListClass final
    : CompiledCollection<TextureListClass, &PtrList::Destruct<TextureListClass>, &PtrList::Serialize> {};
// The sound manager's list.
struct SoundListClass final
    : CompiledCollection<SoundListClass, &PtrList::Destruct<SoundListClass>, &PtrList::Serialize> {};

// Maps whose elements need nothing.
// LoadFigureRegistry's map.
struct FigureRegistryMapClass final
    : CompiledCollection<FigureRegistryMapClass, &Map32::Destruct<FigureRegistryMapClass>, &Map32::Serialize> {};
// IndexMeshCorners' maps.
struct CornerMapAClass final
    : CompiledCollection<CornerMapAClass, &Map32::Destruct<CornerMapAClass>, &Map64x64::Serialize> {};
struct CornerMapBClass final
    : CompiledCollection<CornerMapBClass, &Map32::Destruct<CornerMapBClass>, &Map64x32::Serialize> {};
struct CornerMapCClass final
    : CompiledCollection<CornerMapCClass, &Map32::Destruct<CornerMapCClass>, &Map32::Serialize> {};

// Arrays whose elements own something.
// A fake unit's 20-byte CObject elements (FakeUnitConstruct).
struct FakeUnitBoneArraysClass final
    : CompiledCollection<FakeUnitBoneArraysClass, &FakeUnitBoneArrays::Destruct<FakeUnitBoneArraysClass>, &OwnedBoneLinkArrays::Serialize> {};
// The scene loader's 0x1C-byte CObject elements (SceneInfo::Load).
struct SceneObjectArrayClass final
    : CompiledCollection<SceneObjectArrayClass, &SceneObjects::Destruct<SceneObjectArrayClass>, &OwnedDwordMaps::Serialize> {};
// The terrain's CByteArray elements (Serialize).
struct TerrainByteArraysClass final
    : CompiledCollection<TerrainByteArraysClass, &ByteArrays::Destruct<TerrainByteArraysClass>, &ByteArrays::Serialize> {};
// The scene loader's 20-byte morph keys (a frame and a point vector).
struct MorphKeyArrayClass final
    : CompiledCollection<MorphKeyArrayClass, &ArrayHeader<MorphKeyArrayClass>::Destruct, &Plain20::Serialize> {};
// Patrols (32 bytes, a waypoint array at +0xC).
struct PatrolArrayClass
    : CompiledCollection<PatrolArrayClass, &OwnedPatrols::Destruct<PatrolArrayClass>, &OwnedPatrols::Serialize> {};
// A unit record behaviour's patrols (mapspec::Patrol elements): derived from PatrolArrayClass.
struct BehaviourPatrolsArrayClass final : PatrolArrayClass {
    static void* Vtable() { return CompiledVtable<BehaviourPatrolsArrayClass>(); }
};
// The script's areas (24-byte elements that are arrays; game_arrays.h's AreaArrayClass is the same table).
struct ScriptAreaArrayClass final
    : CompiledCollection<ScriptAreaArrayClass, &ScriptAreas::Destruct<ScriptAreaArrayClass>, &ScriptAreas::Serialize> {};

// A map trap record's areas (12-byte elements; the magic trap's circles).
struct TrapAreasArrayClass final : TrapCircleArrayClass {
    static void* Vtable() { return CompiledVtable<TrapAreasArrayClass>(); }
};
// A map trap record's targets (8-byte elements; the magic trap's points).
struct TrapTargetsArrayClass final : TrapPointArrayClass {
    static void* Vtable() { return CompiledVtable<TrapTargetsArrayClass>(); }
};

} // namespace mfc::impl
