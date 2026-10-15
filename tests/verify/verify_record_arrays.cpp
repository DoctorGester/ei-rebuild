// Checks for the game records' CArray<T> instantiations (src/game/record_arrays.cpp, ticket S6b).
// Element constructors that allocate nothing and SetSize growing such arrays run against the
// original: the constructors on a copy of the bytes in our memory, SetSize ours first, then the
// original on a twin (the same header with its own copy of the storage, freed after; SV2f). The
// by-value GetAts and the bindings' copy constructor run ours first, then the original into a twin
// whose blocks are freed after. The rest (allocating constructors, destructors, the command copies)
// run ours only, with checks of the state they leave.
#include "verify.h"

#include "../../src/game/record_arrays.h"
#include "../../src/lib/mfc_gdi.h"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace recordarrays;
using mfc::impl::ArrayHeader;
using mfc::impl::GlobalMapPointRecords;

namespace {

using ei::verify::Check;
void* s_unused;

// "CArray <what> <address>", kept for the process (Report keeps the pointer).
const char* Name(const char* what, std::uintptr_t address) {
    char* name = new char[48];
    std::snprintf(name, 48, "CArray %s %08X", what, static_cast<unsigned>(address));
    return name;
}

// The header and elements before a call, kept in our memory; the twin is made from it after ours
// ran (its own copy of the storage, the same allocated size), its storage freed after.
template <typename Array>
struct Before {
    using T = std::remove_pointer_t<decltype(Array::data)>;
    Array header;
    std::vector<std::uint8_t> bytes;
    explicit Before(const Array& a) : header(a) {
        if (a.data)
            bytes.assign(reinterpret_cast<const std::uint8_t*>(a.data),
                         reinterpret_cast<const std::uint8_t*>(a.data) + static_cast<std::size_t>(a.size) * sizeof(T));
    }
    Array Twin() const {
        Array twin = header;
        if (header.data) {
            twin.data = static_cast<T*>(gamecrt::operator_new(static_cast<std::size_t>(header.maxSize) * sizeof(T)));
            if (!bytes.empty()) std::memcpy(static_cast<void*>(twin.data), bytes.data(), bytes.size());
        }
        return twin;
    }
};

template <typename Array>
bool Same(const Array& expected, const Array& actual) {
    using T = std::remove_pointer_t<decltype(actual.data)>;
    return expected.size == actual.size && expected.maxSize == actual.maxSize && expected.growBy == actual.growBy &&
           expected.vtable == actual.vtable && (expected.data == nullptr) == (actual.data == nullptr) &&
           (actual.size == 0 ||
            std::memcmp(expected.data, actual.data, static_cast<std::size_t>(actual.size) * sizeof(T)) == 0);
}

// ---- Against the original ------------------------------------------------------------------------

// Elements whose constructor allocates nothing: the original on a copy of the bytes in our memory,
// then ours on the caller's.
template <std::uintptr_t Address, typename Abi>
struct TwinConstructElements {
    using T = typename Abi::Element;
    static inline void(__stdcall* original)(T*, int);
    static void __stdcall Run(T* elements, int count) {
        static const char* name = Name("ConstructElements", Address);
        std::size_t bytes = static_cast<std::size_t>(count) * sizeof(T);
        std::vector<std::uint32_t> buffer((bytes + 3) / 4 + 1);
        auto* twin = reinterpret_cast<T*>(buffer.data());
        std::memcpy(static_cast<void*>(twin), elements, bytes);
        original(twin, count);
        Abi::ConstructElements(elements, count);
        Check(name, true, std::memcmp(twin, elements, bytes) == 0, "%d", count);
    }
};

// SetSize growing (new elements built in place, the old ones moved with memcpy): ours first, then
// the original on a twin. Shrinking destroys elements: ours only.
template <std::uintptr_t Address, typename Abi>
struct TwinSetSizeGrow {
    using Array = typename Abi::Array;
    static inline void(__fastcall* original)(Array*, void*, int, int);
    static void __fastcall Run(Array* self, void*, int newSize, int newGrowBy) {
        static const char* name = Name("SetSize", Address);
        if (newSize < self->size) {
            Abi::SetSize(self, nullptr, newSize, newGrowBy);
            Check(name, true, self->size == newSize && self->maxSize >= newSize && (newSize == 0) == (self->data == nullptr),
                  "%d: %d / %d", newSize, self->size, self->maxSize);
            return;
        }
        Before<Array> before(*self);
        Abi::SetSize(self, nullptr, newSize, newGrowBy);
        Array twin = before.Twin();
        original(&twin, nullptr, newSize, newGrowBy);
        Check(name, true, Same(twin, *self), "%d, %d: %d / %d (original %d / %d)", newSize, newGrowBy, self->size,
              self->maxSize, twin.size, twin.maxSize);
        gamecrt::operator_delete(twin.data);
    }
};

template <std::uintptr_t Address, typename Class>
struct TwinConstruct {
    using Array = mfc::CArray<Bytes<1>>;
    static inline Array*(__fastcall* original)(Array*);
    static Array* __fastcall Run(Array* self) {
        static const char* name = Name("constructor", Address);
        Array twin = *self;
        Array* expected = original(&twin);
        Array* result = ArrayHeader<Class>::Construct(self);
        Check(name, true, std::memcmp(&twin, self, sizeof(Array)) == 0 && expected == &twin && result == self, "");
        return result;
    }
};

// A patrol by value: ours first, then the original into a twin (its waypoint storage freed after).
template <std::uintptr_t Address>
struct TwinPatrolGetAt {
    using Array = mfc::CArray<mapspec::Patrol>;
    static inline mapspec::Patrol*(__fastcall* original)(const Array*, void*, mapspec::Patrol*, int);
    static mapspec::Patrol* __fastcall Run(const Array* self, void*, mapspec::Patrol* result, int index) {
        static const char* name = Name("GetAt", Address);
        mapspec::Patrol* actual = PatrolGetAt(self, nullptr, result, index);
        mapspec::Patrol twin{};
        mapspec::Patrol* expected = original(self, nullptr, &twin, index);
        Check(name, true,
              expected == &twin && actual == result &&
                  std::memcmp(&twin.position, &result->position, sizeof(twin.position)) == 0 &&
                  Same(twin.waypoints, result->waypoints),
              "%d: %d waypoint(s)", index, result->waypoints.size);
        gamecrt::operator_delete(twin.waypoints.data);
        return actual;
    }
};

// A zone exit by value: ours first, then the original into a twin (its target string freed after).
template <std::uintptr_t Address>
struct TwinExitGetAt {
    using Array = mfc::CArray<ZoneExit>;
    static inline ZoneExit*(__fastcall* original)(const Array*, void*, ZoneExit*, int);
    static ZoneExit* __fastcall Run(const Array* self, void*, ZoneExit* result, int index) {
        static const char* name = Name("GetAt", Address);
        ZoneExit* actual = ExitGetAt(self, nullptr, result, index);
        ZoneExit twin{};
        ZoneExit* expected = original(self, nullptr, &twin, index);
        bool same = std::memcmp(&twin, result, offsetof(ZoneExit, target)) == 0 &&
                    std::strcmp(twin.target, result->target) == 0 && twin.entrance == result->entrance &&
                    twin.view == result->view && twin.hasDeployAngle == result->hasDeployAngle &&
                    twin.deployAngle == result->deployAngle;
        Check(name, true, expected == &twin && actual == result && same, "%d: %s", index, result->target);
        reinterpret_cast<mfc::CString*>(&twin.target)->~CString();
        return actual;
    }
};

// The bindings' copy constructor: ours first, then the original on a zeroed twin header (its
// storage freed after).
template <std::uintptr_t Address>
struct TwinBindingsCopyConstruct {
    using Array = Bindings::Array;
    static inline Array*(__fastcall* original)(Array*, void*, const Array*);
    static Array* __fastcall Run(Array* self, void*, const Array* source) {
        static const char* name = Name("copy constructor", Address);
        Array* result = BindingsCopyConstruct(self, nullptr, source);
        Array twin{};
        Array* expected = original(&twin, nullptr, source);
        Check(name, true, Same(twin, *self) && expected == &twin && result == self, "%d", self->size);
        gamecrt::operator_delete(twin.data);
        return result;
    }
};

// ---- Ours only -----------------------------------------------------------------------------------

// SetSize of elements whose constructor allocates.
template <std::uintptr_t Address, typename Abi>
struct OursSetSize {
    using Array = typename Abi::Array;
    static inline void* original;
    static void __fastcall Run(Array* self, void*, int newSize, int newGrowBy) {
        static const char* name = Name("SetSize", Address);
        Abi::SetSize(self, nullptr, newSize, newGrowBy);
        Check(name, true, self->size == newSize && self->maxSize >= newSize && (newSize == 0) == (self->data == nullptr),
              "%d: %d / %d", newSize, self->size, self->maxSize);
    }
};

// ConstructElements that allocate: ours, then `Ok` on each element.
template <std::uintptr_t Address, typename Abi, bool (*Ok)(const typename Abi::Element*)>
struct OursConstructElements {
    using T = typename Abi::Element;
    static inline void* original;
    static void __stdcall Run(T* elements, int count) {
        static const char* name = Name("ConstructElements", Address);
        Abi::ConstructElements(elements, count);
        bool ok = count >= 0;
        for (int i = 0; ok && i < count; ++i) ok = Ok(&elements[i]);
        Check(name, true, ok, "%d", count);
    }
};

bool MaterialOk(const CMaterial* p) { return *reinterpret_cast<void* const*>(p) != nullptr; }
bool WaveOk(const WaveRecord*) { return true; }
bool SubCallOk(const ScriptSubCall* p) { return p->type == 0 && p->argIndex == 0 && p->cmd != nullptr; }

template <std::uintptr_t Address, typename Abi>
struct OursDestructElements {
    using T = typename Abi::Element;
    static inline void* original;
    static void __stdcall Run(T* elements, int count) {
        static const char* name = Name("DestructElements", Address);
        Abi::DestructElements(elements, count);
        Check(name, true, count >= 0, "%d", count);
    }
};

template <std::uintptr_t Address, typename E, void(__fastcall* Fn)(E*)>
struct OursDestroy {
    static inline void* original;
    static void __fastcall Run(E* p) {
        static const char* name = Name("element destructor", Address);
        Fn(p);
        Check(name, true, true, "");
    }
};

// An array destructor: the vtable of `Class` left, the storage freed.
template <std::uintptr_t Address, typename Abi, typename Class>
struct OursArrayDestruct {
    using Array = typename Abi::Array;
    static inline void* original;
    static void __fastcall Run(Array* self) {
        static const char* name = Name("destructor", Address);
        bool ok = self->size <= self->maxSize && (self->size == 0 || self->data);
        Abi::template Destruct<Class>(self);
        Check(name, true, ok && self->vtable == Class::Vtable(), "");
    }
};

// A scalar deleting destructor: run on an object whose vtable has it in slot 1, returns the object
// and, when it keeps the memory, leaves the destructor's vtable.
template <std::uintptr_t Address, auto Fn, typename Class>
struct OursDelete {
    static inline void* original;
    static void* __fastcall Run(void* self, void*, unsigned flags) {
        static const char* name = Name("deleting destructor", Address);
        auto* slots = *static_cast<void* const* const*>(self);
        bool ours = slots && slots[1] == reinterpret_cast<void*>(Address);
        void* result = mfc::impl::ScalarDelete<Fn>(self, nullptr, flags);
        Check(name, true, ours && result == self && ((flags & 1) || *static_cast<void* const*>(self) == Class::Vtable()),
              "%u", flags);
        return result;
    }
};

// An engine object's destructor body (ours only): the engine object's vtable left.
template <std::uintptr_t Address, typename T, void (T::*Fn)()>
struct OursObjectDestruct {
    static inline void* original;
    static void __fastcall Run(T* self) {
        static const char* name = Name("element destructor", Address);
        (self->*Fn)();
        Check(name, true, *reinterpret_cast<void* const*>(self) == reinterpret_cast<void*>(CEngineObject::kVtableAddress), "");
    }
};

// Its slot 0 (ours only): run on an object whose vtable has it in slot 0, returns the object.
template <std::uintptr_t Address, typename T, T* (T::*Fn)(unsigned)>
struct OursObjectDelete {
    static inline void* original;
    static T* __fastcall Run(T* self, void*, unsigned flags) {
        static const char* name = Name("deleting destructor", Address);
        auto* slots = *reinterpret_cast<void* const* const*>(self);
        bool ours = slots && slots[0] == reinterpret_cast<void*>(Address);
        T* result = (self->*Fn)(flags);
        Check(name, true, ours && result == self, "%u", flags);
        return result;
    }
};

// The game's inlined ~CGdiObject (ours only): the CGdiObject vtable, no handle left.
template <std::uintptr_t Address>
struct OursGdiDestruct {
    static inline void* original;
    static void __fastcall Run(mfc::impl::CGdiObject* self) {
        static const char* name = Name("GDI object destructor", Address);
        self->Destruct();
        Check(name, true, self->vtable == mfc::impl::CGdiObject::Vtable() && !self->handle, "");
    }
};

// Its slot 1 (ours only): run on an object whose vtable has it in slot 1, returns the object.
template <std::uintptr_t Address>
struct OursGdiDelete {
    static inline void* original;
    static mfc::impl::CGdiObject* __fastcall Run(mfc::impl::CGdiObject* self, void*, unsigned flags) {
        static const char* name = Name("GDI object deleting destructor", Address);
        auto* slots = static_cast<void* const*>(self->vtable);
        bool ours = slots && slots[1] == reinterpret_cast<void*>(Address);
        mfc::impl::CGdiObject* result = self->DeletingDestruct(flags);
        Check(name, true, ours && result == self, "%u", flags);
        return result;
    }
};

// The command copies: the size of the source, the nested calls' type and argument.
template <std::uintptr_t Address>
struct OursCmdArrayCopy {
    static inline void* original;
    static void __fastcall Run(Cmds::Array* self, void*, const Cmds::Array* source) {
        static const char* name = Name("Copy", Address);
        CmdArrayCopy(self, nullptr, source);
        bool ok = self->size == source->size;
        for (int i = 0; ok && i < self->size; ++i) ok = self->data[i].id == source->data[i].id;
        Check(name, true, ok, "%d", self->size);
    }
};

bool SameSubCalls(const SubCalls::Array* a, const SubCalls::Array* b) {
    if (a->size != b->size) return false;
    for (int i = 0; i < a->size; ++i)
        if (a->data[i].type != b->data[i].type || a->data[i].argIndex != b->data[i].argIndex || !a->data[i].cmd)
            return false;
    return true;
}

template <std::uintptr_t Address>
struct OursSubCallArrayCopy {
    static inline void* original;
    static void __fastcall Run(SubCalls::Array* self, void*, const SubCalls::Array* source) {
        static const char* name = Name("Copy", Address);
        SubCallArrayCopy(self, nullptr, source);
        Check(name, true, SameSubCalls(self, source), "%d", self->size);
    }
};

template <std::uintptr_t Address>
struct OursSubCallsCopyConstruct {
    static inline void* original;
    static SubCalls::Array* __fastcall Run(SubCalls::Array* self, void*, const SubCalls::Array* source) {
        static const char* name = Name("copy constructor", Address);
        SubCalls::Array* result = SubCallsCopyConstruct(self, nullptr, source);
        Check(name, true,
              result == self && self->vtable == SubCallArrayClass::Vtable() && self->maxSize == self->size &&
                  SameSubCalls(self, source),
              "%d", self->size);
        return result;
    }
};

// The by-value GetAt wrappers of the commands and nested calls: what the bodies return.
template <std::uintptr_t Address>
struct OursCmdCopyAt {
    static inline void* original;
    static SSCCmd* __fastcall Run(const mfc::CArray<SSCCmd>* self, void*, SSCCmd* out, int index) {
        static const char* name = Name("GetAt", Address);
        SSCCmd* result = ScriptCmdCopyAt(self, nullptr, out, index);
        Check(name, true, result == out && out->id == self->data[index].id, "%d", index);
        return result;
    }
};

template <std::uintptr_t Address>
struct OursSubCallCopyAt {
    static inline void* original;
    static ScriptSubCall* __fastcall Run(const mfc::CArray<ScriptSubCall>* self, void*, ScriptSubCall* out, int index) {
        static const char* name = Name("GetAt", Address);
        ScriptSubCall* result = ScriptSubCallCopyAt(self, nullptr, out, index);
        const ScriptSubCall& from = self->data[index];
        Check(name, true,
              result == out && out->type == from.type && out->argIndex == from.argIndex &&
                  (out->cmd == nullptr) == (from.cmd == nullptr),
              "%d", index);
        return result;
    }
};

} // namespace

// ---- Registrations (one alias per address: each twin check keeps its own original) ---------------

// Scene records.
using A0044FCD0 = TwinSetSizeGrow<0x0044FCD0, FigureParts>;
EI_VERIFY(0x0044FCD0, A0044FCD0::Run, 5, A0044FCD0::original);
using A00453240 = TwinConstructElements<0x00453240, FigureParts>;
EI_VERIFY(0x00453240, A00453240::Run, 10, A00453240::original);
using O00453550 = OursSetSize<0x00453550, Materials>;
EI_VERIFY(0x00453550, O00453550::Run, 0, s_unused);
using O00454E40 = OursConstructElements<0x00454E40, Materials, MaterialOk>;
EI_VERIFY(0x00454E40, O00454E40::Run, 0, s_unused);
using A004545C0 = TwinConstructElements<0x004545C0, SceneMeshes>;
EI_VERIFY(0x004545C0, A004545C0::Run, 6, A004545C0::original);
using A00454F80 = TwinConstructElements<0x00454F80, SceneNodes>;
EI_VERIFY(0x00454F80, A00454F80::Run, 7, A00454F80::original);
using O00477100 = OursDestructElements<0x00477100, SceneNodes>;
EI_VERIFY(0x00477100, O00477100::Run, 0, s_unused);
using A00474DB0 = TwinSetSizeGrow<0x00474DB0, FaceGroups>;
EI_VERIFY(0x00474DB0, A00474DB0::Run, 5, A00474DB0::original);
using O00476F20 = OursDestructElements<0x00476F20, FaceGroups>;
EI_VERIFY(0x00476F20, O00476F20::Run, 0, s_unused);
using A00454A50 = TwinConstruct<0x00454A50, FaceGroupArrayClass>;
EI_VERIFY(0x00454A50, A00454A50::Run, 10, A00454A50::original);

// Records.
using A0050FA30 = TwinSetSizeGrow<0x0050FA30, DeltaFields>;
EI_VERIFY(0x0050FA30, A0050FA30::Run, 5, A0050FA30::original);
using A005101A0 = TwinConstructElements<0x005101A0, DeltaFields>;
EI_VERIFY(0x005101A0, A005101A0::Run, 5, A005101A0::original);
using A006B59A0 = TwinSetSizeGrow<0x006B59A0, Themes>;
EI_VERIFY(0x006B59A0, A006B59A0::Run, 5, A006B59A0::original);
using A006B5BC0 = TwinConstructElements<0x006B5BC0, Themes>;
EI_VERIFY(0x006B5BC0, A006B5BC0::Run, 10, A006B5BC0::original);
using O006B4D90 = OursDestroy<0x006B4D90, MusicTheme, DestroyTheme>;
EI_VERIFY(0x006B4D90, O006B4D90::Run, 0, s_unused);
using O006B56C0 = OursArrayDestruct<0x006B56C0, Themes, ThemeArrayClass>;
EI_VERIFY(0x006B56C0, O006B56C0::Run, 0, s_unused);
using O006BE240 = OursConstructElements<0x006BE240, Waves, WaveOk>;
EI_VERIFY(0x006BE240, O006BE240::Run, 0, s_unused);
using O006BE210 = OursDestructElements<0x006BE210, Waves>;
EI_VERIFY(0x006BE210, O006BE210::Run, 0, s_unused);
using O006B90C0 = OursDestroy<0x006B90C0, WaveRecord, DestroyWave>;
EI_VERIFY(0x006B90C0, O006B90C0::Run, 0, s_unused);
using O00557890 = OursArrayDestruct<0x00557890, Behaviours, BehaviourArrayClass>;
EI_VERIFY(0x00557890, O00557890::Run, 0, s_unused);
using A0046EAB0 = TwinPatrolGetAt<0x0046EAB0>;
EI_VERIFY(0x0046EAB0, A0046EAB0::Run, 5, A0046EAB0::original);
using A0046EFD0 = TwinPatrolGetAt<0x0046EFD0>;
EI_VERIFY(0x0046EFD0, A0046EFD0::Run, 7, A0046EFD0::original);
using A00584EA0 = TwinExitGetAt<0x00584EA0>;
EI_VERIFY(0x00584EA0, A00584EA0::Run, 5, A00584EA0::original);
using A00587DD0 = TwinExitGetAt<0x00587DD0>;
EI_VERIFY(0x00587DD0, A00587DD0::Run, 5, A00587DD0::original);
using A00631E70 = TwinSetSizeGrow<0x00631E70, GlobalMapPointRecords>;
EI_VERIFY(0x00631E70, A00631E70::Run, 5, A00631E70::original);
using A00631FF0 = TwinSetSizeGrow<0x00631FF0, GlobalMapPointRecords>;
EI_VERIFY(0x00631FF0, A00631FF0::Run, 5, A00631FF0::original);
using A005FBFB0 = TwinConstructElements<0x005FBFB0, GlobalMapPointRecords>;
EI_VERIFY(0x005FBFB0, A005FBFB0::Run, 5, A005FBFB0::original);
using A005FC010 = TwinConstructElements<0x005FC010, GlobalMapPointRecords>;
EI_VERIFY(0x005FC010, A005FC010::Run, 5, A005FC010::original);
using O005FBD80 = OursDestroy<0x005FBD80, Bytes<0x18>, DestroyGlobalMapRecord>;
EI_VERIFY(0x005FBD80, O005FBD80::Run, 0, s_unused);
using O005FBDD0 = OursDestroy<0x005FBDD0, Bytes<0x18>, DestroyGlobalMapRecord>;
EI_VERIFY(0x005FBDD0, O005FBDD0::Run, 0, s_unused);

// The script's command arrays.
using A004CD2B0 = TwinSetSizeGrow<0x004CD2B0, Cmds>;
EI_VERIFY(0x004CD2B0, A004CD2B0::Run, 5, A004CD2B0::original);
using A004CE920 = TwinConstructElements<0x004CE920, Cmds>;
EI_VERIFY(0x004CE920, A004CE920::Run, 5, A004CE920::original);
using O004CD470 = OursDestructElements<0x004CD470, Cmds>;
EI_VERIFY(0x004CD470, O004CD470::Run, 0, s_unused);
using O004C4FD0 = OursSetSize<0x004C4FD0, SubCalls>;
EI_VERIFY(0x004C4FD0, O004C4FD0::Run, 0, s_unused);
using O004CD130 = OursConstructElements<0x004CD130, SubCalls, SubCallOk>;
EI_VERIFY(0x004CD130, O004CD130::Run, 0, s_unused);
using O004CD0F0 = OursDestructElements<0x004CD0F0, SubCalls>;
EI_VERIFY(0x004CD0F0, O004CD0F0::Run, 0, s_unused);
using A004CD770 = TwinSetSizeGrow<0x004CD770, Definitions>;
EI_VERIFY(0x004CD770, A004CD770::Run, 5, A004CD770::original);
using O004CD900 = OursDestructElements<0x004CD900, Definitions>;
EI_VERIFY(0x004CD900, O004CD900::Run, 0, s_unused);
using O004CEC80 = OursDestructElements<0x004CEC80, Runs>;
EI_VERIFY(0x004CEC80, O004CEC80::Run, 0, s_unused);
using O004CD740 = OursDestructElements<0x004CD740, Decls>;
EI_VERIFY(0x004CD740, O004CD740::Run, 0, s_unused);
using O004CC0C0 = OursCmdArrayCopy<0x004CC0C0>;
EI_VERIFY(0x004CC0C0, O004CC0C0::Run, 0, s_unused);
using O004C6D10 = OursSubCallArrayCopy<0x004C6D10>;
EI_VERIFY(0x004C6D10, O004C6D10::Run, 0, s_unused);
using A004CCCB0 = TwinBindingsCopyConstruct<0x004CCCB0>;
EI_VERIFY(0x004CCCB0, A004CCCB0::Run, 7, A004CCCB0::original);
using O004CCD90 = OursSubCallsCopyConstruct<0x004CCD90>;
EI_VERIFY(0x004CCD90, O004CCD90::Run, 0, s_unused);
using O004CD450 = OursCmdCopyAt<0x004CD450>;
EI_VERIFY(0x004CD450, O004CD450::Run, 0, s_unused);
using O004C7510 = OursSubCallCopyAt<0x004C7510>;
EI_VERIFY(0x004C7510, O004C7510::Run, 0, s_unused);

// Destructors only the vtables point at (S6c).
using O004514B0 = OursArrayDestruct<0x004514B0, SceneNodes, SceneNodeArrayClass>;
EI_VERIFY(0x004514B0, O004514B0::Run, 0, s_unused);
using O004C5180 = OursArrayDestruct<0x004C5180, SubCalls, SubCallBaseClass>;
EI_VERIFY(0x004C5180, O004C5180::Run, 0, s_unused);
using O004CCF70 = OursArrayDestruct<0x004CCF70, SubCalls, SubCallBaseClass>;
EI_VERIFY(0x004CCF70, O004CCF70::Run, 0, s_unused);
using O0050FBC0 = OursArrayDestruct<0x0050FBC0, DeltaFields, DeltaFieldArrayClass>;
EI_VERIFY(0x0050FBC0, O0050FBC0::Run, 0, s_unused);
using O00545BA0 = OursArrayDestruct<0x00545BA0, Behaviours, BehaviourArrayClass>;
EI_VERIFY(0x00545BA0, O00545BA0::Run, 0, s_unused);
using O006C4C40 = OursArrayDestruct<0x006C4C40, TerrainAlignedRecords, TerrainAlignedArrayClass>;
EI_VERIFY(0x006C4C40, O006C4C40::Run, 0, s_unused);

using X004529F0 = OursDelete<0x004529F0, &SceneNodes::Destruct<SceneNodeArrayClass>, SceneNodeArrayClass>;
EI_VERIFY(0x004529F0, X004529F0::Run, 0, s_unused);
using X004CB0D0 = OursDelete<0x004CB0D0, &SubCalls::Destruct<SubCallBaseClass>, SubCallBaseClass>;
EI_VERIFY(0x004CB0D0, X004CB0D0::Run, 0, s_unused);
using X004CCF50 = OursDelete<0x004CCF50, &SubCalls::Destruct<SubCallBaseClass>, SubCallBaseClass>;
EI_VERIFY(0x004CCF50, X004CCF50::Run, 0, s_unused);
using X00510160 = OursDelete<0x00510160, &DeltaFields::Destruct<DeltaFieldArrayClass>, DeltaFieldArrayClass>;
EI_VERIFY(0x00510160, X00510160::Run, 0, s_unused);
using X00556BE0 = OursDelete<0x00556BE0, &Behaviours::Destruct<BehaviourArrayClass>, BehaviourArrayClass>;
EI_VERIFY(0x00556BE0, X00556BE0::Run, 0, s_unused);
using X005587E0 = OursDelete<0x005587E0, &Behaviours::Destruct<BehaviourArrayClass>, BehaviourArrayClass>;
EI_VERIFY(0x005587E0, X005587E0::Run, 0, s_unused);
using X006B5980 = OursDelete<0x006B5980, &Themes::Destruct<ThemeArrayClass>, ThemeArrayClass>;
EI_VERIFY(0x006B5980, X006B5980::Run, 0, s_unused);
using X00454CC0 = OursDelete<0x00454CC0, &scene::FaceGroupArrayDestruct, FaceGroupArrayClass>;
EI_VERIFY(0x00454CC0, X00454CC0::Run, 0, s_unused);
using X006C4EC0 = OursDelete<0x006C4EC0, &TerrainAlignedRecords::Destruct<TerrainAlignedArrayClass>, TerrainAlignedArrayClass>;
EI_VERIFY(0x006C4EC0, X006C4EC0::Run, 0, s_unused);

// The scene's meshes and the figure's parts (S6c).
using O00454830 = OursObjectDestruct<0x00454830, scene::SceneMesh, &scene::SceneMesh::Destruct>;
EI_VERIFY(0x00454830, O00454830::Run, 0, s_unused);
using X00454810 = OursObjectDelete<0x00454810, scene::SceneMesh, &scene::SceneMesh::DeletingDestruct>;
EI_VERIFY(0x00454810, X00454810::Run, 0, s_unused);
using O00454510 = OursObjectDestruct<0x00454510, CFigureMesh, &CFigureMesh::DestructPlainMesh>;
EI_VERIFY(0x00454510, O00454510::Run, 0, s_unused);
using X004543A0 = OursObjectDelete<0x004543A0, CFigureMesh, &CFigureMesh::DeletingDestructPlainMesh>;
EI_VERIFY(0x004543A0, X004543A0::Run, 0, s_unused);

// The fonts' inlined ~CGdiObject (S6c).
using G00444B30 = OursGdiDestruct<0x00444B30>;
EI_VERIFY(0x00444B30, G00444B30::Run, 0, s_unused);
using G00444B90 = OursGdiDestruct<0x00444B90>;
EI_VERIFY(0x00444B90, G00444B90::Run, 0, s_unused);
using G005FDDE0 = OursGdiDestruct<0x005FDDE0>;
EI_VERIFY(0x005FDDE0, G005FDDE0::Run, 0, s_unused);
using G00444B10 = OursGdiDelete<0x00444B10>;
EI_VERIFY(0x00444B10, G00444B10::Run, 0, s_unused);
using G00444B70 = OursGdiDelete<0x00444B70>;
EI_VERIFY(0x00444B70, G00444B70::Run, 0, s_unused);
using G005FDDC0 = OursGdiDelete<0x005FDDC0>;
EI_VERIFY(0x005FDDC0, G005FDDC0::Run, 0, s_unused);
