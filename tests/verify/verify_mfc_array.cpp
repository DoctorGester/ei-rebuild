// Checks for the game's CArray<T> instantiations (src/lib/mfc_array.cpp, ticket S6). Where the
// elements are plain data (zero filled, or built by a constructor that only writes the element) the
// original runs on a twin (the same header with its own copy of the storage) and ours on the caller's
// array, and the two are compared: header and element bytes. Constructors compare the header, the
// element helpers the elements they built. Destructors and the instantiations whose elements own
// something (strings, counted references, nested storage) run ours only, with checks of the state
// they leave.
#include "verify.h"

#include "../../src/lib/mfc_array.h"

#include <cstdio>
#include <cstring>

using namespace mfc::impl;

namespace {

using ei::verify::Check;
void* s_unused;

// "CArray <what> <address>", kept for the process (Report keeps the pointer).
const char* Name(const char* what, std::uintptr_t address) {
    char* name = new char[48];
    std::snprintf(name, 48, "CArray %s %08X", what, static_cast<unsigned>(address));
    return name;
}

// A copy of the header with its own storage of the same allocated size.
template <typename Array>
Array Twin(const Array& a) {
    Array twin = a;
    if (a.data) {
        using T = std::remove_pointer_t<decltype(a.data)>;
        twin.data = static_cast<T*>(gamecrt::operator_new(static_cast<std::size_t>(a.maxSize) * sizeof(T)));
        std::memcpy(static_cast<void*>(twin.data), a.data, static_cast<std::size_t>(a.size) * sizeof(T));
    }
    return twin;
}

template <typename Array>
bool Same(const Array& expected, const Array& actual) {
    using T = std::remove_pointer_t<decltype(actual.data)>;
    return expected.size == actual.size && expected.maxSize == actual.maxSize && expected.growBy == actual.growBy &&
           expected.vtable == actual.vtable && (expected.data == nullptr) == (actual.data == nullptr) &&
           (actual.size == 0 ||
            std::memcmp(expected.data, actual.data, static_cast<std::size_t>(actual.size) * sizeof(T)) == 0);
}

// ---- Against the original on a twin --------------------------------------------------------------

template <std::uintptr_t Address, typename Abi>
struct TwinSetSize {
    using Array = typename Abi::Array;
    static inline void(__fastcall* original)(Array*, void*, int, int);
    static void __fastcall Run(Array* self, void*, int newSize, int newGrowBy) {
        static const char* name = Name("SetSize", Address);
        Array twin = Twin(*self);
        original(&twin, nullptr, newSize, newGrowBy);
        Abi::SetSize(self, nullptr, newSize, newGrowBy);
        Check(name, true, Same(twin, *self), "%d, %d: %d / %d (original %d / %d)", newSize, newGrowBy, self->size,
              self->maxSize, twin.size, twin.maxSize);
        gamecrt::operator_delete(twin.data);
    }
};

template <std::uintptr_t Address, typename Abi>
struct TwinSetAtGrow {
    using Array = typename Abi::Array;
    using T = typename Abi::Element;
    static inline void(__fastcall* original)(Array*, void*, int, const T*);
    static void __fastcall Run(Array* self, void*, int index, const T* value) {
        static const char* name = Name("SetAtGrow", Address);
        T copy = *value;   // `value` may be an element the call moves
        Array twin = Twin(*self);
        original(&twin, nullptr, index, &copy);
        Abi::SetAtGrow(self, nullptr, index, value);
        Check(name, true, Same(twin, *self), "%d: %d / %d", index, self->size, self->maxSize);
        gamecrt::operator_delete(twin.data);
    }
};

template <std::uintptr_t Address, typename Abi>
struct TwinSetAtGrowByValue {
    using Array = typename Abi::Array;
    using T = typename Abi::Element;
    static inline void(__fastcall* original)(Array*, void*, int, T);
    static void __fastcall Run(Array* self, void*, int index, T value) {
        static const char* name = Name("SetAtGrow", Address);
        Array twin = Twin(*self);
        original(&twin, nullptr, index, value);
        Abi::SetAtGrowByValue(self, nullptr, index, value);
        Check(name, true, Same(twin, *self), "%d: %d / %d", index, self->size, self->maxSize);
        gamecrt::operator_delete(twin.data);
    }
};

template <std::uintptr_t Address, typename Abi>
struct TwinAdd {
    using Array = typename Abi::Array;
    using T = typename Abi::Element;
    static inline int(__fastcall* original)(Array*, void*, const T*);
    static int __fastcall Run(Array* self, void*, const T* value) {
        static const char* name = Name("Add", Address);
        T copy = *value;
        Array twin = Twin(*self);
        int expected = original(&twin, nullptr, &copy);
        int result = Abi::Add(self, nullptr, value);
        Check(name, true, Same(twin, *self) && expected == result, "%d: %d / %d", result, self->size, self->maxSize);
        gamecrt::operator_delete(twin.data);
        return result;
    }
};

// The elements built from the same bytes.
template <std::uintptr_t Address, typename Abi>
struct TwinConstructElements {
    using T = typename Abi::Element;
    static inline void(__stdcall* original)(T*, int);
    static void __stdcall Run(T* elements, int count) {
        static const char* name = Name("ConstructElements", Address);
        std::size_t bytes = static_cast<std::size_t>(count) * sizeof(T);
        auto* twin = static_cast<T*>(gamecrt::operator_new(bytes ? bytes : 1));
        std::memcpy(static_cast<void*>(twin), elements, bytes);
        original(twin, count);
        Abi::ConstructElements(elements, count);
        Check(name, true, std::memcmp(twin, elements, bytes) == 0, "%d", count);
        gamecrt::operator_delete(twin);
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

// ---- Ours only -----------------------------------------------------------------------------------

template <typename Class>
struct HeaderChecks {
    static void __fastcall Destruct(mfc::CArray<Bytes<1>>* self) {
        static const char* name = Name("destructor", reinterpret_cast<std::uintptr_t>(Class::Vtable()));
        bool ok = self->size <= self->maxSize && (self->size == 0 || self->data);
        ArrayHeader<Class>::Destruct(self);
        Check(name, true, ok && self->vtable == Class::Vtable(), "%d", self->size);
    }
};

} // namespace

// ---- Registrations (one alias per address: each twin check keeps its own original) ---------------

using A00468D60 = TwinConstruct<0x00468D60, SpriteArrayClass>;
EI_VERIFY(0x00468D60, A00468D60::Run, 10, A00468D60::original);
using A00475500 = TwinConstruct<0x00475500, RotKeyArrayClass>;
EI_VERIFY(0x00475500, A00475500::Run, 10, A00475500::original);
using A00475860 = TwinConstruct<0x00475860, PosKeyArrayClass>;
EI_VERIFY(0x00475860, A00475860::Run, 10, A00475860::original);
using A00475BC0 = TwinConstruct<0x00475BC0, MorphHeadArrayClass>;
EI_VERIFY(0x00475BC0, A00475BC0::Run, 10, A00475BC0::original);
using A00476930 = TwinConstruct<0x00476930, NormalArrayClass>;
EI_VERIFY(0x00476930, A00476930::Run, 10, A00476930::original);
using A004BB000 = TwinConstruct<0x004BB000, PartySlotArrayClass>;
EI_VERIFY(0x004BB000, A004BB000::Run, 10, A004BB000::original);
using A004C2030 = TwinConstruct<0x004C2030, ScriptGroupClass>;
EI_VERIFY(0x004C2030, A004C2030::Run, 10, A004C2030::original);
using A004CD080 = TwinConstruct<0x004CD080, FloatArrayClass>;
EI_VERIFY(0x004CD080, A004CD080::Run, 10, A004CD080::original);
using A004D5990 = TwinConstruct<0x004D5990, ArgFloatArrayClass>;
EI_VERIFY(0x004D5990, A004D5990::Run, 7, A004D5990::original);
using A00556810 = TwinConstruct<0x00556810, MotionWaypointArrayClass>;
EI_VERIFY(0x00556810, A00556810::Run, 7, A00556810::original);
using A00556850 = TwinConstruct<0x00556850, AnimationStepsCopyClass>;
EI_VERIFY(0x00556850, A00556850::Run, 7, A00556850::original);
using A00556D70 = TwinConstruct<0x00556D70, MotionPointArrayClass>;
EI_VERIFY(0x00556D70, A00556D70::Run, 10, A00556D70::original);
using A00557230 = TwinConstruct<0x00557230, PointArrayClass>;
EI_VERIFY(0x00557230, A00557230::Run, 10, A00557230::original);
using A00557B60 = TwinConstruct<0x00557B60, AnimationStepsClass>;
EI_VERIFY(0x00557B60, A00557B60::Run, 10, A00557B60::original);
using A0056FFE0 = TwinConstruct<0x0056FFE0, ExitArrayClass>;
EI_VERIFY(0x0056FFE0, A0056FFE0::Run, 10, A0056FFE0::original);
using A005711C0 = TwinConstruct<0x005711C0, ClientMapArrayClass>;
EI_VERIFY(0x005711C0, A005711C0::Run, 10, A005711C0::original);
using A005985C0 = TwinConstruct<0x005985C0, ObjectPtrArrayClass>;
EI_VERIFY(0x005985C0, A005985C0::Run, 7, A005985C0::original);
using A005BD1C0 = TwinConstruct<0x005BD1C0, SectorArrayClass>;
EI_VERIFY(0x005BD1C0, A005BD1C0::Run, 10, A005BD1C0::original);
using A005BD1E0 = TwinConstruct<0x005BD1E0, CellRowArrayClass>;
EI_VERIFY(0x005BD1E0, A005BD1E0::Run, 10, A005BD1E0::original);
using A005C1760 = TwinConstruct<0x005C1760, IntArrayClass>;
EI_VERIFY(0x005C1760, A005C1760::Run, 10, A005C1760::original);
using A005FAF80 = TwinConstruct<0x005FAF80, UnitPtrArrayClass>;
EI_VERIFY(0x005FAF80, A005FAF80::Run, 10, A005FAF80::original);

EI_VERIFY(0x004600D0, HeaderChecks<IntArrayClass>::Destruct, 0, s_unused);
EI_VERIFY(0x00474F30, HeaderChecks<FilePointArrayClass>::Destruct, 0, s_unused);
EI_VERIFY(0x00475110, HeaderChecks<FileUvArrayClass>::Destruct, 0, s_unused);
EI_VERIFY(0x004752F0, HeaderChecks<FileFaceArrayClass>::Destruct, 0, s_unused);
EI_VERIFY(0x00475680, HeaderChecks<RotKeyArrayClass>::Destruct, 0, s_unused);
EI_VERIFY(0x004759E0, HeaderChecks<PosKeyArrayClass>::Destruct, 0, s_unused);
EI_VERIFY(0x00475D40, HeaderChecks<MorphHeadArrayClass>::Destruct, 0, s_unused);
EI_VERIFY(0x00476780, HeaderChecks<IndexArrayClass>::Destruct, 0, s_unused);
EI_VERIFY(0x00476AB0, HeaderChecks<NormalArrayClass>::Destruct, 0, s_unused);
EI_VERIFY(0x004BB020, HeaderChecks<PartySlotArrayClass>::Destruct, 0, s_unused);
EI_VERIFY(0x004BCF10, HeaderChecks<ScriptAreaClass>::Destruct, 0, s_unused);
EI_VERIFY(0x004C29C0, HeaderChecks<ScriptFloatsClass>::Destruct, 0, s_unused);
EI_VERIFY(0x004C5AF0, HeaderChecks<ScriptIfThenArrayClass>::Destruct, 0, s_unused);
EI_VERIFY(0x004CB050, HeaderChecks<FloatArrayClass>::Destruct, 0, s_unused);
EI_VERIFY(0x004CB770, HeaderChecks<AiLookArrayClass>::Destruct, 0, s_unused);
EI_VERIFY(0x00557250, HeaderChecks<PointArrayClass>::Destruct, 0, s_unused);
EI_VERIFY(0x00557410, HeaderChecks<AttackOptionArrayClass>::Destruct, 0, s_unused);
EI_VERIFY(0x00557E70, HeaderChecks<AnimationStepsClass>::Destruct, 0, s_unused);
EI_VERIFY(0x00599180, HeaderChecks<CellRowArrayClass>::Destruct, 0, s_unused);
EI_VERIFY(0x005997A0, HeaderChecks<AreaListClass>::Destruct, 0, s_unused);

using A00453790 = TwinSetSize<0x00453790, Plain4>;
EI_VERIFY(0x00453790, A00453790::Run, 5, A00453790::original);
using A004C2880 = TwinSetSize<0x004C2880, Plain4>;
EI_VERIFY(0x004C2880, A004C2880::Run, 5, A004C2880::original);
using A004C4410 = TwinSetSize<0x004C4410, Plain4>;
EI_VERIFY(0x004C4410, A004C4410::Run, 5, A004C4410::original);
using A004C46F0 = TwinSetSize<0x004C46F0, Plain4>;
EI_VERIFY(0x004C46F0, A004C46F0::Run, 5, A004C46F0::original);
using A004C49F0 = TwinSetSize<0x004C49F0, Plain4>;
EI_VERIFY(0x004C49F0, A004C49F0::Run, 5, A004C49F0::original);
using A005719B0 = TwinSetSize<0x005719B0, Plain4>;
EI_VERIFY(0x005719B0, A005719B0::Run, 5, A005719B0::original);
using A0059C660 = TwinSetSize<0x0059C660, Plain4>;
EI_VERIFY(0x0059C660, A0059C660::Run, 5, A0059C660::original);
using A005EBDB0 = TwinSetSize<0x005EBDB0, Plain4>;
EI_VERIFY(0x005EBDB0, A005EBDB0::Run, 5, A005EBDB0::original);
using A0062A390 = TwinSetSize<0x0062A390, Plain4>;
EI_VERIFY(0x0062A390, A0062A390::Run, 5, A0062A390::original);
using A0065E010 = TwinSetSize<0x0065E010, Plain4>;
EI_VERIFY(0x0065E010, A0065E010::Run, 5, A0065E010::original);
using A00453A30 = TwinSetSize<0x00453A30, Plain8>;
EI_VERIFY(0x00453A30, A00453A30::Run, 5, A00453A30::original);
using A00475BE0 = TwinSetSize<0x00475BE0, Plain8>;
EI_VERIFY(0x00475BE0, A00475BE0::Run, 5, A00475BE0::original);
using A0064B060 = TwinSetSize<0x0064B060, Plain12>;
EI_VERIFY(0x0064B060, A0064B060::Run, 5, A0064B060::original);
using A00453CA0 = TwinSetSize<0x00453CA0, Plain16>;
EI_VERIFY(0x00453CA0, A00453CA0::Run, 5, A00453CA0::original);
using A00475520 = TwinSetSize<0x00475520, Plain20>;
EI_VERIFY(0x00475520, A00475520::Run, 5, A00475520::original);
using A00476950 = TwinSetSize<0x00476950, Plain20>;
EI_VERIFY(0x00476950, A00476950::Run, 5, A00476950::original);
using A004CC640 = TwinSetSize<0x004CC640, Plain20>;
EI_VERIFY(0x004CC640, A004CC640::Run, 5, A004CC640::original);
using A00475880 = TwinSetSize<0x00475880, Plain24>;
EI_VERIFY(0x00475880, A00475880::Run, 5, A00475880::original);

using A00460500 = TwinSetAtGrowByValue<0x00460500, Plain4>;
EI_VERIFY(0x00460500, A00460500::Run, 6, A00460500::original);
using A0059D2E0 = TwinSetAtGrowByValue<0x0059D2E0, Plain4>;
EI_VERIFY(0x0059D2E0, A0059D2E0::Run, 6, A0059D2E0::original);
using A0059D450 = TwinSetAtGrowByValue<0x0059D450, Plain4>;
EI_VERIFY(0x0059D450, A0059D450::Run, 6, A0059D450::original);
using A0060AD50 = TwinSetAtGrowByValue<0x0060AD50, Plain4>;
EI_VERIFY(0x0060AD50, A0060AD50::Run, 6, A0060AD50::original);
using A004773F0 = TwinSetAtGrow<0x004773F0, Plain4>;
EI_VERIFY(0x004773F0, A004773F0::Run, 6, A004773F0::original);
using A00453E00 = TwinSetAtGrow<0x00453E00, Plain16>;
EI_VERIFY(0x00453E00, A00453E00::Run, 6, A00453E00::original);
using A00476640 = TwinAdd<0x00476640, Plain4>;
EI_VERIFY(0x00476640, A00476640::Run, 6, A00476640::original);

using A004CC870 = TwinConstructElements<0x004CC870, Plain4>;
EI_VERIFY(0x004CC870, A004CC870::Run, 5, A004CC870::original);
using A004CCA10 = TwinConstructElements<0x004CCA10, Plain4>;
EI_VERIFY(0x004CCA10, A004CCA10::Run, 5, A004CCA10::original);
using A0060AEC0 = TwinConstructElements<0x0060AEC0, Plain4>;
EI_VERIFY(0x0060AEC0, A0060AEC0::Run, 5, A0060AEC0::original);
using A00468F90 = TwinConstructElements<0x00468F90, Plain8>;
EI_VERIFY(0x00468F90, A00468F90::Run, 5, A00468F90::original);
using A00452DC0 = TwinConstructElements<0x00452DC0, Plain12>;
EI_VERIFY(0x00452DC0, A00452DC0::Run, 5, A00452DC0::original);
using A005B1C70 = TwinConstructElements<0x005B1C70, Plain12>;
EI_VERIFY(0x005B1C70, A005B1C70::Run, 5, A005B1C70::original);
using A004CDE10 = TwinConstructElements<0x004CDE10, Plain16>;
EI_VERIFY(0x004CDE10, A004CDE10::Run, 5, A004CDE10::original);
using A00454F30 = TwinConstructElements<0x00454F30, Plain20>;
EI_VERIFY(0x00454F30, A00454F30::Run, 5, A00454F30::original);
using A0045E130 = TwinConstructElements<0x0045E130, Plain20>;
EI_VERIFY(0x0045E130, A0045E130::Run, 5, A0045E130::original);
using A00452E20 = TwinConstructElements<0x00452E20, Plain24>;
EI_VERIFY(0x00452E20, A00452E20::Run, 5, A00452E20::original);
using A00453730 = TwinConstructElements<0x00453730, KeyFrames>;
EI_VERIFY(0x00453730, A00453730::Run, 5, A00453730::original);
using A004553F0 = TwinConstructElements<0x004553F0, NamedDwords>;
EI_VERIFY(0x004553F0, A004553F0::Run, 5, A004553F0::original);
using A004606A0 = TwinConstructElements<0x004606A0, Exits3C>;
EI_VERIFY(0x004606A0, A004606A0::Run, 5, A004606A0::original);
using A00460720 = TwinConstructElements<0x00460720, Allods>;
EI_VERIFY(0x00460720, A00460720::Run, 6, A00460720::original);
using A004607E0 = TwinConstructElements<0x004607E0, MapRecords>;
EI_VERIFY(0x004607E0, A004607E0::Run, 6, A004607E0::original);
using A004534F0 = TwinConstructElements<0x004534F0, BoneLinkArrays>;
EI_VERIFY(0x004534F0, A004534F0::Run, 6, A004534F0::original);
using A0045E180 = TwinConstructElements<0x0045E180, Patrols>;
EI_VERIFY(0x0045E180, A0045E180::Run, 5, A0045E180::original);
