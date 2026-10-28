#pragma once

// MFC 4.2's CArray<TYPE, ARG_TYPE> (afxtempl.h) as game.exe instantiates it (docs/notes/standalone.md section
// 2). The bodies are mfc::CArray's (mfc.h); the instantiations differ in the element: its size, how
// ConstructElements builds new ones (MFC's memset, then the element's constructor if it has one: inlined, or an
// out-of-line helper) and how DestructElements destroys them: the element policies below. ArrayAbi turns the
// members into the instantiations' calling conventions; mfc_array.cpp registers each instantiation at its
// library address.

#include "containers.h"
#include "mfc.h"
#include "mfc_coll.h"
#include "mfc_string.h"
#include "game/game_data.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>

namespace mfc::impl {

struct CArchive;

// An element seen only as its bytes (plain data whose type the array code never looks into).
template <std::size_t N>
struct Bytes {
    std::uint8_t bytes[N];
};

// ---- Element policies ---------------------------------------------------------------------------
// Each has Construct(T*, int) and Destruct(T*, int). Plain data is mfc::PlainElements<T> (zero fill,
// nothing destroyed).

// Zero fill, then `Init` on each element: an element constructor the instantiation inlined after
// MFC's memset (placement new; MFC 4.2 tests the pointer for null first, never null here).
template <typename T, void (*Init)(T*)>
struct InitElements {
    static void Construct(T* p, int n) {
        PlainElements<T>::Construct(p, n);
        for (; n--; ++p) Init(p);
    }
    static void Destruct(T*, int) {}
};

// Zero fill, then `Init` on each element; `Destroy` on each, in order, when destroyed (MFC's
// DestructElements calling the element's destructor). Either nullptr: nothing. Each takes T* (an
// element constructor or destructor of the game's, ours, called directly).
template <typename T, auto Init, auto Destroy>
struct EachElements {
    static void Construct(T* p, int n) {
        PlainElements<T>::Construct(p, n);
        if constexpr (!std::is_null_pointer_v<decltype(Init)>)
            for (; n--; ++p) Init(p);
    }
    static void Destruct(T* p, int n) {
        if constexpr (!std::is_null_pointer_v<decltype(Destroy)>)
            for (; n--; ++p) Destroy(p);
    }
};

// Elements built by `ConstructFn` and destroyed by `DestructFn` (each void(T*, int): an out-of-line
// ConstructElements / DestructElements, ours or the game's). nullptr: zero fill / nothing destroyed.
template <typename T, auto ConstructFn, auto DestructFn>
struct HelperElements {
    static void Construct(T* p, int n) {
        if constexpr (std::is_null_pointer_v<decltype(ConstructFn)>) PlainElements<T>::Construct(p, n);
        else ConstructFn(p, n);
    }
    static void Destruct(T* p, int n) {
        if constexpr (!std::is_null_pointer_v<decltype(DestructFn)>) DestructFn(p, n);
    }
};

// An array header inside an element (the CArray constructor inlined): the vtable of `Class` (a type with a
// static Vtable(), the instantiation's vtable in the exe's data), then data, size, maxSize and growBy zero.
template <typename Class>
void ConstructHeader(void* at) {
    static_cast<CArray<Bytes<1>>*>(at)->Construct(Class::Vtable());
}

// ---- The instantiations' calling conventions ----------------------------------------------------

template <typename T, typename E = PlainElements<T>>
struct ArrayAbi : CArray<T, E> {
    using Element = T;
    using Array = CArray<T, E>;

    // Serialize (defined in mfc_slots.h): the count (loading: ReadCount, then SetSize(count, -1)), then the
    // elements in one block.
    void Serialize(CArchive* ar);

    // The instantiations' SetSize(nNewSize, nGrowBy), SetAtGrow(nIndex, ARG_TYPE) and Add(ARG_TYPE)
    // (ARG_TYPE a reference: the element's address), Copy(const CArray&), RemoveAt(nIndex, nCount)
    // ElementAt(nIndex) and the by-value GetAt(nIndex) are CArray's own members
    // (mfc.h), reached through this type (Abi::SetSize ...).
    // SetAtGrow for the dword instantiations: ARG_TYPE the value.
    void SetAtGrowByValue(int index, T value) { Array::SetAtGrow(index, value); }
    static void ConstructElements(T* elements, int count) { E::Construct(elements, count); }
    static void DestructElements(T* elements, int count) { E::Destruct(elements, count); }

    // The destructor (the vtable of `Class` written first): the elements destroyed, the storage freed.
    template <typename Class>
    static void Destruct(Array* self) {
        self->vtable = Class::Vtable();
        self->FreeData();
    }
};

// The constructor of every instantiation: the vtable of `Class`, the rest zero; returns this.
template <typename Class>
struct ArrayHeader {
    using Array = CArray<Bytes<1>>;
    static Array* Construct(Array* self) {
        return self->Construct(Class::Vtable());
    }
    // The destructor of an instantiation whose elements need nothing: the storage freed.
    static void Destruct(Array* self) { ArrayAbi<Bytes<1>>::Destruct<Class>(self); }
};

// An array declared with the default element policy seen as the instantiation `Abi` of the same
// layout (its elements built and destroyed as that instantiation's out-of-line code does).
template <typename Abi, typename T, typename E>
typename Abi::Array& ArrayAs(CArray<T, E>& a) {
    static_assert(sizeof(typename Abi::Element) == sizeof(T));
    return reinterpret_cast<typename Abi::Array&>(a);
}

// An instantiation's ConstructElements / DestructElements (__stdcall) over elements declared
// as another type of the same size, or untyped.
template <typename Abi>
void ConstructElementsAt(void* elements, int count) {
    Abi::ConstructElements(static_cast<typename Abi::Element*>(elements), count);
}
template <typename Abi>
void DestructElementsAt(void* elements, int count) {
    Abi::DestructElements(static_cast<typename Abi::Element*>(elements), count);
}

// The same constructor and destructor on a typed array: the vtable of `Class`; the elements
// destroyed by the array's policy, the storage freed (ArrayAbi::Destruct).
template <typename Class, typename T, typename E>
CArray<T, E>* ConstructArrayOf(CArray<T, E>& a) {
    return a.Construct(Class::Vtable());
}
template <typename Class, typename T, typename E>
void DestructArrayOf(CArray<T, E>& a);

// The destructor of an instantiation whose elements need nothing, on a header inside an element or
// a layout (ConstructHeader's counterpart): the vtable of `Class`, the storage freed.
template <typename Class>
void DestructHeader(void* at) {
    DestructArrayOf<Class>(*static_cast<CArray<Bytes<1>>*>(at));
}

template <typename Class, typename T, typename E>
void DestructArrayOf(CArray<T, E>& a) {
    a.vtable = Class::Vtable();
    a.FreeData();
}

// One type per vtable, for the templates: Vtable() is the address of the instantiation's vtable. The
// forward-declared ones are compiler-made vtables (classes of mfc_slots.h and the game headers deriving from
// CompiledCollection); the element policies and aliases below only name them. What each is: mfc_slots.h.

struct IntArrayClass;
struct ScriptAreaClass;
struct AiLookArrayClass;
struct BoneLinkArrayClass;
struct WaypointArrayClass;
struct ScriptAreaPartsClass;
struct ArrayBaseClass;
struct AiLookRecordArrayClass;

// ---- Elements ------------------------------------------------------------------------------------

// 40-byte key frames: a quaternion at +0x18, identity (w first) after the zero fill.
struct KeyFrame40 {
    std::uint8_t head[0x18];
    float w, x, y, z;
};
static_assert(sizeof(KeyFrame40) == 0x28);
inline void InitKeyFrame40(KeyFrame40* p) {
    p->w = 1.0f;
    p->x = 0.0f;
    p->y = 0.0f;
    p->z = 0.0f;
}

// Elements that hold an array header of `Class` at `ArrayAt` (the 20-byte element is the
// array; 32-byte patrols with the array at +0xC).
template <std::size_t Size, std::size_t ArrayAt, typename Class>
struct WithArray {
    std::uint8_t bytes[Size];
};
template <std::size_t Size, std::size_t ArrayAt, typename Class>
void InitWithArray(WithArray<Size, ArrayAt, Class>* p) {
    ConstructHeader<Class>(p->bytes + ArrayAt);
}

// 40-byte placements: a quaternion at +0x10, identity (w first) after the zero fill.
struct Placement40 {
    std::uint8_t head[0x10];
    float w, x, y, z;
    std::uint8_t tail[8];
};
static_assert(sizeof(Placement40) == 0x28);
inline void InitPlacement40(Placement40* p) {
    p->w = 1.0f;
    p->x = 0.0f;
    p->y = 0.0f;
    p->z = 0.0f;
}

// 12-byte path records: 0, then two indices -1.
struct PathRecord12 {
    int value;
    int first;
    int second;
};
inline void InitPathRecord12(PathRecord12* p) {
    p->value = 0;
    p->first = -1;
    p->second = -1;
}

// 20-byte effect sound records: 0, two handles -1, 0, a flag byte 0.
struct SoundRecord20 {
    int value;
    int first;
    int second;
    int extra;
    std::uint8_t flag;
    std::uint8_t padding[3];
};
static_assert(sizeof(SoundRecord20) == 0x14);
inline void InitSoundRecord20(SoundRecord20* p) {
    p->value = 0;
    p->flag = 0;
    p->first = -1;
    p->second = -1;
    p->extra = 0;
}

// A unit's animation step (12 bytes; game/combat.h's AnimationStep): the blank step after the zero
// fill ('s values): animation and variant 0xFF, flags 0, duration -1, kind 0.
struct AnimationStep12 {
    std::uint8_t animation;
    std::uint8_t variant;
    std::uint8_t flags;
    std::uint8_t padding;
    int duration;
    int kind;
};
static_assert(sizeof(AnimationStep12) == 0xC);
inline void InitAnimationStep12(AnimationStep12* p) {
    p->variant = 0xFF;
    p->animation = 0xFF;
    p->duration = -1;
    p->kind = 0;
    p->flags = 0;
}

// 16-byte effect particles (game/unit.h EffectParticle): type 0, power 0x10, no source, not present
struct Particle16 {
    int type;
    int power;
    void* source;
    std::uint8_t present;
    std::uint8_t padding[3];
};
static_assert(sizeof(Particle16) == 0x10);
inline void InitParticle16(Particle16* p) {
    p->source = nullptr;
    p->type = 0;
    p->power = 0x10;
    p->present = 0;
}

// The destructors of elements that hold an array header at `ArrayAt`: the vtable of `BaseClass`, the storage
// freed, the fields left.
template <typename E, std::size_t ArrayAt, typename BaseClass>
void DestroyArrayAt(E* p) {
    auto* header = reinterpret_cast<CArray<Bytes<1>>*>(reinterpret_cast<std::uint8_t*>(p) + ArrayAt);
    header->vtable = BaseClass::Vtable();
    if (header->data) gamecrt::operator_delete(header->data);
}

// ---- The instantiations --------------------------------------------------------------------------

using Plain4 = ArrayAbi<Bytes<4>>;
using Plain8 = ArrayAbi<Bytes<8>>;
using Plain12 = ArrayAbi<Bytes<12>>;
using Plain16 = ArrayAbi<Bytes<16>>;
using Plain20 = ArrayAbi<Bytes<20>>;
using Plain24 = ArrayAbi<Bytes<24>>;
using Plain28 = ArrayAbi<Bytes<28>>;
using Plain40 = ArrayAbi<Bytes<40>>;
using Floats = ArrayAbi<float>;

using KeyFrames = ArrayAbi<KeyFrame40, InitElements<KeyFrame40, InitKeyFrame40>>;
using Patrols = ArrayAbi<WithArray<0x20, 0xC, WaypointArrayClass>,
                         InitElements<WithArray<0x20, 0xC, WaypointArrayClass>, InitWithArray<0x20, 0xC, WaypointArrayClass>>>;

using Particles = ArrayAbi<Particle16, InitElements<Particle16, InitParticle16>>;
using Placements = ArrayAbi<Placement40, InitElements<Placement40, InitPlacement40>>;
using PathRecords = ArrayAbi<PathRecord12, InitElements<PathRecord12, InitPathRecord12>>;
using SoundRecords = ArrayAbi<SoundRecord20, InitElements<SoundRecord20, InitSoundRecord20>>;
using AnimationSteps = ArrayAbi<AnimationStep12, InitElements<AnimationStep12, InitAnimationStep12>>;

// A unit's animation list (game/combat.h's AnimationSteps): the steps' array and a
// dword copied along with them.
struct AnimationStepList : AnimationSteps::Array {
    int extra;
};
static_assert(sizeof(AnimationStepList) == 0x18);

struct AnimationStepListAbi : AnimationStepList {
    using Array = AnimationSteps::Array;
    // The copy constructor: the steps (SetSize to the source's count, then each
    // step), then extra. Returns this.
    AnimationStepList* CopyConstruct(const AnimationStepList* source) {
        CopyConstructSteps(source);
        this->extra = source->extra;
        return this;
    }
    // The same without extra (the steps' array alone). Defined in mfc_slots.h, after AnimationStepsCopyClass.
    Array* CopyConstructSteps(const Array* source);
    // Operator=: Copy, then extra. Returns this.
    AnimationStepList* Assign(const AnimationStepList* source) {
        this->Copy(*source);
        this->extra = source->extra;
        return this;
    }
};

// Elements that own their array: built as above, destroyed by the destructors above.
using PatrolElement = WithArray<0x20, 0xC, WaypointArrayClass>;
using OwnedPatrols = ArrayAbi<PatrolElement, EachElements<PatrolElement, InitWithArray<0x20, 0xC, WaypointArrayClass>,
                                                        DestroyArrayAt<PatrolElement, 0xC, ArrayBaseClass>>>;
using AiLookElement = WithArray<0x20, 0xC, AiLookRecordArrayClass>;
using AiLookRecords = ArrayAbi<AiLookElement, EachElements<AiLookElement, InitWithArray<0x20, 0xC, AiLookRecordArrayClass>,
                                                          DestroyArrayAt<AiLookElement, 0xC, AiLookArrayClass>>>;

// Script areas: 24-byte elements that are an array of ScriptAreaPartsClass, destroyed by
// the base's destructor (ScriptAreaClass's vtable, the storage freed).
using ScriptAreaElement = WithArray<0x18, 0, ScriptAreaPartsClass>;
using ScriptAreas = ArrayAbi<ScriptAreaElement, EachElements<ScriptAreaElement, InitWithArray<0x18, 0, ScriptAreaPartsClass>,
                                                            DestroyArrayAt<ScriptAreaElement, 0, ScriptAreaClass>>>;

// Elements destroyed through their virtual destructor with flags 0 (MFC's `pElements->~TYPE()`):
// slot 1 for CObject-derived elements (an array's DeletingDestructor), slot 0 for the C++ classes
// whose virtual destructor comes first. Declared only: the call goes through the element's vtable.
class __declspec(novtable) DeletingFirst {
public:
    virtual void* DeletingDestructor(unsigned flags);
};
template <typename E>
void DeleteThroughObject(E* p) { reinterpret_cast<CObjectVirtuals*>(p)->DeletingDestructor(0); }
template <typename E>
void DeleteThroughFirst(E* p) { reinterpret_cast<DeletingFirst*>(p)->DeletingDestructor(0); }

// The figure's bone link arrays (20-byte elements that are arrays) and the terrain's byte
// arrays (CByteArray's constructor on each).
using BoneLinkElement = WithArray<0x14, 0, BoneLinkArrayClass>;
using OwnedBoneLinkArrays = ArrayAbi<BoneLinkElement, EachElements<BoneLinkElement, InitWithArray<0x14, 0, BoneLinkArrayClass>,
                                                                  DeleteThroughObject<BoneLinkElement>>>;
using ByteArrayElement = WithArray<0x14, 0, ByteArrayClass>;
using ByteArrays = ArrayAbi<ByteArrayElement, EachElements<ByteArrayElement, InitWithArray<0x14, 0, ByteArrayClass>,
                                                          DeleteThroughObject<ByteArrayElement>>>;

// Elements that are the engine's aligned arrays (lib/containers.h AlignedArray, 16 bytes; the terrain
// sectors' lists): zero filled; destroyed by freeing the block, the fields left.
inline void FreeAlignedData(Bytes<16>* p) {
    void* data = *reinterpret_cast<void**>(p->bytes);
    if (data) lib::FreeAligned32(data);
}
using AlignedArrays = ArrayAbi<Bytes<16>, EachElements<Bytes<16>, nullptr, FreeAlignedData>>;

// The script's string arrays (game.exe's CArray<CString>): std::string elements.
using Strings = ArrayAbi<std::string>;
} // namespace mfc::impl
