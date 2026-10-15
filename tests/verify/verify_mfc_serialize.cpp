// Checks for the CArray / CList / CMap Serialize functions the exe's vtables point at
// (src/lib/mfc_slots.cpp, src/game/game_slots.cpp, ticket S6a). In the game: ours only, counted. At
// start-up each runs next to the original (through its stolen bytes) on scratch CArchives over memory:
// storing one object gives the same bytes; loading those bytes into objects that are empty or already
// hold elements gives the same objects and reads as far.
#include "verify.h"

#include "../../src/lib/mfc_slots.h"

#include <cstdio>
#include <cstring>
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
using S004C29E0 = ArraySerialize<0x004C29E0, Plain4, ScriptFloatsClass>;
using S004C4560 = ArraySerialize<0x004C4560, Plain4, FloatArrayClass>;
using S004C4860 = ArraySerialize<0x004C4860, Plain4, ArgObjectsBaseClass>;
using S004C4B50 = ArraySerialize<0x004C4B50, Plain4, ArgGroupsBaseClass>;
using S004C4E40 = ArraySerialize<0x004C4E40, Plain12, BindingsBaseClass>;
using S004C5B10 = ArraySerialize<0x004C5B10, Plain8, ScriptIfThenArrayClass>;
using S004C88E0 = ArraySerialize<0x004C88E0, ScriptAreas, ScriptAreaArrayClass, 1>;
using S004C9510 = ArraySerialize<0x004C9510, Placements, PartySlotArrayClass>;
using S004C9750 = ListSerialize<0x004C9750, PtrList, PtrListClass>;
using S004C9B10 = ArraySerialize<0x004C9B10, Plain16, AiLookArrayClass>;
using S0050A210 = ArraySerialize<0x0050A210, AlignedAt8Records, AlignedAt8ArrayClass, 1>;
using S0050DC20 = ListSerialize<0x0050DC20, PtrList, BoneListClass>;
using S0050DE90 = ListSerialize<0x0050DE90, PtrList, AnimationStateListClass>;
using S0050FFD0 = ArraySerialize<0x0050FFD0, Plain20, EffectArrayClass>;
using S00511E20 = ArraySerialize<0x00511E20, SoundRecords, EffectSoundArrayClass>;
using S005198D0 = ListSerialize<0x005198D0, PtrList, Object3DListClass>;
using S00519B50 = ArraySerialize<0x00519B50, Plain56, FrameArrayClass>;
using S00532E70 = ArraySerialize<0x00532E70, Plain28, MotionPointArrayClass>;
using S00533030 = ArraySerialize<0x00533030, Plain12, MotionWaypointBaseClass>;
using S005331E0 = ArraySerialize<0x005331E0, Plain4, CarriedArrayClass>;
using S00533390 = ArraySerialize<0x00533390, Plain4, IdArrayClass>;
using S00539440 = ListSerialize<0x00539440, List8, CellListClass>;
using S005396E0 = ArraySerialize<0x005396E0, PathRecords, SkinLayerArrayClass>;
using S00557270 = ArraySerialize<0x00557270, Plain8, PointArrayClass>;
using S00557430 = ArraySerialize<0x00557430, Plain40, AttackOptionArrayClass>;
using S00557E90 = ArraySerialize<0x00557E90, AnimationSteps, AnimationStepsClass>;
using S005584D0 = ListSerialize<0x005584D0, PtrList, LootListClass>;
using S00560200 = ArraySerialize<0x00560200, Plain16, CountdownArrayClass>;
using S00570570 = ListSerialize<0x00570570, PtrList, ClientObjectListClass>;
using S00570F30 = ListSerialize<0x00570F30, PtrList, ClientList2Class>;
using S00571660 = ListSerialize<0x00571660, PtrList, ClientList3Class>;
using S005991A0 = ArraySerialize<0x005991A0, Plain4, CellRowArrayClass>;
using S00599640 = ListSerialize<0x00599640, List1C, NodeListClass>;
using S005997C0 = ArraySerialize<0x005997C0, Plain8, AreaListClass>;
using S0059B0D0 = ArraySerialize<0x0059B0D0, Plain4, PartyUnitArrayClass>;
using S0059B2C0 = ListSerialize<0x0059B2C0, List8, ExperienceListClass>;
using S0059E0C0 = ArraySerialize<0x0059E0C0, Plain8, SectorOccluderArrayClass>;
using S005C1900 = ArraySerialize<0x005C1900, Plain4, SectorCellArrayClass>;
using S005C1AB0 = ArraySerialize<0x005C1AB0, Plain8, SpiralCellArrayClass>;
using S005C4D80 = ArraySerialize<0x005C4D80, Plain8, SmoothRowArrayClass>;
using S005C5070 = ArraySerialize<0x005C5070, Plain20, SmoothEdgeArrayClass>;
using S005C5360 = ArraySerialize<0x005C5360, Plain8, SmoothCopyArrayClass>;
using S005C5670 = ArraySerialize<0x005C5670, Plain36, SmoothStepArrayClass>;
using S005D9610 = ArraySerialize<0x005D9610, AiLookRecords, VtableAt<0x0073FF74>, 1>;
using S005EBC00 = ArraySerialize<0x005EBC00, Plain4, UnitPtrArrayClass>;
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
    S004C29E0::Startup();
    S004C4560::Startup();
    S004C4860::Startup();
    S004C4B50::Startup();
    S004C4E40::Startup();
    S004C5B10::Startup();
    S004C88E0::Startup();
    S004C9510::Startup();
    S004C9750::Startup();
    S004C9B10::Startup();
    S0050A210::Startup();
    S0050DC20::Startup();
    S0050DE90::Startup();
    S0050FFD0::Startup();
    S00511E20::Startup();
    S005198D0::Startup();
    S00519B50::Startup();
    S00532E70::Startup();
    S00533030::Startup();
    S005331E0::Startup();
    S00533390::Startup();
    S00539440::Startup();
    S005396E0::Startup();
    S00557270::Startup();
    S00557430::Startup();
    S00557E90::Startup();
    S005584D0::Startup();
    S00560200::Startup();
    S00570570::Startup();
    S00570F30::Startup();
    S00571660::Startup();
    S005991A0::Startup();
    S00599640::Startup();
    S005997C0::Startup();
    S0059B0D0::Startup();
    S0059B2C0::Startup();
    S0059E0C0::Startup();
    S005C1900::Startup();
    S005C1AB0::Startup();
    S005C4D80::Startup();
    S005C5070::Startup();
    S005C5360::Startup();
    S005C5670::Startup();
    S005D9610::Startup();
    S005EBC00::Startup();
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
EI_VERIFY(0x004C29E0, S004C29E0::Run, 5, S004C29E0::original);
EI_VERIFY(0x004C4560, S004C4560::Run, 5, S004C4560::original);
EI_VERIFY(0x004C4860, S004C4860::Run, 5, S004C4860::original);
EI_VERIFY(0x004C4B50, S004C4B50::Run, 5, S004C4B50::original);
EI_VERIFY(0x004C4E40, S004C4E40::Run, 5, S004C4E40::original);
EI_VERIFY(0x004C5B10, S004C5B10::Run, 5, S004C5B10::original);
EI_VERIFY(0x004C88E0, S004C88E0::Run, 5, S004C88E0::original);
EI_VERIFY(0x004C9510, S004C9510::Run, 5, S004C9510::original);
EI_VERIFY(0x004C9750, S004C9750::Run, 6, S004C9750::original);
EI_VERIFY(0x004C9B10, S004C9B10::Run, 5, S004C9B10::original);
EI_VERIFY(0x0050A210, S0050A210::Run, 5, S0050A210::original);
EI_VERIFY(0x0050DC20, S0050DC20::Run, 6, S0050DC20::original);
EI_VERIFY(0x0050DE90, S0050DE90::Run, 6, S0050DE90::original);
EI_VERIFY(0x0050FFD0, S0050FFD0::Run, 5, S0050FFD0::original);
EI_VERIFY(0x00511E20, S00511E20::Run, 5, S00511E20::original);
EI_VERIFY(0x005198D0, S005198D0::Run, 6, S005198D0::original);
EI_VERIFY(0x00519B50, S00519B50::Run, 5, S00519B50::original);
EI_VERIFY(0x00532E70, S00532E70::Run, 5, S00532E70::original);
EI_VERIFY(0x00533030, S00533030::Run, 5, S00533030::original);
EI_VERIFY(0x005331E0, S005331E0::Run, 5, S005331E0::original);
EI_VERIFY(0x00533390, S00533390::Run, 5, S00533390::original);
EI_VERIFY(0x00539440, S00539440::Run, 8, S00539440::original);
EI_VERIFY(0x005396E0, S005396E0::Run, 5, S005396E0::original);
EI_VERIFY(0x00557270, S00557270::Run, 5, S00557270::original);
EI_VERIFY(0x00557430, S00557430::Run, 5, S00557430::original);
EI_VERIFY(0x00557E90, S00557E90::Run, 5, S00557E90::original);
EI_VERIFY(0x005584D0, S005584D0::Run, 6, S005584D0::original);
EI_VERIFY(0x00560200, S00560200::Run, 5, S00560200::original);
EI_VERIFY(0x00570570, S00570570::Run, 6, S00570570::original);
EI_VERIFY(0x00570F30, S00570F30::Run, 6, S00570F30::original);
EI_VERIFY(0x00571660, S00571660::Run, 6, S00571660::original);
EI_VERIFY(0x005991A0, S005991A0::Run, 5, S005991A0::original);
EI_VERIFY(0x00599640, S00599640::Run, 5, S00599640::original);
EI_VERIFY(0x005997C0, S005997C0::Run, 5, S005997C0::original);
EI_VERIFY(0x0059B0D0, S0059B0D0::Run, 5, S0059B0D0::original);
EI_VERIFY(0x0059B2C0, S0059B2C0::Run, 8, S0059B2C0::original);
EI_VERIFY(0x0059E0C0, S0059E0C0::Run, 5, S0059E0C0::original);
EI_VERIFY(0x005C1900, S005C1900::Run, 5, S005C1900::original);
EI_VERIFY(0x005C1AB0, S005C1AB0::Run, 5, S005C1AB0::original);
EI_VERIFY(0x005C4D80, S005C4D80::Run, 5, S005C4D80::original);
EI_VERIFY(0x005C5070, S005C5070::Run, 5, S005C5070::original);
EI_VERIFY(0x005C5360, S005C5360::Run, 5, S005C5360::original);
EI_VERIFY(0x005C5670, S005C5670::Run, 5, S005C5670::original);
EI_VERIFY(0x005D9610, S005D9610::Run, 5, S005D9610::original);
EI_VERIFY(0x005EBC00, S005EBC00::Run, 5, S005EBC00::original);
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
