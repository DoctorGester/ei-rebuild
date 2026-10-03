// Checks for the party's arrival in a zone (src/game/world_party.cpp).
//
// TakeZoneQuestItems only moves pointers from an array to a list, so the original runs first on
// a copy of the array into a scratch list and ours on the real ones; the lists and arrays are
// compared. RestorePlayerItems and Redeploy create items and units, so only ours runs and the
// resulting state is checked.
#include "verify.h"

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
    int kind = self->unknown12D64_;
    self->RestorePlayerItems(player);
    bool ok = items->size >= 0 && (kind != 4 || (items->size == before && items->data == data));
    ei::verify::Check("CWorldServer::RestorePlayerItems", true, ok, "kind %d, carried items %d -> %d", kind, before,
                      items->size);
}
EI_VERIFY(0x00590460, RestorePlayerItemsVerify, 0, s_unused);

// 00590700: when the party is deployed (single player or kind 3, with +0x15C) the player's last
// tick becomes the world's; otherwise it is left alone.
static void __fastcall RedeployVerify(CWorldServer* self, void*, WorldPlayer* player, void* placements, char runScripts) {
    if (!player) {
        self->Redeploy(player, placements, runScripts);
        return;
    }
    int lastTick = player->lastTick;
    bool deploys = (self->unknown12D64_ == kKindSinglePlayer || self->unknown12D64_ == kKindNoRangeTest) &&
                   self->unknown15C_ != 0;
    self->Redeploy(player, placements, runScripts);
    bool ok = deploys ? player->lastTick == self->tick_ : player->lastTick == lastTick;
    ei::verify::Check("CWorldServer::Redeploy", true, ok, "kind %d, %s, %d records, tick %d", self->unknown12D64_,
                      deploys ? "deployed" : "not deployed", player->mercCount, player->lastTick);
}
EI_VERIFY(0x00590700, RedeployVerify, 0, s_unused);
