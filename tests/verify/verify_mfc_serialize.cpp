// Checks for the CArray / CList / CMap Serialize functions the exe's vtables point at
// (src/lib/mfc_slots.cpp, src/game/game_slots.cpp, ticket S6a). In the game: ours only, counted. At
// start-up each runs next to the original (through its stolen bytes) on scratch CArchives over memory:
// storing one object gives the same bytes; loading those bytes into objects that are empty or already
// hold elements gives the same objects and reads as far.
#include "verify.h"

#include "../../src/game/game_slots.h"
#include "../../src/game/record_arrays.h"
#include "../../src/lib/mfc_slots.h"
#include "../../src/lib/stlp_string.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

using namespace mfc::impl;

namespace {

using ei::verify::Check;
using ei::verify::Report;

using SerializeFn = void(__fastcall*)(void*, void*, CArchive*);

// "MFC Serialize <address>" / "... start-up", kept for the process (Report keeps the pointer).
const char* Name(const char* what, std::uintptr_t address) {
    char* name = new char[48];
    std::snprintf(name, 48, "MFC Serialize %08X%s", static_cast<unsigned>(address), what);
    return name;
}

// A CArchive over memory, as CArchive's constructor sets one up without a file. Storing fills a
// buffer large enough never to flush; loading reads the given bytes, then zeros (so a port that reads
// too far shows as reading further, not as a FillBuffer without a file).
struct MemoryArchive {
    CArchive ar{};
    std::vector<unsigned char> buffer;
    MemoryArchive() : buffer(1 << 16) { Open(CArchive::store); }
    explicit MemoryArchive(const std::vector<unsigned char>& bytes) : buffer(bytes) {
        buffer.resize(bytes.size() + 64);
        Open(CArchive::load);
    }
    MemoryArchive(const MemoryArchive&) = delete;
    MemoryArchive& operator=(const MemoryArchive&) = delete;
    void Open(unsigned mode) {
        ar.mode = mode;
        ar.bufferSize = static_cast<unsigned>(buffer.size());
        ar.bufferStart = buffer.data();
        ar.bufferCur = ar.bufferStart;
        ar.bufferMax = ar.bufferStart + buffer.size();
    }
    std::vector<unsigned char> Stored() const { return {ar.bufferStart, ar.bufferCur}; }
    std::size_t Used() const { return static_cast<std::size_t>(ar.bufferCur - ar.bufferStart); }
};

// A vtable named by its address (the checks never look into it).
template <std::uintptr_t Address>
struct VtableAt {
    static void* Vtable() { return reinterpret_cast<void*>(Address); }
};

void Pattern(void* at, std::size_t size, int seed) {
    auto* p = static_cast<unsigned char*>(at);
    for (std::size_t i = 0; i < size; ++i) p[i] = static_cast<unsigned char>(i * 37 + seed * 101 + 1);
}

// Runs the original and ours on the same kind of object: `make(object, which)` builds it, `same`
// compares two, `release` frees one. Stores `source` with both, then loads the original's bytes into
// objects made by `start` (each kind of starting object) with both.
template <typename Object, typename Make, typename Same, typename Release>
bool Compare(SerializeFn original, SerializeFn ours, Make make, Same same, Release release, int starts,
             std::vector<int> sources) {
    bool ok = true;
    for (int source : sources) {
        Object src;
        make(src, source);
        MemoryArchive a, b;
        original(&src, nullptr, &a.ar);
        ours(&src, nullptr, &b.ar);
        ok = ok && a.Stored() == b.Stored();
        for (int start = 0; start < starts; ++start) {
            Object x, y;
            make(x, -1 - start);
            make(y, -1 - start);
            MemoryArchive lx(a.Stored()), ly(a.Stored());
            original(&x, nullptr, &lx.ar);
            ours(&y, nullptr, &ly.ar);
            ok = ok && lx.Used() == ly.Used() && same(x, y);
            release(x);
            release(y);
        }
        release(src);
    }
    return ok;
}

// ---- Arrays ----------------------------------------------------------------------------------------

// Sources of 5, 0 and 37 elements; loaded into an empty array, one of 2 elements (grows) and one of 8
// (shrinks). Mode 0 (elements that need nothing): the element bytes a pattern. Mode 1 (elements built
// and destroyed by the instantiation's code): as SetSize builds them, so every load reads copies of
// built elements. Mode 2: as 1, with no load that destroys elements (their destruction needs more
// than SetSize builds): no shrinking, no 0 elements into a filled array.
template <std::uintptr_t Address, typename Abi, typename Class, int Mode = 0>
struct ArraySerialize {
    using Array = typename Abi::Array;
    using T = typename Abi::Element;
    static inline void* original = nullptr;

    static void __fastcall Run(Array* self, void*, CArchive* ar) {
        static const char* name = Name("", Address);
        SerializeCArray<Abi>(self, nullptr, ar);
        Report(name, true, "");
    }

    static void Make(Array& a, int which) {
        static const int kSizes[] = {0, 2, 8};
        a.Construct(reinterpret_cast<std::uintptr_t>(Class::Vtable()));
        int n = which >= 0 ? which : kSizes[-1 - which];
        a.SetSize(n, -1);
        if constexpr (Mode == 0) Pattern(a.data, static_cast<std::size_t>(n) * sizeof(T), which);
    }
    static bool Same(const Array& x, const Array& y) {
        return x.size == y.size && x.maxSize == y.maxSize && x.growBy == y.growBy && (x.data == nullptr) == (y.data == nullptr) &&
               std::memcmp(x.data, y.data, static_cast<std::size_t>(x.size) * sizeof(T)) == 0;
    }
    static void Release(Array& a) { gamecrt::operator_delete(a.data); }

    static void Startup() {
        bool ok = Compare<Array>(reinterpret_cast<SerializeFn>(original), reinterpret_cast<SerializeFn>(&SerializeCArray<Abi>), Make,
                                 Same, Release, Mode == 2 ? 2 : 3, Mode == 2 ? std::vector<int>{5, 37} : std::vector<int>{5, 0, 37});
        Check(Name(" start-up", Address), true, ok, "");
    }
};

// ---- Lists -----------------------------------------------------------------------------------------

// Sources of 5 and 0 elements (blocks of 3, so the nodes span blocks); loaded into an empty list and
// one of 2 elements (added after them).
template <std::uintptr_t Address, typename Abi, typename Class>
struct ListSerialize {
    using List = typename Abi::List;
    using T = typename Abi::Element;
    static inline void* original = nullptr;

    static void __fastcall Run(List* self, void*, CArchive* ar) {
        static const char* name = Name("", Address);
        SerializeCList<Abi>(self, nullptr, ar);
        Report(name, true, "");
    }

    static void Make(List& l, int which) {
        ListHeader<Class>::Construct(reinterpret_cast<mfc::CList<void*>*>(&l), nullptr, 3);
        int n = which >= 0 ? which : (which == -1 ? 0 : 2);
        for (int i = 0; i < n; ++i) {
            T value;
            Pattern(&value, sizeof(T), which * 16 + i);
            l.AddTail(value);
        }
    }
    static bool Same(const List& x, const List& y) {
        if (x.count != y.count || x.blockSize != y.blockSize) return false;
        auto* a = x.head;
        auto* b = y.head;
        for (; a && b; a = a->next, b = b->next)
            if (std::memcmp(&a->data, &b->data, sizeof(T)) != 0 || (a->prev == nullptr) != (b->prev == nullptr)) return false;
        return !a && !b && (x.tail == nullptr) == (y.tail == nullptr);
    }
    static void Release(List& l) { l.RemoveAll(); }

    static void Startup() {
        bool ok = Compare<List>(reinterpret_cast<SerializeFn>(original), reinterpret_cast<SerializeFn>(&SerializeCList<Abi>), Make,
                                Same, Release, 2, {5, 0});
        Check(Name(" start-up", Address), true, ok, "");
    }
};

// ---- Maps ------------------------------------------------------------------------------------------

// Sources of 7 and 0 associations; loaded into an empty map and one of 2 associations (one key in
// common with the source, so a load replaces a value).
template <std::uintptr_t Address, typename Abi, typename Class>
struct MapSerialize {
    using Map = typename Abi::Map;
    using Assoc = typename Abi::Assoc;
    using K = decltype(Assoc::key);
    using V = decltype(Assoc::value);
    static inline void* original = nullptr;

    static void __fastcall Run(Map* self, void*, CArchive* ar) {
        static const char* name = Name("", Address);
        SerializeCMap<Abi>(self, nullptr, ar);
        Report(name, true, "");
    }

    static void Make(Map& m, int which) {
        m.vtable = Class::Vtable();
        m.table = nullptr;
        m.hashSize = 17;
        m.count = 0;
        m.freeList = nullptr;
        m.blocks = nullptr;
        m.blockSize = 4;
        int n = which >= 0 ? which : (which == -1 ? 0 : 2);
        for (int i = 0; i < n; ++i) {
            K key{};
            Pattern(&key, sizeof(K), which * 16 + i);
            if (i == 0) key = K{};   // every map's first key the same
            V value;
            Pattern(&value, sizeof(V), which * 16 + i + 7);
            *Abi::At(&m, nullptr, &key) = value;
        }
    }
    static bool Same(const Map& x, const Map& y) {
        if (x.count != y.count || x.hashSize != y.hashSize || (x.table == nullptr) != (y.table == nullptr)) return false;
        if (!x.table) return true;
        for (std::uint32_t b = 0; b < x.hashSize; ++b) {
            const Assoc* a = x.table[b];
            const Assoc* c = y.table[b];
            for (; a && c; a = a->next, c = c->next)
                if (a->hash != c->hash || std::memcmp(&a->key, &c->key, sizeof(K)) != 0 ||
                    std::memcmp(&a->value, &c->value, sizeof(V)) != 0)
                    return false;
            if (a || c) return false;
        }
        return true;
    }
    static void Release(Map& m) { m.RemoveAll(); }

    static void Startup() {
        bool ok = Compare<Map>(reinterpret_cast<SerializeFn>(original), reinterpret_cast<SerializeFn>(&SerializeCMap<Abi>), Make,
                               Same, Release, 2, {7, 0});
        Check(Name(" start-up", Address), true, ok, "");
    }
};

using S00405190 = ListSerialize<0x00405190, PtrList, TempListClass>;
using S00427890 = ListSerialize<0x00427890, PtrList, ItemListClass>;
using S0044F650 = ArraySerialize<0x0044F650, Plain12, Vector3ArrayClass>;
using S00450570 = ArraySerialize<0x00450570, OwnedBoneLinkArrays, FakeUnitBoneArraysClass, 1>;
using S00450A00 = ArraySerialize<0x00450A00, Plain20, NodeItemsAClass>;
using S00450BB0 = ArraySerialize<0x00450BB0, Plain24, NodeItemsBClass>;
using S00450D40 = ArraySerialize<0x00450D40, AlignedAt4Records, AlignedAt4ArrayClass, 1>;
using S00451060 = ArraySerialize<0x00451060, KeyFrames, VtableAt<0x0073C12C>>;
using S004511F0 = ArraySerialize<0x004511F0, Plain4, NodeItemsCClass>;
using S00451850 = ArraySerialize<0x00451850, Plain8, TextureLinkArrayClass>;
using S00451A70 = ArraySerialize<0x00451A70, NamedStrings, TextureNameArrayClass, 1>;
using S00451CC0 = MapSerialize<0x00451CC0, Map32, FigureRegistryMapClass>;
using S00451EE0 = ArraySerialize<0x00451EE0, Plain16, MeshCornerArrayClass>;
using S00452110 = MapSerialize<0x00452110, Map64x64, CornerMapAClass>;
using S004523B0 = MapSerialize<0x004523B0, Map64x32, CornerMapBClass>;
using S00452650 = MapSerialize<0x00452650, Map32, CornerMapCClass>;
using S0045D950 = ArraySerialize<0x0045D950, Plain20, ArrayBaseClass>;
using S0045DC60 = ArraySerialize<0x0045DC60, OwnedPatrols, PatrolArrayClass, 1>;
using S0045FA40 = ArraySerialize<0x0045FA40, Plain4, IntArrayClass>;
using S0045FCB0 = ArraySerialize<0x0045FCB0, OwnedAllods, AllodArrayClass, 1>;
using S0045FEB0 = ArraySerialize<0x0045FEB0, gameslots::BuiltZoneRecords, VtableAt<0x0073C3E8>, 1>;
using S00460270 = ArraySerialize<0x00460270, gamearrays::Exits, VtableAt<0x0073C3F4>, 1>;
using S00468DA0 = ArraySerialize<0x00468DA0, Plain8, SpriteArrayClass>;
using S0046EC10 = ArraySerialize<0x0046EC10, Plain12, TrapCircleArrayClass>;
using S0046EDA0 = ArraySerialize<0x0046EDA0, Plain8, TrapPointArrayClass>;
using S00474F50 = ArraySerialize<0x00474F50, Plain12, FilePointArrayClass>;
using S00475130 = ArraySerialize<0x00475130, Plain12, FileUvArrayClass>;
using S00475310 = ArraySerialize<0x00475310, Plain28, FileFaceArrayClass>;
using S004756A0 = ArraySerialize<0x004756A0, Plain20, RotKeyArrayClass>;
using S00475A00 = ArraySerialize<0x00475A00, Plain24, PosKeyArrayClass>;
using S00475D60 = ArraySerialize<0x00475D60, Plain8, MorphHeadArrayClass>;
using S004761C0 = MapSerialize<0x004761C0, Map32, DwordMapClass>;
using S00476430 = ArraySerialize<0x00476430, OwnedDwordMaps, SceneObjectArrayClass, 1>;
using S004767A0 = ArraySerialize<0x004767A0, Plain4, IndexArrayClass>;
using S00476AD0 = ArraySerialize<0x00476AD0, Plain20, NormalArrayClass>;
using S00498D00 = ListSerialize<0x00498D00, PtrList, ClientListClass>;
using S00498EA0 = ArraySerialize<0x00498EA0, Plain8, Array0498Class>;
using S004C1E70 = ArraySerialize<0x004C1E70, Plain20, ScriptAreaClass>;
using S004C2310 = ArraySerialize<0x004C2310, gamearrays::ObjectRefs, VtableAt<0x0073D110>, 1>;
using S004C29E0 = ArraySerialize<0x004C29E0, Plain4, ScriptFloatsClass>;
using S004C3BE0 = ArraySerialize<0x004C3BE0, gamearrays::Groups, VtableAt<0x0073D304>, 1>;
using S004C4560 = ArraySerialize<0x004C4560, Plain4, FloatArrayClass>;
using S004C4860 = ArraySerialize<0x004C4860, Plain4, ArgObjectsBaseClass>;
using S004C4B50 = ArraySerialize<0x004C4B50, Plain4, ArgGroupsBaseClass>;
using S004C4E40 = ArraySerialize<0x004C4E40, Plain12, BindingsBaseClass>;
using S004C55C0 = ArraySerialize<0x004C55C0, gameslots::BuiltCommands, VtableAt<0x0073D3D0>, 1>;
using S004C5B10 = ArraySerialize<0x004C5B10, Plain8, ScriptIfThenArrayClass>;
using S004C5E60 = ArraySerialize<0x004C5E60, gamearrays::PointLights, VtableAt<0x0073D394>, 1>;
using S004C88E0 = ArraySerialize<0x004C88E0, ScriptAreas, ScriptAreaArrayClass, 1>;
using S004C9510 = ArraySerialize<0x004C9510, Placements, PartySlotArrayClass>;
using S004C9750 = ListSerialize<0x004C9750, PtrList, PtrListClass>;
using S004C9B10 = ArraySerialize<0x004C9B10, Plain16, AiLookArrayClass>;
using S004C9E20 = ArraySerialize<0x004C9E20, gamearrays::Objectives, VtableAt<0x0073D46C>, 1>;
using S0050A210 = ArraySerialize<0x0050A210, AlignedAt8Records, AlignedAt8ArrayClass, 1>;
using S0050DC20 = ListSerialize<0x0050DC20, PtrList, BoneListClass>;
using S0050DE90 = ListSerialize<0x0050DE90, PtrList, AnimationStateListClass>;
using S0050FFD0 = ArraySerialize<0x0050FFD0, Plain20, EffectArrayClass>;
using S00511C30 = ArraySerialize<0x00511C30, gamearrays::Particles, VtableAt<0x0073DC48>, 1>;
using S00511E20 = ArraySerialize<0x00511E20, SoundRecords, EffectSoundArrayClass>;
using S005198D0 = ListSerialize<0x005198D0, PtrList, Object3DListClass>;
using S00519B50 = ArraySerialize<0x00519B50, Plain56, FrameArrayClass>;
using S00532E70 = ArraySerialize<0x00532E70, Plain28, MotionPointArrayClass>;
using S00533030 = ArraySerialize<0x00533030, Plain12, MotionWaypointBaseClass>;
using S005331E0 = ArraySerialize<0x005331E0, Plain4, CarriedArrayClass>;
using S00533390 = ArraySerialize<0x00533390, Plain4, IdArrayClass>;
using S00539440 = ListSerialize<0x00539440, List8, CellListClass>;
using S005396E0 = ArraySerialize<0x005396E0, PathRecords, SkinLayerArrayClass>;
using S00556E10 = ArraySerialize<0x00556E10, gameslots::ObjectValues, VtableAt<0x0073F058>, 1>;
using S00557050 = ArraySerialize<0x00557050, MemoryCallRecords, MemoryRecordArrayClass, 1>;
using S00557270 = ArraySerialize<0x00557270, Plain8, PointArrayClass>;
using S00557430 = ArraySerialize<0x00557430, Plain40, AttackOptionArrayClass>;
using S00557E90 = ArraySerialize<0x00557E90, AnimationSteps, AnimationStepsClass>;
using S005584D0 = ListSerialize<0x005584D0, PtrList, LootListClass>;
using S00560200 = ArraySerialize<0x00560200, Plain16, CountdownArrayClass>;
using S0056FEA0 = ListSerialize<0x0056FEA0, List4C, PlatformListClass>;
using S005701F0 = ArraySerialize<0x005701F0, gamearrays::ObjectPtrs, VtableAt<0x0073F53C>, 1>;
using S00570570 = ListSerialize<0x00570570, PtrList, ClientObjectListClass>;
using S00570F30 = ListSerialize<0x00570F30, PtrList, ClientList2Class>;
using S00571660 = ListSerialize<0x00571660, PtrList, ClientList3Class>;
using S005991A0 = ArraySerialize<0x005991A0, Plain4, CellRowArrayClass>;
using S00599640 = ListSerialize<0x00599640, List1C, NodeListClass>;
using S005997C0 = ArraySerialize<0x005997C0, Plain8, AreaListClass>;
using S0059A170 = ArraySerialize<0x0059A170, gamearrays::ObjectPtrs, VtableAt<0x0073FABC>, 1>;
using S0059B0D0 = ArraySerialize<0x0059B0D0, Plain4, PartyUnitArrayClass>;
using S0059B2C0 = ListSerialize<0x0059B2C0, List8, ExperienceListClass>;
using S0059E0C0 = ArraySerialize<0x0059E0C0, Plain8, SectorOccluderArrayClass>;
using S005B1870 = ListSerialize<0x005B1870, List12, BlockCostListClass>;
using S005B1A00 = ListSerialize<0x005B1A00, List8, AiMapListClass>;
using S005BDF40 = ListSerialize<0x005BDF40, List12, WaypointListClass>;
using S005C1900 = ArraySerialize<0x005C1900, Plain4, SectorCellArrayClass>;
using S005C1AB0 = ArraySerialize<0x005C1AB0, Plain8, SpiralCellArrayClass>;
using S005C4D80 = ArraySerialize<0x005C4D80, Plain8, SmoothRowArrayClass>;
using S005C5070 = ArraySerialize<0x005C5070, Plain20, SmoothEdgeArrayClass>;
using S005C5360 = ArraySerialize<0x005C5360, Plain8, SmoothCopyArrayClass>;
using S005C5670 = ArraySerialize<0x005C5670, Plain36, SmoothStepArrayClass>;
using S005CDC00 = ListSerialize<0x005CDC00, List30, StaticListClass>;
using S005D9610 = ArraySerialize<0x005D9610, AiLookRecords, VtableAt<0x0073FF74>, 1>;
using S005D9A40 = ListSerialize<0x005D9A40, AttentionList, AttentionListClass>;
using S005EBC00 = ArraySerialize<0x005EBC00, Plain4, UnitPtrArrayClass>;
using S005FB120 = ArraySerialize<0x005FB120, GlobalMapZoneRecords, GlobalMapZoneArrayClass, 1>;
using S005FB380 = ArraySerialize<0x005FB380, GlobalMapPointRecords, GlobalMapPointArrayClass, 1>;
using S005FB5F0 = ArraySerialize<0x005FB5F0, GlobalMapPointRecords, GlobalMapPathArrayClass, 1>;
using S0060A890 = ArraySerialize<0x0060A890, Plain28, ObjectiveExitArrayClass>;
using S00613690 = ArraySerialize<0x00613690, Plain4, ItemStripArrayClass>;
using S0064B1E0 = ArraySerialize<0x0064B1E0, Plain12, QuickEntryArrayClass>;
using S0065DC90 = ArraySerialize<0x0065DC90, Plain4, TokenPtrArrayClass>;
using S0065DE40 = ArraySerialize<0x0065DE40, Plain4, LinePtrArrayClass>;
using S00666050 = ListSerialize<0x00666050, PtrList, PlayerListClass>;
using S006941C0 = ListSerialize<0x006941C0, PtrList, TextureListClass>;
using S006BC5D0 = ListSerialize<0x006BC5D0, PtrList, SoundListClass>;
using S006C4A40 = ArraySerialize<0x006C4A40, ByteArrays, TerrainByteArraysClass, 1>;
using S006C4C90 = ArraySerialize<0x006C4C90, AlignedArrays, VtableAt<0x007441B4>, 1>;
using S006CD6F0 = ArraySerialize<0x006CD6F0, Plain4, TerrainArrayAClass>;
using S006CD8A0 = ArraySerialize<0x006CD8A0, Plain16, TerrainArrayBClass>;
using S006CDA40 = ArraySerialize<0x006CDA40, Plain4, TerrainArrayCClass>;

// S6c: the game records' arrays (record_arrays.h). Mode 2 where a destroyed copy would free what the
// source owns (materials, nested calls) or reach further (figure parts, meshes, definitions).
using S0044FEF0 = ArraySerialize<0x0044FEF0, recordarrays::FigureParts, FakeUnitPartArrayClass, 2>;
using S00450160 = ArraySerialize<0x00450160, recordarrays::SceneMeshes, FakeUnitSceneRecordArrayClass, 2>;
using S004507E0 = ArraySerialize<0x004507E0, recordarrays::Materials, FakeUnitRecord9CArrayClass, 2>;
using S00451640 = ArraySerialize<0x00451640, recordarrays::SceneNodes, recordarrays::SceneNodeArrayClass, 1>;
using S00454A70 = ArraySerialize<0x00454A70, recordarrays::FaceGroups, recordarrays::FaceGroupArrayClass, 1>;
using S004C52A0 = ArraySerialize<0x004C52A0, recordarrays::SubCalls, recordarrays::SubCallArrayClass, 2>;
using S004C7820 = ArraySerialize<0x004C7820, recordarrays::Definitions, gameslots::DefinitionArrayClass, 2>;
using S0050FC80 = ArraySerialize<0x0050FC80, recordarrays::DeltaFields, recordarrays::DeltaFieldArrayClass, 1>;
using S00557960 = ArraySerialize<0x00557960, recordarrays::Behaviours, recordarrays::BehaviourArrayClass, 1>;
using S006B5770 = ArraySerialize<0x006B5770, recordarrays::Themes, recordarrays::ThemeArrayClass, 1>;
using S006C4790 = ArraySerialize<0x006C4790, recordarrays::SectorVertices, recordarrays::SectorVertexArrayClass, 1>;
using S00571240 = ArraySerialize<0x00571240, recordarrays::WaterLevels, ClientMapArrayClass, 1>;
using S005993A0 = ArraySerialize<0x005993A0, recordarrays::Sectors, VtableAt<CSectorMap::kSectorArrayVtable>, 2>;

// ---- Lists loaded through a temporary (S6d) -----------------------------------------------------------

// Fake engine objects (slot 4 releases: the count at +4 dropped) and fake counted objects (slot 0 the
// deleting destructor, slot 2 OnShutdown, slot 3 Clone: fake i's clone is fake i + 4), every call
// logged. The loads of the original and ours start from the same state and must leave the same.
struct FakeEngine {
    void* const* vtable;
    int refCount;
};
struct FakeCounted {
    void* const* vtable;
    std::int16_t refs;
    std::uint8_t flags6;
    std::uint8_t flags7;
};
struct FakeWorld {
    FakeEngine engines[4];
    FakeCounted counted[8];
    int log[64];
    int logged;
};
FakeWorld g_world;

void Log(int event) {
    if (g_world.logged < 64) g_world.log[g_world.logged++] = event;
}
int EngineIndex(const FakeEngine* p) { return static_cast<int>(p - g_world.engines); }
int CountedIndex(const FakeCounted* p) { return static_cast<int>(p - g_world.counted); }

void __fastcall FakeEngineRelease(FakeEngine* self) {
    Log(0x100 + EngineIndex(self));
    --self->refCount;
}
void* __fastcall FakeCountedDelete(FakeCounted* self, void*, unsigned flags) {
    Log(0x200 + CountedIndex(self) * 2 + static_cast<int>(flags & 1));
    return self;
}
int __fastcall FakeCountedShutdown(FakeCounted* self) {
    Log(0x300 + CountedIndex(self));
    return 0;
}
FakeCounted* __fastcall FakeCountedClone(FakeCounted* self) {
    Log(0x400 + CountedIndex(self));
    return CountedIndex(self) < 4 ? self + 4 : self;
}
void* const* FakeEngineVtable() {
    static void* const vtable[5] = {nullptr, nullptr, nullptr, nullptr, reinterpret_cast<void*>(&FakeEngineRelease)};
    return vtable;
}
void* const* FakeCountedVtable() {
    static void* const vtable[4] = {reinterpret_cast<void*>(&FakeCountedDelete), nullptr,
                                    reinterpret_cast<void*>(&FakeCountedShutdown), reinterpret_cast<void*>(&FakeCountedClone)};
    return vtable;
}

// Engine objects with counts 2, 1, 1, 3. Counted objects: 0 plain, 1 copy-on-share (shared as its
// clone 5), 2 detached (shared as null), 3 with no reference (released to zero, so deleted); 4-7 the
// clones.
void ResetWorld() {
    static const int kEngineRefs[4] = {2, 1, 1, 3};
    for (int i = 0; i < 4; ++i) g_world.engines[i] = {FakeEngineVtable(), kEngineRefs[i]};
    for (int i = 0; i < 8; ++i) g_world.counted[i] = {FakeCountedVtable(), static_cast<std::int16_t>(i < 3 ? 1 : 0), 0, 0};
    g_world.counted[1].flags6 = CRefCounted::kCopyOnShare;
    g_world.counted[2].flags7 = CRefCounted::kDetached;
    g_world.logged = 0;
}
// Element `seed` of a list: fake (seed & 3), none for every fifth.
CEngineObject* EngineAt(int seed) {
    if (seed % 5 == 4) return nullptr;
    return reinterpret_cast<CEngineObject*>(&g_world.engines[seed & 3]);
}
CRefObject* CountedAt(int seed) {
    if (seed % 5 == 4) return nullptr;
    return reinterpret_cast<CRefObject*>(&g_world.counted[seed & 3]);
}
// A text of its own for element `seed`: the prefix, the seed and as many '.' as seed % 7.
CString TextAt(const char* prefix, int seed) {
    char text[32];
    int n = std::snprintf(text, sizeof(text), "%s%d", prefix, seed);
    for (int i = 0; i < seed % 7 && n < 30; ++i) text[n++] = '.';
    text[n] = 0;
    return CString(text);
}

// What a list's elements hold in the checks. `Set` gives an element its values (`owned`: a source,
// which also gets strings of its own; the starting lists' elements hold plain values only), `Drop`
// destroys what a source's element owns. The loaded elements share what the source holds without a
// reference of their own (the temporary released it again), so the loaded lists are freed without
// destroying their elements, and before their source. kFresh (the wave records): each load frees the
// strings the stored bytes point at, so each load reads a source of its own, and its elements own
// their copies (compared by text).
// Leaks: what a load leaks for each element, taken back after it, so that the game's heap is as the
// check found it (the goldens' runs depend on it). Most loads leak nothing.
struct NoLeaks {
    void Probe(int) {}
    bool Reclaim() { return true; }
};

// The block allocators hand out their free blocks last freed first, and each element of these loads
// leaks the temporary's block and frees the new node's before the next element: the n blocks a load
// leaks are the first n of the free list as the load found it. Probe allocates and frees them (in
// reverse, so the list is as it was) to learn them; Reclaim checks each is the leaked block and frees
// it.
// Tokens: the temporary's "None" (CString's 64-byte blocks); a leaked one holds one reference.
struct TokenLeaks {
    char* blocks[16] = {};
    int count = 0;
    void Probe(int n) {
        count = n;
        CString probes[16];
        for (int i = 0; i < n; ++i) {
            probes[i] = "None";
            blocks[i] = const_cast<char*>(static_cast<const char*>(probes[i]));
        }
        for (int i = n; i--;) {
            probes[i].~CString();
            new (&probes[i]) CString;
        }
    }
    bool Reclaim() {
        bool ok = true;
        for (int i = 0; i < count; ++i) {
            char* text = blocks[i];
            const int* header = reinterpret_cast<const int*>(text);
            if (header[-3] != 1 || header[-2] != 4 || std::strcmp(text, "None") != 0) {
                ok = false;   // not the temporary's: left alone
                continue;
            }
            reinterpret_cast<CString*>(&text)->~CString();
        }
        return ok;
    }
};
// Wave records: the temporary's 8-byte name buffer (STLport's node allocator). A free block is told
// from a leaked one by allocating as many: none may come back.
struct WaveLeaks {
    using Words = std::uint32_t[3];   // a string's start, finish and end of storage
    char* blocks[16] = {};
    int count = 0;
    static stlp::string* AsString(Words& w) { return reinterpret_cast<stlp::string*>(&w); }
    static int Allocate(char** out, int n) {
        Words strings[16];
        for (int i = 0; i < n; ++i) {
            stlp::StringElement::Default(AsString(strings[i]));
            out[i] = stlp::StringAbi::Start(*AsString(strings[i]));
        }
        for (int i = n; i--;) stlp::StringElement::Destroy(AsString(strings[i]));
        return n;
    }
    void Probe(int n) { count = Allocate(blocks, n); }
    bool Reclaim() {
        char* fresh[16];
        Allocate(fresh, count);
        for (int i = 0; i < count; ++i)
            for (int j = 0; j < count; ++j)
                if (fresh[i] == blocks[j]) return false;   // a probe was free after all: nothing freed
        for (int i = count; i--;) {
            Words w = {reinterpret_cast<std::uintptr_t>(blocks[i]), reinterpret_cast<std::uintptr_t>(blocks[i]),
                       reinterpret_cast<std::uintptr_t>(blocks[i] + 8)};
            stlp::StringElement::Destroy(AsString(w));
        }
        return true;
    }
};

template <typename T>
struct FillBase {
    using Leaks = NoLeaks;
    static constexpr bool kFresh = false;
    static void Drop(T*) {}
    static bool Same(const T& a, const T& b) { return std::memcmp(&a, &b, sizeof(T)) == 0; }
};
struct EngineFill : FillBase<CEngineObject*> {
    static void Set(CEngineObject** p, int seed, bool) { *p = EngineAt(seed); }
};
struct ObjectRefFill : FillBase<ScriptObjectRef> {
    static void Set(ScriptObjectRef* p, int seed, bool) { p->object = EngineAt(seed); }
};
struct RefFill : FillBase<RefPtr> {
    static void Set(RefPtr* p, int seed, bool) { p->ptr = CountedAt(seed); }
};
struct ArmourSpellFill : FillBase<gameslots::ArmourSpellRecord> {
    static void Set(gameslots::ArmourSpellRecord* p, int seed, bool) {
        p->object = EngineAt(seed);
        p->value = static_cast<std::uint32_t>(seed * 7 + 1);
        p->flag = static_cast<std::uint8_t>(seed + 3);
        Pattern(p->pad, sizeof(p->pad), seed);
    }
};
struct ParticleSourceFill : FillBase<ScriptParticleSource> {
    static void Set(ScriptParticleSource* p, int seed, bool) {
        p->id = seed;
        p->deleteAfterMove = seed & 1;
        p->moving = seed + 2;
        p->steps = seed * 3;
        p->step = {static_cast<float>(seed) * 0.25f, 1.5f, -2.0f};
        p->lifetime = seed - 4;
    }
};
struct FxSourceFill : FillBase<ScriptFxSource> {
    static void Set(ScriptFxSource* p, int seed, bool owned) {
        p->id = seed;
        if (owned) p->name = TextAt("fx", seed);
        p->position = {1.0f, static_cast<float>(seed), 3.0f};
        p->volume = 0.5f;
        p->radius = static_cast<float>(seed) * 2.0f;
        p->idle = seed & 1;
        p->spread = 4.0f;
        p->minDelay = seed;
        p->maxDelay = seed + 10;
        p->countdown = seed * 5;
    }
    static void Drop(ScriptFxSource* p) { p->name.~CString(); }
};
struct LightningFill : FillBase<ScriptLightning> {
    static void Set(ScriptLightning* p, int seed, bool) { p->id = seed * 3 + 1; }
};
struct DeclFill : FillBase<ScriptDecl> {   // no commands (a loaded copy's temporary frees the source's)
    static void Set(ScriptDecl* p, int seed, bool) { p->id = seed + 100; }
};
struct DefinitionFill : FillBase<ScriptDefinition> {   // no pairs or variables (as for the commands)
    static void Set(ScriptDefinition* p, int seed, bool owned) {
        p->index = seed;
        if (!owned) return;
        p->name = TextAt("script", seed);
        p->params = TextAt("f", seed);
    }
    static void Drop(ScriptDefinition* p) { ScriptDefinitionDestruct(p); }
};
struct RunFill : FillBase<ScriptRun> {   // no variables
    static void Set(ScriptRun* p, int seed, bool) {
        p->block = reinterpret_cast<ScriptDecl*>(static_cast<std::uintptr_t>(0x1000 + seed * 4));
        p->index = seed;
        p->ticks = seed * 2;
        p->sleepStart = seed - 1;
        p->killFlag = reinterpret_cast<int*>(static_cast<std::uintptr_t>(0x2000 + seed * 4));
    }
};
struct GroupFill : FillBase<ScriptGroup> {   // empty groups (a loaded copy's temporary frees the source's references)
    static void Set(ScriptGroup*, int, bool) {}
};
struct TokenFill : FillBase<ScriptToken> {
    using Leaks = TokenLeaks;
    static void Set(ScriptToken* p, int seed, bool owned) {
        if (owned) p->text = TextAt("tok", seed);
        p->line = seed * 11;
    }
    static void Drop(ScriptToken* p) { p->text.~CString(); }
    // By text: each new node's "None" is a string of its own.
    static bool Same(const ScriptToken& a, const ScriptToken& b) {
        return a.line == b.line && a.text.GetLength() == b.text.GetLength() && std::strcmp(a.text, b.text) == 0;
    }
};
struct DefineFill : FillBase<ScriptDefine> {
    static void Set(ScriptDefine* p, int seed, bool owned) {
        if (!owned) return;
        p->name = TextAt("NAME", seed);
        p->value = TextAt("value", seed + 1);
    }
    static void Drop(ScriptDefine* p) { ScriptDefineElements::Destruct(p, 1); }
};
struct SharedObjectFill : FillBase<SharedObject> {   // no objects (a loaded copy's temporary frees the source's array)
    static void Set(SharedObject* p, int seed, bool) {
        p->objectId = seed + 1;
        p->player = seed & 3;
        p->money = static_cast<std::uint32_t>(seed * 100);
    }
};
struct WaveFill : FillBase<WaveRecord> {
    using Leaks = WaveLeaks;
    static constexpr bool kFresh = true;
    static void Set(WaveRecord* p, int seed, bool owned) {
        p->data = reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x3000 + seed * 4));
        if (owned) {
            CString text = TextAt("sound\\wave", seed);
            const char* first = text;
            gamelists::WaveName(p)->Assign(first, first + text.GetLength());
        }
        p->lastUsed = static_cast<std::uint32_t>(seed * 13);
        p->size = seed * 1024;
        p->uses = seed & 1;
    }
    static void Drop(WaveRecord* p) { gamelists::DestroyWave(p); }
    static bool Same(const WaveRecord& a, const WaveRecord& b) {
        const stlp::string* x = gamelists::WaveName(const_cast<WaveRecord*>(&a));
        const stlp::string* y = gamelists::WaveName(const_cast<WaveRecord*>(&b));
        return a.data == b.data && a.lastUsed == b.lastUsed && a.size == b.size && a.uses == b.uses && *x == *y &&
               stlp::StringAbi::EndOfStorage(*x) - x->data() == stlp::StringAbi::EndOfStorage(*y) - y->data();
    }
};

// Sources of 7 and 0 elements (blocks of 3); stored with both; loaded into an empty list and one of 2
// elements with both, from the original's bytes, with the fakes as they were before each load. The
// loaded lists, what the reads consumed and the fakes after the load (counts, flags and the log of
// releases, deletions, shutdowns and clones) compared.
template <std::uintptr_t Address, typename Through, typename Class, typename Fill>
struct ListThroughSerialize {
    using List = typename Through::Abi::List;
    static inline void* original = nullptr;

    static void __fastcall Run(List* self, void*, CArchive* ar) {
        static const char* name = Name("", Address);
        SerializeCListThrough<Through>(self, nullptr, ar);
        Report(name, true, "");
    }

    static void Make(List& l, int which) {
        ListHeader<Class>::Construct(reinterpret_cast<mfc::CList<void*>*>(&l), nullptr, 3);
        int n = which >= 0 ? which : (which == -1 ? 0 : 2);
        for (int i = 0; i < n; ++i) {
            auto* node = l.NewNode(l.tail, nullptr);
            Fill::Set(&node->data, (which & 0xF) * 16 + i, which >= 0);
            if (l.tail) l.tail->next = node;
            else l.head = node;
            l.tail = node;
        }
    }
    static bool Same(const List& x, const List& y) {
        if (x.count != y.count || x.blockSize != y.blockSize) return false;
        auto* a = x.head;
        auto* b = y.head;
        for (; a && b; a = a->next, b = b->next)
            if (!Fill::Same(a->data, b->data) || (a->prev == nullptr) != (b->prev == nullptr)) return false;
        return !a && !b && (x.tail == nullptr) == (y.tail == nullptr);
    }
    // The list's blocks freed, its first `drop` elements (-1: all) destroyed first.
    static void Free(List& l, int drop) {
        for (auto* node = l.head; node && drop--; node = node->next) Fill::Drop(&node->data);
        mfc::PlexFreeDataChain(l.blocks);
    }
    static std::vector<unsigned char> Store(SerializeFn fn, List& l) {
        MemoryArchive ar;
        fn(&l, nullptr, &ar.ar);
        return ar.Stored();
    }

    static bool Compare(SerializeFn theirs, SerializeFn ours, int source) {
        ResetWorld();
        List src;
        Make(src, source);
        std::vector<unsigned char> stored = Store(theirs, src);
        bool ok = stored == Store(ours, src);
        for (int start = 0; start < 2; ++start) {
            List sx, sy, x, y;
            std::vector<unsigned char> bytesX = stored, bytesY = stored;
            if constexpr (Fill::kFresh) {
                Make(sx, source);
                Make(sy, source);
                bytesX = Store(theirs, sx);
                bytesY = Store(theirs, sy);
            }
            Make(x, -1 - start);
            Make(y, -1 - start);
            MemoryArchive lx(bytesX), ly(bytesY);
            FakeWorld before = g_world;
            typename Fill::Leaks leaks;
            leaks.Probe(source);
            theirs(&x, nullptr, &lx.ar);
            ok = leaks.Reclaim() && ok;
            FakeWorld afterOriginal = g_world;
            g_world = before;
            leaks.Probe(source);
            ours(&y, nullptr, &ly.ar);
            ok = leaks.Reclaim() && ok;
            ok = ok && lx.Used() == ly.Used() && Same(x, y) && std::memcmp(&afterOriginal, &g_world, sizeof(FakeWorld)) == 0;
            g_world = before;
            // The starting elements (and the wave records' copies) are the list's own.
            int own = Fill::kFresh ? -1 : (start == 0 ? 0 : 2);
            Free(x, own);
            Free(y, own);
            if constexpr (Fill::kFresh) {
                Free(sx, 0);   // its strings went with the load's temporaries
                Free(sy, 0);
            }
        }
        Free(src, -1);
        return ok;
    }

    static void Startup() {
        auto ours = reinterpret_cast<SerializeFn>(&SerializeCListThrough<Through>);
        bool ok = Compare(reinterpret_cast<SerializeFn>(original), ours, 7) &&
                  Compare(reinterpret_cast<SerializeFn>(original), ours, 0);
        Check(Name(" start-up", Address), true, ok, "");
    }
};

// ---- CArray<CString> and SerializeElements<CString> (S6d) ----------------------------------------------

// Texts of 0 to 299 characters (so some lengths take the WORD form), by seed.
void SetText(CString& s, int seed) {
    char text[300];
    int n = (seed * 71) % 300;
    for (int i = 0; i < n; ++i) text[i] = static_cast<char>('a' + (seed + i) % 26);
    text[n] = 0;
    s = text;
}
bool SameTexts(const CString* a, const CString* b, int count) {
    for (int i = 0; i < count; ++i)
        if (a[i].GetLength() != b[i].GetLength() || std::strcmp(a[i], b[i]) != 0) return false;
    return true;
}

// 004C2700: sources of 5, 0 and 3 strings; loaded into an empty array, one of 2 strings (grows) and
// one of 8 (shrinks); the strings compared by text.
struct StringArraySerialize {
    using Array = Strings::Array;
    static inline void* original = nullptr;

    static void __fastcall Run(Array* self, void*, CArchive* ar) {
        static const char* name = Name("", 0x004C2700);
        SerializeStringArray(self, nullptr, ar);
        Report(name, true, "");
    }
    static void Make(Array& a, int which) {
        static const int kSizes[] = {0, 2, 8};
        a.Construct(reinterpret_cast<std::uintptr_t>(StringArrayClass::Vtable()));
        int n = which >= 0 ? which : kSizes[-1 - which];
        a.SetSize(n, -1);
        for (int i = 0; i < n; ++i) SetText(a.data[i], (which & 0xF) * 16 + i);
    }
    static bool Same(const Array& x, const Array& y) {
        return x.size == y.size && x.maxSize == y.maxSize && x.growBy == y.growBy && (x.data == nullptr) == (y.data == nullptr) &&
               SameTexts(x.data, y.data, x.size);
    }
    static void Release(Array& a) { Strings::Destruct<StringArrayClass>(&a); }

    static void Startup() {
        bool ok = Compare<Array>(reinterpret_cast<SerializeFn>(original), reinterpret_cast<SerializeFn>(&SerializeStringArray), Make,
                                 Same, Release, 3, {5, 0, 3});
        Check(Name(" start-up", 0x004C2700), true, ok, "");
    }
};

// 007085AF: six strings stored with both (the same bytes), then loaded with both into six strings that
// hold texts already.
struct StringsSerialize {
    using Fn = void(__stdcall*)(CArchive*, CString*, int);
    static inline void* original = nullptr;

    static void __stdcall Run(CArchive* ar, CString* strings, int count) {
        static const char* name = Name("", 0x007085AF);
        SerializeStrings(ar, strings, count);
        Report(name, true, "");
    }
    static void Startup() {
        auto theirs = reinterpret_cast<Fn>(original);
        CString source[6], x[6], y[6];
        for (int i = 0; i < 6; ++i) {
            SetText(source[i], i + 1);
            SetText(x[i], i + 20);
            SetText(y[i], i + 20);
        }
        MemoryArchive a, b;
        theirs(&a.ar, source, 6);
        SerializeStrings(&b.ar, source, 6);
        MemoryArchive lx(a.Stored()), ly(a.Stored());
        theirs(&lx.ar, x, 6);
        SerializeStrings(&ly.ar, y, 6);
        bool ok = a.Stored() == b.Stored() && lx.Used() == ly.Used() && SameTexts(x, y, 6) && SameTexts(x, source, 6);
        Check(Name(" start-up", 0x007085AF), true, ok, "");
    }
};

using S00427690 = ListThroughSerialize<0x00427690, gameslots::EngineObjectsThrough, gameslots::ItemObjectListClass, EngineFill>;
using S00571420 = ListThroughSerialize<0x00571420, gameslots::EngineObjectsThrough, gameslots::ClientObjectList2Class, EngineFill>;
using S004C9350 = ListThroughSerialize<0x004C9350, gameslots::ObjectRefsThrough, gameslots::ObjectRefListClass, ObjectRefFill>;
using S0052C740 = ListThroughSerialize<0x0052C740, gameslots::VictimsThrough, gameslots::VictimListClass, ObjectRefFill>;
using S005576C0 = ListThroughSerialize<0x005576C0, gameslots::OwnedRefsThrough, gamelists::OwnedRefListClass, RefFill>;
using S00598FD0 = ListThroughSerialize<0x00598FD0, gameslots::RefsThrough, gameslots::RefListAClass, RefFill>;
using S00599DF0 = ListThroughSerialize<0x00599DF0, gameslots::RefsThrough, gameslots::RefListBClass, RefFill>;
using S00558100 = ListThroughSerialize<0x00558100, gameslots::ArmourSpellsThrough, gameslots::ArmourSpellListClass, ArmourSpellFill>;
using S004C61C0 =
    ListThroughSerialize<0x004C61C0, gameslots::ParticleSourcesThrough, gameslots::ParticleSourceListClass, ParticleSourceFill>;
using S004C64C0 = ListThroughSerialize<0x004C64C0, gameslots::FxSourcesThrough, gameslots::FxSourceListClass, FxSourceFill>;
using S004C6800 = ListThroughSerialize<0x004C6800, gameslots::LightningsThrough, gameslots::LightningListClass, LightningFill>;
using S004C7640 = ListThroughSerialize<0x004C7640, gameslots::DeclsThrough, gameslots::DeclListClass, DeclFill>;
using S004C7F60 = ListThroughSerialize<0x004C7F60, gameslots::DefinitionsThrough, gameslots::DefinitionListClass, DefinitionFill>;
using S004C8640 = ListThroughSerialize<0x004C8640, gameslots::RunsThrough, gameslots::RunListClass, RunFill>;
using S004C8F00 = ListThroughSerialize<0x004C8F00, gameslots::GroupsThrough, gameslots::GroupListClass, GroupFill>;
using S004CA2E0 = ListThroughSerialize<0x004CA2E0, gameslots::TokensThrough, gamelists::TokenListClass, TokenFill>;
using S004D63E0 = ListThroughSerialize<0x004D63E0, gameslots::DefinesThrough, gameslots::DefineListClass, DefineFill>;
using S0059A4A0 = ListThroughSerialize<0x0059A4A0, gameslots::SharedObjectsThrough, gamelists::SharedObjectListClass, SharedObjectFill>;
using S006BC920 = ListThroughSerialize<0x006BC920, gameslots::WavesThrough, VtableAt<0x00744124>, WaveFill>;

void SerializeStartupCheck() {
    S00405190::Startup();
    S00427890::Startup();
    S0044F650::Startup();
    S00450570::Startup();
    S00450A00::Startup();
    S00450BB0::Startup();
    S00450D40::Startup();
    S00451060::Startup();
    S004511F0::Startup();
    S00451850::Startup();
    S00451A70::Startup();
    S00451CC0::Startup();
    S00451EE0::Startup();
    S00452110::Startup();
    S004523B0::Startup();
    S00452650::Startup();
    S0045D950::Startup();
    S0045DC60::Startup();
    S0045FA40::Startup();
    S0045FCB0::Startup();
    S0045FEB0::Startup();
    S00460270::Startup();
    S00468DA0::Startup();
    S0046EC10::Startup();
    S0046EDA0::Startup();
    S00474F50::Startup();
    S00475130::Startup();
    S00475310::Startup();
    S004756A0::Startup();
    S00475A00::Startup();
    S00475D60::Startup();
    S004761C0::Startup();
    S00476430::Startup();
    S004767A0::Startup();
    S00476AD0::Startup();
    S00498D00::Startup();
    S00498EA0::Startup();
    S004C1E70::Startup();
    S004C2310::Startup();
    S004C29E0::Startup();
    S004C3BE0::Startup();
    S004C4560::Startup();
    S004C4860::Startup();
    S004C4B50::Startup();
    S004C4E40::Startup();
    S004C55C0::Startup();
    S004C5B10::Startup();
    S004C5E60::Startup();
    S004C88E0::Startup();
    S004C9510::Startup();
    S004C9750::Startup();
    S004C9B10::Startup();
    S004C9E20::Startup();
    S0050A210::Startup();
    S0050DC20::Startup();
    S0050DE90::Startup();
    S0050FFD0::Startup();
    S00511C30::Startup();
    S00511E20::Startup();
    S005198D0::Startup();
    S00519B50::Startup();
    S00532E70::Startup();
    S00533030::Startup();
    S005331E0::Startup();
    S00533390::Startup();
    S00539440::Startup();
    S005396E0::Startup();
    S00556E10::Startup();
    S00557050::Startup();
    S00557270::Startup();
    S00557430::Startup();
    S00557E90::Startup();
    S005584D0::Startup();
    S00560200::Startup();
    S0056FEA0::Startup();
    S005701F0::Startup();
    S00570570::Startup();
    S00570F30::Startup();
    S00571660::Startup();
    S005991A0::Startup();
    S00599640::Startup();
    S005997C0::Startup();
    S0059A170::Startup();
    S0059B0D0::Startup();
    S0059B2C0::Startup();
    S0059E0C0::Startup();
    S005B1870::Startup();
    S005B1A00::Startup();
    S005BDF40::Startup();
    S005C1900::Startup();
    S005C1AB0::Startup();
    S005C4D80::Startup();
    S005C5070::Startup();
    S005C5360::Startup();
    S005C5670::Startup();
    S005CDC00::Startup();
    S005D9610::Startup();
    S005D9A40::Startup();
    S005EBC00::Startup();
    S005FB120::Startup();
    S005FB380::Startup();
    S005FB5F0::Startup();
    S0060A890::Startup();
    S00613690::Startup();
    S0064B1E0::Startup();
    S0065DC90::Startup();
    S0065DE40::Startup();
    S00666050::Startup();
    S006941C0::Startup();
    S006BC5D0::Startup();
    S006C4A40::Startup();
    S006C4C90::Startup();
    S006CD6F0::Startup();
    S006CD8A0::Startup();
    S006CDA40::Startup();
    // S6c: the game records' arrays (record_arrays.h).
    S0044FEF0::Startup();
    S00450160::Startup();
    S004507E0::Startup();
    S00451640::Startup();
    S00454A70::Startup();
    S004C52A0::Startup();
    S004C7820::Startup();
    S0050FC80::Startup();
    S00557960::Startup();
    S006B5770::Startup();
    S006C4790::Startup();
    S00571240::Startup();
    S005993A0::Startup();
    // S6d: the lists loaded through a temporary, CArray<CString> and its SerializeElements.
    S00427690::Startup();
    S00571420::Startup();
    S004C9350::Startup();
    S0052C740::Startup();
    S005576C0::Startup();
    S00598FD0::Startup();
    S00599DF0::Startup();
    S00558100::Startup();
    S004C61C0::Startup();
    S004C64C0::Startup();
    S004C6800::Startup();
    S004C7640::Startup();
    S004C7F60::Startup();
    S004C8640::Startup();
    S004C8F00::Startup();
    S004CA2E0::Startup();
    S004D63E0::Startup();
    S0059A4A0::Startup();
    S006BC920::Startup();
    StringsSerialize::Startup();
    StringArraySerialize::Startup();
}

} // namespace

EI_STARTUP_CHECK(SerializeStartupCheck);

EI_VERIFY(0x00405190, S00405190::Run, 6, S00405190::original);
EI_VERIFY(0x00427890, S00427890::Run, 6, S00427890::original);
EI_VERIFY(0x0044F650, S0044F650::Run, 5, S0044F650::original);
EI_VERIFY(0x00450570, S00450570::Run, 5, S00450570::original);
EI_VERIFY(0x00450A00, S00450A00::Run, 5, S00450A00::original);
EI_VERIFY(0x00450BB0, S00450BB0::Run, 5, S00450BB0::original);
EI_VERIFY(0x00450D40, S00450D40::Run, 5, S00450D40::original);
EI_VERIFY(0x00451060, S00451060::Run, 5, S00451060::original);
EI_VERIFY(0x004511F0, S004511F0::Run, 5, S004511F0::original);
EI_VERIFY(0x00451850, S00451850::Run, 5, S00451850::original);
EI_VERIFY(0x00451A70, S00451A70::Run, 5, S00451A70::original);
EI_VERIFY(0x00451CC0, S00451CC0::Run, 8, S00451CC0::original);
EI_VERIFY(0x00451EE0, S00451EE0::Run, 5, S00451EE0::original);
EI_VERIFY(0x00452110, S00452110::Run, 8, S00452110::original);
EI_VERIFY(0x004523B0, S004523B0::Run, 8, S004523B0::original);
EI_VERIFY(0x00452650, S00452650::Run, 8, S00452650::original);
EI_VERIFY(0x0045D950, S0045D950::Run, 5, S0045D950::original);
EI_VERIFY(0x0045DC60, S0045DC60::Run, 5, S0045DC60::original);
EI_VERIFY(0x0045FA40, S0045FA40::Run, 5, S0045FA40::original);
EI_VERIFY(0x0045FCB0, S0045FCB0::Run, 5, S0045FCB0::original);
EI_VERIFY(0x0045FEB0, S0045FEB0::Run, 5, S0045FEB0::original);
EI_VERIFY(0x00460270, S00460270::Run, 5, S00460270::original);
EI_VERIFY(0x00468DA0, S00468DA0::Run, 5, S00468DA0::original);
EI_VERIFY(0x0046EC10, S0046EC10::Run, 5, S0046EC10::original);
EI_VERIFY(0x0046EDA0, S0046EDA0::Run, 5, S0046EDA0::original);
EI_VERIFY(0x00474F50, S00474F50::Run, 5, S00474F50::original);
EI_VERIFY(0x00475130, S00475130::Run, 5, S00475130::original);
EI_VERIFY(0x00475310, S00475310::Run, 5, S00475310::original);
EI_VERIFY(0x004756A0, S004756A0::Run, 5, S004756A0::original);
EI_VERIFY(0x00475A00, S00475A00::Run, 5, S00475A00::original);
EI_VERIFY(0x00475D60, S00475D60::Run, 5, S00475D60::original);
EI_VERIFY(0x004761C0, S004761C0::Run, 8, S004761C0::original);
EI_VERIFY(0x00476430, S00476430::Run, 5, S00476430::original);
EI_VERIFY(0x004767A0, S004767A0::Run, 5, S004767A0::original);
EI_VERIFY(0x00476AD0, S00476AD0::Run, 5, S00476AD0::original);
EI_VERIFY(0x00498D00, S00498D00::Run, 6, S00498D00::original);
EI_VERIFY(0x00498EA0, S00498EA0::Run, 5, S00498EA0::original);
EI_VERIFY(0x004C1E70, S004C1E70::Run, 5, S004C1E70::original);
EI_VERIFY(0x004C2310, S004C2310::Run, 5, S004C2310::original);
EI_VERIFY(0x004C29E0, S004C29E0::Run, 5, S004C29E0::original);
EI_VERIFY(0x004C3BE0, S004C3BE0::Run, 5, S004C3BE0::original);
EI_VERIFY(0x004C4560, S004C4560::Run, 5, S004C4560::original);
EI_VERIFY(0x004C4860, S004C4860::Run, 5, S004C4860::original);
EI_VERIFY(0x004C4B50, S004C4B50::Run, 5, S004C4B50::original);
EI_VERIFY(0x004C4E40, S004C4E40::Run, 5, S004C4E40::original);
EI_VERIFY(0x004C55C0, S004C55C0::Run, 5, S004C55C0::original);
EI_VERIFY(0x004C5B10, S004C5B10::Run, 5, S004C5B10::original);
EI_VERIFY(0x004C5E60, S004C5E60::Run, 5, S004C5E60::original);
EI_VERIFY(0x004C88E0, S004C88E0::Run, 5, S004C88E0::original);
EI_VERIFY(0x004C9510, S004C9510::Run, 5, S004C9510::original);
EI_VERIFY(0x004C9750, S004C9750::Run, 6, S004C9750::original);
EI_VERIFY(0x004C9B10, S004C9B10::Run, 5, S004C9B10::original);
EI_VERIFY(0x004C9E20, S004C9E20::Run, 5, S004C9E20::original);
EI_VERIFY(0x0050A210, S0050A210::Run, 5, S0050A210::original);
EI_VERIFY(0x0050DC20, S0050DC20::Run, 6, S0050DC20::original);
EI_VERIFY(0x0050DE90, S0050DE90::Run, 6, S0050DE90::original);
EI_VERIFY(0x0050FFD0, S0050FFD0::Run, 5, S0050FFD0::original);
EI_VERIFY(0x00511C30, S00511C30::Run, 5, S00511C30::original);
EI_VERIFY(0x00511E20, S00511E20::Run, 5, S00511E20::original);
EI_VERIFY(0x005198D0, S005198D0::Run, 6, S005198D0::original);
EI_VERIFY(0x00519B50, S00519B50::Run, 5, S00519B50::original);
EI_VERIFY(0x00532E70, S00532E70::Run, 5, S00532E70::original);
EI_VERIFY(0x00533030, S00533030::Run, 5, S00533030::original);
EI_VERIFY(0x005331E0, S005331E0::Run, 5, S005331E0::original);
EI_VERIFY(0x00533390, S00533390::Run, 5, S00533390::original);
EI_VERIFY(0x00539440, S00539440::Run, 8, S00539440::original);
EI_VERIFY(0x005396E0, S005396E0::Run, 5, S005396E0::original);
EI_VERIFY(0x00556E10, S00556E10::Run, 5, S00556E10::original);
EI_VERIFY(0x00557050, S00557050::Run, 5, S00557050::original);
EI_VERIFY(0x00557270, S00557270::Run, 5, S00557270::original);
EI_VERIFY(0x00557430, S00557430::Run, 5, S00557430::original);
EI_VERIFY(0x00557E90, S00557E90::Run, 5, S00557E90::original);
EI_VERIFY(0x005584D0, S005584D0::Run, 6, S005584D0::original);
EI_VERIFY(0x00560200, S00560200::Run, 5, S00560200::original);
EI_VERIFY(0x0056FEA0, S0056FEA0::Run, 5, S0056FEA0::original);
EI_VERIFY(0x005701F0, S005701F0::Run, 5, S005701F0::original);
EI_VERIFY(0x00570570, S00570570::Run, 6, S00570570::original);
EI_VERIFY(0x00570F30, S00570F30::Run, 6, S00570F30::original);
EI_VERIFY(0x00571660, S00571660::Run, 6, S00571660::original);
EI_VERIFY(0x005991A0, S005991A0::Run, 5, S005991A0::original);
EI_VERIFY(0x00599640, S00599640::Run, 5, S00599640::original);
EI_VERIFY(0x005997C0, S005997C0::Run, 5, S005997C0::original);
EI_VERIFY(0x0059A170, S0059A170::Run, 5, S0059A170::original);
EI_VERIFY(0x0059B0D0, S0059B0D0::Run, 5, S0059B0D0::original);
EI_VERIFY(0x0059B2C0, S0059B2C0::Run, 8, S0059B2C0::original);
EI_VERIFY(0x0059E0C0, S0059E0C0::Run, 5, S0059E0C0::original);
EI_VERIFY(0x005B1870, S005B1870::Run, 8, S005B1870::original);
EI_VERIFY(0x005B1A00, S005B1A00::Run, 8, S005B1A00::original);
EI_VERIFY(0x005BDF40, S005BDF40::Run, 8, S005BDF40::original);
EI_VERIFY(0x005C1900, S005C1900::Run, 5, S005C1900::original);
EI_VERIFY(0x005C1AB0, S005C1AB0::Run, 5, S005C1AB0::original);
EI_VERIFY(0x005C4D80, S005C4D80::Run, 5, S005C4D80::original);
EI_VERIFY(0x005C5070, S005C5070::Run, 5, S005C5070::original);
EI_VERIFY(0x005C5360, S005C5360::Run, 5, S005C5360::original);
EI_VERIFY(0x005C5670, S005C5670::Run, 5, S005C5670::original);
EI_VERIFY(0x005CDC00, S005CDC00::Run, 5, S005CDC00::original);
EI_VERIFY(0x005D9610, S005D9610::Run, 5, S005D9610::original);
EI_VERIFY(0x005D9A40, S005D9A40::Run, 8, S005D9A40::original);
EI_VERIFY(0x005EBC00, S005EBC00::Run, 5, S005EBC00::original);
EI_VERIFY(0x005FB120, S005FB120::Run, 5, S005FB120::original);
EI_VERIFY(0x005FB380, S005FB380::Run, 5, S005FB380::original);
EI_VERIFY(0x005FB5F0, S005FB5F0::Run, 5, S005FB5F0::original);
EI_VERIFY(0x0060A890, S0060A890::Run, 5, S0060A890::original);
EI_VERIFY(0x00613690, S00613690::Run, 5, S00613690::original);
EI_VERIFY(0x0064B1E0, S0064B1E0::Run, 5, S0064B1E0::original);
EI_VERIFY(0x0065DC90, S0065DC90::Run, 5, S0065DC90::original);
EI_VERIFY(0x0065DE40, S0065DE40::Run, 5, S0065DE40::original);
EI_VERIFY(0x00666050, S00666050::Run, 6, S00666050::original);
EI_VERIFY(0x006941C0, S006941C0::Run, 6, S006941C0::original);
EI_VERIFY(0x006BC5D0, S006BC5D0::Run, 6, S006BC5D0::original);
EI_VERIFY(0x006C4A40, S006C4A40::Run, 5, S006C4A40::original);
EI_VERIFY(0x006C4C90, S006C4C90::Run, 5, S006C4C90::original);
EI_VERIFY(0x006CD6F0, S006CD6F0::Run, 5, S006CD6F0::original);
EI_VERIFY(0x006CD8A0, S006CD8A0::Run, 5, S006CD8A0::original);
EI_VERIFY(0x006CDA40, S006CDA40::Run, 5, S006CDA40::original);
EI_VERIFY(0x0044FEF0, S0044FEF0::Run, 5, S0044FEF0::original);
EI_VERIFY(0x00450160, S00450160::Run, 5, S00450160::original);
EI_VERIFY(0x004507E0, S004507E0::Run, 5, S004507E0::original);
EI_VERIFY(0x00451640, S00451640::Run, 5, S00451640::original);
EI_VERIFY(0x00454A70, S00454A70::Run, 5, S00454A70::original);
EI_VERIFY(0x004C52A0, S004C52A0::Run, 5, S004C52A0::original);
EI_VERIFY(0x004C7820, S004C7820::Run, 5, S004C7820::original);
EI_VERIFY(0x0050FC80, S0050FC80::Run, 5, S0050FC80::original);
EI_VERIFY(0x00557960, S00557960::Run, 5, S00557960::original);
EI_VERIFY(0x006B5770, S006B5770::Run, 5, S006B5770::original);
EI_VERIFY(0x006C4790, S006C4790::Run, 5, S006C4790::original);
EI_VERIFY(0x00571240, S00571240::Run, 5, S00571240::original);
EI_VERIFY(0x005993A0, S005993A0::Run, 5, S005993A0::original);
EI_VERIFY(0x00427690, S00427690::Run, 8, S00427690::original);
EI_VERIFY(0x00571420, S00571420::Run, 8, S00571420::original);
EI_VERIFY(0x004C9350, S004C9350::Run, 7, S004C9350::original);
EI_VERIFY(0x0052C740, S0052C740::Run, 8, S0052C740::original);
EI_VERIFY(0x005576C0, S005576C0::Run, 8, S005576C0::original);
EI_VERIFY(0x00598FD0, S00598FD0::Run, 8, S00598FD0::original);
EI_VERIFY(0x00599DF0, S00599DF0::Run, 8, S00599DF0::original);
EI_VERIFY(0x00558100, S00558100::Run, 8, S00558100::original);
EI_VERIFY(0x004C61C0, S004C61C0::Run, 8, S004C61C0::original);
EI_VERIFY(0x004C64C0, S004C64C0::Run, 8, S004C64C0::original);
EI_VERIFY(0x004C6800, S004C6800::Run, 8, S004C6800::original);
EI_VERIFY(0x004C7640, S004C7640::Run, 8, S004C7640::original);
EI_VERIFY(0x004C7F60, S004C7F60::Run, 8, S004C7F60::original);
EI_VERIFY(0x004C8640, S004C8640::Run, 8, S004C8640::original);
EI_VERIFY(0x004C8F00, S004C8F00::Run, 8, S004C8F00::original);
EI_VERIFY(0x004CA2E0, S004CA2E0::Run, 8, S004CA2E0::original);
EI_VERIFY(0x004D63E0, S004D63E0::Run, 8, S004D63E0::original);
EI_VERIFY(0x0059A4A0, S0059A4A0::Run, 8, S0059A4A0::original);
EI_VERIFY(0x006BC920, S006BC920::Run, 8, S006BC920::original);
EI_VERIFY(0x004C2700, StringArraySerialize::Run, 5, StringArraySerialize::original);
EI_VERIFY(0x007085AF, StringsSerialize::Run, 5, StringsSerialize::original);
