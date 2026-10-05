// CWorldServer: the party arriving in a zone (Redeploy 00590700) and the quest items it brings
// back (00590460, 005901E0). See world.h and docs/notes/world.md (seventh round).
#include "world.h"

#include "chunk_stream.h"
#include "core_maths.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_engine.h"

#include <cstring>

// The arrival maths keeps the original's operation order: no reassociation in this file.
#pragma float_control(precise, on, push)

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

constexpr std::uintptr_t kPtrListVtable = 0x0073BE80;
constexpr std::uintptr_t kIdArrayVtable = 0x0073EBD4;
constexpr std::uintptr_t kRecordArrayVtable = 0x0073FAA4;
constexpr std::uintptr_t kMapDatabase = 0x0079BF00;
constexpr std::uintptr_t kHalf = 0x0073B86C;        // 0.5f
constexpr std::uintptr_t kCellSize = 0x0073F904;    // the map cell size
constexpr std::uintptr_t kFacingOffset = 0x007AFED8;
constexpr std::uintptr_t kNoRotation = 0x007C2DE0;  // 16 bytes: the identity rotation
constexpr int kClassQuestItem = 0x14;
constexpr int kKindSinglePlayer = 1;
constexpr int kKindNoRangeTest = 3;
constexpr int kKindNetwork = 4;
constexpr int kGameResultLost = 2;
constexpr std::uint32_t kNoStart = 0xC4A42000;   // -1313.0f at player state +0x40: no start position
constexpr int kMercRecordSize = 0x784;
constexpr int kExitSize = 0x3C;

using ObjectList = mfc::CTypedPtrList<CEngineObject>;

// A CArray of item pointers (the player's carried items at WorldPlayer + 0xB0: data, size, ...).
struct ItemArray {
    CEngineObject** data;
    int size;
};

// CArray<int> (vtable 0073EBD4), and the CArray of record pointers CreateUnits takes (0073FAA4).
template <typename T>
struct LocalArray {
    void* vtable;
    T* data;
    int size;
    int capacity;
    int growBy;
};
using IdArray = LocalArray<int>;
using RecordArray = LocalArray<std::uint8_t*>;

// 004621A0 (RET 8). CPtrList::NewNode(prev, next), out of line.
__declspec(naked) ObjectList::Node* __fastcall PtrListNewNode(ObjectList*, void*, ObjectList::Node*, ObjectList::Node*)
    EI_ORIGINAL(0x004621A0)
// 00429F10. CPtrList::RemoveAll.
__declspec(naked) void __fastcall PtrListRemoveAll(ObjectList*, void*) EI_ORIGINAL(0x00429F10)
// 00427830. CPtrList destructor.
__declspec(naked) void __fastcall PtrListDestruct(ObjectList*, void*) EI_ORIGINAL(0x00427830)
// 0059D450 (RET 8). CArray<int>::SetAtGrow(index, value).
__declspec(naked) void __fastcall IdArraySetAtGrow(IdArray*, void*, int, int) EI_ORIGINAL(0x0059D450)
// 0059CB50 (RET 8). The item array's SetSize(size, growBy).
__declspec(naked) void __fastcall ItemArraySetSize(void*, void*, int, int) EI_ORIGINAL(0x0059CB50)
// 005719B0 (RET 8). The record array's SetSize(size, growBy).
__declspec(naked) void __fastcall RecordArraySetSize(RecordArray*, void*, int, int) EI_ORIGINAL(0x005719B0)
// 0059C3D0 (RET 8). The placement array's SetSize(size, growBy).
__declspec(naked) void __fastcall PlacementsSetSize(void*, void*, int, int) EI_ORIGINAL(0x0059C3D0)
// 006A0110. A quest item's description: the names of the zones it belongs to (a vector at +0x70).
__declspec(naked) std::uint8_t* __fastcall QuestItemInfo(CEngineObject*, void*) EI_ORIGINAL(0x006A0110)
// 00665470 (RET 8). Puts a list of items into the player's inventory.
__declspec(naked) void __fastcall PlayerGiveItems(WorldPlayer*, void*, ObjectList*, int) EI_ORIGINAL(0x00665470)
// 0045E850 (RET 4). The map database's zone record by name.
// 0049ECC0 (RET 8). Quaternion from an axis and an angle.
__declspec(naked) void* __fastcall QuaternionFromAxisAngle(void*, void*, const Vector3*, float) EI_ORIGINAL(0x0049ECC0)
// 0066ED30 (RET 0xC). Sends the player state its start position.
__declspec(naked) void __fastcall StateSendStart(void*, void*, const Vector3*, std::uint32_t, int) EI_ORIGINAL(0x0066ED30)
// 004BF000. Runs the script command queue once.
__declspec(naked) void __fastcall CommandQueueRun(void*, void*) EI_ORIGINAL(0x004BF000)
// Rewritten here and elsewhere, reached through their addresses.
__declspec(naked) void __fastcall TakeZoneQuestItemsAt(CWorldServer*, void*, ItemArray*, ObjectList*) EI_ORIGINAL(0x005901E0)
__declspec(naked) void __fastcall RestorePlayerItemsAt(CWorldServer*, void*, WorldPlayer*) EI_ORIGINAL(0x00590460)
__declspec(naked) void __fastcall CreateUnitsAt(CWorldServer*, void*, WorldPlayer*, RecordArray*, const Vector3*, Float4,
                                                void*) EI_ORIGINAL(0x0058EF20)
__declspec(naked) void __fastcall UpdateMercBriefingsAt(CWorldServer*, void*) EI_ORIGINAL(0x00591800)

// FPATAN: atan2(y, x) as the x87 computes it.
__declspec(naked) double __cdecl Fpatan(double /*y*/, double /*x*/) {
    __asm fld qword ptr [esp + 4]
    __asm fld qword ptr [esp + 12]
    __asm fpatan
    __asm ret
}

constexpr int kSlotItemFromItem = 24;   // world slot 24 (+0x60): a fresh item built from an item
constexpr int kSlotHasQuest = 38;       // object slot 38 (+0x98)
constexpr int kSlotQuestId = 40;        // object slot 40 (+0xA0)

void InitList(ObjectList& list) {
    list.vtable = reinterpret_cast<void*>(kPtrListVtable);
    list.head = nullptr;
    list.tail = nullptr;
    list.count = 0;
    list.free = nullptr;
    list.blocks = nullptr;
    list.blockSize = 10;
}

// AddTail through the game's out-of-line NewNode.
void AddTail(ObjectList& list, CEngineObject* object) {
    ObjectList::Node* node = PtrListNewNode(&list, nullptr, list.tail, nullptr);
    node->data = object;
    if (list.tail) list.tail->next = node;
    else list.head = node;
    list.tail = node;
}

bool Contains(const IdArray& ids, int id) {
    for (int i = 0; i < ids.size; ++i)
        if (ids.data[i] == id) return true;
    return false;
}

// Whether a quest item's description names this zone.
bool NamesZone(std::uint8_t* info, const char* zone) {
    bool found = false;
    for (unsigned i = 0; i < static_cast<unsigned>(At<const char**>(info, 0x74) - At<const char**>(info, 0x70)); ++i)
        if (gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(At<const char**>(info, 0x70)[i]),
                             reinterpret_cast<const unsigned char*>(zone)) == 0)
            found = true;
    return found;
}

} // namespace

void CWorldServer::TakeZoneQuestItems(void* itemArray, void* listArg) {
    auto* items = static_cast<ItemArray*>(itemArray);
    auto* list = static_cast<ObjectList*>(listArg);
    // The quest ids this zone's objects ask for (slot 38, slot 40), each once.
    IdArray wanted{reinterpret_cast<void*>(kIdArrayVtable), nullptr, 0, 0, 0};
    for (auto* node = objects_.map.First(); node && node->value; node = objects_.map.After(node)) {
        CEngineObject* object = node->value;
        if (!ei::VCall<int>(object, kSlotHasQuest)) continue;
        int id = ei::VCall<int>(object, kSlotQuestId);
        if (!Contains(wanted, id)) IdArraySetAtGrow(&wanted, nullptr, wanted.size, id);
    }

    // Quest items move from the array to the list: in single player only those whose
    // description names this zone or whose id (+0x4C) an object asks for; otherwise all of them.
    for (int i = 0; i < items->size; ++i) {
        if (items->data[i]->GetClassId() != kClassQuestItem) continue;
        CEngineObject* item = items->data[i];
        std::uint8_t* info = QuestItemInfo(item, nullptr);
        if (unknown12D64_ == kKindSinglePlayer && !IsMultiplayerClient()) {
            bool here = info && NamesZone(info, zoneName_);
            if (!here && !Contains(wanted, At<int>(item, 0x4C))) continue;
        }
        AddTail(*list, items->data[i]);
        int following = items->size - i - 1;
        if (following) std::memmove(&items->data[i], &items->data[i + 1], static_cast<std::size_t>(following) * 4);
        --items->size;
        --i;
    }
    wanted.vtable = reinterpret_cast<void*>(kIdArrayVtable);
    if (wanted.data) gamecrt::operator_delete(wanted.data);
}

void CWorldServer::RestorePlayerItems(WorldPlayer* player) {
    ObjectList taken;
    InitList(taken);
    if (unknown12D64_ != kKindNetwork) {
        auto* items = &At<ItemArray>(player, 0xB0);
        if (IsMultiplayerClient()) {
            // A client takes everything back.
            for (int i = 0; i < items->size; ++i) AddTail(taken, items->data[i]);
            ItemArraySetSize(items, nullptr, 0, -1);
        } else {
            TakeZoneQuestItemsAt(this, nullptr, items, &taken);
        }
        if (player) {
            // Each item is given back as a fresh copy (world slot 24) keeping its count (+0x48);
            // the old one is released.
            for (ObjectList::Node* node = taken.head; node;) {
                ObjectList::Node* next = node->next;
                CEngineObject* item = node->data;
                ObjectList one;
                InitList(one);
                auto* fresh = ei::VCall<CEngineObject*>(this, kSlotItemFromItem, item);
                At<int>(fresh, 0x48) = At<int>(item, 0x48);
                AddTail(one, fresh);
                PlayerGiveItems(player, nullptr, &one, 0);
                item->Release();
                PtrListDestruct(&one, nullptr);
                node = next;
            }
            PtrListRemoveAll(&taken, nullptr);
        }
    }
    // The list's inline destructor.
    taken.vtable = reinterpret_cast<void*>(kPtrListVtable);
    taken.count = 0;
    taken.free = nullptr;
    taken.tail = nullptr;
    taken.head = nullptr;
    mfc::PlexFreeDataChain(taken.blocks);
}

void CWorldServer::Redeploy(WorldPlayer* player, void* placements, char runScripts) {
    if (!player) return;
    RestorePlayerItemsAt(this, nullptr, player);
    if (!(unknown12D64_ == kKindSinglePlayer || unknown12D64_ == kKindNoRangeTest) || unknown15C_ == 0) return;

    RecordArray records{reinterpret_cast<void*>(kRecordArrayVtable), nullptr, 0, 0, 0};
    for (int i = 0; i < player->mercCount; ++i) {
        int index = records.size;
        RecordArraySetSize(&records, nullptr, index + 1, -1);
        records.data[index] = player->mercs + i * kMercRecordSize;
    }
    // A lost game sends the party back to this zone's first exit.
    if (gameResult_ == kGameResultLost) {
        At<mfc::CString>(player, 0xD8) = *reinterpret_cast<const mfc::CString*>(&zoneName_);
        At<int>(player, 0xDC) = 0;
    }

    // The party arrives at the centre of its entrance (the target zone's exit +0xDC), facing
    // away from the map's centre unless the exit gives a heading (+0x34, +0x38).
    double cell = static_cast<double>(GameFloat(kCellSize)) * GameFloat(kHalf);
    float centreX = StoreFloat(static_cast<float>(gridWidth_ * cell * GameFloat(kHalf)));
    float centreY = StoreFloat(static_cast<float>(gridHeight_ * cell * GameFloat(kHalf)));
    Float4 rotation;
    std::memcpy(&rotation, reinterpret_cast<const void*>(kNoRotation), sizeof(rotation));
    Vector3 start{0.0f, 0.0f, 0.0f};
    std::uint32_t startValue = 0;
    std::uint8_t* exit = nullptr;
    std::uint8_t* zone = MapDatabaseFind(reinterpret_cast<void*>(kMapDatabase), nullptr, At<const char*>(player, 0xD8));
    int entrance = At<int>(player, 0xDC);
    if (zone && entrance >= 0 && entrance < At<int>(zone, 0x50)) {
        exit = At<std::uint8_t*>(zone, 0x4C) + entrance * kExitSize;
        start.x = StoreFloat(static_cast<float>((static_cast<double>(At<float>(exit, 0xC)) + At<float>(exit, 0x4)) * GameFloat(kHalf)));
        start.y = StoreFloat(static_cast<float>((static_cast<double>(At<float>(exit, 0x10)) + At<float>(exit, 0x8)) * GameFloat(kHalf)));
        start.z = 0.0f;
        startValue = At<std::uint32_t>(exit, 0x30);
    }
    float angle = StoreFloat(static_cast<float>(Fpatan(static_cast<double>(start.y) - centreY,
                                                       static_cast<double>(start.x) - centreX) - GameFloat(kFacingOffset)));
    if (exit && exit[0x34]) std::memcpy(&angle, exit + 0x38, sizeof(angle));
    Vector3 up{0.0f, 0.0f, 1.0f};
    QuaternionFromAxisAngle(&rotation, nullptr, &up, angle);

    CreateUnitsAt(this, nullptr, player, &records, &start, rotation, placements);
    if (placements) PlacementsSetSize(placements, nullptr, 0, -1);
    void* state = At<void*>(player, 0x20);
    if (At<std::uint32_t>(state, 0x40) != kNoStart) {
        std::memcpy(&At<std::uint8_t>(state, 0x38), &start, sizeof(start));
        StateSendStart(state, nullptr, &start, startValue, unknown12D64_);
    }
    player->lastTick = tick_;
    if (unknown12D64_ == kKindNoRangeTest) UpdateMercBriefingsAt(this, nullptr);
    if (runScripts) {
        unknown160_ = 1;
        for (int i = 0; i < 5; ++i) CommandQueueRun(commandsA_, nullptr);
    }
    records.vtable = reinterpret_cast<void*>(kRecordArrayVtable);
    if (records.data) gamecrt::operator_delete(records.data);
}

#pragma float_control(pop)

// ---- CopyItem ---------------------------------------------------------------------------------

namespace {

// 006AD000 (RET 0x10). The item factory (world slot 18) makes an item: type, record index, the
// two halves of the item's +0x18, -1.
__declspec(naked) CEngineObject* __fastcall FactoryMakeItem(void*, void*, int, int, int, int) EI_ORIGINAL(0x006AD000)
// 00599FA0 (RET 4). CTypedPtrList<CRefPtr> AddTail: the list holds a reference.
__declspec(naked) void __fastcall CountedAddTail(void*, void*, CEngineObject*) EI_ORIGINAL(0x00599FA0)

constexpr int kSlotItemFactory = 18;   // world slot 18 (+0x48)
constexpr int kSlotManager4C = 22;     // world slot 22 (+0x58)
constexpr int kSlotItemCopyTo = 15;    // item slot 15 (+0x3C): copies itself into an item

} // namespace

CEngineObject* CWorldServer::CopyItem(CEngineObject* item) {
    // The original also builds and frees two empty CPtrLists (vtable 0073B8D4) here, unused.
    auto packed = At<std::uint32_t>(item, 0x18);
    int type = At<int>(item, 0x10);
    void* factory = ei::VCall<void*>(this, kSlotItemFactory);
    CEngineObject* copy = FactoryMakeItem(factory, nullptr, type, static_cast<int>(packed >> 16),
                                          static_cast<int>(packed & 0xFFFF), -1);
    ei::VCall<void>(item, kSlotItemCopyTo, copy, ei::VCall<void*>(this, kSlotManager4C));
    At<std::uint32_t>(copy, 0x28) = At<std::uint32_t>(item, 0x28);
    // An item below its maximum (+0x2C > +0x28) is kept on the counted list (once).
    float current = At<float>(item, 0x28);
    float maximum = At<float>(item, 0x2C);
    if (maximum > current && copy && !counted_.Find(copy)) CountedAddTail(&counted_, nullptr, copy);
    return copy;
}

// ---- Items and spells by name -------------------------------------------------------------------

namespace {

// 006AC970 (RET 4, patched by Rebirth). The item manager makes an item by name.
__declspec(naked) CEngineObject* __fastcall ManagerCreateItem(void*, void*, const char*) EI_ORIGINAL(0x006AC970)
// 006AD1E0 (RET 8). The item manager makes a spell by name (with world slot 22).
__declspec(naked) CEngineObject* __fastcall ManagerCreateSpell(void*, void*, const char*, void*) EI_ORIGINAL(0x006AD1E0)

void* ItemManager() { return *reinterpret_cast<void**>(0x007AFFB4); }

} // namespace

// As in CopyItem, the original also builds and frees two empty CPtrLists around the call.
CEngineObject* CWorldServer::CreateItemNamed(const char* name) { return ManagerCreateItem(ItemManager(), nullptr, name); }

CEngineObject* CWorldServer::CreateSpellNamed(const char* name) {
    void* manager = ItemManager();   // read before slot 22, as in the original
    return ManagerCreateSpell(manager, nullptr, name, ei::VCall<void*>(this, kSlotManager4C));
}

void CWorldServer::AddCounted(CEngineObject* item) {
    if (!item || counted_.Find(item)) return;
    CountedAddTail(&counted_, nullptr, item);   // the original inlines the same AddTail
}

EI_REPLACE_METHOD(0x00594CE0, CWorldServer::CopyItem);
EI_REPLACE_METHOD(0x00594F30, CWorldServer::AddCounted);
EI_REPLACE_METHOD(0x00594AB0, CWorldServer::CreateItemNamed);
EI_REPLACE_METHOD(0x00594BC0, CWorldServer::CreateSpellNamed);
EI_REPLACE_METHOD(0x005901E0, CWorldServer::TakeZoneQuestItems);
EI_REPLACE_METHOD(0x00590460, CWorldServer::RestorePlayerItems);
EI_REPLACE_METHOD(0x00590700, CWorldServer::Redeploy);
