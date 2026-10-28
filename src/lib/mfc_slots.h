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

// The animation list's copy constructor of the steps' array alone: the copy tag's table written first.
inline AnimationStepListAbi::Array* AnimationStepListAbi::CopyConstructSteps(const Array* source) {
    Array::Construct(AnimationStepsCopyClass::Vtable());
    Array::Copy(*source);
    return this;
}

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
// A map object's effect sounds.
// A 3D object's frames.
struct FrameArrayClass final
    : CompiledCollection<FrameArrayClass, &ArrayHeader<FrameArrayClass>::Destruct, &Plain56::Serialize> {};
// A unit's carried objects.
struct CarriedArrayClass final
    : CompiledCollection<CarriedArrayClass, &ArrayHeader<CarriedArrayClass>::Destruct, &Plain4::Serialize> {};
// A unit's skin layers (ComposeSkin).
// A sector's occluders.
struct SectorOccluderArrayClass final
    : CompiledCollection<SectorOccluderArrayClass, &ArrayHeader<SectorOccluderArrayClass>::Destruct, &Plain8::Serialize> {};
// The path smoother's 36-byte steps.
struct SmoothStepArrayClass final
    : CompiledCollection<SmoothStepArrayClass, &ArrayHeader<SmoothStepArrayClass>::Destruct, &Plain36::Serialize> {};

// Arrays whose elements own something.
// The terrain's CByteArray elements (Serialize).
struct TerrainByteArraysClass final
    : CompiledCollection<TerrainByteArraysClass, &ByteArrays::Destruct<TerrainByteArraysClass>, &ByteArrays::Serialize> {};
// The script's areas (24-byte elements that are arrays; game_arrays.h's AreaArrayClass is the same table).
struct ScriptAreaArrayClass final
    : CompiledCollection<ScriptAreaArrayClass, &ScriptAreas::Destruct<ScriptAreaArrayClass>, &ScriptAreas::Serialize> {};

} // namespace mfc::impl
