// Checks for text_sources.cpp, ours only (they open and free archives): the list's shape after
// each call. Add: an archive already listed gives 1 and no change, otherwise one more node at the
// end named `path` with an archive object. Remove: one node fewer when the name was listed, and the
// name is gone. Clear: the list is empty. Reached by the client's start (00561E50 adds the text
// archives: `newgame`, `world_load`), the zone briefings (005809D0 / 00580F10 / 005800C0 /
// 0057F560 add and remove "<zone>" texts: `newgame`) and the client's teardown (005629B0 / 005628E0).
#include "verify.h"

#include "../../src/game/engine_object.h"
#include "../../src/game/item_db_text.h"

#include <cstdint>
#include <cstring>

namespace {

void* s_unused;

using Node = TextSourceList::Node;

int CountNodes(const TextSourceList* list) {
    int n = 0;
    for (Node* node = list->head->next; node != list->head; node = node->next) ++n;
    return n;
}

bool Named(const Node* node, const char* path) {
    std::size_t length = std::strlen(path);
    return node->data.name.size() == length && std::memcmp(node->data.name.data(), path, length) == 0;
}

bool Listed(const TextSourceList* list, const char* path) {
    for (Node* node = list->head->next; node != list->head; node = node->next) {
        if (Named(node, path)) return true;
    }
    return false;
}

BOOL CheckedAdd(TextSourceList* list, const char* path, BOOL writable, const char* name) {
    int before = CountNodes(list);
    bool listed = Listed(list, path);
    BOOL result = TextSourcesAdd(list, nullptr, path, writable);
    int after = CountNodes(list);
    Node* last = list->head->prev;
    bool ok = listed ? (result == 1 && after == before)
                     : (after == before + 1 && Named(last, path) && last->data.source != nullptr);
    ei::verify::Check(name, true, ok, "%s: listed %d, %d -> %d nodes, result %d", path, listed, before, after, result);
    return result;
}

void CheckedRemove(TextSourceList* list, const char* path, const char* name) {
    int before = CountNodes(list);
    bool listed = Listed(list, path);
    TextSourcesRemove(list, nullptr, path);
    int after = CountNodes(list);
    bool ok = after == before - (listed ? 1 : 0) && !Listed(list, path);
    ei::verify::Check(name, true, ok, "%s: listed %d, %d -> %d nodes", path, listed, before, after);
}

void CheckedClear(TextSourceList* list, const char* name) {
    int before = CountNodes(list);
    TextSourcesClear(list);
    bool ok = list->head->next == list->head && list->head->prev == list->head;
    ei::verify::Check(name, true, ok, "%d nodes before", before);
}

TextSourceList* SourcesOf(CTextManager* self) { return &self->sources_; }

BOOL __fastcall AddVerify(TextSourceList* self, void*, const char* path, BOOL writable) {
    return CheckedAdd(self, path, writable, "TextSourcesAdd");
}
void __fastcall RemoveVerify(TextSourceList* self, void*, const char* path) {
    CheckedRemove(self, path, "TextSourcesRemove");
}
void __fastcall ClearVerify(TextSourceList* self) { CheckedClear(self, "TextSourcesClear"); }
BOOL __fastcall AddSourceVerify(CTextManager* self, void*, const char* path) {
    return CheckedAdd(SourcesOf(self), path, 0, "CTextManager::AddSource");
}
void __fastcall RemoveSourceVerify(CTextManager* self, void*, const char* path) {
    CheckedRemove(SourcesOf(self), path, "CTextManager::RemoveSource");
}
void __fastcall ClearSourcesVerify(CTextManager* self) { CheckedClear(SourcesOf(self), "CTextManager::ClearSources"); }

// 005629B0 / 00562990 (A-W11): the destructor ends with the engine object's vtable (DestructBase);
// the deleting destructor returns this (its object may be freed, so nothing else is read).
void __fastcall DestructVerify(CTextManager* self) {
    self->Destruct();
    std::uintptr_t vtable;
    std::memcpy(&vtable, self, sizeof(vtable));
    ei::verify::Check("CTextManager::Destruct", static_cast<std::uintptr_t>(CEngineObject::kVtableAddress), vtable, "");
}
CTextManager* __fastcall DeletingDestructVerify(CTextManager* self, void*, unsigned flags) {
    CTextManager* result = self->DeletingDestruct(flags);
    ei::verify::Check("CTextManager::DeletingDestruct", self, result, "flags %u", flags);
    return result;
}

} // namespace

EI_VERIFY(0x004DE930, AddVerify, 0, s_unused);
EI_VERIFY(0x004DEAF0, RemoveVerify, 0, s_unused);
EI_VERIFY(0x004DEB60, ClearVerify, 0, s_unused);
EI_VERIFY(0x004DEDB0, AddSourceVerify, 0, s_unused);
EI_VERIFY(0x004DEDD0, RemoveSourceVerify, 0, s_unused);
EI_VERIFY(0x004DED40, ClearSourcesVerify, 0, s_unused);
EI_VERIFY(0x005629B0, DestructVerify, 0, s_unused);
EI_VERIFY(0x00562990, DeletingDestructVerify, 0, s_unused);

// A-W13. 005628E0 frees the list with its sentinel: ours only, nothing is left to read afterwards.
// Reached only when the owner's construction unwinds.
static void __fastcall ListDestructVerify(TextSourceList* self) {
    TextSourcesDestruct(self);
    ei::verify::Check("TextSourcesDestruct", true, true, "");
}
EI_VERIFY(0x005628E0, ListDestructVerify, 0, s_unused);
