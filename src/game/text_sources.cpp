// The text manager's archive list (004DE930-004DEDDF): adding, removing and clearing the
// archives the texts are looked up in. The list and its node helpers are in item_db_text.h.
#include "item_db_text.h"
#include "resource.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/stlp.h"

#include <cstring>

namespace {

// The exact name comparison of the list (length, then the bytes).
bool SameName(const TextSourceList::Node* node, const char* path) {
    std::size_t length = std::strlen(path);
    return static_cast<std::size_t>(node->nameEnd - node->nameBegin) == length &&
           std::memcmp(node->nameBegin, path, length) == 0;
}

void CloseAndDelete(CMappedResource* source) {
    source->CloseResource();
    delete source;   // the scalar deleting destructor (slot 0, flag 1); a null source is skipped
}

// The value Insert copies into the new node: the pair<string, CMappedResource*>.
struct SourcePair {
    stlp::string name;
    CMappedResource* source;
};
static_assert(sizeof(SourcePair) == 0x10);

} // namespace

BOOL TextSourceList::Add(const char* path, BOOL writable) {
    for (Node* node = sentinel->next; node != sentinel; node = node->next) {
        if (SameName(node, path)) return 1;
    }
    void* memory = gamecrt::operator_new(sizeof(CMappedResource));
    CMappedResource* resource = memory ? static_cast<CMappedResource*>(memory)->Construct() : nullptr;
    {
        SourcePair value{stlp::string(path), resource};
        Node* inserted;
        Insert(&inserted, sentinel, &value);
    }
    return sentinel->prev->source->OpenResource(path, writable);
}

void TextSourceList::Remove(const char* path) {
    for (Node* node = sentinel->next; node != sentinel; node = node->next) {
        if (!SameName(node, path)) continue;
        CloseAndDelete(node->source);
        Node* next;
        Erase(&next, node);
        return;
    }
}

void TextSourceList::Clear() {
    for (Node* node = sentinel->next; node != sentinel; node = node->next) CloseAndDelete(node->source);
    for (Node* node = sentinel->next; node != sentinel;) {
        Node* next = node->next;
        FreeStringStorage(&node->nameBegin);
        stlp::deallocate(node, sizeof(Node));
        node = next;
    }
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
}

BOOL CTextManager::AddSource(const char* path) {
    return reinterpret_cast<TextSourceList*>(&sources_)->Add(path, 0);
}

void CTextManager::RemoveSource(const char* path) { reinterpret_cast<TextSourceList*>(&sources_)->Remove(path); }

void CTextManager::ClearSources() { reinterpret_cast<TextSourceList*>(&sources_)->Clear(); }

EI_REPLACE_METHOD(0x004DE930, TextSourceList::Add);
EI_REPLACE_METHOD(0x004DEAF0, TextSourceList::Remove);
EI_REPLACE_METHOD(0x004DEB60, TextSourceList::Clear);
EI_REPLACE_METHOD(0x004DEDB0, CTextManager::AddSource);
EI_REPLACE_METHOD(0x004DEDD0, CTextManager::RemoveSource);
EI_REPLACE_METHOD(0x004DED40, CTextManager::ClearSources);
