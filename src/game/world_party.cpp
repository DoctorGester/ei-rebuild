// CWorldServer: the party arriving in a zone (Redeploy 00590700) and the quest items it brings
// back (00590460, 005901E0). See world.h and docs/notes/world.md.
#include "world.h"

#include "chunk_stream.h"
#include "core_maths.h"
#include "item.h"
#include "item_db.h"
#include "map_object.h"
#include "object_tables.h"
#include "party.h"
#include "player_messages.h"
#include "unit.h"

#include "originals_world.h"
#include "lib/crt.h"
#include "lib/containers.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "originals_engine.h"

#include <cstring>
#include "originals_gameplay.h"
#include "originals_library.h"
#include "originals_script.h"

// The arrival maths keeps the original's operation order: no reassociation in this file.
#pragma float_control(precise, on, push)

namespace {

// Values and vtables read from the exe's memory as the original does (rule 9).
float Half() { return *reinterpret_cast<const volatile float*>(0x0073B86C); }            // 0.5f
float CellSize() { return *reinterpret_cast<const volatile float*>(0x0073F904); }        // the map cell size
float FacingOffset() { return *reinterpret_cast<const volatile float*>(0x007AFED8); }
const void* NoRotation() { return reinterpret_cast<const void*>(0x007C2DE0); }           // 16 bytes: the identity rotation
void* PtrListVtable() { return reinterpret_cast<void*>(0x0073BE80); }                    // the item lists' CPtrList
void* IdArrayVtable() { return reinterpret_cast<void*>(0x0073EBD4); }                    // CArray<int>
void* RecordArrayVtable() { return reinterpret_cast<void*>(0x0073FAA4); }                // the record pointers' CArray

constexpr int kClassQuestItem = 0x14;
constexpr int kKindSinglePlayer = 1;
constexpr int kKindNoRangeTest = 3;
constexpr int kKindNetwork = 4;
constexpr int kGameResultLost = 2;
constexpr std::uint32_t kNoStart = 0xC4A42000;   // -1313.0f as the state's last z: no start position

using ObjectList = mfc::CTypedPtrList<CEngineObject>;

// The player's carried items (CParty::items_): an aligned array of item pointers (its SetSize
// 0059CB50 is lib::AlignedArray's, called through ItemArraySetSize).
using ItemArray = AlignedPtrArray;

// CArray<int> (vtable 0073EBD4), and the CArray of record pointers CreateUnits takes (0073FAA4).
using IdArray = mfc::CArray<int>;
using RecordArray = RecordPtrArray;

// 004621A0 (RET 8). CPtrList::NewNode(prev, next), out of line.
inline ObjectList::Node* __fastcall PtrListNewNode(ObjectList* l, void*, ObjectList::Node* prev, ObjectList::Node* next) { return static_cast<ObjectList::Node*>(PtrListNewNodeOriginal(l, nullptr, prev, next)); }
// 00429F10. CPtrList::RemoveAll.
inline void __fastcall PtrListRemoveAll(ObjectList* a0, void*) { PtrListRemoveAllNodesOriginal((void*)a0); }
// 00427830. CPtrList destructor.
inline void __fastcall PtrListDestruct(ObjectList* a0, void*) { PtrListDestructOriginal((void*)a0); }
// 0059D450 (RET 8). CArray<int>::SetAtGrow(index, value).
__declspec(naked) void __fastcall IdArraySetAtGrow(IdArray*, void*, int, int) EI_ORIGINAL(0x0059D450)
// 0059CB50 (RET 8). The item array's SetSize(size, growBy).
__declspec(naked) void __fastcall ItemArraySetSize(ItemArray*, void*, int, int) EI_ORIGINAL(0x0059CB50)
// 005719B0 (RET 8). The record array's SetSize(size, growBy).
inline void __fastcall RecordArraySetSize(RecordArray* a, void*, int n, int g) { RecordArraySetSizeOriginal(a, nullptr, n, g); }
// 006A0110 (CItem::GetPrototype). A quest item's description: the names of the zones it belongs to.
// 00665470 (RET 8). Puts a list of items into the player's inventory.
void __fastcall PlayerGiveItems(WorldPlayer* a0, void*, ObjectList* a2, int a3) { PlayerGiveItemsOriginal((void*)a0, nullptr, (void*)a2, a3); }
// 0045E850 (RET 4). The map database's zone record by name.
const MapRecord* FindZone(const char* name) {
    return reinterpret_cast<const MapRecord*>(MapDatabaseFind(GameMapDatabase(), nullptr, name));
}
// 0049ECC0 (RET 8). Quaternion from an axis and an angle.
__declspec(naked) void* __fastcall QuaternionFromAxisAngle(void*, void*, const Vector3*, float) EI_ORIGINAL(0x0049ECC0)
// 0066ED30 (RET 0xC). Sends the player state its start position.
__declspec(naked) void __fastcall StateSendStart(CPlayerServer*, void*, const Vector3*, std::uint32_t, int) EI_ORIGINAL(0x0066ED30)
// Rewritten here and elsewhere, reached through their addresses.
__declspec(naked) void __fastcall TakeZoneQuestItemsAt(CWorldServer*, void*, ItemArray*, ObjectList*) EI_ORIGINAL(0x005901E0)
__declspec(naked) void __fastcall RestorePlayerItemsAt(CWorldServer*, void*, WorldPlayer*) EI_ORIGINAL(0x00590460)

// FPATAN: atan2(y, x) as the x87 computes it.
__declspec(naked) double __cdecl Fpatan(double /*y*/, double /*x*/) {
    __asm fld qword ptr [esp + 4]
    __asm fld qword ptr [esp + 12]
    __asm fpatan
    __asm ret
}

void InitList(ObjectList& list) {
    list.vtable = PtrListVtable();
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
bool NamesZone(const itemdb::QuestItemRecord* info, const char* zone) {
    bool found = false;
    for (unsigned i = 0; i < static_cast<unsigned>(info->zonesEnd - info->zonesBegin); ++i)
        if (gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(info->zonesBegin[i])),
                             reinterpret_cast<const unsigned char*>(zone)) == 0)
            found = true;
    return found;
}

CEngineObject* ItemAt(const ItemArray* items, int i) { return static_cast<CEngineObject*>(items->data[i]); }

} // namespace

void CWorldServer::TakeZoneQuestItems(void* itemArray, void* listArg) {
    auto* items = static_cast<ItemArray*>(itemArray);
    auto* list = static_cast<ObjectList*>(listArg);
    // The quest ids this zone's objects ask for (slot 38, slot 40), each once.
    IdArray wanted{IdArrayVtable(), nullptr, 0, 0, 0};
    for (auto* node = objects_.map.First(); node && node->value; node = objects_.map.After(node)) {
        CEngineObject* object = node->value;
        if (!ObjectSlots(object)->IsUsable()) continue;
        int id = ObjectSlots(object)->KeyId();
        if (!Contains(wanted, id)) IdArraySetAtGrow(&wanted, nullptr, wanted.size, id);
    }

    // Quest items move from the array to the list: in single player only those whose
    // description names this zone or whose id an object asks for; otherwise all of them.
    for (int i = 0; i < items->size; ++i) {
        if (ItemAt(items, i)->GetClassId() != kClassQuestItem) continue;
        CEngineObject* item = ItemAt(items, i);
        itemdb::QuestItemRecord* info = reinterpret_cast<itemdb::QuestItemRecord*>(ItemGetPrototypeOriginal(reinterpret_cast<CItem*>(item)));
        if (unknown12D64_ == kKindSinglePlayer && !IsMultiplayerClient()) {
            bool here = info && NamesZone(info, zoneName_);
            if (!here && !Contains(wanted, static_cast<int>(static_cast<CItemQuest*>(item)->unknown_4C))) continue;
        }
        AddTail(*list, ItemAt(items, i));
        int following = items->size - i - 1;
        if (following) std::memmove(&items->data[i], &items->data[i + 1], static_cast<std::size_t>(following) * 4);
        --items->size;
        --i;
    }
    wanted.vtable = IdArrayVtable();
    if (wanted.data) gamecrt::operator_delete(wanted.data);
}

void CWorldServer::RestorePlayerItems(WorldPlayer* player) {
    ObjectList taken;
    InitList(taken);
    if (unknown12D64_ != kKindNetwork) {
        ItemArray* items = &player->Party()->items_;
        if (IsMultiplayerClient()) {
            // A client takes everything back.
            for (int i = 0; i < items->size; ++i) AddTail(taken, ItemAt(items, i));
            ItemArraySetSize(items, nullptr, 0, -1);
        } else {
            TakeZoneQuestItemsAt(this, nullptr, items, &taken);
        }
        if (player) {
            // Each item is given back as a fresh copy (world slot 24) keeping its count; the old
            // one is released.
            for (ObjectList::Node* node = taken.head; node;) {
                ObjectList::Node* next = node->next;
                CEngineObject* item = node->data;
                ObjectList one;
                InitList(one);
                CEngineObject* fresh = DuplicateItem(item);
                static_cast<CItem*>(fresh)->unknown_48 = static_cast<CItem*>(item)->unknown_48;
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
    taken.vtable = PtrListVtable();
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

    CParty* party = player->Party();
    RecordArray records{RecordArrayVtable(), nullptr, 0, 0, 0};
    for (int i = 0; i < party->members_.size; ++i) {
        int index = records.size;
        RecordArraySetSize(&records, nullptr, index + 1, -1);
        records.data[index] = reinterpret_cast<std::uint8_t*>(&party->members_.data[i]);
    }
    // A lost game sends the party back to this zone's first exit.
    if (gameResult_ == kGameResultLost) {
        party->zone_ = *reinterpret_cast<const mfc::CString*>(&zoneName_);
        party->unknown_6C = 0;
    }

    // The party arrives at the centre of its entrance's deploy rectangle (the target zone's exit
    // `unknown_6C`), facing away from the map's centre unless the exit gives a heading.
    double cell = static_cast<double>(CellSize()) * Half();
    float centreX = StoreFloat(static_cast<float>(gridWidth_ * cell * Half()));
    float centreY = StoreFloat(static_cast<float>(gridHeight_ * cell * Half()));
    Quaternion rotation;
    std::memcpy(&rotation, NoRotation(), sizeof(rotation));
    Vector3 start{0.0f, 0.0f, 0.0f};
    std::uint32_t startValue = 0;
    const MapExit* exit = nullptr;
    const MapRecord* zone = FindZone(party->zone_);
    int entrance = static_cast<int>(party->unknown_6C);
    if (zone && entrance >= 0 && entrance < zone->exits.size) {
        exit = &zone->exits.data[entrance];
        start.x = StoreFloat(static_cast<float>((static_cast<double>(exit->deploy[2]) + exit->deploy[0]) * Half()));
        start.y = StoreFloat(static_cast<float>((static_cast<double>(exit->deploy[3]) + exit->deploy[1]) * Half()));
        start.z = 0.0f;
        std::memcpy(&startValue, &exit->view, 4);
    }
    float angle = StoreFloat(static_cast<float>(Fpatan(static_cast<double>(start.y) - centreY,
                                                       static_cast<double>(start.x) - centreX) - FacingOffset()));
    if (exit && *reinterpret_cast<const std::uint8_t*>(&exit->hasDeployAngle)) std::memcpy(&angle, &exit->deployAngle, sizeof(angle));
    Vector3 up{0.0f, 0.0f, 1.0f};
    QuaternionFromAxisAngle(&rotation, nullptr, &up, angle);

    WorldCreateUnitsOriginal(this, nullptr, player, &records, &start, rotation, placements);
    if (placements) PlacementsSetSizeOriginal(placements, nullptr, 0, -1);
    CPlayerServer* state = player->server;
    std::uint32_t lastZ;
    std::memcpy(&lastZ, &state->lastPosition_.z, 4);
    if (lastZ != kNoStart) {
        std::memcpy(&state->lastPosition_, &start, sizeof(start));
        StateSendStart(state, nullptr, &start, startValue, unknown12D64_);
    }
    player->lastTick = tick_;
    if (unknown12D64_ == kKindNoRangeTest) WorldUpdateMercBriefingsOriginal(this);
    if (runScripts) {
        unknown160_ = 1;
        for (int i = 0; i < 5; ++i) ScriptsRun(MainScript());
    }
    records.vtable = RecordArrayVtable();
    if (records.data) gamecrt::operator_delete(records.data);
}

#pragma float_control(pop)

// ---- CopyItem ---------------------------------------------------------------------------------

namespace {

// 006AD000 (RET 0x10). The item factory (world slot 18) makes an item: type, record index, the
// two halves of the item's +0x18, -1.
CEngineObject* __fastcall FactoryMakeItem(void* a0, void*, int a2, int a3, int a4, int a5) { return (CEngineObject*)FactoryMakeItemOriginal(a0, nullptr, a2, a3, a4, a5); }
// 00599FA0 (RET 4). CTypedPtrList<CRefPtr> AddTail: the list holds a reference.
__declspec(naked) void __fastcall CountedAddTail(void*, void*, CEngineObject*) EI_ORIGINAL(0x00599FA0)

} // namespace

CEngineObject* CWorldServer::CopyItem(CEngineObject* item) {
    // The original also builds and frees two empty CPtrLists (vtable 0073B8D4) here, unused.
    auto* source = static_cast<CItem*>(item);
    std::uint32_t packed = source->unknown_18;
    int type = source->type_;
    CEngineObject* factory = ItemFactory();
    CEngineObject* copy = FactoryMakeItem(factory, nullptr, type, static_cast<int>(packed >> 16),
                                          static_cast<int>(packed & 0xFFFF), -1);
    source->CopyToSlot(static_cast<CItem*>(copy), SpellManager());
    std::memcpy(&static_cast<CItem*>(copy)->charge_, &source->charge_, 4);
    // An item below its maximum is kept on the counted list (once).
    float current = source->charge_;
    float maximum = source->chargeMax_;
    if (maximum > current && copy && !counted_.Find(copy)) CountedAddTail(&counted_, nullptr, copy);
    return copy;
}

// ---- Items and spells by name -------------------------------------------------------------------

namespace {

// 006AC970 (RET 4, patched by Rebirth). The item manager makes an item by name.
// 006AD1E0 (RET 8). The item manager makes a spell by name (with world slot 22).
CEngineObject* __fastcall ManagerCreateSpell(void* a0, void*, const char* a2, void* a3) { return (CEngineObject*)FactoryCreateSpellOriginal(a0, nullptr, a2, a3); }

// The world server's item manager (CWorld::itemManager_, 007AFFB4).
CEngineObject* ItemManager() { return WorldServer()->itemManager_; }

} // namespace

// As in CopyItem, the original also builds and frees two empty CPtrLists around the call.
CEngineObject* CWorldServer::CreateItemNamed(const char* name) { return ItemManagerCreateItemOriginal(ItemManager(), nullptr, name); }

CEngineObject* CWorldServer::CreateSpellNamed(const char* name) {
    CEngineObject* manager = ItemManager();   // read before slot 22, as in the original
    return ManagerCreateSpell(manager, nullptr, name, SpellManager());
}

void CWorldServer::AddCounted(CEngineObject* item) {
    if (!item || counted_.Find(item)) return;
    CountedAddTail(&counted_, nullptr, item);   // the original inlines the same AddTail
}

EI_REPLACE_METHOD(0x00594CE0, CWorldServer::CopyItem);

void CWorldServer::UseItemUp(CUnit* unit, CItem* item) {
    if (unit->player) {
        if (auto* link = reinterpret_cast<ItemSourceRef*>(item->unknown_1C)) {
            CParty* party = reinterpret_cast<WorldPlayer*>(unit->player)->Party();
            if (reinterpret_cast<PartyRecords*>(party)->Find(unit->id_)) {
                CEngineObject* source = party->ItemManager()->FindObject(link->itemId);
                if (source) --static_cast<CItem*>(source)->unknown_48;
            }
        }
    }
    --item->unknown_48;
}
EI_REPLACE_METHOD(0x00595020, CWorldServer::UseItemUp);
EI_REPLACE_METHOD(0x00594F30, CWorldServer::AddCounted);
EI_REPLACE_METHOD(0x00594AB0, CWorldServer::CreateItemNamed);
EI_REPLACE_METHOD(0x00594BC0, CWorldServer::CreateSpellNamed);
EI_REPLACE_METHOD(0x005901E0, CWorldServer::TakeZoneQuestItems);
EI_REPLACE_METHOD(0x00590460, CWorldServer::RestorePlayerItems);
EI_REPLACE_METHOD(0x00590700, CWorldServer::Redeploy);
