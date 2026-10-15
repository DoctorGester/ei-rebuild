// Check for ScriptGroup::CopyToList (004B3200; script_group.cpp; A-E1): side by side. Ours runs on
// the real list, then the original on a shadow list (built after ours) holding the real list's
// elements from before (each counted once more); both must end as the group's objects in order. The shadow's references are dropped after.
// Main thread. Reached by the server's party sync of a player without a party copy
// (player_sync.cpp), by reading; which scenario runs that branch is not known.
#include "verify.h"

#include "../../src/game/script.h"

#include <vector>

namespace {
using ObjectList = mfc::CTypedPtrList<CEngineObject>;

// 004B3200: PUSH ECX, PUSH EBX, PUSH EBP, PUSH ESI (4), MOV ESI,[ESP+0x14] (4).
void(__fastcall* s_copyToList)(const ScriptGroup*, void*, ObjectList*);
void __fastcall CopyToListVerify(const ScriptGroup* self, void*, ObjectList* list) {
    // The real list's elements are kept in our memory (each counted once more) before ours runs;
    // the shadow list is built after it (SV2f).
    int before = list->count;
    std::vector<CEngineObject*> kept;
    for (auto* node = list->head; node; node = node->next) {
        kept.push_back(node->data);
        if (node->data) ++node->data->refCount_;
    }
    ObjectList shadow{};
    shadow.vtable = list->vtable;
    shadow.blockSize = list->blockSize;
    self->CopyToList(list);
    for (CEngineObject* object : kept) shadow.AddTail(object);
    s_copyToList(self, nullptr, &shadow);
    bool same = shadow.count == list->count && list->count == self->size;
    auto* expected = shadow.head;
    auto* actual = list->head;
    for (int i = 0; same && i < self->size; ++i) {
        same = expected && actual && expected->data == actual->data && actual->data == self->data[i].object;
        if (same) {
            expected = expected->next;
            actual = actual->next;
        }
    }
    same = same && !expected && !actual;
    for (auto* node = shadow.head; node; node = node->next)
        if (node->data) node->data->Release();
    shadow.RemoveAll();
    ei::verify::Check("ScriptGroup::CopyToList", true, same, "%d objects (the list had %d)", self->size, before);
}
} // namespace

EI_VERIFY(0x004B3200, CopyToListVerify, 8, s_copyToList);
