// The text manager's archive list (004DE930-004DEDDF): adding, removing and clearing the
// archives the texts are looked up in. The list and its node helpers are in item_db_text.h.
#include "item_db_text.h"
#include "resource.h"

#include "engine_object.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/stlp.h"

#include <cstring>

namespace {

// The exact name comparison of the list (length, then the bytes).
bool SameName(const TextSourceNode* node, const char* path) {
    std::size_t length = std::strlen(path);
    return node->data.name.size() == length && std::memcmp(node->data.name.data(), path, length) == 0;
}

void CloseAndDelete(CMappedResource* source) {
    source->CloseResource();
    delete source;   // the scalar deleting destructor (slot 0, flag 1); a null source is skipped
}

} // namespace

BOOL __fastcall TextSourcesAdd(TextSourceList* list, void*, const char* path, BOOL writable) {
    using Node = TextSourceNode;
    for (Node* node = list->head->next; node != list->head; node = node->next) {
        if (SameName(node, path)) return 1;
    }
    void* memory = gamecrt::operator_new(sizeof(CMappedResource));
    CMappedResource* resource = memory ? static_cast<CMappedResource*>(memory)->Construct() : nullptr;
    {
        TextSource value{stlp::string(path), resource};
        Node* inserted;
        TextSourcesInsert(list, nullptr, &inserted, list->head, &value);
    }
    return list->head->prev->data.source->OpenResource(path, writable);
}

void __fastcall TextSourcesRemove(TextSourceList* list, void*, const char* path) {
    using Node = TextSourceNode;
    for (Node* node = list->head->next; node != list->head; node = node->next) {
        if (!SameName(node, path)) continue;
        CloseAndDelete(node->data.source);
        Node* next;
        TextSourcesErase(list, nullptr, &next, node);
        return;
    }
}

void __fastcall TextSourcesClear(TextSourceList* list) {
    using Node = TextSourceNode;
    Node* sentinel = list->head;
    for (Node* node = sentinel->next; node != sentinel; node = node->next) CloseAndDelete(node->data.source);
    for (Node* node = sentinel->next; node != sentinel;) {
        Node* next = node->next;
        FreeStringStorage(&node->data.name);
        stlp::deallocate(node, sizeof(Node));
        node = next;
    }
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
}

BOOL CTextManager::AddSource(const char* path) {
    return TextSourcesAdd(&sources_, nullptr, path, 0);
}

void CTextManager::RemoveSource(const char* path) { TextSourcesRemove(&sources_, nullptr, path); }

void CTextManager::ClearSources() { TextSourcesClear(&sources_); }

void CTextManager::Destruct() {
    ei::SetVtable(this, kVtableAddress);
    ClearSources();
    TextSourcesClear(&sources_);
    // The list's destructor: its clear (nothing is left) and the sentinel back to the pool.
    sources_.ClearWith([](TextSource* source) { source->name.~string(); });
    stlp::deallocate(sources_.head, sizeof(TextSourceNode));
    reinterpret_cast<CEngineObject*>(this)->DestructBase();
}

CTextManager* CTextManager::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

EI_REPLACE(0x004DE930, TextSourcesAdd);
EI_REPLACE(0x004DEAF0, TextSourcesRemove);
EI_REPLACE(0x004DEB60, TextSourcesClear);
EI_REPLACE_METHOD(0x004DEDB0, CTextManager::AddSource);
EI_REPLACE_METHOD(0x004DEDD0, CTextManager::RemoveSource);
EI_REPLACE_METHOD(0x004DED40, CTextManager::ClearSources);
EI_REPLACE_METHOD(0x005629B0, CTextManager::Destruct);
EI_REPLACE_METHOD(0x00562990, CTextManager::DeletingDestruct);
