#pragma once

// MFC 4.2's CArray<TYPE, ARG_TYPE> (afxtempl.h) as game.exe instantiates it, for the standalone exe
// (ticket S6, docs/notes/standalone.md section 2). The bodies are mfc::CArray's (mfc.h); what differs
// between the instantiations is the element: its size, how ConstructElements builds new ones (MFC's
// memset, then the element's constructor if it has one: inlined, or an out-of-line helper) and how
// DestructElements destroys them. Those are the element policies below. ArrayAbi turns the members
// into the instantiations' calling conventions (__thiscall as __fastcall with EDX unused; the element
// helpers __stdcall), and mfc_array.cpp registers each instantiation our code forwards to at its
// library address. Checked by tests/verify/verify_mfc_array.cpp.

#include "mfc.h"
#include "mfc_string.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace mfc::impl {

// An element seen only as its bytes (plain data whose type the array code never looks into).
template <std::size_t N>
struct Bytes {
    std::uint8_t bytes[N];
};

// ---- Element policies ---------------------------------------------------------------------------
//
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

// An array header inside an element (the CArray constructor inlined): the vtable of `Class` (a type
// with a static Vtable(), the instantiation's vtable in the exe's data), then data, size, maxSize and
// growBy zero.
template <typename Class>
void ConstructHeader(void* at) {
    static_cast<CArray<Bytes<1>>*>(at)->Construct(reinterpret_cast<std::uintptr_t>(Class::Vtable()));
}

// ---- The instantiations' calling conventions ----------------------------------------------------

template <typename T, typename E = PlainElements<T>>
struct ArrayAbi {
    using Element = T;
    using Array = CArray<T, E>;

    // SetSize(nNewSize, nGrowBy) (RET 8).
    static void __fastcall SetSize(Array* self, void*, int newSize, int newGrowBy) { self->SetSize(newSize, newGrowBy); }
    // SetAtGrow(nIndex, ARG_TYPE) (RET 8): ARG_TYPE a reference (the element's address) or, for the
    // dword instantiations, the value.
    static void __fastcall SetAtGrow(Array* self, void*, int index, const T* value) { self->SetAtGrow(index, *value); }
    static void __fastcall SetAtGrowByValue(Array* self, void*, int index, T value) { self->SetAtGrow(index, value); }
    // Add(ARG_TYPE) (RET 4): the new element's index.
    static int __fastcall Add(Array* self, void*, const T* value) { return self->Add(*value); }
    // Copy(const CArray&) (RET 4): SetSize to the source's size, then the elements assigned.
    static void __fastcall Copy(Array* self, void*, const Array* source) { self->Copy(*source); }
    // RemoveAt(nIndex, nCount) (RET 8).
    static void __fastcall RemoveAt(Array* self, void*, int index, int count) { self->RemoveAt(index, count); }
    // ElementAt(nIndex) (RET 4).
    static T* __fastcall ElementAt(Array* self, void*, int index) { return &self->data[index]; }
    // GetAt(nIndex) returning the element by value (RET 8: the hidden result pointer first; returns it).
    static T* __fastcall GetAt(const Array* self, void*, T* result, int index) {
        *result = self->data[index];
        return result;
    }
    // ConstructElements / DestructElements (__stdcall, RET 8).
    static void __stdcall ConstructElements(T* elements, int count) { E::Construct(elements, count); }
    static void __stdcall DestructElements(T* elements, int count) { E::Destruct(elements, count); }

    // The destructor (the vtable of `Class` written first): the elements destroyed, the storage freed.
    template <typename Class>
    static void __fastcall Destruct(Array* self) {
        self->vtable = Class::Vtable();
        self->FreeData();
    }
};

// The constructor of every instantiation: the vtable of `Class`, the rest zero; returns this.
template <typename Class>
struct ArrayHeader {
    using Array = CArray<Bytes<1>>;
    static Array* __fastcall Construct(Array* self) {
        return self->Construct(reinterpret_cast<std::uintptr_t>(Class::Vtable()));
    }
    // The destructor of an instantiation whose elements need nothing: the storage freed.
    static void __fastcall Destruct(Array* self) { ArrayAbi<Bytes<1>>::Destruct<Class>(self); }
};

// ---- The instantiations' vtables (game data) -------------------------------------------------------
//
// One type per vtable, for the templates: Vtable() is the address of the instantiation's vtable.

struct IntArrayClass {   // CArray<int> / CDWordArray-like: the zone and quest id lists, the climb tables
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C3D0); }
};
struct UnitPtrArrayClass {   // the interface's unit pointer arrays
    static void* Vtable() { return reinterpret_cast<void*>(0x007402AC); }
};
struct FloatArrayClass {   // CArray<float>
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D3E8); }
};
struct ArgFloatArrayClass {   // the script commands' float arguments (a CArray<float> subclass)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D490); }
};
struct ObjectRefArrayClass {   // CArray<ScriptObjectRef> (a script group's base)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D11C); }
};
struct ScriptGroupClass {   // a script group (game/script.h ScriptGroup)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D110); }
};
struct ScriptFloatsClass {   // the script's float variables
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D2EC); }
};
struct ScriptAreaClass {   // a script area's parts
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D31C); }
};
struct ScriptIfThenArrayClass {   // the script's if-then blocks
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D3AC); }
};
struct PartySlotArrayClass {   // the script's party slots
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D2C8); }
};
struct AiLookArrayClass {   // the AI's look records
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D460); }
};
struct SpriteArrayClass {   // the weather and lightning sprites
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C584); }
};
struct RotKeyArrayClass {   // an animation file's rotation keys
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C83C); }
};
struct PosKeyArrayClass {   // an animation file's position keys
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C830); }
};
struct MorphHeadArrayClass {   // an animation file's morph headers
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C824); }
};
struct NormalArrayClass {   // a model file's normals
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C86C); }
};
struct FilePointArrayClass {   // a model file's points
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C860); }
};
struct FileUvArrayClass {   // a model file's texture coordinates
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C854); }
};
struct FileFaceArrayClass {   // a model file's faces
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C848); }
};
struct IndexArrayClass {   // a model file's index lists
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C800); }
};
struct BoneLinkArrayClass {   // a figure's bone links
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C12C); }
};
struct WaypointArrayClass {   // a patrol's waypoints
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C3A0); }
};
struct ExitArrayClass {   // a zone's exits
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C3F4); }
};
struct MapRecordItemsClass {   // the array inside a map record
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C400); }
};
struct MotionWaypointArrayClass {   // a unit's motion waypoints
    static void* Vtable() { return reinterpret_cast<void*>(0x0073EB90); }
};
struct MotionPointArrayClass {   // a unit's motion points
    static void* Vtable() { return reinterpret_cast<void*>(0x0073EBA8); }
};
struct PointArrayClass {   // a unit's point lists
    static void* Vtable() { return reinterpret_cast<void*>(0x0073F07C); }
};
struct AttackOptionArrayClass {   // a unit's attack options
    static void* Vtable() { return reinterpret_cast<void*>(0x0073F040); }
};
struct AnimationStepsClass {   // a unit's animation steps
    static void* Vtable() { return reinterpret_cast<void*>(0x0073EF2C); }
};
struct AnimationStepsCopyClass {   // the animation steps a motion copies
    static void* Vtable() { return reinterpret_cast<void*>(0x0073F01C); }
};
struct ClientMapArrayClass {   // the client map arrays (world_client_maps)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073F730); }
};
struct ObjectPtrArrayClass {   // a save's shared object pointers
    static void* Vtable() { return reinterpret_cast<void*>(0x0073FAC8); }
};
struct SectorArrayClass {   // the AI map's sectors
    static void* Vtable() { return reinterpret_cast<void*>(0x0073F9EC); }
};
struct CellRowArrayClass {   // the AI map's cell rows
    static void* Vtable() { return reinterpret_cast<void*>(0x0073FA04); }
};
struct AreaListClass {   // the AI map's area lists
    static void* Vtable() { return reinterpret_cast<void*>(0x0073F9E0); }
};
struct ArrayBaseClass {   // the base vtable the element arrays' destructors restore (patrols, motion records)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073C388); }
};
struct AiLookRecordArrayClass {   // the array inside the AI's 32-byte look records (base AiLookArrayClass)
    static void* Vtable() { return reinterpret_cast<void*>(0x0073D4B4); }
};

// ---- Elements ------------------------------------------------------------------------------------

// 40-byte key frames: a quaternion at +0x18, identity (w first) after the zero fill (00453730).
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

// {dword, CString} (004553F0) and 0x3C-byte records with a CString at +0x28 (004606A0): the string
// set to the shared empty one.
template <std::size_t Size, std::size_t StringAt>
struct WithString {
    std::uint8_t bytes[Size];
};
template <std::size_t Size, std::size_t StringAt>
void InitWithString(WithString<Size, StringAt>* p) {
    *reinterpret_cast<char**>(p->bytes + StringAt) = PchNil();
}

// Records that hold strings and an array: `Strings` CStrings from +0, an array header of `Class` at
// `ArrayAt` (00460720: 0x2C bytes, six strings, the array at +0x18; 004607E0: 0x5C bytes, eight
// strings, the array at +0x48).
template <std::size_t Size, int Strings, std::size_t ArrayAt, typename Class>
struct StringsAndArray {
    std::uint8_t bytes[Size];
};
template <std::size_t Size, int Strings, std::size_t ArrayAt, typename Class>
void InitStringsAndArray(StringsAndArray<Size, Strings, ArrayAt, Class>* p) {
    for (int i = 0; i < Strings; ++i) reinterpret_cast<char**>(p->bytes)[i] = PchNil();
    ConstructHeader<Class>(p->bytes + ArrayAt);
}

// Elements that hold an array header of `Class` at `ArrayAt` (004534F0: the 20-byte element is the
// array; 0045E180: 32-byte patrols with the array at +0xC).
template <std::size_t Size, std::size_t ArrayAt, typename Class>
struct WithArray {
    std::uint8_t bytes[Size];
};
template <std::size_t Size, std::size_t ArrayAt, typename Class>
void InitWithArray(WithArray<Size, ArrayAt, Class>* p) {
    ConstructHeader<Class>(p->bytes + ArrayAt);
}

// 40-byte placements: a quaternion at +0x10, identity (w first) after the zero fill (004CEE20).
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

// 12-byte path records: 0, then two indices -1 (00539910).
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

// 20-byte effect sound records: 0, two handles -1, 0, a flag byte 0 (00512520).
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
// fill (00558AB0, 00550A60's values): animation and variant 0xFF, flags 0, duration -1, kind 0.
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
// (005124D0).
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

// The destructors of elements that hold an array header at `ArrayAt` (0045E1E0, 004C9AF0): the
// vtable of `BaseClass`, the storage freed, the fields left. With `Strings` CStrings from +0 (004608D0):
// those destroyed afterwards, the last first.
template <typename E, std::size_t ArrayAt, typename BaseClass>
void __fastcall DestroyArrayAt(E* p) {
    auto* header = reinterpret_cast<CArray<Bytes<1>>*>(reinterpret_cast<std::uint8_t*>(p) + ArrayAt);
    header->vtable = BaseClass::Vtable();
    if (header->data) gamecrt::operator_delete(header->data);
}
template <typename E, int Strings, std::size_t ArrayAt, typename BaseClass>
void __fastcall DestroyStringsAndArray(E* p) {
    DestroyArrayAt<E, ArrayAt, BaseClass>(p);
    for (int i = Strings; i-- > 0;) reinterpret_cast<CString*>(reinterpret_cast<std::uint8_t*>(p) + 4 * i)->~CString();
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
using Plain9C = ArrayAbi<Bytes<0x9C>>;
using Floats = ArrayAbi<float>;

using KeyFrames = ArrayAbi<KeyFrame40, InitElements<KeyFrame40, InitKeyFrame40>>;
using NamedDwords = ArrayAbi<WithString<8, 4>, InitElements<WithString<8, 4>, InitWithString<8, 4>>>;
using Exits3C = ArrayAbi<WithString<0x3C, 0x28>, InitElements<WithString<0x3C, 0x28>, InitWithString<0x3C, 0x28>>>;
using Allods = ArrayAbi<StringsAndArray<0x2C, 6, 0x18, IntArrayClass>,
                        InitElements<StringsAndArray<0x2C, 6, 0x18, IntArrayClass>,
                                     InitStringsAndArray<0x2C, 6, 0x18, IntArrayClass>>>;
using MapRecords = ArrayAbi<StringsAndArray<0x5C, 8, 0x48, MapRecordItemsClass>,
                            InitElements<StringsAndArray<0x5C, 8, 0x48, MapRecordItemsClass>,
                                         InitStringsAndArray<0x5C, 8, 0x48, MapRecordItemsClass>>>;
using BoneLinkArrays = ArrayAbi<WithArray<0x14, 0, BoneLinkArrayClass>,
                                InitElements<WithArray<0x14, 0, BoneLinkArrayClass>, InitWithArray<0x14, 0, BoneLinkArrayClass>>>;
using Patrols = ArrayAbi<WithArray<0x20, 0xC, WaypointArrayClass>,
                         InitElements<WithArray<0x20, 0xC, WaypointArrayClass>, InitWithArray<0x20, 0xC, WaypointArrayClass>>>;

using QuestObjectives = ArrayAbi<WithString<12, 4>, InitElements<WithString<12, 4>, InitWithString<12, 4>>>;
using ScriptGroups = ArrayAbi<WithArray<0x14, 0, ScriptGroupClass>,
                              InitElements<WithArray<0x14, 0, ScriptGroupClass>, InitWithArray<0x14, 0, ScriptGroupClass>>>;
using Particles = ArrayAbi<Particle16, InitElements<Particle16, InitParticle16>>;
using Placements = ArrayAbi<Placement40, InitElements<Placement40, InitPlacement40>>;
using PathRecords = ArrayAbi<PathRecord12, InitElements<PathRecord12, InitPathRecord12>>;
using SoundRecords = ArrayAbi<SoundRecord20, InitElements<SoundRecord20, InitSoundRecord20>>;
using AnimationSteps = ArrayAbi<AnimationStep12, InitElements<AnimationStep12, InitAnimationStep12>>;

// A unit's animation list (game/combat.h's AnimationSteps): the steps' array (vtable 0073F01C) and a
// dword copied along with them.
struct AnimationStepList : AnimationSteps::Array {
    int extra;   // +0x14
};
static_assert(sizeof(AnimationStepList) == 0x18);

struct AnimationStepListAbi {
    using Array = AnimationSteps::Array;
    // 0054B260: the copy constructor (RET 4): the steps (SetSize to the source's count, then each
    // step), then extra. Returns this.
    static AnimationStepList* __fastcall CopyConstruct(AnimationStepList* self, void*, const AnimationStepList* source) {
        CopyConstructSteps(self, nullptr, source);
        self->extra = source->extra;
        return self;
    }
    // 00556870: the same without extra (the steps' array alone).
    static Array* __fastcall CopyConstructSteps(Array* self, void*, const Array* source) {
        self->Construct(reinterpret_cast<std::uintptr_t>(AnimationStepsCopyClass::Vtable()));
        self->Copy(*source);
        return self;
    }
    // 005483C0: operator= (RET 4): Copy, then extra. Returns this.
    static AnimationStepList* __fastcall Assign(AnimationStepList* self, void*, const AnimationStepList* source) {
        self->Copy(*source);
        self->extra = source->extra;
        return self;
    }
};

// Elements that own their array: built as above, destroyed by the destructors above.
using PatrolElement = WithArray<0x20, 0xC, WaypointArrayClass>;
using OwnedPatrols = ArrayAbi<PatrolElement, EachElements<PatrolElement, InitWithArray<0x20, 0xC, WaypointArrayClass>,
                                                        DestroyArrayAt<PatrolElement, 0xC, ArrayBaseClass>>>;
using AiLookElement = WithArray<0x20, 0xC, AiLookRecordArrayClass>;
using AiLookRecords = ArrayAbi<AiLookElement, EachElements<AiLookElement, InitWithArray<0x20, 0xC, AiLookRecordArrayClass>,
                                                          DestroyArrayAt<AiLookElement, 0xC, AiLookArrayClass>>>;
using AllodElement = StringsAndArray<0x2C, 6, 0x18, IntArrayClass>;
using OwnedAllods = ArrayAbi<AllodElement, EachElements<AllodElement, InitStringsAndArray<0x2C, 6, 0x18, IntArrayClass>,
                                                       DestroyStringsAndArray<AllodElement, 6, 0x18, IntArrayClass>>>;

// mfc_array.cpp: the element destructors registered on their own (0045E1E0, 004C9AF0, 004608D0).
void __fastcall DestroyPatrol(PatrolElement* p);
void __fastcall DestroyAiLookRecord(AiLookElement* p);
void __fastcall DestroyAllod(AllodElement* p);

} // namespace mfc::impl
