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
#include "lib/mfc_list.h"
#include "game_lists.h"

#include "ei/hook.h"
#include "originals_engine.h"

#include <cstring>
#include "originals_gameplay.h"
#include "originals_library.h"
#include "originals_script.h"
#include "ei/x87.h"
#include "lib/mfc_array.h"
#include "game_data.h"
#include "game_singletons.h"

namespace {

// Values and vtables read from the exe's memory as the original does (rule 9).
float Half() { return 0.5f /*0073B86C*/; }            // 0.5f
float CellSize() { return 1.0f /*0073F904*/; }        // the map cell size
constexpr float kFacingOffset = /*007AFED8*/ 1.5707964f;   // pi / 2
const void* NoRotation() { return ei::GamePointer<const void>(0x007C2DE0); }           // 16 bytes: the identity rotation
void* PtrListVtable() { return ei::GamePointer<void>(0x0073BE80); }                    // the item lists' CPtrList
void* IdArrayVtable() { return ei::GamePointer<void>(0x0073EBD4); }                    // CArray<int>
void* RecordArrayVtable() { return ei::GamePointer<void>(0x0073FAA4); }                // the record pointers' CArray

constexpr int kClassQuestItem = 0x14;
constexpr int kKindSinglePlayer = 1;
constexpr int kKindNoRangeTest = 3;
constexpr int kKindNetwork = 4;
constexpr int kGameResultLost = 2;
constexpr std::uint32_t kNoStart = 0xC4A42000;   // -1313.0f as the state's last z: no start position

using ObjectList = mfc::CTypedPtrList<CItem>;

// The player's carried items (CParty::items_): an aligned array of item pointers (its SetSize
// 0059CB50 is lib::AlignedArray's).
using ItemArray = PartyItemArray;

// CArray<int> (vtable 0073EBD4), and the CArray of record pointers CreateUnits takes (0073FAA4).
using IdArray = mfc::CArray<int>;
using RecordArray = RecordPtrArray;

// 0045E850 (RET 4). The map database's zone record by name.
const MapRecord* FindZone(const char* name) {
    return TheMapDatabase()->FindMap(name);
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
void AddTail(ObjectList& list, CItem* object) {
    ObjectList::Node* node = list.NewNode(list.tail, nullptr);
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
        if (gamecrt::mbsicmp(static_cast<const char*>(info->zonesBegin[i]),
                            zone) == 0)
            found = true;
    return found;
}

CItem* ItemAt(const ItemArray* items, int i) { return items->data[i]; }

} // namespace

void CWorldServer::TakeZoneQuestItems(ItemArray* items, ObjectList* list) {
    // The quest ids this zone's objects ask for (slot 38, slot 40), each once.
    IdArray wanted{IdArrayVtable(), nullptr, 0, 0, 0};
    for (auto* node = objects_.map.First(); node && node->value; node = objects_.map.After(node)) {
        CEngineObject* object = node->value;
        if (!ObjectSlots(object)->IsUsable()) continue;
        int id = ObjectSlots(object)->KeyId();
        if (!Contains(wanted, id)) wanted.SetAtGrow(wanted.size, id);
    }

    // Quest items move from the array to the list: in single player only those whose
    // description names this zone or whose id an object asks for; otherwise all of them.
    for (int i = 0; i < items->size; ++i) {
        if (ItemAt(items, i)->GetClassId() != kClassQuestItem) continue;
        CItem* item = ItemAt(items, i);
        itemdb::QuestItemRecord* info = reinterpret_cast<itemdb::QuestItemRecord*>(item->GetPrototype());
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

void CWorldServer::RestorePlayerItems(CPlayer* player) {
    ObjectList taken;
    InitList(taken);
    if (sessionKind_ != kKindNetwork) {
        ItemArray* items = &player->Party()->items_;
        if (IsMultiplayerClient()) {
            // A client takes everything back.
            for (int i = 0; i < items->size; ++i) AddTail(taken, ItemAt(items, i));
            items->SetSize(0, -1);
        } else {
            TakeZoneQuestItems(items, &taken);
        }
        if (player) {
            // Each item is given back as a fresh copy (world slot 24) keeping its count; the old
            // one is released.
            for (ObjectList::Node* node = taken.head; node;) {
                ObjectList::Node* next = node->next;
                CItem* item = node->data;
                ObjectList one;
                InitList(one);
                auto* fresh = static_cast<CItem*>(DuplicateItem(item));
                fresh->count_ = item->count_;
                AddTail(one, fresh);
                player->GiveItems(&one, 0);
                item->Release();
                mfc::impl::DestructList<mfc::impl::ItemListClass>(one);
                node = next;
            }
            taken.RemoveAll();
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

void CWorldServer::Redeploy(CPlayer* player, void* placements, char runScripts) {
    if (!player) return;
    RestorePlayerItems(player);
    if (!(sessionKind_ == kKindSinglePlayer || sessionKind_ == kKindNoRangeTest) || zoneLoaded_ == 0) return;

    CParty* party = player->Party();
    RecordArray records{RecordArrayVtable(), nullptr, 0, 0, 0};
    for (int i = 0; i < party->members_.size; ++i) {
        int index = records.size;
        records.SetSize(index + 1, -1);
        records.data[index] = reinterpret_cast<std::uint8_t*>(&party->members_.data[i]);
    }
    // A lost game sends the party back to this zone's first exit.
    if (gameResult_ == kGameResultLost) {
        party->zone_ = zoneName_;
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
                                                       static_cast<double>(start.x) - centreX) - kFacingOffset);
    if (exit && *reinterpret_cast<const std::uint8_t*>(&exit->hasDeployAngle)) std::memcpy(&angle, &exit->deployAngle, sizeof(angle));
    Vector3 up{0.0f, 0.0f, 1.0f};
    QuaternionFromAxisAngle(&rotation, nullptr, &up, angle);

    WorldCreateUnitsOriginal(this, nullptr, player, &records, &start, rotation, placements);
    if (placements)   // 0059C3D0
        mfc::impl::Placements::SetSize(static_cast<mfc::impl::Placements::Array*>(placements), nullptr, 0, -1);
    CPlayerServer* state = player->Server();
    std::uint32_t lastZ;
    std::memcpy(&lastZ, &state->lastPosition_.z, 4);
    if (lastZ != kNoStart) {
        std::memcpy(&state->lastPosition_, &start, sizeof(start));
        state->SendPosition04(&start, startValue, sessionKind_);
    }
    player->lastTick_ = tick_;
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

// 00599FA0. CTypedPtrList<CRefPtr> AddTail: the list holds a reference (gamelists::CountedAddTail).
void AddCountedTail(mfc::CTypedPtrList<CEngineObject>* list, CEngineObject* object) {
    gamelists::CountedAddTail(mfc::impl::ListAs<gamelists::EngineObjects>(list), nullptr, object);
}

} // namespace

CEngineObject* CWorldServer::CopyItem(CEngineObject* item) {
    // The original also builds and frees two empty CPtrLists (vtable 0073B8D4) here, unused.
    auto* source = static_cast<CItem*>(item);
    std::uint32_t packed = source->prototypeId_;
    int type = source->type_;
    CEngineObject* factory = ItemFactory();
    CEngineObject* copy = (CEngineObject*)static_cast<CItemManager*>(factory)->MakeItem(type, static_cast<int>(packed >> 16), static_cast<int>(packed & 0xFFFF), -1);
    source->CopyTo(static_cast<CItem*>(copy), SpellManager());
    std::memcpy(&static_cast<CItem*>(copy)->charge_, &source->charge_, 4);
    // An item below its maximum is kept on the counted list (once).
    float current = source->charge_;
    float maximum = source->chargeMax_;
    if (maximum > current && copy && !counted_.Find(copy)) AddCountedTail(&counted_, copy);
    return copy;
}

// ---- Items and spells by name -------------------------------------------------------------------

namespace {

// 006AC970 (RET 4, patched by Rebirth). The item manager makes an item by name.
// 006AD1E0 (RET 8). The item manager makes a spell by name (with world slot 22).

// The world server's item manager (CWorld::itemManager_, 007AFFB4).
CEngineObject* ItemManager() { return TheServerWorld()->itemManager_; }

} // namespace

// As in CopyItem, the original also builds and frees two empty CPtrLists around the call.
CItem* CWorldServer::CreateItemNamed(const char* name) { return ItemManagerCreateItemOriginal(ItemManager(), nullptr, name); }

CItem* CWorldServer::CreateSpellNamed(const char* name) {
    CEngineObject* manager = ItemManager();   // read before slot 22, as in the original
    return static_cast<CItemManager*>(manager)->CreateSpellItem(name, SpellManager());
}

void CWorldServer::AddCounted(CEngineObject* item) {
    if (!item || counted_.Find(item)) return;
    AddCountedTail(&counted_, item);   // the original inlines the same AddTail
}

EI_REPLACE_METHOD(0x00594CE0, CWorldServer::CopyItem);

void CWorldServer::UseItemUp(CUnit* unit, CItem* item) {
    if (unit->player) {
        if (auto* link = reinterpret_cast<ItemSourceRef*>(item->link_)) {
            CParty* party = unit->player->Party();
            if (party->Find(unit->id_)) {
                CEngineObject* source = party->ItemManager()->FindObject(link->itemId);
                if (source) --static_cast<CItem*>(source)->count_;
            }
        }
    }
    --item->count_;
}
EI_REWRITTEN(0x00595020, CWorldServer::UseItemUp);
EI_REWRITTEN(0x00594F30, CWorldServer::AddCounted);
EI_REWRITTEN(0x00594AB0, CWorldServer::CreateItemNamed);
EI_REWRITTEN(0x00594BC0, CWorldServer::CreateSpellNamed);
EI_REWRITTEN(0x005901E0, CWorldServer::TakeZoneQuestItems);
EI_REWRITTEN(0x00590460, CWorldServer::RestorePlayerItems);
EI_REWRITTEN(0x00590700, CWorldServer::Redeploy);
