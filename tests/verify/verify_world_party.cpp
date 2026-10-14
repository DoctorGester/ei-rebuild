// Checks for the party's arrival in a zone (src/game/world_party.cpp).
//
// TakeZoneQuestItems only moves pointers from an array to a list, so the original runs first on
// a copy of the array into a scratch list and ours on the real ones; the lists and arrays are
// compared. RestorePlayerItems and Redeploy create items and units, so only ours runs and the
// resulting state is checked.
#include "verify.h"

#include "../../src/game/item.h"
#include "../../src/game/unit.h"
#include "../../src/game/world.h"

#include <cstdlib>
#include <cstring>

namespace {
void* s_unused;

struct ItemArray {
    CEngineObject** data;
    int size;
};
using ObjectList = mfc::CTypedPtrList<CEngineObject>;

using TakeFn = void(__fastcall*)(CWorldServer*, void*, ItemArray*, ObjectList*);
TakeFn s_originalTake;

__declspec(naked) void __fastcall PtrListRemoveAll(ObjectList*, void*) {
    __asm mov eax, 0x00429F10
    __asm jmp eax
}

constexpr int kKindSinglePlayer = 1;
constexpr int kKindNoRangeTest = 3;
} // namespace

// 005901E0: the original on a copy of the array into a scratch list, ours on the real ones.
static void __fastcall TakeZoneQuestItemsVerify(CWorldServer* self, void*, ItemArray* items, ObjectList* list) {
    int size = items->size;
    auto** copyData = static_cast<CEngineObject**>(std::malloc(static_cast<std::size_t>(size > 0 ? size : 1) * 4));
    if (size > 0) std::memcpy(copyData, items->data, static_cast<std::size_t>(size) * 4);
    ItemArray copy{copyData, size};
    ObjectList scratch{reinterpret_cast<void*>(0x0073BE80), nullptr, nullptr, 0, nullptr, nullptr, 10};
    s_originalTake(self, nullptr, &copy, &scratch);

    ObjectList::Node* oldTail = list->tail;
    int oldCount = list->count;
    self->TakeZoneQuestItems(items, list);

    bool ok = copy.size == items->size && list->count - oldCount == scratch.count;
    for (int i = 0; ok && i < copy.size; ++i) ok = copy.data[i] == items->data[i];
    ObjectList::Node* ours = oldTail ? oldTail->next : list->head;
    for (ObjectList::Node* theirs = scratch.head; ok && theirs; theirs = theirs->next, ours = ours ? ours->next : nullptr)
        ok = ours && ours->data == theirs->data;
    ei::verify::Check("CWorldServer::TakeZoneQuestItems", true, ok, "items %d -> %d (original %d), taken %d (original %d)",
                      size, items->size, copy.size, list->count - oldCount, scratch.count);
    PtrListRemoveAll(&scratch, nullptr);
    std::free(copyData);
}
EI_VERIFY(0x005901E0, TakeZoneQuestItemsVerify, 7, s_originalTake);

// 00590460: in a network game (kind 4) the carried items stay as they were; otherwise counted.
static void __fastcall RestorePlayerItemsVerify(CWorldServer* self, void*, WorldPlayer* player) {
    auto* items = reinterpret_cast<ItemArray*>(reinterpret_cast<std::uint8_t*>(player) + 0xB0);
    int before = items->size;
    CEngineObject** data = items->data;
    int kind = self->sessionKind_;
    self->RestorePlayerItems(player);
    bool ok = items->size >= 0 && (kind != 4 || (items->size == before && items->data == data));
    ei::verify::Check("CWorldServer::RestorePlayerItems", true, ok, "kind %d, carried items %d -> %d", kind, before,
                      items->size);
}
EI_VERIFY(0x00590460, RestorePlayerItemsVerify, 0, s_unused);

// 00594CE0: the copy has the item's type (+0x10), record (+0x18) and count (+0x28).
static CEngineObject* __fastcall CopyItemVerify(CWorldServer* self, void*, CEngineObject* item) {
    CEngineObject* copy = self->CopyItem(item);
    auto field = [](CEngineObject* o, int offset) { return *reinterpret_cast<std::uint32_t*>(reinterpret_cast<std::uint8_t*>(o) + offset); };
    bool ok = copy && field(copy, 0x10) == field(item, 0x10) && field(copy, 0x18) == field(item, 0x18) &&
              field(copy, 0x28) == field(item, 0x28);
    ei::verify::Check("CWorldServer::CopyItem", true, ok, "type %u record %08X", field(item, 0x10), field(item, 0x18));
    return copy;
}
EI_VERIFY(0x00594CE0, CopyItemVerify, 0, s_unused);

namespace {
using AddCountedFn = void(__fastcall*)(CWorldServer*, void*, CEngineObject*);
AddCountedFn s_originalAddCounted;
}

// 00594F30: ours adds the item at most once; the original run afterwards must find it there and
// change nothing.
static void __fastcall AddCountedVerify(CWorldServer* self, void*, CEngineObject* item) {
    int before = self->counted_.count;
    self->AddCounted(item);
    int ours = self->counted_.count;
    s_originalAddCounted(self, nullptr, item);
    int original = self->counted_.count;
    bool ok = original == ours && (item ? self->counted_.Find(item) != nullptr && ours - before <= 1 : ours == before);
    ei::verify::Check("CWorldServer::AddCounted", true, ok, "counted %d -> %d (original %d)", before, ours, original);
}
EI_VERIFY(0x00594F30, AddCountedVerify, 5, s_originalAddCounted);

// 00594AB0 / 00594BC0: the managers create objects, so only ours runs; logged.
static CEngineObject* __fastcall CreateItemNamedVerify(CWorldServer* self, void*, const char* name) {
    CEngineObject* item = self->CreateItemNamed(name);
    ei::verify::Check("CWorldServer::CreateItemNamed", true, true, "%s -> %s", name ? name : "", item ? "item" : "none");
    return item;
}
EI_VERIFY(0x00594AB0, CreateItemNamedVerify, 0, s_unused);

static CEngineObject* __fastcall CreateSpellNamedVerify(CWorldServer* self, void*, const char* name) {
    CEngineObject* spell = self->CreateSpellNamed(name);
    ei::verify::Check("CWorldServer::CreateSpellNamed", true, true, "%s -> %s", name ? name : "", spell ? "spell" : "none");
    return spell;
}
EI_VERIFY(0x00594BC0, CreateSpellNamedVerify, 0, s_unused);

// 00590700: when the party is deployed (single player or kind 3, with +0x15C) the player's last
// tick becomes the world's; otherwise it is left alone.
static void __fastcall RedeployVerify(CWorldServer* self, void*, WorldPlayer* player, void* placements, char runScripts) {
    if (!player) {
        self->Redeploy(player, placements, runScripts);
        return;
    }
    int lastTick = player->lastTick;
    bool deploys = (self->sessionKind_ == kKindSinglePlayer || self->sessionKind_ == kKindNoRangeTest) &&
                   self->zoneLoaded_ != 0;
    self->Redeploy(player, placements, runScripts);
    bool ok = deploys ? player->lastTick == self->tick_ : player->lastTick == lastTick;
    ei::verify::Check("CWorldServer::Redeploy", true, ok, "kind %d, %s, %d records, tick %d", self->sessionKind_,
                      deploys ? "deployed" : "not deployed", player->mercCount, player->lastTick);
}
EI_VERIFY(0x00590700, RedeployVerify, 0, s_unused);

// A-W13. 00595020 UseItemUp: ours only (it changes the counts). Checked: the item's count is one
// lower. A unit casting from a scroll (combat_orders.cpp); no scenario reaches it.
static void __fastcall UseItemUpVerify(CWorldServer* self, void*, CUnit* unit, CItem* item) {
    std::uint32_t before = item->count_;
    self->UseItemUp(unit, item);
    ei::verify::Check("CWorldServer::UseItemUp", before - 1, item->count_, "item %d", item->id_);
}
EI_VERIFY(0x00595020, UseItemUpVerify, 0, s_unused);
