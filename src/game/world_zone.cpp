// CWorldServer: zone loading and teardown (zone exits, .mob object files, clearing the map
// objects), the script's AddUnitToServer, and writing the map objects into a save. See world.h and
// docs/notes/world.md.
#include "world.h"
#include "game_strings.h"

#include "chunk_stream.h"
#include "object_tables.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ei/vcall.h"
#include "map_object.h"
#include "net_delta.h"
#include "party.h"
#include "unit.h"

#include <cstring>
#include <new>
#include "originals_world.h"
#include "originals_units.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_gameplay.h"
#include "lib/mfc_slots.h"

namespace {

// ---- Game data --------------------------------------------------------------------------------

// The empty string the game compares against and passes as "no text" (a zero byte in .data).
constexpr const char* kNoExit = "none";   // 007846F0

// Set while LoadMobFile reads a .mob file (byte at 007C554C; the object loaders read it).
std::uint8_t& LoadingMobFile() { return *reinterpret_cast<std::uint8_t*>(0x007C554C); }

// The default orientation of new units (16 bytes at 007C2DE0), passed by value.
Quaternion DefaultRotation() {
    Quaternion value;
    std::memcpy(&value, reinterpret_cast<const void*>(0x007C2DE0), sizeof(value));
    return value;
}


// ---- Callees not rewritten (EI_ORIGINAL forwarders; __thiscall called as __fastcall) ----------


// 0058EF20 (RET 0x20): WorldCreateUnitsOriginal creates the units described by `records` (pointers to 0x784-byte unit
// records) around `position` with `rotation`; ids come from the records (+0x1C).
// 005935C0 (RET 4). RemoveObjectFromServer. Patched by Rebirth: always the original address.
// 00595BA0 (RET 8). The full path of a map file: `name` as is when it has a drive, else under the
// game folder; constructs a CString into `out` and returns it. Patched by Rebirth.
char** __fastcall ServerMapPath(CWorldServer* a0, void*, char** a2, const char* a3) { return (char**)ServerMapPathOriginal((void*)a0, nullptr, (void*)a2, a3); }
// 00481660 (RET 8). Reads the objects of an opened .mob file into the server (AL).
__declspec(naked) bool __fastcall ReadMapObjectFile(Chunk*, void*, CWorldServer*, int /*keepIds*/)
    EI_ORIGINAL(0x00481660)
// 0040A3A0. CRefObject::Release (16-bit count at +4): the missiles.
// 0059DDB0 (RET 4). Destructor of a counted pointer (releases through slot 4; flags & 1 frees it).
__declspec(naked) void* __fastcall CountedPtrDestroy(void** /*slot*/, void*, unsigned /*flags*/)
    EI_ORIGINAL(0x0059DDB0)
// 00530930. CUnit::GetPrototype: the unit's prototype record (its name first).
static char** UnitPrototype(CEngineObject* object, void*) { return static_cast<char**>(::UnitPrototypeOf(object)); }

// The unit record AddUnitToServer builds on its stack: a CParty (party.h; constructor 00661050,
// destructor 006611D0) given one member by 006628D0 (RET 0xC); CreateUnits takes a pointer to the
// member's 0x784-byte record.
struct UnitRecordBuilder {
    alignas(4) std::uint8_t storage[sizeof(CParty)];

    CParty* Party() { return reinterpret_cast<CParty*>(storage); }
    void Construct() { static_cast<CParty*>(Party())->Construct(); }
    void Fill(const char* text, const char* prototype, const char* name) {
        Party()->AddMember(text, prototype, name);
    }
    void Destruct() { PartyRecordsDestructOriginal(Party()); }
    PartyUnitRecord* Record() { return Party()->members_.data; }
};

} // namespace

__declspec(naked) ZoneExit* ZoneExit::DeletingDestructor(unsigned) EI_ORIGINAL(0x004608B0)

// ---- Zone exits -------------------------------------------------------------------------------

void CWorldServer::SetZoneExits(const ZoneExitArray* source) {
    auto* exits = reinterpret_cast<ZoneExitArray*>(&exitsVtable_);
    if (exits->data) {
        ZoneExit* exit = exits->data;
        for (int n = exits->size; n != 0; --n, ++exit) exit->DeletingDestructor(0);
        gamecrt::operator_delete(exits->data);
        exits->data = nullptr;
    }
    exits->maxSize = 0;
    exits->size = 0;
    for (int i = 0; i < source->size; ++i) {
        const ZoneExit* from = &source->data[i];
        if (gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(from->target),
                            reinterpret_cast<const unsigned char*>(game::EmptyText())) == 0)
            continue;   // leads nowhere
        if (from->entrance < 0 && gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(from->target),
                                                  reinterpret_cast<const unsigned char*>(kNoExit)) != 0)
            game::FatalError(game::zonetexts::WrongEntrance());
        int index = exits->size;
        ZoneExitsSetSizeOriginal(exits, nullptr, index + 1, -1);
        ZoneExit* to = &exits->data[index];
        auto* src = reinterpret_cast<const std::uint8_t*>(from);
        auto* dst = reinterpret_cast<std::uint8_t*>(to);
        std::memcpy(dst, src, 0x28);   // ten dwords, copied as integers
        *reinterpret_cast<mfc::CString*>(&to->target) = *reinterpret_cast<const mfc::CString*>(&from->target);
        std::memcpy(dst + 0x2C, src + 0x2C, 8);   // entrance, +0x30
        dst[0x34] = src[0x34];
        std::memcpy(dst + 0x38, src + 0x38, 4);
    }
}

// ---- Units ------------------------------------------------------------------------------------

void CWorldServer::AddUnitToServer(unsigned id, const char* prototype, const char* name, const Vector3* position) {
    UnitRecordBuilder builder;
    builder.Construct();
    builder.Fill(game::EmptyText(), prototype, name);
    RecordPtrArray records;
    records.vtable = mfc::impl::PartyUnitArrayClass::Vtable();
    records.data = nullptr;
    records.growBy = 0;
    records.maxSize = 0;
    records.size = 0;
    PartyUnitRecord* record = builder.Record();
    RecordArraySetSizeOriginal(&records, nullptr, 1, -1);
    records.data[0] = reinterpret_cast<std::uint8_t*>(record);
    record->unitId = id;   // the unit's id
    WorldCreateUnitsOriginal(this, nullptr, nullptr, &records, position, DefaultRotation(), nullptr);
    records.vtable = mfc::impl::PartyUnitArrayClass::Vtable();
    if (records.data) gamecrt::operator_delete(records.data);
    builder.Destruct();
}

// ---- .mob object files ------------------------------------------------------------------------

int CWorldServer::LoadMapObjects(const char* file, int keepIds) {
    // The root chunk (vtable 0073FAD4) with its MFC CFile at +0x18, built in place as the
    // original's inlined constructor does.
    alignas(4) std::uint8_t storage[0x28];
    auto* root = reinterpret_cast<RootChunk*>(storage);
    root->Construct(nullptr);
    new (&root->file_) mfc::CFile();
    auto* fileObject = reinterpret_cast<mfc::CFileVirtuals*>(&root->file_);
    ei::SetVtable(root, RootChunk::kMapObjectFileVtableAddress);

    mfc::CString path;
    ServerMapPath(this, nullptr, reinterpret_cast<char**>(&path), file);   // constructs the CString in place
    int result;
    if (!root->Open(path, 0x8020)) {
        result = game::FatalError(game::zonetexts::CantOpenObjects(), file);
    } else {
        bool read = ReadMapObjectFile(root, nullptr, this, keepIds);
        fileObject->Close();
        result = read ? 1 : game::FatalError(game::zonetexts::CantReadObjects(), file);
    }
    root->Destruct();
    return result;
}

int CWorldServer::LoadMobFile(const char* file) {
    for (int i = 0; i < mobFiles_.size; ++i) {
        if (gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(mobFiles_[i])),
                            reinterpret_cast<const unsigned char*>(file)) == 0)
            return 1;
    }
    LoadingMobFile() = 1;
    if (file) {
        int loaded = LoadMapObjects(file, 0);
        if (loaded == 0) {
            LoadingMobFile() = 0;
            zoneLoaded_ = 0;
            return game::FatalError(game::zonetexts::CantLoadMob(), file);
        }
    }
    LoadingMobFile() = 0;
    mobFiles_.SetAtGrow(mobFiles_.size, file);
    return 1;
}

// ---- Clearing and saving the map objects ----------------------------------------------------

void CWorldServer::ClearMapObjects() {
    // The objects are collected first (an MFC list, block size 10), then removed one by one.
    mfc::CTypedPtrList<CEngineObject> pending;
    pending.vtable = ObjectPtrListVtable();
    pending.head = nullptr;
    pending.tail = nullptr;
    pending.count = 0;
    pending.free = nullptr;
    pending.blocks = nullptr;
    pending.blockSize = 10;
    for (auto* node = mapObjects_.map.First(); node; node = mapObjects_.map.After(node)) {
        CEngineObject* object = node->value;
        if (!object) break;   // the walk stops at the first empty entry
        pending.AddTail(object);
    }
    for (auto* node = pending.head; node;) {
        auto* next = node->next;
        WorldRemoveObject(this, nullptr, node->data->id_);
        node = next;
    }

    for (auto* node = projectiles_.head; node; node = node->next)
        if (node->data) RefObjectRelease(node->data);
    projectiles_.RemoveAll();

    for (auto* node = counted_.head; node; node = node->next)
        CountedPtrDestroy(reinterpret_cast<void**>(&node->data), nullptr, 0);
    counted_.RemoveAll();

    TweenArrayRemoveAllOriginal(countdowns_);

    pending.vtable = ObjectPtrListVtable();
    pending.RemoveAll();   // 00570470 (the list destructor's body)
}

void CWorldServer::SaveMapObjects(void* stream) {
    auto* out = static_cast<CStream*>(stream);
    for (auto* node = mapObjects_.map.First(); node; node = mapObjects_.map.After(node)) {
        CEngineObject* object = node->value;
        if (!object) break;
        auto* mapObject = static_cast<CMapObject*>(object);
        int id = object->id_;
        out->Write(&id, 4u);
        const char* prototype =
            object->GetClassId() == world::kClassUnit ? *UnitPrototype(object, nullptr) : mapObject->name;
        out->Write(prototype, static_cast<unsigned>(std::strlen(prototype) + 1));
        object->Save(stream);
        // The parts changed and those already sent (the network state's mask).
        std::uint32_t flags = static_cast<std::uint32_t>(mapObject->changedParts) |
                              static_cast<std::uint32_t>(mapObject->NetState()->sentMask_);
        out->Write(&flags, 4u);
    }
    int end = -1;
    out->Write(&end, 4u);
}

// ---- Loading movie (00595080) -------------------------------------------------------------------

namespace {
const char*& LoadingMovie() { return *reinterpret_cast<const char**>(0x00791EE0); }
bool SameName(const char* a, const char* b) {
    return gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(a), reinterpret_cast<const unsigned char*>(b)) == 0;
}
// "progres5.bik" is not among the game's strings; addon.dll points the name at its own copy.
constexpr const char* kProgres5 = "progres5.bik";
} // namespace

void __stdcall SelectLoadingMovie(const char* zone) {
    mfc::CString allod;
    if (zone) {
        MapRecord* map = reinterpret_cast<CMapDatabase*>(0x0079BF00)->FindMap(zone);
        if (map) allod = *reinterpret_cast<const mfc::CString*>(reinterpret_cast<const char*>(map) + 4);
    }
    if (SameName(allod, "gipat")) {
        LoadingMovie() = game::Text(0x00790A9C);   // progres2.bik
    } else if (SameName(allod, "ingos")) {
        LoadingMovie() = game::Text(0x00790A8C);   // progres3.bik
    } else {
        bool suslanger = SameName(allod, "suslanger");
        LoadingMovie() = game::Text(0x00790A7C);   // progres4.bik
        if (!suslanger) {
            LoadingMovie() = game::Text(0x00790A70);   // progres.bik
            if (ei::rebirth::Enabled(ei::rebirth::Patch::FixProgressNames)) {
                if (SameName(allod, "gipat2")) LoadingMovie() = game::Text(0x00790A9C);
                else if (SameName(allod, "jigran")) LoadingMovie() = kProgres5;
            }
        }
    }
    if (zone) {
        if (libcrt::Stricmp(zone, "gz1g") == 0) LoadingMovie() = game::Text(0x00790A60);   // progres1.bik
        if (libcrt::Stricmp(zone, "bz7g") == 0) LoadingMovie() = game::Text(0x00790A60);
    }
}

EI_REPLACE_METHOD(0x005915E0, CWorldServer::SetZoneExits);
EI_REPLACE_METHOD(0x005910A0, CWorldServer::AddUnitToServer);
EI_REPLACE_METHOD(0x00596030, CWorldServer::LoadMapObjects);
EI_REPLACE_METHOD(0x00595AB0, CWorldServer::LoadMobFile);
EI_REPLACE_METHOD(0x00596C60, CWorldServer::ClearMapObjects);
EI_REPLACE_METHOD(0x00596730, CWorldServer::SaveMapObjects);
EI_REPLACE(0x00595080, SelectLoadingMovie);
