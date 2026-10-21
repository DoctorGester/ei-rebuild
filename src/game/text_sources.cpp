// The text manager's archive list (004DE930-004DEDDF): adding, removing and clearing the
// archives the texts are looked up in. The list and its node helpers are in item_db_text.h.
#include "item_db_text.h"
#include "resource.h"

#include "console.h"   // console::CStringArray
#include "engine_object.h"
#include "options.h"

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

// 004DEC40 through its address: Rebirth patches it (rule 9).
__declspec(naked) void __fastcall TextSourcesListKeysOriginal(TextSources*, void*, mfc::CStringArray*, const char*)
    EI_ORIGINAL(0x004DEC40)

constexpr int kLocaleCtype = 2;     // LC_CTYPE
constexpr int kMbcpLocale = -4;     // _MB_CP_LOCALE

void CloseAndDelete(CMappedResource* source) {
    source->CloseResource();
    delete source;   // the scalar deleting destructor (slot 0, flag 1); a null source is skipped
}

} // namespace

BOOL TextSources::Add(const char* path, BOOL writable) {
    using Node = TextSourceNode;
    for (Node* node = head->next; node != head; node = node->next) {
        if (SameName(node, path)) return 1;
    }
    void* memory = gamecrt::operator_new(sizeof(CMappedResource));
    CMappedResource* resource = memory ? static_cast<CMappedResource*>(memory)->Construct() : nullptr;
    {
        TextSource value{stlp::string(path), resource};
        Node* inserted;
        Insert(&inserted, head, &value);
    }
    return head->prev->data.source->OpenResource(path, writable);
}

void TextSources::Remove(const char* path) {
    using Node = TextSourceNode;
    for (Node* node = head->next; node != head; node = node->next) {
        if (!SameName(node, path)) continue;
        CloseAndDelete(node->data.source);
        Node* next;
        Erase(&next, node);
        return;
    }
}

void TextSources::Clear() {
    using Node = TextSourceNode;
    Node* sentinel = head;
    for (Node* node = sentinel->next; node != sentinel; node = node->next) CloseAndDelete(node->data.source);
    for (Node* node = sentinel->next; node != sentinel;) {
        Node* next = node->next;
        node->data.name.~string();   // 004DEAB0: the storage freed
        stlp::deallocate(node, sizeof(Node));
        node = next;
    }
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
}

void TextSources::DestructList() {
    Clear();
    ClearWith([](TextSource* source) { source->name.~string(); });
    stlp::deallocate(head, sizeof(TextSourceNode));
}

BOOL CTextManager::AddSource(const char* path) {
    return sources_.Add(path, 0);
}

void CTextManager::RemoveSource(const char* path) { sources_.Remove(path); }

void CTextManager::ClearSources() { sources_.Clear(); }

void CTextManager::Destruct() {
    ei::SetVtable(this, kVtableAddress);
    ClearSources();
    sources_.Clear();
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

void* TextSources::Find(const char* key, std::uint32_t* size) {
    for (TextSourceNode* node = head->next; node != head; node = node->next) {
        if (void* data = node->data.source->GetFile(key, size)) return data;
    }
    return nullptr;
}
EI_REPLACE_METHOD(0x004DEBC0, TextSources::Find);

void TextSources::ListKeys(mfc::CStringArray* out, const char* pattern) {
    console::CStringArray found;
    out->SetSize(0, -1);
    for (TextSourceNode* node = head->next; node != head; node = node->next) {
        node->data.source->ListFiles(&found, pattern);
        out->Append(found);
    }
}
EI_REPLACE_METHOD(0x004DEC40, TextSources::ListKeys);

void CTextManager::ListKeys(mfc::CStringArray* out, const char* pattern) {
    TextSourcesListKeysOriginal(&sources_, nullptr, out, pattern);
}
EI_REPLACE_METHOD(0x004DEFA0, CTextManager::ListKeys);

int CTextManager::Create() {
    // LC_CTYPE from the "Font CharSet" option ("english" unless the registry says otherwise), then the
    // multibyte code page from it: the game's only locale set-up (docs/notes/standalone.md, S4c).
    libcrt::Setlocale(kLocaleCtype, options::TheOptions()->fontCharSet_);
    libcrt::Setmbcp(kMbcpLocale);
    return 1;
}
EI_REPLACE_METHOD(0x004DED20, CTextManager::Create);

EI_REPLACE_METHOD(0x004DE930, TextSources::Add);
EI_REPLACE_METHOD(0x004DEAF0, TextSources::Remove);
EI_REPLACE_METHOD(0x004DEB60, TextSources::Clear);
EI_REPLACE_METHOD(0x005628E0, TextSources::DestructList);
EI_REPLACE_METHOD(0x004DEDB0, CTextManager::AddSource);
EI_REPLACE_METHOD(0x004DEDD0, CTextManager::RemoveSource);
EI_REPLACE_METHOD(0x004DED40, CTextManager::ClearSources);
EI_REPLACE_METHOD(0x005629B0, CTextManager::Destruct);
EI_REPLACE_METHOD(0x00562990, CTextManager::DeletingDestruct);
