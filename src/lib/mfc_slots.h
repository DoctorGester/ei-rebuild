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

// The container instantiations of mfc_array.h and mfc_list.h: their tags are declared there and defined
// here, one class per vtable. A tag that is a base of another is not final (the base is defined first); the
// derived tag's table calls the base's destructor.

// CArray<int> / CDWordArray-like: the zone and quest id lists, the climb tables
struct IntArrayClass final
    : CompiledCollection<IntArrayClass, &ArrayHeader<IntArrayClass>::Destruct, &Plain4::Serialize> {};
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
// A figure's bone links
struct BoneLinkArrayClass final
    : CompiledCollection<BoneLinkArrayClass, &ArrayHeader<BoneLinkArrayClass>::Destruct, &KeyFrames::Serialize> {};
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
// The script's string arrays (game.exe's CArray<CString>; std::string elements)
struct StringArrayClass final
    : CompiledCollection<StringArrayClass, &Strings::Destruct<StringArrayClass>, &Strings::Serialize> {};
// The units' path cell lists
// The client's third list
struct ClientList3Class final
    : CompiledCollection<ClientList3Class, &PtrList::Destruct<ClientList3Class>, &PtrList::Serialize> {};
// The AI's block cost lists
// The AI plan's waypoint lists
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

// The engine's aligned block whose pointer is at `At` freed (its allocation's start is the word
// before), the pointer left.
template <typename E, std::size_t At>
void FreeAlignedAt(E* p) {
    void* data = *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(p) + At);
    if (data) lib::FreeAligned32(data);
}

template <std::size_t Size>
using DeletedThroughObject = ArrayAbi<Bytes<Size>, EachElements<Bytes<Size>, nullptr, DeleteThroughObject<Bytes<Size>>>>;
template <std::size_t Size, std::size_t At>
using AlignedBlockAt = ArrayAbi<Bytes<Size>, EachElements<Bytes<Size>, nullptr, FreeAlignedAt<Bytes<Size>, At>>>;

using SceneObjects = DeletedThroughObject<0x1C>;
using FakeUnitBoneArrays = DeletedThroughObject<0x14>;
using AlignedAt4Records = AlignedBlockAt<0x14, 4>;

// The scene's object records: CMap<DWORD, DWORD>s built, destroyed through slot 1.
using DwordMap = CMap<std::uint32_t, std::uint32_t>;
using OwnedDwordMaps = ArrayAbi<DwordMap, EachElements<DwordMap, InitMap<DwordMapClass>, DeleteThroughObject<DwordMap>>>;

// ---- The instantiations' vtables (game data) -------------------------------------------------------

// Arrays whose elements need nothing.
// A trap's circles (map_spec, magic_trap).
// A trap's points.
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
// A unit's carried objects.
struct CarriedArrayClass final
    : CompiledCollection<CarriedArrayClass, &ArrayHeader<CarriedArrayClass>::Destruct, &Plain4::Serialize> {};
// A unit's skin layers (ComposeSkin).
struct SkinLayerArrayClass final
    : CompiledCollection<SkinLayerArrayClass, &ArrayHeader<SkinLayerArrayClass>::Destruct, &PathRecords::Serialize> {};
// A sector's occluders.
struct SectorOccluderArrayClass final
    : CompiledCollection<SectorOccluderArrayClass, &ArrayHeader<SectorOccluderArrayClass>::Destruct, &Plain8::Serialize> {};
// The path smoother's 36-byte steps.
struct SmoothStepArrayClass final
    : CompiledCollection<SmoothStepArrayClass, &ArrayHeader<SmoothStepArrayClass>::Destruct, &Plain36::Serialize> {};

// A model or animation file's arrays (the loader's).
struct IndexArrayClass final
    : CompiledCollection<IndexArrayClass, &ArrayHeader<IndexArrayClass>::Destruct, &Plain4::Serialize> {};

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
// The scene loader's 20-byte records with an aligned block at +4.
struct AlignedAt4ArrayClass final
    : CompiledCollection<AlignedAt4ArrayClass, &AlignedAt4Records::Destruct<AlignedAt4ArrayClass>, &AlignedAt4Records::Serialize> {};
// The script's areas (24-byte elements that are arrays; game_arrays.h's AreaArrayClass is the same table).
struct ScriptAreaArrayClass final
    : CompiledCollection<ScriptAreaArrayClass, &ScriptAreas::Destruct<ScriptAreaArrayClass>, &ScriptAreas::Serialize> {};

} // namespace mfc::impl
