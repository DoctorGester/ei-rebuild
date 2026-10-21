// CWorldServer::Loot (005942D0): a player takes what a dead body or a container holds. See
// world.h and docs/notes/world.md.
#include "unit.h"
#include "world.h"

#include "map_object.h"
#include "player_messages.h"
#include "party.h"

#include "lib/crt.h"
#include "effects.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "originals_world.h"
#include "originals_gameplay.h"
#include "originals_library.h"
#include "lib/mfc_list.h"
#include "game_lists.h"
#include "game_singletons.h"

namespace {

constexpr std::uint32_t kFlagSharedLoot = 0x20000000;     // flags_: the loot is on the shared list
constexpr int kClassQuestItem = 0x14;                     // given to player 0 in multiplayer

using ObjectList = mfc::CTypedPtrList<CEngineObject>;
using GivenItems = mfc::CTypedPtrList<CItem>;

// 0059A670 (a byte-identical copy of NextParticle, effects.h): the item at `position`, which moves on.
CItem* NextItem(GivenItems* list, GivenItems::Node** position) {
    return *reinterpret_cast<CItem**>(
        NextParticle(list, nullptr, reinterpret_cast<mfc::CTypedPtrList<Particle>::Node**>(position)));
}
// 00461CB0 (RET 0x18). The objects (items, loot, gold) `object` holds for the taker (ECX = the
// world's hoover); 0 when there is nothing to take.

template <typename T>
void InitList(mfc::CTypedPtrList<T>& list, void* vtable) {
    list.vtable = vtable;
    list.head = nullptr;
    list.tail = nullptr;
    list.count = 0;
    list.free = nullptr;
    list.blocks = nullptr;
    list.blockSize = 10;
}

} // namespace

bool CWorldServer::Loot(CEngineObject* object, WorldPlayer* player) {
    GivenItems items;    // [0x18]
    ObjectList second;   // [0x5C]
    ObjectList third;    // [0x40]
    InitList(items, mfc::impl::ItemListClass::Vtable());
    InitList(second, mfc::impl::ItemListClass::Vtable());
    InitList(third, mfc::impl::LootListClass::Vtable());
    int gold;
    // The early returns empty the lists without their destructors' bookkeeping (00558C50 /
    // 00429F10); the others destroy them (00558470 / 00427830).
    auto abandon = [&]() {
        third.vtable = mfc::impl::LootListClass::Vtable();
        third.RemoveAll();
        second.vtable = mfc::impl::ItemListClass::Vtable();
        second.RemoveAll();
        items.vtable = mfc::impl::ItemListClass::Vtable();
        items.RemoveAll();
    };
    auto destroy = [&]() {
        mfc::impl::DestructList<mfc::impl::LootListClass>(third);
        mfc::impl::DestructList<mfc::impl::ItemListClass>(second);
        mfc::impl::DestructList<mfc::impl::ItemListClass>(items);
    };

    if (!player) {
        abandon();
        return false;
    }
    if (static_cast<CUnit*>(object)->HasPlayer() && ObjectSlots(object)->IsDead() == 0) {
        abandon();
        return false;   // a living unit of a player
    }

    if (object->flags_ & kFlagSharedLoot) {
        // The shared-object list's entry for the object (SharedObject: its unknown_08 is the gold).
        SharedObjectList& list = sharedObjects_;
        SharedObjectList::Node* node = list.head;
        while (node && node->data.objectId != object->id_) node = node->next;
        if (!node) {
            abandon();
            return false;
        }
        for (int i = 0; i < node->data.objects.size; ++i) {
            auto* item = static_cast<CItem*>(node->data.objects.data[i]);
            GivenItems::Node* entry = items.NewNode(items.tail, nullptr);
            entry->data = item;
            if (items.tail) items.tail->next = entry;
            else items.head = entry;
            items.tail = entry;
        }
        player->Party()->Money() += static_cast<int>(node->data.money);
        reinterpret_cast<CPlayer*>(player)->GiveItems(&items, 0);
        if (node == list.head) list.head = node->next;
        else node->prev->next = node->next;
        if (node == list.tail) list.tail = node->prev;
        else node->next->prev = node->prev;
        mfc::impl::FreeNodeAs<gamelists::SharedObjects>(&list, node);   // 0059CAD0
        abandon();
        return true;
    }

    if (!HooverCollectLoot(hoover_, nullptr, this, reinterpret_cast<char*>(object), &items, &second, &third, &gold)) {   // 00461CB0
        destroy();
        return false;
    }
    if (IsMultiplayerClient()) {
        // Quest items go to player 0.
        WorldPlayer* first = nullptr;
        {
            PlayerIterator it;
            for (WorldPlayer* p = it.First(PlayerList()); p; p = it.Next()) {
                if (p->id == 0) {
                    first = p;
                    break;
                }
            }
        }
        if (first) {
            GivenItems quest;
            mfc::impl::ConstructListHeader<mfc::impl::ItemListClass>(&quest, 10);
            for (GivenItems::Node* node = items.head; node;) {
                GivenItems::Node* next = node->next;
                CItem* item = node->data;
                if (item->GetClassId() == kClassQuestItem) {
                    quest.AddTail(item);
                    items.RemoveAt(node);
                }
                node = next;
            }
            if (quest.count > 0) reinterpret_cast<CPlayer*>(first)->GiveItems(&quest, 0);
            for (GivenItems::Node* position = quest.head; position;)
                NextItem(&quest, &position)->Release();
            mfc::impl::DestructList<mfc::impl::ItemListClass>(quest);
        }
    }
    player->Party()->Money() += gold;
    reinterpret_cast<CPlayer*>(player)->GiveItems(&items, 0);
    for (GivenItems::Node* position = items.head; position;)
        NextItem(&items, &position)->Release();
    bool found = items.count > 0 || gold > 0;
    destroy();
    return found;
}

// ---- LootDeadUnits ----------------------------------------------------------------------------

namespace {

// STLport list<unsigned> on the game's node pool (12-byte nodes {next, prev, value}); the
// constructor (00404A70) allocates the head node, the destructor frees the nodes (00404D10) and
// the head (00498C40). As in world_tick.cpp.
class IdList {
public:
    struct Node {
        Node* next;
        Node* prev;
        unsigned value;
    };

    IdList() {
        head_ = static_cast<Node*>(stlp::allocate(sizeof(Node)));
        head_->next = head_->prev = head_;
    }
    ~IdList() {
        for (Node* node = head_->next; node != head_;) {
            Node* next = node->next;
            stlp::deallocate(node, sizeof(Node));
            node = next;
        }
        head_->next = head_->prev = head_;
        stlp::deallocate(head_, sizeof(Node));
    }
    IdList(const IdList&) = delete;
    IdList& operator=(const IdList&) = delete;

    // insert(end(), value): 004049D0 creates the node, 00404A40 links it.
    void PushBack(unsigned value) {
        auto* node = static_cast<Node*>(stlp::allocate(sizeof(Node)));
        node->value = value;
        node->next = head_;
        node->prev = head_->prev;
        head_->prev->next = node;
        head_->prev = node;
    }
    Node* Begin() const { return head_->next; }
    Node* End() const { return head_; }

private:
    Node* head_;
};

} // namespace

void CWorldServer::LootDeadUnits() {
    // The walk starts before the player is looked up; unlike most walks it does not stop at an
    // empty entry.
    auto* node = objects_.map.First();
    CEngineObject* object = node ? node->value : nullptr;
    WorldPlayer* local = nullptr;
    {
        PlayerIterator it;
        for (WorldPlayer* player = it.First(PlayerList()); player; player = it.Next())
            if (player->id == 0) {
                local = player;
                break;
            }
    }
    if (!local) return;

    IdList dead;
    while (node) {
        if (object->GetClassId() == world::kClassUnit && ObjectSlots(object)->IsDead()) {
            TheServerWorld()->Loot(object, local);
            dead.PushBack(static_cast<unsigned>(object->id_));
        }
        node = objects_.map.After(node);
        object = node ? node->value : nullptr;
    }
    for (IdList::Node* id = dead.Begin(); id != dead.End(); id = id->next) WorldRemoveObject(TheServerWorld(), nullptr, id->value);
}

EI_REWRITTEN(0x005942D0, CWorldServer::Loot);
EI_REWRITTEN(0x00593D60, CWorldServer::LootDeadUnits);
