// CWorldServer::CreateUnits (0058EF20): creates units from unit records (script AddUnitToServer,
// the party entering a zone, mercenaries), places them, gives them their equipment and AI, and
// adds the heroes to the "Heroes" script group. See world.h and docs/notes/world.md (seventh round).
#include "world.h"
#include "game_strings.h"

#include "chunk_stream.h"
#include "core_maths.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "originals_ai.h"
#include "originals_library.h"
#include "originals_units.h"
#include "originals_world.h"
#include "originals_effects.h"
#include "originals_script.h"

#include <cstring>

// The placement maths keeps the original's operation order: no reassociation in this file.
#pragma float_control(precise, on, push)

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

// ---- Data -------------------------------------------------------------------------------------

constexpr std::uintptr_t kStartZone = 0x00790950;          // "zone1"
constexpr std::uintptr_t kScriptBegin = 0x00790940;        // "WorldScript("
constexpr std::uintptr_t kAddToHeroes = 0x00790914;        // AddObject( Heroes, GetObjectByID( "%i" ) )
constexpr std::uintptr_t kHeroAnimation = 0x007908DC;      // PlayAnimation(GetObjectByName( "hero" ),"uspecial25")
constexpr std::uintptr_t kScriptEnd = 0x0078D71C;          // ")"
constexpr std::uintptr_t kHairFormat = 0x0078FB0C;         // "hr.%.2d"
constexpr std::uintptr_t kHairPart = 0x0078FB38;           // "hr"

// The figure parts of a hero ("hd", "bd", "hp", "hr", "lh1" ... "rl3"); "hr" becomes "hr.NN" with
// the prototype's hair style.
constexpr std::uintptr_t kHeroParts[] = {0x0078AD74, 0x0078AD38, 0x007900E4, 0x0078FB38, 0x007900E0, 0x0078AD70,
                                         0x007900DC, 0x007900D8, 0x0078AD6C, 0x0078F95C, 0x007900D4, 0x0078AD68,
                                         0x0078B008, 0x007900D0, 0x0078AD64, 0x0078B004};

// Floats read from the exe's memory (rule 9).
float StartX() { return GameFloat(0x0073FA9C); }       // the hero's start on zone1
float StartY() { return GameFloat(0x0073FAA0); }
float StartRadius2() { return GameFloat(0x0073FA98); }
float AxisEpsilon() { return GameFloat(0x0073BAE0); }
float One() { return GameFloat(0x0073B84C); }
float Zero() { return GameFloat(0x0073B858); }
float AngleOffset() { return GameFloat(0x0073F8EC); }
float LineSpacing() { return GameFloat(0x0073CDF4); }

constexpr int kTypeHero = 0x32;                        // the map objects manager's type of hero prototypes
constexpr int kKindSinglePlayer = 1;
constexpr int kKindNoRangeTest = 3;
constexpr int kGameResultLost = 2;
constexpr std::uintptr_t kItemRecordVtable = 0x0073FA88;
constexpr std::uintptr_t kMessageVtable = 0x0073C6E0;

// The 0x784-byte unit record (unit_record.h has the builder; only what is read here).
constexpr int kRecordController = 0x00;   // handed to the unit's slot 61
constexpr int kRecordName = 0x08;         // CString
constexpr int kRecordC = 0x0C;            // CString
constexpr int kRecord10 = 0x10;           // CString
constexpr int kRecordPrototype = 0x14;    // CString: the prototype's name
constexpr int kRecord18 = 0x18;
constexpr int kRecordId = 0x1C;
constexpr int kRecordStats = 0x20;
constexpr int kRecordColour = 0x724;      // three dwords
constexpr int kRecordItems = 0x730;       // pointer array of item records, count at +4
constexpr int kRecordWorn = 0x740;
constexpr int kRecordQuick = 0x750;
constexpr int kRecordSpells = 0x760;
constexpr int kRecord770 = 0x770;
constexpr int kRecord774 = 0x774;
constexpr int kRecordLeader = 0x77C;
constexpr int kRecord780 = 0x780;

// The placement of one unit (0x28 bytes): id, position, rotation, two values.
struct Placement {
    int id;
    std::uint32_t position[3];
    std::uint32_t rotation[4];
    std::uint32_t value20;    // +0x20
    std::uint32_t value24;    // +0x24
};
static_assert(sizeof(Placement) == 0x28);
struct PlacementArray {
    void* vtable;
    Placement* data;
    int size;
};

struct RecordArray {
    void* vtable;
    std::uint8_t** data;
    int size;
};

// The unit creation message 005927C0 reads (0x324 bytes; constructor 0046C010, vtable 0073C6E0).
// Fields written here: +0x18 id, +0x20 position, +0x2C rotation, +0x48, +0x50 type, +0x54 / +0x70 /
// +0x74 / +0x20C CStrings, +0x60 three dwords, +0x6D / +0x6E bytes, +0x78 a CStringArray of figure
// parts, +0x90 the first of five 0x4C-byte records (+0x90, +0xB8, +0xC8).
constexpr std::size_t kMessageSize = 0x324;

// ---- Callees not rewritten (EI_ORIGINAL forwarders; __thiscall called as __fastcall) ----------

__declspec(naked) void* __fastcall MessageConstruct(void*, void*) EI_ORIGINAL(0x0046C010)
__declspec(naked) void __fastcall MessageDestructBase(void*, void*) EI_ORIGINAL(0x0046AEB0)
void __fastcall StringArraySetAtGrowText(void* a0, void*, int a2, const char* a3) { StringArraySetAtGrowTextOriginal(a0, nullptr, a2, a3); }
void __fastcall StringArraySetAtGrow(void* a0, void*, int a2, const mfc::CString* a3) { StringArraySetAtGrowStringOriginal(a0, nullptr, a2, a3); }
// 006E859F. `eh vector destructor iterator` (array, element size, count, destructor).
void __stdcall VectorDestruct(void* a0, unsigned a1, int a2, void* a3) { EhVectorDestructorIterator(a0, a1, a2, a3); }
constexpr std::uintptr_t kMessagePartDestructor = 0x0046C290;
// 00513240 (RET 4). The map objects manager's type of a prototype name.
// The prototype table's name map (007AF364): an STLport string key (0040C5A0 / 0040C720), the
// pair (005729D0 builds {key, value}) and the insert (00514880, returns the stored pair).
struct RawString {
    std::uint8_t bytes[12];
};
static RawString* RawStringInit(RawString* s, void*, const char* text, void* alloc) { return static_cast<RawString*>(::StringInitFromChars(s, nullptr, text, alloc)); }
__declspec(naked) void* __fastcall NamePairBuild(void* /*out*/, void*, const RawString*, void* const* /*value*/)
    EI_ORIGINAL(0x005729D0)
__declspec(naked) std::uint8_t* __fastcall NameMapInsert(void* /*map*/, void*, const void* /*pair*/) EI_ORIGINAL(0x00514880)
// 005927C0 (RET 4). Creates the unit described by the message; returns its id. Patched by Rebirth:
// original address.
__declspec(naked) int __fastcall CreateObjectAt(CWorldServer*, void*, void* /*message*/) EI_ORIGINAL(0x005927C0)
// _CIacos (006E7320): argument and result in ST0.
__declspec(naked) double __cdecl CIacos(double) {
    __asm fld qword ptr [esp + 4]
    __asm mov eax, 0x006E7320
    __asm call eax
    __asm ret
}
// The unit's AI brain (controller + 0x980).
void __fastcall BrainClearModes(void* a0, void*) { BrainClearModesOriginal(a0); }
void* __fastcall PlayerModeConstruct(void* a0, void*) { return ModePlayerConstructOriginal(a0); }
void __fastcall BrainAddMode(void* a0, void*, void* a2) { BrainAddModeOriginal(a0, nullptr, a2); }
__declspec(naked) void __fastcall BrainStopAction(void*, void*) EI_ORIGINAL(0x005D7790)
__declspec(naked) void* __fastcall BrainFindFollowMode(void*, void*) EI_ORIGINAL(0x005D5E60)
__declspec(naked) void __fastcall FollowModeSetLeader(void*, void*, void* /*brain*/, CEngineObject*) EI_ORIGINAL(0x005D3DE0)
// Placement on the maps.
__declspec(naked) bool __fastcall AIMapFindFreeCellNear(void*, void*, CEngineObject*, const Vector3*, Vector3*)
    EI_ORIGINAL(0x005BA9D0)
__declspec(naked) bool __fastcall AIMapCellFlag(void*, void*, int, const Vector3*) EI_ORIGINAL(0x005BA790)
void __fastcall ControllerReposition(void* a0, void*) { ControllerRepositionOriginal(a0); }
// The unit's setup.
inline void __fastcall UnitSetController(CEngineObject* a0, void*, void* a2) { UnitSetControllerOriginal((void*)a0, nullptr, a2); }
__declspec(naked) void __fastcall UnitSet780(CEngineObject*, void*, std::uint32_t) EI_ORIGINAL(0x0052E9B0)
__declspec(naked) void __fastcall UnitRefreshStats(CEngineObject*, void*) EI_ORIGINAL(0x0050F080)
__declspec(naked) void __fastcall UnitAfterStats(CEngineObject*, void*) EI_ORIGINAL(0x005315B0)
void __fastcall UnitGiveItem(CEngineObject* a0, void*, CEngineObject* a2) { UnitAddWeaponOriginal((void*)a0, nullptr, (void*)a2); }
void __fastcall UnitAddQuickItem(CEngineObject* a0, void*, CEngineObject* a2) { UnitEquipArmourOriginal((void*)a0, nullptr, (void*)a2); }
__declspec(naked) void __fastcall UnitWear(CEngineObject*, void*, CEngineObject*) EI_ORIGINAL(0x00531180)
__declspec(naked) void __fastcall UnitAddSpell(CEngineObject*, void*, CEngineObject*) EI_ORIGINAL(0x00531820)
__declspec(naked) void __fastcall UnitSet18(CEngineObject*, void*, std::uint32_t) EI_ORIGINAL(0x005308D0)
__declspec(naked) void __fastcall UnitSetPose(CEngineObject*, void*, std::uint32_t, std::uint32_t) EI_ORIGINAL(0x0052EB10)
// Items: the 12-byte record at item + 0x1C.
__declspec(naked) void* __fastcall RefValueConstruct(void*, void*) EI_ORIGINAL(0x00419720)
__declspec(naked) void __fastcall RefPtrAssign(void* /*slot*/, void*, void* /*object*/) EI_ORIGINAL(0x00419CD0)
// 004D23A0 (RET 8). Queues a script command line on the first command queue.
int __fastcall CommandQueueAdd(void* a0, void*, const mfc::CString* a2, int a3) { return ScriptLoadLine(a0, nullptr, a2, a3); }

// Vtable slots.
constexpr int kSlotMapObjects = 19;        // world: the map objects manager (+0x40)
constexpr int kSlotCreateItem = 24;        // world (00594CE0): an item from an item record
constexpr int kSlotFindObject = 40;
constexpr int kSlotUnitController = 61;    // unit (+0xF4)
constexpr int kSlotUnitStats = 35;         // unit (+0x8C)
constexpr int kSlotUnitReady = 37;         // unit (+0x94)
constexpr int kSlotUnitCommit = 33;        // unit (+0x84)
constexpr int kSlotControllerAnimate = 25; // controller (+0x64)

// ---- The creation message ---------------------------------------------------------------------

// The inlined destructor of the message: its own members, then the base's (0046AEB0).
void DestroyMessage(std::uint8_t* message) {
    *reinterpret_cast<void**>(message) = reinterpret_cast<void*>(kMessageVtable);
    static constexpr int kPartNameArrays[] = {0x30C, 0x2F8, 0x2E4, 0x2D0, 0x2BC};
    for (int offset : kPartNameArrays) StringArrayDestruct(message + offset);
    reinterpret_cast<mfc::CString*>(message + 0x20C)->~CString();
    VectorDestruct(message + 0x90, 0x4C, 5, reinterpret_cast<void*>(kMessagePartDestructor));
    MessageDestructBase(message, nullptr);
}

// The hero's figure parts, "hr" replaced by its numbered style (none when the style is -1).
void FillHeroParts(std::uint8_t* message, const char* prototypeName, mfc::CString& scratch) {
    RawString key;
    char allocator;
    RawStringInit(&key, nullptr, prototypeName, &allocator);
    void* none = nullptr;
    alignas(4) std::uint8_t pair[0x10];
    NamePairBuild(pair, nullptr, &key, &none);
    std::uint8_t* stored = NameMapInsert(reinterpret_cast<void*>(0x007AF364), nullptr, pair);
    StlStringDestroy(pair);
    std::uint8_t* prototype = At<std::uint8_t*>(stored, 0xC);
    StlStringDestroy(&key);
    int hairStyle = prototype ? At<int>(prototype, 0x10) : 1;

    void* parts = message + 0x78;
    for (std::uintptr_t address : kHeroParts) {
        const char* part = reinterpret_cast<const char*>(address);
        if (std::strcmp(part, reinterpret_cast<const char*>(kHairPart)) == 0) {
            if (hairStyle != -1) {
                scratch.Format(reinterpret_cast<const char*>(kHairFormat), hairStyle);
                StringArraySetAtGrow(parts, nullptr, At<int>(message, 0x80), &scratch);
            }
        } else {
            StringArraySetAtGrowText(parts, nullptr, At<int>(message, 0x80), part);
        }
    }
}

// The place of the `index`th unit when no placement is given: a line from `position` along the
// rotation's heading.
void PlaceInLine(std::uint8_t* message, const Vector3* position, const Float4& rotation, int index) {
    float rx, ry, rz, rw;
    std::memcpy(&rx, &rotation.bits[0], 4);
    std::memcpy(&ry, &rotation.bits[1], 4);
    std::memcpy(&rz, &rotation.bits[2], 4);
    std::memcpy(&rw, &rotation.bits[3], 4);
    double axis = static_cast<double>(ry) * ry + static_cast<double>(rz) * rz + static_cast<double>(rw) * rw;
    double angle;
    if (!(axis > AxisEpsilon())) {
        angle = Zero();
    } else {
        float axisStored = StoreFloat(static_cast<float>(axis));
        angle = CIacos(rx);
        angle = angle + angle;
        double sign = One() / x87::fsqrt(axisStored) * rw;
        if (!(sign >= Zero())) angle = -angle;
    }
    angle = angle + AngleOffset();

    float zStep = 0.0f;
    std::memcpy(message + 0x20, position, 12);
    std::memcpy(message + 0x2C, rotation.bits, 16);
    float cosine = StoreFloat(static_cast<float>(x87::fcos(angle)));
    double sine = x87::fsin(angle);
    float step = StoreFloat(static_cast<float>(index));
    double dy = sine * step;
    float dx = StoreFloat(static_cast<float>(step * static_cast<double>(cosine)));
    float dz = StoreFloat(static_cast<float>(static_cast<double>(zStep) * LineSpacing()));
    dy = dy * LineSpacing();
    float& x = At<float>(message, 0x20);
    float& y = At<float>(message, 0x24);
    float& z = At<float>(message, 0x28);
    x = StoreFloat(static_cast<float>(static_cast<double>(dx) * LineSpacing() + x));
    y = StoreFloat(static_cast<float>(y + dy));
    z = StoreFloat(static_cast<float>(static_cast<double>(z) + dz));
}

// An item from an item record, with its 12-byte record (+0x1C) holding the item record's +0xC.
// `refValueBase`: the record is built through the reference value constructor (00419720) and the
// vtable stored after it, instead of 0058FBB0.
CEngineObject* MakeItem(CWorldServer* world, std::uint8_t* itemRecord, bool refValueBase) {
    auto* item = ei::VCall<CEngineObject*>(world, kSlotCreateItem, itemRecord);
    void* memory = gamecrt::operator_new(0xC);
    At<std::uint32_t>(memory, 4) = 0;   // written before the null test
    void* record = nullptr;
    if (memory) {
        if (refValueBase) {
            RefValueConstruct(memory, nullptr);
            *static_cast<void**>(memory) = reinterpret_cast<void*>(kItemRecordVtable);
            record = memory;
        } else {
            record = ItemRecordConstruct(memory, nullptr);
        }
    }
    At<std::uint32_t>(record, 8) = At<std::uint32_t>(itemRecord, 0xC);
    RefPtrAssign(&At<std::uint8_t>(item, 0x1C), nullptr, record);
    return item;
}

} // namespace

void CWorldServer::CreateUnits(void* player, void* recordsArg, const Vector3* position, Float4 rotation,
                               void* placementsArg) {
    auto* records = static_cast<RecordArray*>(recordsArg);
    auto* placements = static_cast<PlacementArray*>(placementsArg);
    // Destroyed after the two strings (declared after it), as the original does.
    struct Message {
        alignas(4) std::uint8_t bytes[kMessageSize];
        Message() { MessageConstruct(bytes, nullptr); }
        ~Message() { DestroyMessage(bytes); }
    } holder;
    std::uint8_t* message = holder.bytes;
    mfc::CString script;
    mfc::CString line;

    // A hero created next to the start of zone1 plays its arrival animation.
    bool atStart = false;
    if (gamecrt::strnicmp(mapName_, reinterpret_cast<const char*>(kStartZone), 5) == 0) {
        double dz = position->z;
        double dy = static_cast<double>(position->y) - StartY();
        double dx = static_cast<double>(position->x) - StartX();
        double distance = dx * dx + dy * dy + dz * dz;
        atStart = !(distance >= StartRadius2());
    }
    script = reinterpret_cast<const char*>(kScriptBegin);

    for (int i = 0; i < records->size; ++i) {
        std::uint8_t* record = records->data[i];
        *reinterpret_cast<mfc::CString*>(message + 0x20C) = At<mfc::CString>(record, kRecordPrototype);
        *reinterpret_cast<mfc::CString*>(message + 0x54) = At<mfc::CString>(record, kRecordName);
        At<int>(message, 0x90) = 4;
        At<int>(message, 0xC8) = 1;
        At<int>(message, 0xB8) = 0;
        At<int>(message, 0x48) = 5;
        std::memcpy(message + 0x60, record + kRecordColour, 12);
        message[0x6D] = 1;
        message[0x6E] = 0;
        *reinterpret_cast<mfc::CString*>(message + 0x70) = At<mfc::CString>(record, kRecordC);
        *reinterpret_cast<mfc::CString*>(message + 0x74) = At<mfc::CString>(record, kRecord10);
        StringArraySetSize(message + 0x78, nullptr, 0, -1);
        const char* prototypeName = At<const char*>(record, kRecordPrototype);
        int type = PrototypeClass(ei::VCall<void*>(this, kSlotMapObjects), nullptr, prototypeName);
        At<int>(message, 0x50) = type;
        if (type == kTypeHero) FillHeroParts(message, prototypeName, line);

        int id = At<int>(record, kRecordId);
        At<int>(message, 0x18) = id;
        if (ei::VCall<void*>(this, kSlotFindObject, id)) WorldRemoveObject(this, nullptr, At<int>(message, 0x18));

        Placement* placement = nullptr;
        if (placements) {
            for (int p = 0; p < placements->size; ++p) {
                if (placements->data[p].id == At<int>(message, 0x18)) {
                    placement = &placements->data[p];
                    break;
                }
            }
        }
        if (placement) {
            std::memcpy(message + 0x20, placement->position, 12);
            std::memcpy(message + 0x2C, placement->rotation, 16);
        } else {
            PlaceInLine(message, position, rotation, i);
        }

        CEngineObject* unit = WorldFindUnit(this, nullptr, CreateObjectAt(this, nullptr, message));
        if (!unit) continue;

        // A player-controlled AI, standing on a free cell near the requested position.
        void* controller = At<void*>(unit, 0x240);
        void* brain = static_cast<std::uint8_t*>(controller) + 0x980;
        BrainClearModes(brain, nullptr);
        void* memory = gamecrt::operator_new(0x24);
        At<std::uint32_t>(memory, 4) = 0;   // written before the null test
        BrainAddMode(brain, nullptr, memory ? PlayerModeConstruct(memory, nullptr) : nullptr);
        BrainStopAction(brain, nullptr);
        SectorMapRemove(AsSectorMap(sectorMap_), nullptr, unit);
        Vector3 free;
        std::memcpy(&free, &At<Vector3>(unit, 0x1C), sizeof(free));
        AIMapFindFreeCellNear(aiMap_, nullptr, unit, position, &free);
        std::memcpy(&At<Vector3>(unit, 0x1C), &free, sizeof(free));
        SectorMapAdd(AsSectorMap(sectorMap_), nullptr, unit);

        UnitSetController(unit, nullptr, player);
        ei::VCall<void>(unit, kSlotUnitController, At<std::uint32_t>(record, kRecordController));
        UnitSet780(unit, nullptr, At<std::uint32_t>(record, kRecord780));
        UnitRefreshStats(unit, nullptr);
        ei::VCall<void>(unit, kSlotUnitStats, record + kRecordStats);
        UnitAfterStats(unit, nullptr);

        // Equipment: carried items, then the controller's slot 25, quick items, worn items
        // (two fields copied from the record), spells.
        for (int n = 0; n < At<int>(record, kRecordItems + 4); ++n)
            UnitGiveItem(unit, nullptr, MakeItem(this, At<std::uint8_t**>(record, kRecordItems)[n], false));
        ei::VCall<void>(At<void*>(unit, 0x240), kSlotControllerAnimate, 0);
        for (int n = 0; n < At<int>(record, kRecordQuick + 4); ++n)
            UnitAddQuickItem(unit, nullptr, MakeItem(this, At<std::uint8_t**>(record, kRecordQuick)[n], true));
        for (int n = 0; n < At<int>(record, kRecordWorn + 4); ++n) {
            CEngineObject* item = MakeItem(this, At<std::uint8_t**>(record, kRecordWorn)[n], true);
            At<std::uint32_t>(item, 0x48) = At<std::uint32_t>(At<std::uint8_t**>(record, kRecordWorn)[n], 0x48);
            At<std::uint32_t>(item, 0x28) = At<std::uint32_t>(item, 0x2C);
            UnitWear(unit, nullptr, item);
        }
        for (int n = 0; n < At<int>(record, kRecordSpells + 4); ++n)
            UnitAddSpell(unit, nullptr, MakeItem(this, At<std::uint8_t**>(record, kRecordSpells)[n], true));

        ei::VCall<void>(unit, kSlotUnitReady);
        ei::VCall<void>(unit, kSlotUnitCommit, 0);
        UnitSet18(unit, nullptr, At<std::uint32_t>(record, kRecord18));
        line.Format(reinterpret_cast<const char*>(kAddToHeroes), unit->id_);
        script += line;
        if (unknown12D64_ == kKindSinglePlayer && !atStart)
            At<std::uint32_t>(unit, 0x268) = At<std::uint32_t>(record, kRecord770);
        if (placement) {
            bool flagged = AIMapCellFlag(aiMap_, nullptr, 1, &At<Vector3>(unit, 0x1C));
            if (flagged) UnitSetPose(unit, nullptr, placement->value20, placement->value20);
            else At<std::uint32_t>(unit, 0x268) = placement->value20;
        }
        ControllerReposition(controller, nullptr);
        if (unknown12D64_ == kKindSinglePlayer || unknown12D64_ == kKindNoRangeTest)
            At<std::uint32_t>(unit, 0x270) = At<std::uint32_t>(record, kRecord774);
        if (placement && unknown12D64_ != kKindNoRangeTest) At<std::uint32_t>(unit, 0x270) = placement->value24;
    }

    // Followers follow their leaders.
    if (unknown12D64_ == kKindSinglePlayer) {
        for (int i = 0; i < records->size; ++i) {
            std::uint8_t* record = records->data[i];
            CEngineObject* unit = WorldFindUnit(this, nullptr, At<int>(record, kRecordId));
            if (!unit) continue;
            void* controller = At<void*>(unit, 0x240);
            if (At<int>(record, kRecordLeader) <= 0) continue;
            void* brain = static_cast<std::uint8_t*>(controller) + 0x980;
            void* follow = BrainFindFollowMode(brain, nullptr);
            if (!follow) continue;
            FollowModeSetLeader(follow, nullptr, brain, WorldFindUnit(this, nullptr, At<int>(record, kRecordLeader)));
        }
    }

    if (atStart && gameResult_ != kGameResultLost) script += reinterpret_cast<const char*>(kHeroAnimation);
    script += reinterpret_cast<const char*>(kScriptEnd);
    if (player) CommandQueueAdd(commandsA_, nullptr, &script, 1);
}

// ---- UseQuickItem -------------------------------------------------------------------------------

namespace {

constexpr std::uintptr_t kQuickEffectPrototype = 0x007908D0;   // "st.tr.130"
constexpr std::uintptr_t kQuickEffectModel = 0x007908C8;       // "unhuma"
constexpr std::uintptr_t kQuickEffectVtable = 0x0073FA58;      // the base message (0046A5C0) with no members of its own
constexpr std::size_t kBaseMessageSize = 0x8C;
constexpr int kQuickEffectType = 9;
constexpr std::uint32_t kQuickEffectFlags = 0x3010;
constexpr std::uint32_t kFlagsNewEffect = 0x180000;            // 00529D20

__declspec(naked) void* __fastcall BaseMessageConstruct(void*, void*) EI_ORIGINAL(0x0046A5C0)

} // namespace

void CWorldServer::UseQuickItem(void* /*item*/, CEngineObject* unit) {
    alignas(4) std::uint8_t message[kBaseMessageSize];
    BaseMessageConstruct(message, nullptr);
    At<std::uintptr_t>(message, 0) = kQuickEffectVtable;
    At<int>(message, 0x48) = kQuickEffectType;
    At<mfc::CString>(message, 0x54) = reinterpret_cast<const char*>(kQuickEffectPrototype);
    At<int>(message, 0x18) = -1;
    At<mfc::CString>(message, 0x70) = reinterpret_cast<const char*>(kQuickEffectModel);
    At<std::uint32_t>(message, 0x50) = kQuickEffectFlags;
    At<int>(message, 0x64) = 0;
    // At the unit's position, on the ground.
    std::memcpy(message + 0x20, reinterpret_cast<std::uint8_t*>(unit) + 0x1C, 12);
    At<std::uint32_t>(message, 0x28) = 0;
    int id = CreateObjectAt(this, nullptr, message);
    CEngineObject* effect = ei::VCall<CEngineObject*>(this, kSlotFindObject, id);
    effect->flags_ |= kFlagsNewEffect;   // 00529D20, inlined
    MessageDestructBase(message, nullptr);
}

#pragma float_control(pop)

// ---- CreateObject (005927C0) -------------------------------------------------------------------

namespace {

// Object setup, through the original addresses (thiscall as fastcall).
__declspec(naked) void __fastcall ObjectMarkNew(CEngineObject*, void*) EI_ORIGINAL(0x0050EA40)
__declspec(naked) void __fastcall ObjectAnnounce(CEngineObject*, void*) EI_ORIGINAL(0x0050EA70)
__declspec(naked) void __fastcall ObjectSetMovement(CEngineObject*, void*, const void*) EI_ORIGINAL(0x00517700)
__declspec(naked) void __fastcall ObjectSetName(CEngineObject*, void*, const char*) EI_ORIGINAL(0x005179E0)
__declspec(naked) void __fastcall ObjectSetSecondName(CEngineObject*, void*, const char*) EI_ORIGINAL(0x0050F150)
__declspec(naked) void __fastcall ManagerRegister(void*, void*, CEngineObject*) EI_ORIGINAL(0x00513630)
__declspec(naked) void __fastcall ManagerRegisterUnit(void*, void*, CEngineObject*, const char*) EI_ORIGINAL(0x00513B60)
__declspec(naked) void __fastcall ManagerRegisterUnit2(void*, void*, CEngineObject*, const char*) EI_ORIGINAL(0x005141F0)
__declspec(naked) void __fastcall ServerCastSpell(CWorldServer*, void*, CEngineObject*, int, const void*, CEngineObject*,
                                                  const void*, int) EI_ORIGINAL(0x0058E850)
__declspec(naked) CEngineObject* __fastcall SpellsFromText(void*, void*, const char*) EI_ORIGINAL(0x00689030)
inline void __fastcall TextAssignChars(mfc::CString* s, void*, const char* t) { CStringAssignTextOriginal(s, nullptr, t); }
void __fastcall TextAssign(mfc::CString* a0, void*, const mfc::CString* a2) { CStringAssignOriginal((void*)a0, nullptr, (const void*)a2); }
void __fastcall TextArrayCopy(void* a0, void*, const void* a2) { StringArrayCopyOriginal(a0, nullptr, a2); }
__declspec(naked) void __fastcall UnitSetSide(CEngineObject*, void*, int) EI_ORIGINAL(0x00530120)
__declspec(naked) int __fastcall DiplomacyMaskOf(void*, void*, int) EI_ORIGINAL(0x00455660)
__declspec(naked) void __fastcall UnitSetDiplomacy(CEngineObject*, void*, int) EI_ORIGINAL(0x0052E970)
__declspec(naked) CEngineObject* __fastcall ItemsCreateItem(void*, void*, const char*) EI_ORIGINAL(0x006AC970)
__declspec(naked) void __fastcall UnitRecordApply(void*, void*, CEngineObject*) EI_ORIGINAL(0x00477B00)
__declspec(naked) std::uint8_t** __fastcall UnitPrototypeAt(void*, void*, const void*) EI_ORIGINAL(0x005335E0)
__declspec(naked) void __fastcall ObjectSetup8(CEngineObject*, void*, const void*) EI_ORIGINAL(0x0052B3F0)
__declspec(naked) void __fastcall ObjectSetup7(CEngineObject*, void*, const void*) EI_ORIGINAL(0x0052A2C0)
void __fastcall MarkNewObject(CEngineObject* a0, void*) { ObjectMarkNewOriginal((void*)a0); }

constexpr int kSlotSpellManager = 22;      // world (+0x58)
constexpr int kSlotItemManager = 18;       // world (+0x48)
constexpr int kSlotCreate = 23;            // manager (+0x5C): (type, id)
constexpr int kSlotIsLarge = 29;           // object (+0x74, AL): picks +0x11D or +0x11E
constexpr int kSlotSetRace = 34;           // object (+0x88)
constexpr int kSlotRefresh = 30;           // object (+0x78)
constexpr int kSlotFigureShape = 16;       // figure (+0x40): (0x2001, size, radius)
constexpr int kSlotFigureLight = 17;       // figure (+0x44): (size, radius * 10, colour)
constexpr int kSlotFigureFlags = 18;       // figure (+0x48)
constexpr int kTimedSound3D = 0x49, kTimedSound = 0x48, kTimedLight = 0x4B, kTimedOther = 0x4A;
const char* const kCampfire = reinterpret_cast<const char*>(0x00790A54);
const char* const kNoWaveName = reinterpret_cast<const char*>(0x00790A34);
constexpr std::uint32_t kGrey = 0x3F4CCCCD, kOne = 0x3F800000;
void* const kUnitPrototypes = reinterpret_cast<void*>(0x007AF364);
constexpr int kUnitPrototypeSize = 0x168;

int UpdateCountdown(const CEngineObject* object) { return ((reinterpret_cast<int>(object) >> 5) & 0xF) + 0x80; }

std::uint8_t* Bytes(void* p) { return static_cast<std::uint8_t*>(p); }
void Copy(void* to, const void* from, std::size_t n) {
    auto* d = static_cast<std::uint32_t*>(to);
    auto* s = static_cast<const std::uint32_t*>(from);
    for (std::size_t i = 0; i < n / 4; ++i) d[i] = s[i];
}

} // namespace

int CWorldServer::CreateObject(std::uint8_t* message) {
    std::uint8_t* m = message;
    auto created = [this, m]() {
        void* manager = ei::VCall<void*>(this, kSlotMapObjects);
        return ei::VCall<CEngineObject*>(manager, kSlotCreate, At<int>(m, 0x50), At<int>(m, 0x18));
    };
    auto allowed = [this](CEngineObject* object) {
        return (ei::VCall<bool>(object, kSlotIsLarge) ? At<std::uint8_t>(this, 0x11D) : At<std::uint8_t>(this, 0x11E)) != 0;
    };
    auto refuse = [](CEngineObject* object) {
        ++object->refCount_;
        object->Release();
        return -1;
    };
    auto companionTexts = [m](std::uint8_t* companion) {
        TextAssign(&At<mfc::CString>(companion, 0x78), nullptr, &At<mfc::CString>(m, 0x70));
        TextAssign(&At<mfc::CString>(companion, 0x7C), nullptr, &At<mfc::CString>(m, 0x74));
        TextArrayCopy(companion + 0x80, nullptr, m + 0x78);
    };
    auto race = [m](CEngineObject* object) {
        ++object->refCount_;
        ei::VCall<void>(object, kSlotSetRace, At<int>(m, 0x48) == 5 ? At<int>(m, 0x20C) : At<int>(m, 0x58));
        ObjectMarkNew(object, nullptr);
    };
    auto place = [m](CEngineObject* object) {
        ObjectSetMovement(object, nullptr, m + 0x60);
        Copy(Bytes(object) + 0x1AC, m + 0x60, 12);
        ei::VCall<void>(object, kSlotRefresh);
        Copy(Bytes(object) + 0x1C, m + 0x20, 12);
        Copy(Bytes(object) + 0x34, m + 0x2C, 16);
    };
    auto timed = [this](CEngineObject* object) {
        At<int>(object, 0x94) = tick_;
        ++object->refCount_;
        MarkNewObject(object, nullptr);
        timedObjects_.Set(static_cast<unsigned>(object->id_), object);
        return object->id_;
    };
    void* timedManager = manager48_;

    switch (At<int>(m, 0x48)) {
    case 6: {   // a map object with a figure (a campfire when it stands on the ground)
        CEngineObject* object = created();
        if (!allowed(object)) return refuse(object);
        int id = object->id_;
        if (id == -1) return -1;
        if (At<int>(m, 0x18) == -1) At<int>(m, 0x18) = id;
        // |ground height + z| below 0.7 (FCOMP with C0: a NaN counts as below).
        volatile float height = At<float>(m, 0x98) + At<float>(m, 0x28);
        float sum = height;
        std::uint32_t bits;
        std::memcpy(&bits, &sum, 4);
        bits &= 0x7FFFFFFFu;
        float level;
        std::memcpy(&level, &bits, 4);
        if (x87::IsNaN(level) || level < GameFloat(0x0073C538)) {
            volatile float at[3];
            at[2] = At<float>(m, 0x98) + At<float>(m, 0x28);
            at[1] = At<float>(m, 0x94) + At<float>(m, 0x24);
            at[0] = At<float>(m, 0x90) + At<float>(m, 0x20);
            CEngineObject* spell = SpellsFromText(ei::VCall<void*>(this, kSlotSpellManager), nullptr, kCampfire);
            if (spell) ++spell->refCount_;
            ServerCastSpell(this, nullptr, spell, 0, const_cast<float*>(at), nullptr, const_cast<float*>(at), 0);
            if (spell) spell->Release();
        }
        At<CEngineObject*>(m, 0x1C) = object;
        void* figure = At<void*>(object, 0x238);
        std::uint8_t* companion = At<std::uint8_t*>(object, 0x170);
        TextAssignChars(&At<mfc::CString>(object, 0xA4), nullptr, At<const char*>(m, 0x54));
        companionTexts(companion);
        race(object);
        ei::VCall<void>(figure, kSlotFigureShape, 0x2001, m + 0x90, At<std::uint32_t>(m, 0x8C));
        std::uint32_t colour[4] = {kGrey, kGrey, kGrey, kOne};
        float radius = At<float>(m, 0x8C) * GameFloat(0x0073BAAC);
        ei::VCall<void>(figure, kSlotFigureLight, m + 0x90, radius, colour);
        ei::VCall<void>(figure, kSlotFigureFlags, At<std::uint32_t>(m, 0x9C));
        place(object);
        UnitMarkChanged(object, nullptr, 2);
        UnitMarkChanged(object, nullptr, 4);
        if (At<std::uint8_t>(m, 0x6D) == 0) object->flags_ |= 8;
        ManagerRegister(ei::VCall<void*>(this, kSlotMapObjects), nullptr, object);
        if (object->GetClassId() == world::kClassUnit) ControllerReposition(At<void*>(object, 0x240), nullptr);
        ObjectSetName(object, nullptr, At<const char*>(m, 0x54));
        id = object->id_;
        objects_.Set(static_cast<unsigned>(id), object);
        mapObjects_.Set(static_cast<unsigned>(object->id_), object);
        if (object->GetClassId() != world::kClassUnit) otherMapObjects_.Set(static_cast<unsigned>(object->id_), object);
        At<int>(object, 0x174) = UpdateCountdown(object);
        updateMap_.Set(static_cast<unsigned>(object->id_), object);
        SectorMapAdd(AsSectorMap(sectorMap_), nullptr, object);
        int result = object->id_;
        if (object->flags_ & 2) ObjectAnnounce(object, nullptr);
        return result;
    }
    case 7: {   // an object updated every tick (no texts, not in the sector map)
        CEngineObject* object = created();
        if (!allowed(object)) return refuse(object);
        int id = object->id_;
        if (id == -1) return -1;
        if (At<int>(m, 0x18) == -1) At<int>(m, 0x18) = id;
        At<CEngineObject*>(m, 0x1C) = object;
        ++object->refCount_;
        ObjectMarkNew(object, nullptr);
        UnitMarkChanged(object, nullptr, 2);
        object->flags_ |= 8;
        objects_.Set(static_cast<unsigned>(object->id_), object);
        mapObjects_.Set(static_cast<unsigned>(object->id_), object);
        if (object->GetClassId() != world::kClassUnit) otherMapObjects_.Set(static_cast<unsigned>(object->id_), object);
        At<int>(object, 0x174) = UpdateCountdown(object);
        updateMap_.Set(static_cast<unsigned>(object->id_), object);
        int result = object->id_;
        if (object->flags_ & 2) ObjectAnnounce(object, nullptr);
        ObjectSetup7(object, nullptr, m + 0x8C);
        return result;
    }
    case 1:
    case 5:
    case 8:
    case 9: {   // map objects and units (5)
        CEngineObject* object = created();
        if (At<std::uint8_t>(m, 0x6C)) object->flags_ |= 0x20000;
        if (gamecrt::mbscmp(At<const unsigned char*>(m, 0x4C), reinterpret_cast<const unsigned char*>(game::EmptyText())) != 0)
            object->flags_ |= 0x20000;
        if (!allowed(object)) return refuse(object);
        int id = object->id_;
        if (id == -1) return -1;
        if (At<int>(m, 0x18) == -1) At<int>(m, 0x18) = id;
        At<CEngineObject*>(m, 0x1C) = object;
        std::uint8_t* companion = At<std::uint8_t*>(object, 0x170);
        TextAssignChars(&At<mfc::CString>(object, 0xA4), nullptr, At<const char*>(m, 0x54));
        ObjectSetSecondName(object, nullptr, At<const char*>(m, 0x4C));
        companionTexts(companion);
        race(object);
        ei::VCall<void>(object, kSlotUnitCommit, 0);
        int type = At<int>(m, 0x48);
        if (type == 5) {
            UnitSetController(object, nullptr, nullptr);
            UnitSetSide(object, nullptr, At<std::uint8_t>(m, 0x6E));
            UnitSetDiplomacy(object, nullptr, DiplomacyMaskOf(diplomacy_, nullptr, At<std::uint8_t>(m, 0x6E)));
            ManagerRegisterUnit(ei::VCall<void*>(this, kSlotMapObjects), nullptr, object, At<const char*>(m, 0x20C));
            ManagerRegisterUnit2(ei::VCall<void*>(this, kSlotMapObjects), nullptr, object, At<const char*>(m, 0x20C));
            for (int i = 0; i < At<int>(m, 0x2D8); ++i) {
                const char* name = At<const char**>(m, 0x2D4)[i];
                UnitWear(object, nullptr, ItemsCreateItem(ei::VCall<void*>(this, kSlotItemManager), nullptr, name));
            }
            UnitRecordApply(m + 0x210, nullptr, object);
            // The prototype's six movement values (+0x2C) go to the unit's +0x4B0.
            const char* prototypeName = At<const char*>(m, 0x20C);
            std::uint8_t* prototype;
            {
                alignas(4) std::uint8_t key[12];
                char allocator;
                StringInitWithAllocator(key, nullptr, &allocator);
                StringAssignSpan(key, nullptr, prototypeName, prototypeName + std::strlen(prototypeName));
                prototype = *UnitPrototypeAt(kUnitPrototypes, nullptr, key);
                StlStringDestroy(key);
            }
            std::uint8_t* begin = *reinterpret_cast<std::uint8_t**>(0x007AF398);
            std::uint8_t* end = *reinterpret_cast<std::uint8_t**>(0x007AF39C);
            unsigned count = static_cast<unsigned>(static_cast<int>(end - begin) / kUnitPrototypeSize);
            unsigned index;
            if (prototype) {
                index = At<unsigned>(prototype, 8);
            } else {
                // Rebirth's FixAddMapObject (context hook at 00592F68): no prototype record is
                // logged ("Invalid unit's prototype") and index 0 used. The original reads +8 of
                // the null record (and crashes).
                if (!ei::rebirth::Enabled(ei::rebirth::Patch::FixAddMapObject))
                    index = *reinterpret_cast<const volatile unsigned*>(reinterpret_cast<std::uintptr_t>(prototype) + 8);
                else
                    index = 0;
            }
            std::uint8_t* record = index < count ? begin + index * kUnitPrototypeSize : nullptr;
            std::memcpy(Bytes(object) + 0x4B0, reinterpret_cast<const std::uint8_t*>(record) + 0x2C, 24);
            UnitMarkChanged(object, nullptr, 5);
            UnitMarkChanged(object, nullptr, 6);
            UnitMarkChanged(object, nullptr, 0xB);
        } else if (type == 8) {
            ObjectSetup8(object, nullptr, m + 0x8C);
            UnitMarkChanged(object, nullptr, 5);
        } else {
            ManagerRegister(ei::VCall<void*>(this, kSlotMapObjects), nullptr, object);
        }
        place(object);
        UnitMarkChanged(object, nullptr, 2);
        UnitMarkChanged(object, nullptr, 6);
        UnitMarkChanged(object, nullptr, 7);
        UnitMarkChanged(object, nullptr, 8);
        if (At<std::uint8_t>(m, 0x6D) == 0) object->flags_ |= 8;
        ObjectSetName(object, nullptr, At<const char*>(m, 0x54));
        ei::VCall<void>(object, kSlotUnitReady);
        if (object->GetClassId() == world::kClassUnit) ControllerReposition(At<void*>(object, 0x240), nullptr);
        objects_.Set(static_cast<unsigned>(object->id_), object);
        mapObjects_.Set(static_cast<unsigned>(object->id_), object);
        if (object->GetClassId() != world::kClassUnit) otherMapObjects_.Set(static_cast<unsigned>(object->id_), object);
        SectorMapAdd(AsSectorMap(sectorMap_), nullptr, object);
        int result = object->id_;
        if (object->flags_ & 2) ObjectAnnounce(object, nullptr);
        if (type >= 8 && type <= 9) {
            At<int>(object, 0x174) = UpdateCountdown(object);
            updateMap_.Set(static_cast<unsigned>(object->id_), object);
        }
        if (*At<const char*>(object, 0x1A8) != 0) namedObjects_.Set(static_cast<unsigned>(object->id_), object);
        return result;
    }
    case 3: {   // a sound
        if (!At<std::uint8_t>(this, 0x11D)) return -1;
        if (At<std::uint8_t>(m, 0x84)) {
            CEngineObject* sound = ei::VCall<CEngineObject*>(timedManager, kSlotCreate, kTimedSound3D, At<int>(m, 0x18));
            TextArrayCopy(Bytes(sound) + 0x14C, nullptr, m + 0x54);
            Copy(Bytes(sound) + 0x1C, m + 0x20, 12);
            At<float>(sound, 0x160) = static_cast<float>(At<int>(m, 0x68));
            At<float>(sound, 0x164) = static_cast<float>(At<int>(m, 0x6C));
            At<std::uint32_t>(sound, 0x168) = At<std::uint32_t>(m, 0x74);
            At<std::uint32_t>(sound, 0x16C) = At<std::uint32_t>(m, 0x78);
            return timed(sound);
        }
        CEngineObject* sound = ei::VCall<CEngineObject*>(timedManager, kSlotCreate, kTimedSound, At<int>(m, 0x18));
        if (At<int>(m, 0x5C) < 1) {
            AfxMessageBox(kNoWaveName, 0, 0);
        } else {
            // strcpy, inlined in the original (REP MOVS of the length with its terminator).
            const char* wave = *At<const char**>(m, 0x58);
            std::memcpy(Bytes(sound) + 0x14C, wave, std::strlen(wave) + 1);
        }
        Copy(Bytes(sound) + 0x1C, m + 0x20, 12);
        At<float>(sound, 0x24C) = static_cast<float>(At<int>(m, 0x68));
        At<float>(sound, 0x250) = static_cast<float>(At<int>(m, 0x6C));
        return timed(sound);
    }
    case 2: {   // a light
        if (!At<std::uint8_t>(this, 0x11D)) return -1;
        CEngineObject* light = ei::VCall<CEngineObject*>(timedManager, kSlotCreate, kTimedLight, At<int>(m, 0x18));
        Copy(Bytes(light) + 0x1C, m + 0x20, 12);
        At<std::uint32_t>(light, 0x158) = At<std::uint32_t>(m, 0x64);
        Copy(Bytes(light) + 0x14C, m + 0x54, 12);
        return timed(light);
    }
    case 4: {
        if (!At<std::uint8_t>(this, 0x11D)) return -1;
        CEngineObject* object = ei::VCall<CEngineObject*>(timedManager, kSlotCreate, kTimedOther, At<int>(m, 0x18));
        Copy(Bytes(object) + 0x1C, m + 0x20, 12);
        At<std::uint32_t>(object, 0x158) = At<std::uint32_t>(m, 0x54);
        At<std::uint32_t>(object, 0x15C) = At<std::uint32_t>(m, 0x58);
        return timed(object);
    }
    default:
        // The original returns its argument slot untouched: the message's address.
        return reinterpret_cast<int>(message);
    }
}
EI_REPLACE_METHOD(0x005927C0, CWorldServer::CreateObject);
EI_REBIRTH_IMPLEMENTS(0x005927C0, ei::rebirth::Patch::FixAddMapObject);

EI_REPLACE_METHOD(0x0058EF20, CWorldServer::CreateUnits);
EI_REPLACE_METHOD(0x0058ECE0, CWorldServer::UseQuickItem);
