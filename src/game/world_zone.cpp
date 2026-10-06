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

#include <cstring>
#include <new>
#include "originals_world.h"
#include "originals_units.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_gameplay.h"

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

// ---- Game data --------------------------------------------------------------------------------

// The empty string the game compares against and passes as "no text" (a zero byte in .data).
constexpr const char* kNoExit = "none";   // 007846F0
// Error texts (printf formats; the original passes these addresses to FatalError).
const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }
constexpr std::uintptr_t kWrongEntrance = 0x00790958;   // "Wrong entrance number in zone exit..."
constexpr std::uintptr_t kCantLoadMob = 0x00790B08;     // "CWorldServer::LoadMap.\n Can't load .mob file "%s".\n"
constexpr std::uintptr_t kCantReadObjects = 0x00790B3C; // "CWorldServer::LoadMapObjects\nUnable to read file "%s"\n"
constexpr std::uintptr_t kCantOpenObjects = 0x00790B74; // "CWorldServer::LoadMapObjects()\nUnable to read file "%s"\n"

// Set while LoadMobFile reads a .mob file (byte at 007C554C; the object loaders read it).
std::uint8_t& LoadingMobFile() { return *reinterpret_cast<std::uint8_t*>(0x007C554C); }

// The default orientation of new units (16 bytes at 007C2DE0), passed by value.
Float4 DefaultRotation() {
    Float4 value;
    std::memcpy(&value, reinterpret_cast<const void*>(0x007C2DE0), sizeof(value));
    return value;
}

constexpr std::uintptr_t kPtrArrayVtable = 0x0073FAA4;      // CArray of record pointers
constexpr std::uintptr_t kObjectListVtable = 0x0073F530;    // CTypedPtrList<CEngineObject*>
constexpr std::uintptr_t kMapObjectFileVtable = 0x0073FAD4; // the .mob root chunk (inline constructor)

constexpr int kSlotStreamWrite = 2;   // stream slot 2: Write(data, size)
constexpr int kSlotFileClose = 19;    // MFC CFile slot 19 (+0x4C): Close

// ---- Callees not rewritten (EI_ORIGINAL forwarders; __thiscall called as __fastcall) ----------


// 0058EF20 (RET 0x20). Creates the units described by `records` (pointers to 0x784-byte unit
// records) around `position` with `rotation`; ids come from the records (+0x1C).
__declspec(naked) void __fastcall ServerCreateUnits(CWorldServer*, void*, int /*zero*/, RecordPtrArray* /*records*/,
                                                    const Vector3* /*position*/, Float4 /*rotation*/,
                                                    int /*placements*/) EI_ORIGINAL(0x0058EF20)
// 005935C0 (RET 4). RemoveObjectFromServer. Patched by Rebirth: always the original address.
// 00595BA0 (RET 8). The full path of a map file: `name` as is when it has a drive, else under the
// game folder; constructs a CString into `out` and returns it. Patched by Rebirth.
char** __fastcall ServerMapPath(CWorldServer* a0, void*, char** a2, const char* a3) { return (char**)ServerMapPathOriginal((void*)a0, nullptr, (void*)a2, a3); }
// 00481660 (RET 8). Reads the objects of an opened .mob file into the server (AL).
__declspec(naked) bool __fastcall ReadMapObjectFile(Chunk*, void*, CWorldServer*, int /*keepIds*/)
    EI_ORIGINAL(0x00481660)
// 0040A3A0. CRefObject::Release (16-bit count at +4): the missiles.
void __fastcall RefRelease(void* a0, void*) { RefObjectRelease(a0); }
// 0059DDB0 (RET 4). Destructor of a counted pointer (releases through slot 4; flags & 1 frees it).
__declspec(naked) void* __fastcall CountedPtrDestroy(void** /*slot*/, void*, unsigned /*flags*/)
    EI_ORIGINAL(0x0059DDB0)
// 006D43E0. Clears the countdown array (CWorldServer + 0x70): frees it, size and capacity 0.
__declspec(naked) void __fastcall CountdownsClear(void*, void*) EI_ORIGINAL(0x006D43E0)
// 00530930. CUnit::GetPrototype: the unit's prototype record (its name first).
static char** UnitPrototype(CEngineObject* object, void*) { return static_cast<char**>(::UnitPrototypeOf(object)); }

// The unit record AddUnitToServer builds on its stack (0x70 bytes; a pointer to the 0x784-byte
// unit record CreateUnits takes at +0x30).
struct UnitRecordBuilder {
    std::uint8_t bytes[0x70];

    void* Construct();                                                       // 00661050
    void Fill(const char* text, const char* prototype, const char* name);    // 006628D0 (RET 0xC)
    void Destruct();                                                         // 006611D0
    void* Record() { return At<void*>(this, 0x30); }
};
static_assert(sizeof(UnitRecordBuilder) == 0x70);
void* UnitRecordBuilder::Construct() { return PartyRecordsConstructOriginal((void*)this); }
void UnitRecordBuilder::Fill(const char* a0, const char* a1, const char* a2) { PartyAddMemberOriginal((void*)this, nullptr, a0, a1, a2); }
void UnitRecordBuilder::Destruct() { PartyRecordsDestructOriginal((void*)this); }

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
            game::FatalError(Text(kWrongEntrance));
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
    records.vtable = reinterpret_cast<void*>(kPtrArrayVtable);
    records.data = nullptr;
    records.growBy = 0;
    records.maxSize = 0;
    records.size = 0;
    auto* record = static_cast<std::uint8_t*>(builder.Record());
    RecordArraySetSizeOriginal(&records, nullptr, 1, -1);
    records.data[0] = record;
    At<unsigned>(records.data[0], 0x1C) = id;
    ServerCreateUnits(this, nullptr, 0, &records, position, DefaultRotation(), 0);
    records.vtable = reinterpret_cast<void*>(kPtrArrayVtable);
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
    void* fileObject = &root->file_;
    ei::SetVtable(root, kMapObjectFileVtable);

    mfc::CString path;
    ServerMapPath(this, nullptr, reinterpret_cast<char**>(&path), file);   // constructs the CString in place
    int result;
    if (!root->Open(path, 0x8020)) {
        result = game::FatalError(Text(kCantOpenObjects), file);
    } else {
        bool read = ReadMapObjectFile(root, nullptr, this, keepIds);
        ei::VCall<void>(fileObject, kSlotFileClose);
        result = read ? 1 : game::FatalError(Text(kCantReadObjects), file);
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
            unknown15C_ = 0;
            return game::FatalError(Text(kCantLoadMob), file);
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
    pending.vtable = reinterpret_cast<void*>(kObjectListVtable);
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
        if (node->data) RefRelease(node->data, nullptr);
    projectiles_.RemoveAll();

    for (auto* node = counted_.head; node; node = node->next)
        CountedPtrDestroy(reinterpret_cast<void**>(&node->data), nullptr, 0);
    counted_.RemoveAll();

    CountdownsClear(countdowns_, nullptr);

    pending.vtable = reinterpret_cast<void*>(kObjectListVtable);
    pending.RemoveAll();   // 00570470 (the list destructor's body)
}

void CWorldServer::SaveMapObjects(void* stream) {
    for (auto* node = mapObjects_.map.First(); node; node = mapObjects_.map.After(node)) {
        CEngineObject* object = node->value;
        if (!object) break;
        int id = object->id_;
        ei::VCall<void>(stream, kSlotStreamWrite, static_cast<const void*>(&id), 4u);
        const char* prototype =
            object->GetClassId() == world::kClassUnit ? *UnitPrototype(object, nullptr) : At<char*>(object, 0xA4);
        ei::VCall<void>(stream, kSlotStreamWrite, static_cast<const void*>(prototype),
                        static_cast<unsigned>(std::strlen(prototype) + 1));
        object->Save(stream);
        std::uint32_t flags = static_cast<std::uint32_t>(At<std::uint16_t>(object, 0x1DC)) |
                              static_cast<std::uint32_t>(At<std::uint16_t>(object, 0x1D8));
        ei::VCall<void>(stream, kSlotStreamWrite, static_cast<const void*>(&flags), 4u);
    }
    int end = -1;
    ei::VCall<void>(stream, kSlotStreamWrite, static_cast<const void*>(&end), 4u);
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
        LoadingMovie() = Text(0x00790A9C);   // progres2.bik
    } else if (SameName(allod, "ingos")) {
        LoadingMovie() = Text(0x00790A8C);   // progres3.bik
    } else {
        bool suslanger = SameName(allod, "suslanger");
        LoadingMovie() = Text(0x00790A7C);   // progres4.bik
        if (!suslanger) {
            LoadingMovie() = Text(0x00790A70);   // progres.bik
            if (ei::rebirth::Enabled(ei::rebirth::Patch::FixProgressNames)) {
                if (SameName(allod, "gipat2")) LoadingMovie() = Text(0x00790A9C);
                else if (SameName(allod, "jigran")) LoadingMovie() = kProgres5;
            }
        }
    }
    if (zone) {
        if (gamecrt::stricmp(zone, "gz1g") == 0) LoadingMovie() = Text(0x00790A60);   // progres1.bik
        if (gamecrt::stricmp(zone, "bz7g") == 0) LoadingMovie() = Text(0x00790A60);
    }
}

EI_REPLACE_METHOD(0x005915E0, CWorldServer::SetZoneExits);
EI_REPLACE_METHOD(0x005910A0, CWorldServer::AddUnitToServer);
EI_REPLACE_METHOD(0x00596030, CWorldServer::LoadMapObjects);
EI_REPLACE_METHOD(0x00595AB0, CWorldServer::LoadMobFile);
EI_REPLACE_METHOD(0x00596C60, CWorldServer::ClearMapObjects);
EI_REPLACE_METHOD(0x00596730, CWorldServer::SaveMapObjects);
EI_REPLACE(0x00595080, SelectLoadingMovie);
EI_REBIRTH_IMPLEMENTS(0x00595080, ei::rebirth::Patch::FixProgressNames);
