// The game client's tick (0041CCD0, slot 14) and its award handling (0041CD80): timers count down,
// and received awards (+0x2608) are applied to the local player. See net_classes.h and
// docs/notes/network.md.

#include "net_messages.h"
#include "core_refcount.h"
#include "item.h"
#include "party.h"
#include "player_messages.h"

#include "lib/mfc.h"

#include "ei/hook.h"
#include "originals_gameplay.h"
#include "originals_world.h"
#include "originals_library.h"
#include "originals_core.h"
#include "lib/mfc_list.h"
#include "game_slots.h"
#include "lib/crt.h"

#include <cstring>

namespace net {

namespace {

using ObjectList = mfc::CTypedPtrList<CEngineObject>;

// The client base tick (00442AD0) and the timers list erase (library).
__declspec(naked) void __fastcall ClientBaseTick(ClientBase* /*self*/, void*, double /*dt*/) EI_ORIGINAL(0x00442AD0)
__declspec(naked) void** __fastcall TimersErase(void* /*list*/, void*, void** /*out*/, void* /*position*/) EI_ORIGINAL(0x004246E0)

// The world (0x7AFF60) and its players (outside this range).
void __fastcall PlayerGiveItems(void* a0, void*, ObjectList* a2, int a3) { PlayerGiveItemsOriginal(a0, nullptr, (void*)a2, a3); }
void __fastcall PlayerRemoveItem(void* player, void*, int index) { PlayerRemoveItemOriginal(player, nullptr, index); }
// MFC list helpers instantiated in the exe: the counted list's NewNode and element assignment,
// RemoveAll of both list kinds.
ObjectList::Node* __fastcall CountedNewNode(ObjectList* a0, void*, ObjectList::Node* a2, ObjectList::Node* a3) { return (ObjectList::Node*)ListNewNode429EA0Original((void*)a0, nullptr, (void*)a2, (void*)a3); }
__declspec(naked) void __fastcall CountedRemoveAll(ObjectList*) EI_ORIGINAL(0x00429E50)
inline void __fastcall PtrListRemoveAll(ObjectList* a0) { PtrListRemoveAllNodesOriginal((void*)a0); }

constexpr int kClassQuestItem = 0x14;

void* World() { return WorldServer(); }
// 00594180: player 0 of the world (the local player), or null.
CPlayer* LocalPlayer() { return static_cast<CPlayer*>(WorldGetPlayer(World(), nullptr, 0)); }
float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
std::int32_t GameMode() { return *reinterpret_cast<const std::int32_t*>(0x007C2CC4); }
std::uint32_t Unknown7B00BC() { return *reinterpret_cast<const std::uint32_t*>(0x007B00BC); }
void*& NodeFreeList() { return *reinterpret_cast<void**>(0x0079B228); }

void InitList(ObjectList& list, void* vtable) {
    list.vtable = vtable;
    list.head = nullptr;
    list.tail = nullptr;
    list.count = 0;
    list.free = nullptr;
    list.blocks = nullptr;
    list.blockSize = 10;
}

} // namespace

// 0041CD80: applies the received awards (+0x2608: counted award messages, net_server_awards.cpp)
// to player 0 and empties the list. Kind 0: experience (005911C0 with the value's bits); 1: money
// (the party's, the value truncated), then an empty item list given; 2: the item named by the text,
// given; 3: the first given item whose IsSameAs (slot 14) with the named item returns 0 is taken
// (by id); 4: the party's quest items removed and every given quest item (class 0x14) taken.
void ClientSession::ApplyAwards() {
    ClientSession* self = this;
    CPlayer* player = LocalPlayer();
    if (!player) return;
    for (RefNode* node = static_cast<RefNode*>(self->awards_)->next; node != self->awards_; node = node->next) {
        auto* award = static_cast<AwardMessage*>(node->data.ptr);
        const char* name = award->text_.start;
        switch (award->kind_) {
        case 0: {
            std::uint32_t bits;
            std::memcpy(&bits, &award->value_, 4);
            WorldAwardExperienceOriginal(World(), nullptr, nullptr, bits);
            break;
        }
        case 1: {
            ObjectList none;
            InitList(none, mfc::impl::ItemListClass::Vtable());
            player->Party()->Money() += static_cast<int>(gamecrt::Ftol(award->value_));
            PlayerGiveItems(player, nullptr, &none, 0);
            none.vtable = mfc::impl::ItemListClass::Vtable();
            PtrListRemoveAll(&none);
            break;
        }
        case 2: {
            ObjectList items;
            InitList(items, gameslots::ItemObjectListClass::Vtable());
            CEngineObject* item = ItemManagerCreateItemOriginal(player->Party()->ItemManager(), nullptr, name);
            ObjectList::Node* added = CountedNewNode(&items, nullptr, items.tail, nullptr);
            ReferenceAssign(&added->data, nullptr, item);
            if (items.tail) items.tail->next = added;
            else items.head = added;
            items.tail = added;
            PlayerGiveItems(player, nullptr, &items, 0);
            items.vtable = gameslots::ItemObjectListClass::Vtable();
            CountedRemoveAll(&items);
            break;
        }
        case 3: {
            auto* item = static_cast<CItem*>(ItemManagerCreateItemOriginal(player->Party()->ItemManager(), nullptr, name));
            if (item) ++item->refCount_;
            for (ObjectList::Node* position = player->items_.head; position;) {
                ObjectList::Node* current = position;
                position = position->next;
                auto* owned = static_cast<CItem*>(current->data);
                if (!owned->IsSameAs(item)) {
                    std::int32_t id = owned->id_;
                    if (id >= 0) PlayerRemoveItem(player, nullptr, id);
                    break;
                }
            }
            if (item) item->Release();
            break;
        }
        case 4:
            PartyResetItems(player->Party());
            for (ObjectList::Node* position = player->items_.head; position;) {
                ObjectList::Node* current = position;
                position = position->next;
                CEngineObject* owned = current->data;
                if (owned->GetClassId() == kClassQuestItem) PlayerRemoveItem(player, nullptr, owned->id_);
            }
            break;
        default:
            break;
        }
    }

    // The list emptied inline: each award released, each node back to the 16-byte pool list.
    auto* sentinel = static_cast<RefNode*>(self->awards_);
    for (RefNode* node = sentinel->next; node != self->awards_;) {
        RefNode* current = node;
        node = node->next;
        if (CRefObject* award = current->data.ptr) {
            --award->refs_;
            if (award->refs_ == 0) delete award;
        }
        if (current) {
            current->next = static_cast<RefNode*>(NodeFreeList());
            NodeFreeList() = current;
        }
    }
    static_cast<RefNode*>(self->awards_)->next = static_cast<RefNode*>(self->awards_);
    static_cast<RefNode*>(self->awards_)->prev = static_cast<RefNode*>(self->awards_);
}
EI_REPLACE_METHOD(0x0041CD80, ClientSession::ApplyAwards);

// 0041CCD0 (slot 14): the client base tick, then the timers (+0x2604: {next, prev, seconds, ...})
// count down by `dt` (as a float) and those below zero are dropped. Awards are applied in a game of
// mode 3 when player 0 exists with its connection in state 1 and 007B00BC is set.
void ClientSession::TickImpl(double dt) {
    ClientSession* self = this;
    ClientBaseTick(self, nullptr, dt);
    void* timers = &self->timers_;
    auto* sentinel = static_cast<TimedNode*>(self->timers_);
    TimedNode* node = sentinel->next;
    if (node != sentinel) {
        const float step = static_cast<float>(dt);
        do {
            TimedNode* current = node;
            node = node->next;
            float& seconds = current->data.seconds;
            const double left = static_cast<double>(seconds) - step;
            seconds = static_cast<float>(left);
            if (!(left >= GameFloat(0x0073B858))) {
                void* erased;
                TimersErase(timers, nullptr, &erased, current);
            }
        } while (node != self->timers_);
    }
    auto* awards = static_cast<RefNode*>(self->awards_);
    if (awards->next == awards || GameMode() != 3) return;
    CPlayer* player = LocalPlayer();
    if (!player || player->connection_->state_ != 1 || !Unknown7B00BC()) return;
    ApplyAwards();
}
namespace {
} // namespace
EI_REPLACE_METHOD(0x0041CCD0, ClientSession::TickImpl);

} // namespace net
