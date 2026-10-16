// CWorldServer: the party arriving in a zone (Redeploy 00590700) and the quest items it brings
// back (00590460, 005901E0). See world.h and docs/notes/world.md.
#include "script.h"
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
#include "ei/x87.h"

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
// 0059D450 (RET 8). CArray<int>::SetAtGrow(index, value).
__declspec(naked) void __fastcall IdArraySetAtGrow(IdArray*, void*, int, int) EI_ORIGINAL(0x0059D450)
// 0059CB50 (RET 8). The item array's SetSize(size, growBy).
__declspec(naked) void __fastcall ItemArraySetSize(ItemArray*, void*, int, int) EI_ORIGINAL(0x0059CB50)
// 006A0110 (CItem::GetPrototype). A quest item's description: the names of the zones it belongs to.
// 00665470 (RET 8). Puts a list of items into the player's inventory.
void __fastcall PlayerGiveItems(WorldPlayer* a0, void*, ObjectList* a2, int a3) { reinterpret_cast<CPlayer*>(a0)->GiveItems(a2, a3); }
// 0045E850 (RET 4). The map database's zone record by name.
const MapRecord* FindZone(const char* name) {
    return GameMapDatabase()->FindMap(name);
}
// 0049ECC0 (RET 8). Quaternion from an axis and an angle.
__declspec(naked) void* __fastcall QuaternionFromAxisAngle(void*, void*, const Vector3*, float) EI_ORIGINAL(0x0049ECC0)

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
        if (libcrt::Mbsicmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(info->zonesBegin[i])),
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
        itemdb::QuestItemRecord* info = reinterpret_cast<itemdb::QuestItemRecord*>(reinterpret_cast<CItem*>(item)->GetPrototype());
        if (sessionKind_ == kKindSinglePlayer && !IsMultiplayerClient()) {
            bool here = info && NamesZone(info, zoneName_);
            if (!here && !Contains(wanted, static_cast<int>(static_cast<CItemQuest*>(item)->questItemId_))) continue;
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
    if (sessionKind_ != kKindNetwork) {
        ItemArray* items = &player->Party()->items_;
        if (IsMultiplayerClient()) {
            // A client takes everything back.
            for (int i = 0; i < items->size; ++i) AddTail(taken, ItemAt(items, i));
            ItemArraySetSize(items, nullptr, 0, -1);
        } else {
            TakeZoneQuestItems(items, &taken);
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
                static_cast<CItem*>(fresh)->count_ = static_cast<CItem*>(item)->count_;
                AddTail(one, fresh);
                PlayerGiveItems(player, nullptr, &one, 0);
                item->Release();
                PtrListDestructOriginal(&one);
                node = next;
            }
            PtrListRemoveAllNodesOriginal(&taken);
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
    RestorePlayerItems(player);
    if (!(sessionKind_ == kKindSinglePlayer || sessionKind_ == kKindNoRangeTest) || zoneLoaded_ == 0) return;

    CParty* party = player->Party();
    RecordArray records{RecordArrayVtable(), nullptr, 0, 0, 0};
    for (int i = 0; i < party->members_.size; ++i) {
        int index = records.size;
        RecordArraySetSizeOriginal(&records, nullptr, index + 1, -1);
        records.data[index] = reinterpret_cast<std::uint8_t*>(&party->members_.data[i]);
    }
    // A lost game sends the party back to this zone's first exit.
    if (gameResult_ == kGameResultLost) {
        party->zone_ = *reinterpret_cast<const mfc::CString*>(&zoneName_);
        party->entrance_ = 0;
    }

    // The party arrives at the centre of its entrance's deploy rectangle (the target zone's exit
    // `entrance_`), facing away from the map's centre unless the exit gives a heading.
    double cell = static_cast<double>(CellSize()) * Half();
    float centreX = static_cast<float>(gridWidth_ * cell * Half());
    float centreY = static_cast<float>(gridHeight_ * cell * Half());
    Quaternion rotation;
    std::memcpy(&rotation, NoRotation(), sizeof(rotation));
    Vector3 start{0.0f, 0.0f, 0.0f};
    std::uint32_t startValue = 0;
    const MapExit* exit = nullptr;
    const MapRecord* zone = FindZone(party->zone_);
    int entrance = static_cast<int>(party->entrance_);
    if (zone && entrance >= 0 && entrance < zone->exits.size) {
        exit = &zone->exits.data[entrance];
        start.x = static_cast<float>((static_cast<double>(exit->deploy[2]) + exit->deploy[0]) * Half());
        start.y = static_cast<float>((static_cast<double>(exit->deploy[3]) + exit->deploy[1]) * Half());
        start.z = 0.0f;
        std::memcpy(&startValue, &exit->view, 4);
    }
    float angle = static_cast<float>(x87::fpatan(static_cast<double>(start.y) - centreY,
                                                       static_cast<double>(start.x) - centreX) - FacingOffset());
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
        state->SendPosition04(&start, startValue, sessionKind_);
    }
    player->lastTick = tick_;
    if (sessionKind_ == kKindNoRangeTest) UpdateMercBriefings();
    if (runScripts) {
        scriptsActive_ = 1;
        for (int i = 0; i < 5; ++i) ScriptTick(MainScript());
    }
    records.vtable = RecordArrayVtable();
    if (records.data) gamecrt::operator_delete(records.data);
}

// ---- CopyItem ---------------------------------------------------------------------------------

namespace {

// 006AD000 (RET 0x10). The item factory (world slot 18) makes an item: type, record index, the
// two halves of the item's +0x18, -1.
CEngineObject* __fastcall FactoryMakeItem(void* a0, void*, int a2, int a3, int a4, int a5) { return (CEngineObject*)static_cast<CItemManager*>(a0)->MakeItem(a2, a3, a4, a5); }
// 00599FA0 (RET 4). CTypedPtrList<CRefPtr> AddTail: the list holds a reference.
__declspec(naked) void __fastcall CountedAddTail(void*, void*, CEngineObject*) EI_ORIGINAL(0x00599FA0)

} // namespace

CEngineObject* CWorldServer::CopyItem(CEngineObject* item) {
    // The original also builds and frees two empty CPtrLists (vtable 0073B8D4) here, unused.
    auto* source = static_cast<CItem*>(item);
    std::uint32_t packed = source->prototypeId_;
    int type = source->type_;
    CEngineObject* factory = ItemFactory();
    CEngineObject* copy = FactoryMakeItem(factory, nullptr, type, static_cast<int>(packed >> 16),
                                          static_cast<int>(packed & 0xFFFF), -1);
    source->CopyTo(static_cast<CItem*>(copy), SpellManager());
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

// The world server's item manager (CWorld::itemManager_, 007AFFB4).
CEngineObject* ItemManager() { return WorldServer()->itemManager_; }

} // namespace

// As in CopyItem, the original also builds and frees two empty CPtrLists around the call.
CEngineObject* CWorldServer::CreateItemNamed(const char* name) { return ItemManagerCreateItemOriginal(ItemManager(), nullptr, name); }

CEngineObject* CWorldServer::CreateSpellNamed(const char* name) {
    CEngineObject* manager = ItemManager();   // read before slot 22, as in the original
    return (CEngineObject*)static_cast<CItemManager*>(manager)->CreateSpellItem(name, static_cast<CSpellManager*>(SpellManager()));
}

void CWorldServer::AddCounted(CEngineObject* item) {
    if (!item || counted_.Find(item)) return;
    CountedAddTail(&counted_, nullptr, item);   // the original inlines the same AddTail
}

EI_REPLACE_METHOD(0x00594CE0, CWorldServer::CopyItem);

void CWorldServer::UseItemUp(CUnit* unit, CItem* item) {
    if (unit->player) {
        if (auto* link = reinterpret_cast<ItemSourceRef*>(item->link_)) {
            CParty* party = reinterpret_cast<WorldPlayer*>(unit->player)->Party();
            if (reinterpret_cast<PartyRecords*>(party)->Find(unit->id_)) {
                CEngineObject* source = party->ItemManager()->FindObject(link->itemId);
                if (source) --static_cast<CItem*>(source)->count_;
            }
        }
    }
    --item->count_;
}
EI_REPLACE_METHOD(0x00595020, CWorldServer::UseItemUp);
EI_REPLACE_METHOD(0x00594F30, CWorldServer::AddCounted);
EI_REPLACE_METHOD(0x00594AB0, CWorldServer::CreateItemNamed);
EI_REPLACE_METHOD(0x00594BC0, CWorldServer::CreateSpellNamed);
EI_REPLACE_METHOD(0x005901E0, CWorldServer::TakeZoneQuestItems);
EI_REPLACE_METHOD(0x00590460, CWorldServer::RestorePlayerItems);
EI_REPLACE_METHOD(0x00590700, CWorldServer::Redeploy);
