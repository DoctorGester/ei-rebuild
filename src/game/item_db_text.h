#pragma once

// Item names and descriptions from the game's text resources (004E0000-004E15C0).
// The game keeps its localized texts in resource archives (CMappedResource, resource.h). A text
// manager (global pointer at 007AF788; class name unknown, provisional CTextManager) holds a
// list of those archives and a read cursor into the last text it found. A text is looked up by
// key ("string format_item1", "weapon SWORD IRON", ...): the first archive whose GetFile (vtable
// slot 5) knows the key wins, and the cursor is set to the start of the file's data. Then
// 004DF8D0 reads one line from the cursor (the name) and 004DFB80 reads up to a '#' (the
// description).
// Keys of items are built from the database records (item_db.h): the record's name CString,
// spaces replaced by '_' and upper-cased, after a prefix that depends on the item kind.

#include "item.h"

#include "lib/mfc.h"
#include "lib/stlp.h"
#include "game_data.h"

class CMappedResource;
namespace console { class CStringArray; }

// An archive the texts are looked up in: the pair<string, CMappedResource*> of the archive list.
struct TextSource {
    stlp::string name;          // +0x00
    CMappedResource* source;    // +0x0C
};
static_assert(sizeof(TextSource) == 0x10);

// The archive list (STLport list<pair<string, CMappedResource*>>, CTextManager +0x1C). Nodes come
// from the STLport node allocator's 0x18-byte free list.
struct TextSources : stlp::list<TextSource> {
    // 004E1530 and its byte-identical copies (other lists' begin in the world range; A-W12): *out =
    // the first node.
    void Begin(Node** out);
    // Erase(pos): unlinks the node, frees its name and returns the node to the free list;
    // *out = the next node. Returns out.
    Node** Erase(Node** out, Node* pos);
    // _M_create_node(value): a node from the pool, the pair copied into it.
    Node* CreateNode(const TextSource* value);
    // Insert(pos, value): a new node (004E15C0 copies the pair) linked before `pos`; *out =
    // the new node. Returns out.
    Node** Insert(Node** out, Node* pos, const TextSource* value);
    // text_sources.cpp. 004DE930 (RET 8): unless an archive of that exact name is listed (then 1),
    // appends a new CMappedResource under `path` and returns its OpenResource(path, writable); a
    // failed open stays in the list. 004DEAF0 (RET 4): closes, deletes and unlinks the archive named
    // `path`, if any. 004DEB60: closes and deletes every archive, then frees the nodes.
    BOOL Add(const char* path, BOOL writable);
    void Remove(const char* path);
    void Clear();
    // 005628E0 (plain RET; the list's owner unwinding). Clear, then the list's own destructor.
    void DestructList();
    // A-S11. 004DEBC0 (RET 8): the data of file `key` from the first archive that has it (GetFile:
    // its size written to `size`, which must not be null), or null: the file's bytes as they are.
    // FindText (ours): the same, as text: with EI_UTF8 its UTF-8 copy (asset_text.h), the size that
    // copy's. CTextManager::Open reads through FindText, OpenData through Find (the archives also hold
    // binary files: the quest registries). 004DEC40 (RET 8): `out` emptied, then the names matching
    // `pattern` of every archive appended in list order (ListFiles; a name in two archives is listed
    // twice). Rebirth patches 004DEC40 (ResOverride redirects the ListFiles call): ours is the vanilla
    // function and installs in the vanilla launch only.
    void* Find(const char* key, std::uint32_t* size);
    void* FindText(const char* key, std::uint32_t* size);
    void ListKeys(mfc::CStringArray* out, const char* pattern);
};
static_assert(sizeof(TextSources::Node) == 0x18);
static_assert(sizeof(TextSources) == 4);

class CTextManager : public CEngineObject {
public:
    // The constructor as the client world's Setup inlines it (text_sources.cpp): the engine
    // object's fields, the archive list, then Create.
    CTextManager();
    // 005629B0 (deleting: 00562990; text_sources.cpp). ClearSources, then the archive list's own
    // clear and destruction (the sentinel back to the node pool).
    ~CTextManager() override;

    int Create() override;                  // 2: 004DED20 (below)
    void Destroy() override { ClearSources(); }
    const char* GetClassName() override;    // 11: 00562970 "CTextManager" (world_class_slots.cpp)
    int GetClassId() override;              // 12: 00562980 (0x2A)

    // The first word at the cursor (leading spaces and line ends skipped; it ends at a
    // space, tab, CR or LF) into `out`; the cursor does not move. 0 when the cursor is at the end.
    BOOL PeekWord(mfc::CString* out);
    using SourceNode = TextSources::Node;

    // Whether `p` is at or past the end of the current text (signed compare).
    BOOL IsPastEnd(const char* p);

    // Looks up "string " + name and reads it (up to a '#', trailing CR/LF trimmed) into
    // `out`. When there is no such text `out` becomes `name` itself (unless it already holds that
    // very buffer) and the result is 0. Either way the cursor is cleared afterwards.
    BOOL GetString(const char* name, mfc::CString* out);

    // The name, description and kind ("item_weapon", ...) of an item, each looked up
    // in the texts; parts that are not found stay empty. Always returns 1.
    BOOL GetItemTexts(CItem* item, mfc::CString* name, mfc::CString* description, mfc::CString* kind);

    // The item's display name: "format_item1" (or "format_item2" with the stack count
    // when the item's +0x48 is above 1) applied to the kind and the name.
    BOOL FormatItemName(CItem* item, mfc::CString* out);

    // The line reader (text_reader.cpp).
    // Makes the text `key` current (the first archive whose GetFile knows it), cursor at its
    // start. True when found and not empty.
    BOOL Open(const char* key);
    // Ours: Open for a file that is not text (a quest registry read from the cursor): its bytes as they
    // are, never converted (EI_UTF8). Without EI_UTF8 the same as Open.
    BOOL OpenData(const char* key);
    // The rest of the current line (up to CR or LF) into `out`; with `advance` the cursor
    // then moves to the next line that is not a comment. 0 when the cursor is at the end.
    BOOL ReadLine(mfc::CString* out, BOOL advance);
    // The text up to the next '#' into `out`; with `advance` the cursor moves there.
    BOOL ReadToHash(mfc::CString* out, BOOL advance);
    // The current line's words (separated by spaces and tabs; a word still open when the
    // text ends is dropped) into `words`; with `advance` the cursor moves to the next line that is not
    // a comment. 0 when the cursor is at the end.
    BOOL Split(console::CStringArray* words, BOOL advance);
    // Moves the cursor to the line that starts with `key`: null finds the next "#..." header
    // (not a "##" comment); one word compares the line's first word; several words (split at spaces)
    // compare the line's words, lower-cased, with them. 1 when found, else 0 (cursor at the end).
    BOOL FindLine(const char* key);

    // The archive list (text_sources.cpp): 004DEDB0 sources_.Add(path, 0) (RET 4), 004DEDD0
    // sources_.Remove(path) (RET 4), 004DED40 (slot 3) sources_.Clear().
    BOOL AddSource(const char* path);
    void RemoveSource(const char* path);
    void ClearSources();
    // A-S11 (text_sources.cpp). 004DEFA0 (RET 8): the names matching `pattern` ("pers *") of every
    // archive into `out` (004DEC40 on sources_). 004DED20 (slot 2, Create; the constructor also
    // calls it): setlocale(LC_CTYPE, the "Font CharSet" option, "english" by default), then the C
    // runtime's multibyte code page set to the locale's; returns 1.
    void ListKeys(mfc::CStringArray* out, const char* pattern);

    TextSources sources_;   // +0x1C
    const char* cursor_;    // +0x20 read position in the current text
    const char* text_;      // +0x24 start of the current text (null: none)
    std::uint32_t length_;  // +0x28 size of the current text, written by GetFile
};
static_assert(offsetof(CTextManager, sources_) == 0x1C);
static_assert(offsetof(CTextManager, length_) == 0x28);
static_assert(offsetof(CTextManager::SourceNode, data.source) == 0x14);

// The game's text manager (the global pointer at 007AF788).

using TextSourceNode = TextSources::Node;

// Copies an iterator (thiscall: *out = *this).
struct TextSourceIterator {
    CTextManager::SourceNode* node;
    // 004E1540 and its byte-identical copies: *out = node; returns `out` in EAX, which some callers use
    // (the sector lists' End, 0056DEC0).
    CTextManager::SourceNode** CopyTo(CTextManager::SourceNode** out);
};

