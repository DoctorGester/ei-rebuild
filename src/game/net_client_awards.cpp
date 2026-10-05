// The game client's tick (0041CCD0, slot 14) and its award handling (0041CD80): timers count down,
// and received awards (+0x2608) are applied to the local player. See net_classes.h and
// docs/notes/unmapped_410_470.md ("Seventh round").
#pragma float_control(precise, on)

#include "net_messages.h"
#include "core_refcount.h"

#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_gameplay.h"
#include "originals_world.h"
#include "originals_library.h"

#include <cstring>

class CEngineObject;

namespace net {

namespace {

using ObjectList = mfc::CTypedPtrList<CEngineObject>;

// The client base tick (00442AD0) and the timers list erase (library).
__declspec(naked) void __fastcall ClientBaseTick(ClientBase* /*self*/, void*, double /*dt*/) EI_ORIGINAL(0x00442AD0)
__declspec(naked) void** __fastcall TimersErase(void* /*list*/, void*, void** /*out*/, void* /*position*/) EI_ORIGINAL(0x004246E0)

// The world (0x7AFF60) and its players (outside this range).
__declspec(naked) void __fastcall AwardExperience(void* /*world*/, void*, void* /*player*/, std::uint32_t /*amount bits*/) EI_ORIGINAL(0x005911C0)
void __fastcall PlayerGiveItems(void* a0, void*, ObjectList* a2, int a3) { PlayerGiveItemsOriginal(a0, nullptr, (void*)a2, a3); }
__declspec(naked) void __fastcall PlayerRemoveItem(void* /*player*/, void*, int /*index*/) EI_ORIGINAL(0x00665870)
__declspec(naked) CEngineObject* __fastcall ManagerCreateItem(void* /*manager*/, void*, const char* /*name*/) EI_ORIGINAL(0x006AC970)
// MFC list helpers instantiated in the exe: the counted list's NewNode and element assignment,
// RemoveAll of both list kinds.
ObjectList::Node* __fastcall CountedNewNode(ObjectList* a0, void*, ObjectList::Node* a2, ObjectList::Node* a3) { return (ObjectList::Node*)ListNewNode429EA0Original((void*)a0, nullptr, (void*)a2, (void*)a3); }
__declspec(naked) void __fastcall CountedAssign(CEngineObject** /*slot*/, void*, CEngineObject*) EI_ORIGINAL(0x00429B50)
__declspec(naked) void __fastcall CountedRemoveAll(ObjectList*) EI_ORIGINAL(0x00429E50)
inline void __fastcall PtrListRemoveAll(ObjectList* a0) { PtrListRemoveAllNodesOriginal((void*)a0); }

// The game's _ftol (006E853C) on a value loaded from a float.
__declspec(naked) int __cdecl GameFtol(double) {
    __asm {
        fld qword ptr [esp + 4]
        mov eax, 0x006E853C
        call eax
        ret
    }
}

constexpr std::uintptr_t kWorld = 0x007AFF60;
constexpr std::uintptr_t kPtrListVtable = 0x0073BE80;       // CPtrList of objects
constexpr std::uintptr_t kCountedListVtable = 0x0073BE74;   // list of counted objects
constexpr int kPlayerGold = 0xC0;
constexpr int kPlayerParty = 0x70;     // the player's party / inventory object (00661050 class)
constexpr int kPlayerItems = 0xE4;     // head of the player's item list
constexpr int kClassQuestItem = 0x14;
constexpr int kSlotFactory = 18;       // of the party: the item manager

void* World() { return reinterpret_cast<void*>(kWorld); }
float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
std::int32_t GameMode() { return *reinterpret_cast<const std::int32_t*>(0x007C2CC4); }
std::uint32_t Unknown7B00BC() { return *reinterpret_cast<const std::uint32_t*>(0x007B00BC); }
void*& NodeFreeList() { return *reinterpret_cast<void**>(0x0079B228); }
template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

void InitList(ObjectList& list, std::uintptr_t vtable) {
    list.vtable = reinterpret_cast<void*>(vtable);
    list.head = nullptr;
    list.tail = nullptr;
    list.count = 0;
    list.free = nullptr;
    list.blocks = nullptr;
    list.blockSize = 10;
}

} // namespace

// 0041CD80: applies the received awards (+0x2608: counted award messages, net_server_awards.cpp)
// to player 0 and empties the list. Kind 0: experience (005911C0 with the value's bits); 1: gold
// (+0xC0, the value truncated), then an empty item list given; 2: the item named by the text,
// given; 3: the first item of the player's list that the named item's slot 14 accepts (returns 0)
// is removed; 4: the party's items are reset and every quest item (class 0x14) removed.
void ClientSession::ApplyAwards() {
    ClientSession* self = this;
    std::uint8_t* player = static_cast<std::uint8_t*>(WorldGetPlayer(World(), nullptr, 0));
    if (!player) return;
    for (void** node = static_cast<void**>(static_cast<void**>(self->awards_)[0]); node != self->awards_;
         node = static_cast<void**>(node[0])) {
        auto* award = static_cast<AwardMessage*>(node[2]);
        const char* name = award->text_.start;
        switch (award->kind_) {
        case 0: {
            std::uint32_t bits;
            std::memcpy(&bits, &award->value_, 4);
            AwardExperience(World(), nullptr, nullptr, bits);
            break;
        }
        case 1: {
            ObjectList none;
            InitList(none, kPtrListVtable);
            At<std::int32_t>(player, kPlayerGold) += GameFtol(award->value_);
            PlayerGiveItems(player, nullptr, &none, 0);
            none.vtable = reinterpret_cast<void*>(kPtrListVtable);
            PtrListRemoveAll(&none);
            break;
        }
        case 2: {
            ObjectList items;
            InitList(items, kCountedListVtable);
            void* manager = ei::VCall<void*>(player + kPlayerParty, kSlotFactory);
            CEngineObject* item = ManagerCreateItem(manager, nullptr, name);
            ObjectList::Node* added = CountedNewNode(&items, nullptr, items.tail, nullptr);
            CountedAssign(&added->data, nullptr, item);
            if (items.tail) items.tail->next = added;
            else items.head = added;
            items.tail = added;
            PlayerGiveItems(player, nullptr, &items, 0);
            items.vtable = reinterpret_cast<void*>(kCountedListVtable);
            CountedRemoveAll(&items);
            break;
        }
        case 3: {
            void* manager = ei::VCall<void*>(player + kPlayerParty, kSlotFactory);
            CEngineObject* item = ManagerCreateItem(manager, nullptr, name);
            if (item) ++At<std::int32_t>(item, 4);
            for (auto* position = At<ObjectList::Node*>(player, kPlayerItems); position;) {
                ObjectList::Node* current = position;
                position = position->next;
                CEngineObject* owned = current->data;
                if (!ei::VCall<int>(owned, 14, item)) {
                    std::int32_t index = At<std::int32_t>(owned, 0xC);
                    if (index >= 0) PlayerRemoveItem(player, nullptr, index);
                    break;
                }
            }
            if (item) ei::VCall<int>(item, 4);   // Release
            break;
        }
        case 4:
            PartyResetItems(player + kPlayerParty);
            for (auto* position = At<ObjectList::Node*>(player, kPlayerItems); position;) {
                ObjectList::Node* current = position;
                position = position->next;
                CEngineObject* owned = current->data;
                if (ei::VCall<int>(owned, 12) == kClassQuestItem) PlayerRemoveItem(player, nullptr, At<std::int32_t>(owned, 0xC));
            }
            break;
        default:
            break;
        }
    }

    // The list emptied inline: each award released, each node back to the 16-byte pool list.
    for (void** node = static_cast<void**>(static_cast<void**>(self->awards_)[0]); node != self->awards_;) {
        void** current = node;
        node = static_cast<void**>(node[0]);
        if (auto* award = static_cast<CRefObject*>(current[2])) {
            --award->refs_;
            if (award->refs_ == 0) delete award;
        }
        if (current) {
            current[0] = NodeFreeList();
            NodeFreeList() = current;
        }
    }
    static_cast<void**>(self->awards_)[0] = self->awards_;
    static_cast<void**>(self->awards_)[1] = self->awards_;
}
EI_REPLACE_METHOD(0x0041CD80, ClientSession::ApplyAwards);

// 0041CCD0 (slot 14): the client base tick, then the timers (+0x2604: {next, prev, seconds, ...})
// count down by `dt` (as a float) and those below zero are dropped. Awards are applied in a game of
// mode 3 when player 0 exists with its +0x20 object's +0x20 at 1 and 007B00BC is set.
void ClientSession::Tick(double dt) {
    ClientSession* self = this;
    ClientBaseTick(self, nullptr, dt);
    void* timers = &self->timers_;
    void** sentinel = static_cast<void**>(self->timers_);
    void** node = static_cast<void**>(sentinel[0]);
    if (node != sentinel) {
        const float step = static_cast<float>(dt);
        do {
            void** current = node;
            node = static_cast<void**>(node[0]);
            float& seconds = *reinterpret_cast<float*>(&current[2]);
            const double left = static_cast<double>(seconds) - step;
            seconds = static_cast<float>(left);
            if (!(left >= GameFloat(0x0073B858))) {
                void* erased;
                TimersErase(timers, nullptr, &erased, current);
            }
        } while (node != self->timers_);
    }
    void** awards = static_cast<void**>(self->awards_);
    if (awards[0] == awards || GameMode() != 3) return;
    std::uint8_t* player = static_cast<std::uint8_t*>(WorldGetPlayer(World(), nullptr, 0));
    if (!player || At<std::int32_t>(At<void*>(player, 0x20), 0x20) != 1 || !Unknown7B00BC()) return;
    ApplyAwards();
}
namespace {
void __fastcall ClientTickHook(ClientSession* self, void*, double dt) { self->ClientSession::Tick(dt); }
} // namespace
EI_REPLACE(0x0041CCD0, ClientTickHook);

} // namespace net
