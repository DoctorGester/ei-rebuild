// CWorldServer::Loot (005942D0): a player takes what a dead body or a container holds. See
// world.h and docs/notes/world.md (seventh round).
#include "world.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/vcall.h"

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

constexpr std::uintptr_t kPtrListVtable = 0x0073BE80;     // CPtrList of objects
constexpr std::uintptr_t kLootListVtable = 0x0073F12C;    // the third output list of 00461CB0
constexpr std::uint32_t kFlagSharedLoot = 0x20000000;     // flags_: the loot is on the shared list
constexpr int kClassQuestItem = 0x14;                     // given to player 0 in multiplayer
constexpr int kPlayerGold = 0xC0;                         // WorldPlayer: money

using ObjectList = mfc::CTypedPtrList<CEngineObject>;

// MFC list helpers instantiated in the exe (not rewritten).
__declspec(naked) void __fastcall PtrListRemoveAll(ObjectList*, void*) EI_ORIGINAL(0x00429F10)
__declspec(naked) void __fastcall PtrListDestruct(ObjectList*, void*) EI_ORIGINAL(0x00427830)
__declspec(naked) void __fastcall LootListRemoveAll(ObjectList*, void*) EI_ORIGINAL(0x00558C50)
__declspec(naked) void __fastcall LootListDestruct(ObjectList*, void*) EI_ORIGINAL(0x00558470)
__declspec(naked) ObjectList::Node* __fastcall PtrListNewNode(ObjectList*, void*, ObjectList::Node* /*prev*/,
                                                            ObjectList::Node* /*next*/) EI_ORIGINAL(0x004621A0)
__declspec(naked) void __fastcall PtrListConstruct(ObjectList*, void*, int /*blockSize*/) EI_ORIGINAL(0x004C98C0)
__declspec(naked) void __fastcall PtrListAddTail(ObjectList*, void*, CEngineObject*) EI_ORIGINAL(0x004C98F0)
__declspec(naked) void __fastcall PtrListRemoveAt(ObjectList*, void*, ObjectList::Node*) EI_ORIGINAL(0x0059A680)
__declspec(naked) CEngineObject** __fastcall PtrListGetNext(ObjectList*, void*, ObjectList::Node** /*position*/)
    EI_ORIGINAL(0x0059A670)
// 0059CAD0 (RET 4). Frees a node of the shared-object list (+0x12E60).
__declspec(naked) void __fastcall SharedListFreeNode(void*, void*, void* /*node*/) EI_ORIGINAL(0x0059CAD0)
// 005301A0. CUnit: whether the unit belongs to a player (+0x24C).
__declspec(naked) int __fastcall UnitHasOwner(CEngineObject*, void*) EI_ORIGINAL(0x005301A0)
// 00665470 (RET 8). The player receives the objects of a list.
__declspec(naked) void __fastcall PlayerGiveItems(WorldPlayer*, void*, ObjectList*, int) EI_ORIGINAL(0x00665470)
// 00461CB0 (RET 0x18). The objects (items, loot, gold) `object` holds for the taker (ECX = the
// world's +0x140); 0 when there is nothing to take.
__declspec(naked) int __fastcall CollectLoot(void*, void*, CWorldServer*, CEngineObject*, ObjectList* /*items*/,
                                             ObjectList* /*second*/, ObjectList* /*third*/, int* /*gold*/)
    EI_ORIGINAL(0x00461CB0)

constexpr int kSlotIsDead = 55;

void InitList(ObjectList& list, std::uintptr_t vtable) {
    list.vtable = reinterpret_cast<void*>(vtable);
    list.head = nullptr;
    list.tail = nullptr;
    list.count = 0;
    list.free = nullptr;
    list.blocks = nullptr;
    list.blockSize = 10;
}

// The SharedObject element of the shared-object list (world_save.cpp has the full layout).
struct SharedLootNode {
    SharedLootNode* next;
    SharedLootNode* prev;
    int objectId;
    int player;
    int gold;                  // +0x10
    void* arrayVtable;
    CEngineObject** objects;   // +0x18
    int count;                 // +0x1C
};

} // namespace

bool CWorldServer::Loot(CEngineObject* object, WorldPlayer* player) {
    ObjectList items;    // [0x18]
    ObjectList second;   // [0x5C]
    ObjectList third;    // [0x40]
    InitList(items, kPtrListVtable);
    InitList(second, kPtrListVtable);
    InitList(third, kLootListVtable);
    int gold;
    // The early returns empty the lists without their destructors' bookkeeping (00558C50 /
    // 00429F10); the others destroy them (00558470 / 00427830).
    auto abandon = [&]() {
        third.vtable = reinterpret_cast<void*>(kLootListVtable);
        LootListRemoveAll(&third, nullptr);
        second.vtable = reinterpret_cast<void*>(kPtrListVtable);
        PtrListRemoveAll(&second, nullptr);
        items.vtable = reinterpret_cast<void*>(kPtrListVtable);
        PtrListRemoveAll(&items, nullptr);
    };
    auto destroy = [&]() {
        LootListDestruct(&third, nullptr);
        PtrListDestruct(&second, nullptr);
        PtrListDestruct(&items, nullptr);
    };

    if (!player) {
        abandon();
        return false;
    }
    if (UnitHasOwner(object, nullptr) && ei::VCall<int>(object, kSlotIsDead) == 0) {
        abandon();
        return false;   // a living unit of a player
    }

    if (object->flags_ & kFlagSharedLoot) {
        auto& list = *reinterpret_cast<mfc::CTypedPtrList<void>*>(reinterpret_cast<std::uint8_t*>(this) + 0x12E60);
        SharedLootNode* node = reinterpret_cast<SharedLootNode*>(list.head);
        while (node && node->objectId != object->id_) node = node->next;
        if (!node) {
            abandon();
            return false;
        }
        for (int i = 0; i < node->count; ++i) {
            CEngineObject* item = node->objects[i];
            ObjectList::Node* entry = PtrListNewNode(&items, nullptr, items.tail, nullptr);
            entry->data = item;
            if (items.tail) items.tail->next = entry;
            else items.head = entry;
            items.tail = entry;
        }
        At<int>(player, kPlayerGold) += node->gold;
        PlayerGiveItems(player, nullptr, &items, 0);
        auto* raw = reinterpret_cast<SharedLootNode*>(node);
        if (raw == reinterpret_cast<SharedLootNode*>(list.head)) list.head = reinterpret_cast<decltype(list.head)>(raw->next);
        else raw->prev->next = raw->next;
        if (raw == reinterpret_cast<SharedLootNode*>(list.tail)) list.tail = reinterpret_cast<decltype(list.tail)>(raw->prev);
        else raw->next->prev = raw->prev;
        SharedListFreeNode(&list, nullptr, raw);
        abandon();
        return true;
    }

    if (!CollectLoot(At<void*>(this, 0x140), nullptr, this, object, &items, &second, &third, &gold)) {
        destroy();
        return false;
    }
    if (IsMultiplayerClient()) {
        // Quest items go to player 0.
        WorldPlayer* first = nullptr;
        {
            PlayerIterator it;
            for (WorldPlayer* p = it.First(ei::VCall<CEngineObject*>(this, 20)); p; p = it.Next()) {
                if (p->id == 0) {
                    first = p;
                    break;
                }
            }
        }
        if (first) {
            ObjectList quest;
            PtrListConstruct(&quest, nullptr, 10);
            for (ObjectList::Node* node = items.head; node;) {
                ObjectList::Node* next = node->next;
                CEngineObject* item = node->data;
                if (item->GetClassId() == kClassQuestItem) {
                    PtrListAddTail(&quest, nullptr, item);
                    PtrListRemoveAt(&items, nullptr, node);
                }
                node = next;
            }
            if (quest.count > 0) PlayerGiveItems(first, nullptr, &quest, 0);
            for (ObjectList::Node* position = quest.head; position;)
                (*PtrListGetNext(&quest, nullptr, &position))->Release();
            PtrListDestruct(&quest, nullptr);
        }
    }
    At<int>(player, kPlayerGold) += gold;
    PlayerGiveItems(player, nullptr, &items, 0);
    for (ObjectList::Node* position = items.head; position;)
        (*PtrListGetNext(&items, nullptr, &position))->Release();
    bool found = items.count > 0 || gold > 0;
    destroy();
    return found;
}

EI_REPLACE_METHOD(0x005942D0, CWorldServer::Loot);
