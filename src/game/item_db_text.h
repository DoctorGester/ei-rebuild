#pragma once

// Item names and descriptions from the game's text resources (004E0000-004E15C0).
//
// The game keeps its localized texts in resource archives (CMappedResource, resource.h). A text
// manager (global pointer at 007AF788; class name unknown, provisional CTextManager) holds a
// list of those archives and a read cursor into the last text it found. A text is looked up by
// key ("string format_item1", "weapon SWORD IRON", ...): the first archive whose GetFile (vtable
// slot 5) knows the key wins, and the cursor is set to the start of the file's data. Then
// 004DF8D0 reads one line from the cursor (the name) and 004DFB80 reads up to a '#' (the
// description).
//
// Keys of items are built from the database records (item_db.h): the record's name CString,
// spaces replaced by '_' and upper-cased, after a prefix that depends on the item kind.

#include "item.h"

#include "lib/mfc.h"

class CMappedResource;

class CTextManager {
public:
    // A node of the archive list (an STLport list): the value is a pair of the archive's name
    // (stlp::string, +0x08) and the archive.
    struct SourceNode {
        SourceNode* next;
        SourceNode* prev;
        char* nameBegin;     // +0x08
        char* nameEnd;       // +0x0C
        char* nameCapacity;  // +0x10
        CMappedResource* source;   // +0x14
    };

    // 004E0000. Whether `p` is at or past the end of the current text (signed compare).
    BOOL IsPastEnd(const char* p);

    // 004E0020. Looks up "string " + name and reads it (up to a '#', trailing CR/LF trimmed) into
    // `out`. When there is no such text `out` becomes `name` itself (unless it already holds that
    // very buffer) and the result is 0. Either way the cursor is cleared afterwards.
    BOOL GetString(const char* name, mfc::CString* out);

    // 004E0150. The name, description and kind ("item_weapon", ...) of an item, each looked up
    // in the texts; parts that are not found stay empty. Always returns 1.
    BOOL GetItemTexts(CItem* item, mfc::CString* name, mfc::CString* description, mfc::CString* kind);

    // 004E13E0. The item's display name: "format_item1" (or "format_item2" with the stack count
    // when the item's +0x48 is above 1) applied to the kind and the name.
    BOOL FormatItemName(CItem* item, mfc::CString* out);

    // Not rewritten (other range; forwarders).
    BOOL ReadLine(mfc::CString* out, BOOL advance);    // 004DF8D0: up to CR/LF, skips "##" lines
    BOOL ReadToHash(mfc::CString* out, BOOL advance);  // 004DFB80: up to '#'

    // The archive list (text_sources.cpp): 004DEDB0 sources_.Add(path, 0) (RET 4), 004DEDD0
    // sources_.Remove(path) (RET 4), 004DED40 (vtable 0073F6F0 slot 3) sources_.Clear().
    BOOL AddSource(const char* path);
    void RemoveSource(const char* path);
    void ClearSources();

    std::uint8_t unknown_00[0x1C];
    SourceNode* sources_;   // +0x1C list sentinel
    const char* cursor_;    // +0x20 read position in the current text
    const char* text_;      // +0x24 start of the current text (null: none)
    std::uint32_t length_;  // +0x28 size of the current text, written by GetFile
};
static_assert(offsetof(CTextManager, sources_) == 0x1C);
static_assert(offsetof(CTextManager, length_) == 0x28);
static_assert(offsetof(CTextManager::SourceNode, source) == 0x14);

// The game's text manager (the global pointer at 007AF788).
inline CTextManager* GameTexts() { return *reinterpret_cast<CTextManager* const*>(0x007AF788); }

// The archive list (STLport list<pair<string, CMappedResource*>>, CTextManager +0x1C). Nodes come
// from the STLport node allocator's 0x18-byte free list (0079B22C).
struct TextSourceList {
    using Node = CTextManager::SourceNode;
    Node* sentinel;

    // 004E1530. Begin iterator: *out = sentinel->next.
    void Begin(Node** out);
    // 004E1550. erase(pos): unlinks the node, frees its name and returns the node to the free
    // list; *out = the next node. Returns out.
    Node** Erase(Node** out, Node* pos);
    // 004E1590. insert(pos, value): a new node (004E15C0 copies the pair) linked before `pos`;
    // *out = the new node. Returns out.
    Node** Insert(Node** out, Node* pos, const void* value);
    // 004E15C0 (not rewritten): allocates a node and copy-constructs the pair (RET 4).
    Node* CreateNode(const void* value);

    // text_sources.cpp. 004DE930 (RET 8): unless an archive of that exact name is listed (then 1),
    // appends a new CMappedResource under `path` and returns its OpenResource(path, writable); a
    // failed open stays in the list. 004DEAF0 (RET 4): closes, deletes and unlinks the archive named
    // `path`, if any. 004DEB60: closes and deletes every archive, then frees the nodes.
    BOOL Add(const char* path, BOOL writable);
    void Remove(const char* path);
    void Clear();
};

// 004E1540. Copies an iterator (thiscall: *out = *this).
struct TextSourceIterator {
    CTextManager::SourceNode* node;
    void CopyTo(CTextManager::SourceNode** out);  // 004E1540
};

// 004DEAB0 (thiscall on an stlp::string, not rewritten): frees the string's storage.
void __fastcall FreeStringStorage(void* string);

// 004DC400 (__fastcall, not rewritten): replaces every `from` in `s` by `to`. Returns 1.
BOOL __fastcall ReplaceChar(mfc::CString* s, char from, char to);
