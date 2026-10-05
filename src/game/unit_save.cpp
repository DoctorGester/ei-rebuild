// CUnit's Load and Save slots (vtable 0073EA8C slots 9 and 10). See unit.h and
// docs/notes/units.md ("Third round").
#include "unit.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_world.h"
#include "originals_units.h"
#include "originals_gameplay.h"

#include <cstring>

namespace {
// Fields of CUnit that unit.h leaves unnamed.
constexpr int kName = 0x238;            // CString: the unit's own name
constexpr int kPrototypeIndex = 0x18;   // index into the unit prototype table (-1 none)
constexpr int kMemory = 0x27C;          // a sub-object loaded and saved by 005C7860 / 005C79B0
constexpr int kField328 = 0x328;
constexpr int kParams = 0x330;          // UnitParams
constexpr int kBlock4C8 = 0x4C8;        // 0x14 bytes saved as they are
constexpr unsigned kBlock4C8Size = 0x14;

constexpr std::uint32_t kFlag800000 = 0x800000;     // flags_, cleared by Load
constexpr int kPartPlayer = 0xB;                    // MarkChanged part after a player change
constexpr int kWorldServerClassId = 8;              // ManagerContext()'s slot 12 for a server world

// Virtual slots.
constexpr int kControllerLoad = 9;
constexpr int kControllerSave = 10;
constexpr int kControllerPlayerChanged = 0x48 / 4;
constexpr int kUnitStatsChanged = 0x80 / 4;         // CUnit slot 32
constexpr int kContextClassId = 0x30 / 4;
constexpr int kContextItemManager = 0x48 / 4;

constexpr int kVersionField438 = 0x49;
constexpr int kVersionPrototypeNames = 0x4D;
constexpr int kVersionSecondName = 0x6A;

constexpr int kCarriedGrowMin = 4;
constexpr int kCarriedGrowMax = 0x400;

constexpr std::size_t kPrototypeSize = 0x120;
inline char*& PrototypesBegin() { return *reinterpret_cast<char**>(0x007AF358); }
inline char*& PrototypesEnd() { return *reinterpret_cast<char**>(0x007AF35C); }
void* const kPrototypeTable = reinterpret_cast<void*>(0x007AF358);
void* const kPrototypeNameMap = reinterpret_cast<void*>(0x007AF364);    // hash_map<string, record*>

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

// The record of prototype `index`, or null when out of range (the table holds 0x120-byte records
// whose first field is the name CString).
char* PrototypeAt(std::uint32_t index) {
    std::uint32_t count = static_cast<std::uint32_t>((PrototypesEnd() - PrototypesBegin()) / static_cast<int>(kPrototypeSize));
    return index < count ? PrototypesBegin() + index * kPrototypeSize : nullptr;
}

int PrototypeIndexOf(const char* record) {
    return record ? static_cast<int>((record - PrototypesBegin()) / static_cast<int>(kPrototypeSize)) : -1;
}

// Writes `value` from a stack copy, as the original does for most scalars.
template <typename T>
void WriteCopy(UnitStream* stream, T value) {
    stream->Write(&value, sizeof(T));
}

void WriteString(UnitStream* stream, const char* text) {
    stream->Write(text, static_cast<unsigned>(std::strlen(text) + 1));
}

void WriteItemIds(UnitStream* stream, CEngineObject* const* items, int count) {
    for (int i = 0; i < count; ++i) WriteCopy<int>(stream, items[i] ? items[i]->id_ : -1);
}

// A raw STLport string (12 bytes) built and destroyed by the original's routines.
struct RawString {
    void* InitFrom(const char* text, void* allocator);   // 0040C5A0
    std::uint8_t bytes[12];
};
} // namespace

void* RawString::InitFrom(const char* text, void* alloc) { return ::StringInitFromChars(this, nullptr, text, alloc); }

// unit_items.cpp: 00514930, the prototype table's record named `name`.
char* __fastcall FindUnitPrototype(const void* table, void*, const char* name);

namespace {
// Not rewritten (forwarders):
// 0050F7A0 / 0050F900. The map object part of Load / Save.
void __fastcall MapObjectLoad(CUnit* a0, void*, UnitStream* a2) { MapObjectLoadOriginal((void*)a0, nullptr, (void*)a2); }
void __fastcall MapObjectSave(CUnit* a0, void*, UnitStream* a2) { MapObjectSaveOriginal((void*)a0, nullptr, (void*)a2); }
// 005C7860 / 005C79B0. Load / Save of the sub-object at CUnit + 0x27C.
__declspec(naked) void __fastcall UnitMemoryLoad(void*, void*, UnitStream*) EI_ORIGINAL(0x005C7860)
__declspec(naked) void __fastcall UnitMemorySave(void*, void*, UnitStream*) EI_ORIGINAL(0x005C79B0)
// 00461190. Reads a zero-terminated string from the stream, one byte at a time; constructs the
// CString at `out` and returns it.
__declspec(naked) mfc::CString* __fastcall StreamReadString(UnitStream*, void*, void* /*out*/) EI_ORIGINAL(0x00461190)
// 00665B50 / 00665A70. The player loses / gains the unit.
void __fastcall PlayerRemoveUnit(void* a0, void*, CUnit* a2) { PlayerRemoveUnitOriginal(a0, nullptr, a2); }
void __fastcall PlayerAddUnit(void* a0, void*, CUnit* a2) { PlayerAddUnitOriginal(a0, nullptr, a2); }
// 005335E0. hash_map<string, record*>::operator[] (inserts a null entry for an unknown name).
char** __fastcall PrototypeNameMapAt(void* a0, void*, const RawString* a2) { return (char**)PrototypeMapAtOriginal(a0, nullptr, a2); }
// Rewritten elsewhere, called through their game addresses: 00594180 CWorldServer::GetPlayer
// (world_server.cpp), 00523550 UnitParams::Reset (unit_stats_pack.cpp), 00403F40
// CBaseManager::FindObject (base_manager.cpp).
__declspec(naked) void __fastcall UnitParamsResetAt(void* /*params*/) EI_ORIGINAL(0x00523550)

// Reads a string and assigns it to `target` (the original's temporary is destroyed at once).
void ReadStringInto(UnitStream* stream, mfc::CString& target) {
    alignas(4) unsigned char storage[sizeof(mfc::CString)];
    mfc::CString* read = StreamReadString(stream, nullptr, storage);
    target = *read;
    read->~CString();
}

// The prototype record named `name` through the name map (an unknown name is added to it).
char* PrototypeByName(const char* name) {
    RawString key;
    char allocator;
    key.InitFrom(name, &allocator);
    char* record = *PrototypeNameMapAt(kPrototypeNameMap, nullptr, &key);
    StlStringDestroy(&key);
    return record;
}

// Reads an item id and resolves it in the world's item manager (asked before each read), with a
// counted reference.
CEngineObject* ReadItem(CUnit* unit, UnitStream* stream) {
    void* manager = ei::VCall<void*>(unit->ManagerContext(), kContextItemManager);
    int id;
    stream->Read(&id, 4);
    if (id == -1) return nullptr;
    CEngineObject* item = ManagerFindObject(manager, nullptr, id);
    if (item) ++item->refCount_;
    return item;
}

void ReadItems(CUnit* unit, UnitStream* stream, CEngineObject** slots, int count) {
    for (int i = 0; i < count; ++i) slots[i] = ReadItem(unit, stream);
}

void ZeroSlots(CEngineObject** slots, int count) {
    for (int i = 0; i < count; ++i) slots[i] = nullptr;
}

// CArray::SetSize of the carried items as the original inlines it.
void SetCarriedSize(CUnit* unit, int size) {
    if (size == 0) {
        if (unit->carried) {
            gamecrt::operator_delete(unit->carried);
            unit->carried = nullptr;
        }
        unit->carriedCapacity = 0;
        unit->carriedCount = 0;
        return;
    }
    if (!unit->carried) {
        unit->carried = static_cast<CEngineObject**>(gamecrt::operator_new(static_cast<std::size_t>(size) << 2));
        ZeroSlots(unit->carried, size);
        unit->carriedCapacity = size;
        unit->carriedCount = size;
        return;
    }
    if (size <= unit->carriedCapacity) {
        if (size > unit->carriedCount) ZeroSlots(unit->carried + unit->carriedCount, size - unit->carriedCount);
        unit->carriedCount = size;
        return;
    }
    int grow = unit->carriedGrowBy;
    if (grow == 0) {
        grow = unit->carriedCount / 8;
        if (grow < kCarriedGrowMin) grow = kCarriedGrowMin;
        else if (grow > kCarriedGrowMax) grow = kCarriedGrowMax;
    }
    int capacity = grow + unit->carriedCapacity;
    if (size >= capacity) capacity = size;
    auto* items = static_cast<CEngineObject**>(gamecrt::operator_new(static_cast<std::size_t>(capacity) * 4));
    std::memcpy(items, unit->carried, static_cast<std::size_t>(unit->carriedCount) << 2);
    ZeroSlots(items + unit->carriedCount, size - unit->carriedCount);
    gamecrt::operator_delete(unit->carried);
    unit->carried = items;
    unit->carriedCount = size;
    unit->carriedCapacity = capacity;
}

// The unit's new owner: on a server world, player `id` (null when there is none).
void* PlayerById(CUnit* unit, int id) {
    void* player = nullptr;
    if (ei::VCall<int>(unit->ManagerContext(), kContextClassId) == kWorldServerClassId)
        player = WorldGetPlayer(unit->ManagerContext(), nullptr, id);
    return player;
}

void ChangePlayer(CUnit* unit, void* player) {
    void* old = unit->player;
    if (old == player) return;
    unit->player = static_cast<char*>(player);
    if (old) PlayerRemoveUnit(old, nullptr, unit);
    if (unit->player) PlayerAddUnit(unit->player, nullptr, unit);
    unit->MarkChanged(kPartPlayer);
    if (unit->controller) ei::VCall<void>(unit->controller, kControllerPlayerChanged, player);
}

void ReadFields(CUnit* unit, UnitStream* stream, int first, int last) {
    for (int offset = first; offset <= last; offset += 4) stream->Read(&At<int>(unit, offset), 4);
}
} // namespace

void CUnit::LoadUnit(UnitStream* stream) {
    MapObjectLoad(this, nullptr, stream);
    ei::VCall<void>(controller, kControllerLoad, stream);
    flags_ &= ~kFlag800000;
    ReadStringInto(stream, At<mfc::CString>(this, kName));
    stream->Read(&relationsA, 4);
    int playerId;
    stream->Read(&playerId, 4);
    void* newPlayer = PlayerById(this, playerId);

    stream->Read(&side, 4);
    stream->Read(&unknown_254, 1);
    stream->Read(&unknown_255, 1);
    ReadFields(this, stream, 0x258, 0x278);     // +0x258 .. +0x278, activeWeapon among them
    UnitMemoryLoad(reinterpret_cast<char*>(this) + kMemory, nullptr, stream);
    stream->Read(&heightOffset, 4);
    stream->Read(&sizeClass, 4);
    stream->Read(&At<int>(this, kField328), 4);
    UnitParamsResetAt(reinterpret_cast<char*>(this) + kParams);
    stream->Read(&activeWeapon, 4);             // saved twice; the second copy wins

    ReadItems(this, stream, weapons, 4);
    ReadItems(this, stream, quickItems, 4);
    ReadItems(this, stream, spells, 8);
    ReadItems(this, stream, armor, 7);
    stream->Read(reinterpret_cast<char*>(this) + kBlock4C8, kBlock4C8Size);
    ei::VCall<int>(this, kUnitStatsChanged);
    ChangePlayer(this, newPlayer);

    stream->Read(&relationsB, 4);
    int count;
    stream->Read(&count, 4);
    SetCarriedSize(this, count);
    for (int i = 0; i < count; ++i) carried[i] = ReadItem(this, stream);
    stream->Read(&standingHeight, 4);

    if (stream->version < kVersionField438) return;
    stream->Read(&unknown_438, 4);
    if (stream->version < kVersionPrototypeNames) return;

    mfc::CString name;
    ReadStringInto(stream, name);
    At<int>(this, kPrototypeIndex) = PrototypeIndexOf(PrototypeByName(name));
    char* second;
    if (stream->version < kVersionSecondName) {
        second = FindUnitPrototype(kPrototypeTable, nullptr, name);
    } else {
        ReadStringInto(stream, name);
        second = PrototypeByName(name);
    }
    prototype23C = PrototypeIndexOf(second);
}
EI_REPLACE_METHOD(0x00531A80, CUnit::LoadUnit);

void CUnit::SaveUnit(UnitStream* stream) {
    MapObjectSave(this, nullptr, stream);
    ei::VCall<void>(controller, kControllerSave, stream);
    WriteString(stream, At<mfc::CString>(this, kName));
    WriteCopy(stream, relationsA);
    WriteCopy<int>(stream, player ? *reinterpret_cast<int*>(player + 0x24) : -1);
    WriteCopy(stream, side);
    WriteCopy(stream, unknown_254);
    WriteCopy(stream, unknown_255);
    WriteCopy(stream, unknown_258);
    stream->Write(&unknown_25C, 4);
    stream->Write(&unknown_260, 4);
    stream->Write(&moveMode, 4);
    stream->Write(&moveMode2, 4);
    WriteCopy(stream, activeWeapon);
    WriteCopy(stream, unknown_270);
    WriteCopy(stream, dead);
    WriteCopy(stream, At<int>(this, 0x278));
    UnitMemorySave(reinterpret_cast<char*>(this) + kMemory, nullptr, stream);
    WriteCopy(stream, heightOffset);
    WriteCopy(stream, sizeClass);
    WriteCopy(stream, At<int>(this, kField328));
    WriteCopy(stream, activeWeapon);

    WriteItemIds(stream, weapons, 4);
    WriteItemIds(stream, quickItems, 4);
    WriteItemIds(stream, spells, 8);
    WriteItemIds(stream, armor, 7);
    stream->Write(reinterpret_cast<char*>(this) + kBlock4C8, kBlock4C8Size);
    WriteCopy(stream, relationsB);
    WriteCopy(stream, carriedCount);
    WriteItemIds(stream, carried, carriedCount);
    WriteCopy(stream, standingHeight);
    stream->Write(&unknown_438, 4);

    // The prototype's name (the record's first field is a CString; a record out of range is
    // copied from address 0, as the original does), then the second prototype's or "".
    mfc::CString name(*reinterpret_cast<const mfc::CString*>(PrototypeAt(static_cast<std::uint32_t>(At<int>(this, kPrototypeIndex)))));
    WriteString(stream, name);
    const char* second = PrototypeAt(static_cast<std::uint32_t>(prototype23C));
    if (second) name = *reinterpret_cast<const mfc::CString*>(second);
    else name = "";
    WriteString(stream, name);
}
EI_REPLACE_METHOD(0x005321C0, CUnit::SaveUnit);
