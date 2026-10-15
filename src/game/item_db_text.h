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
#include "lib/stlp.h"

class CMappedResource;
namespace console { class CStringArray; }

// An archive the texts are looked up in: the pair<string, CMappedResource*> of the archive list.
struct TextSource {
    stlp::string name;          // +0x00
    CMappedResource* source;    // +0x0C
};
static_assert(sizeof(TextSource) == 0x10);

// The archive list (STLport list<pair<string, CMappedResource*>>, CTextManager +0x1C). Nodes come
// from the STLport node allocator's 0x18-byte free list (0079B22C).
using TextSourceList = stlp::list<TextSource>;
static_assert(sizeof(TextSourceList::Node) == 0x18);

class CTextManager {
public:
    // 004DF770. The first word at the cursor (leading spaces and line ends skipped; it ends at a
    // space, tab, CR or LF) into `out`; the cursor does not move. 0 when the cursor is at the end.
    BOOL PeekWord(mfc::CString* out);
    using SourceNode = TextSourceList::Node;

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

    // The line reader (text_reader.cpp).
    // 004DEFC0. Makes the text `key` current (the first archive whose GetFile knows it), cursor at its
    // start. True when found and not empty.
    BOOL Open(const char* key);
    // 004DF8D0. The rest of the current line (up to CR or LF) into `out`; with `advance` the cursor
    // then moves to the next line that is not a comment. 0 when the cursor is at the end.
    BOOL ReadLine(mfc::CString* out, BOOL advance);
    // 004DFB80. The text up to the next '#' into `out`; with `advance` the cursor moves there.
    BOOL ReadToHash(mfc::CString* out, BOOL advance);
    // 004DFCB0. The current line's words (separated by spaces and tabs; a word still open when the
    // text ends is dropped) into `words`; with `advance` the cursor moves to the next line that is not
    // a comment. 0 when the cursor is at the end.
    BOOL Split(console::CStringArray* words, BOOL advance);
    // 004DF030. Moves the cursor to the line that starts with `key`: null finds the next "#..." header
    // (not a "##" comment); one word compares the line's first word; several words (split at spaces)
    // compare the line's words, lower-cased, with them. 1 when found, else 0 (cursor at the end).
    BOOL FindLine(const char* key);

    // The archive list (text_sources.cpp): 004DEDB0 sources_.Add(path, 0) (RET 4), 004DEDD0
    // sources_.Remove(path) (RET 4), 004DED40 (vtable 0073F6F0 slot 3) sources_.Clear().
    BOOL AddSource(const char* path);
    void RemoveSource(const char* path);
    void ClearSources();
    // A-S11 (text_sources.cpp). 004DEFA0 (RET 8): the names matching `pattern` ("pers *") of every
    // archive into `out` (004DEC40 on sources_). 004DED20 (vtable slot 2, Create; the client
    // world's set-up also calls it directly): setlocale(LC_CTYPE, the "Font CharSet" option,
    // "english" by default), then the C runtime's multibyte code page set to the locale's; returns 1.
    void ListKeys(mfc::CStringArray* out, const char* pattern);
    int Create();

    // The text manager is an engine object (the first 0x1C bytes) with this vtable.
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073F6F0);
    // 005629B0 (text_sources.cpp, A-W11). The destructor body: its vtable, ClearSources, the
    // archive list's own clear (004DEB60) and destruction (the sentinel back to the node pool),
    // then the engine object's DestructBase. 00562990 (slot 0, RET 4): it, then operator delete
    // when bit 0 of `flags` is set; returns this.
    void Destruct();
    CTextManager* DeletingDestruct(unsigned flags);

    std::uint8_t unused_00[0x1C];
    TextSourceList sources_;   // +0x1C
    const char* cursor_;    // +0x20 read position in the current text
    const char* text_;      // +0x24 start of the current text (null: none)
    std::uint32_t length_;  // +0x28 size of the current text, written by GetFile
};
static_assert(offsetof(CTextManager, sources_) == 0x1C);
static_assert(offsetof(CTextManager, length_) == 0x28);
static_assert(offsetof(CTextManager::SourceNode, data.source) == 0x14);

// The game's text manager (the global pointer at 007AF788).
inline CTextManager* GameTexts() { return *reinterpret_cast<CTextManager* const*>(0x007AF788); }

// The archive list's members (thiscall on the list).
using TextSourceNode = TextSourceList::Node;
// 004E1530. Begin iterator: *out = the first node.
void __fastcall TextSourcesBegin(TextSourceList* list, void*, TextSourceNode** out);
// 004E1550. erase(pos): unlinks the node, frees its name and returns the node to the free list;
// *out = the next node. Returns out.
TextSourceNode** __fastcall TextSourcesErase(TextSourceList* list, void*, TextSourceNode** out, TextSourceNode* pos);
// 004E15C0 (RET 4). _M_create_node(value): a node from the pool, the pair copied into it.
TextSourceNode* __fastcall TextSourcesCreateNode(TextSourceList* list, void*, const TextSource* value);
// 004E1590. insert(pos, value): a new node (004E15C0 copies the pair) linked before `pos`; *out =
// the new node. Returns out.
TextSourceNode** __fastcall TextSourcesInsert(TextSourceList* list, void*, TextSourceNode** out, TextSourceNode* pos,
                                              const TextSource* value);
// text_sources.cpp. 004DE930 (RET 8): unless an archive of that exact name is listed (then 1),
// appends a new CMappedResource under `path` and returns its OpenResource(path, writable); a
// failed open stays in the list. 004DEAF0 (RET 4): closes, deletes and unlinks the archive named
// `path`, if any. 004DEB60: closes and deletes every archive, then frees the nodes.
BOOL __fastcall TextSourcesAdd(TextSourceList* list, void*, const char* path, BOOL writable);
void __fastcall TextSourcesRemove(TextSourceList* list, void*, const char* path);
void __fastcall TextSourcesClear(TextSourceList* list);
// 005628E0 (plain RET; the list's owner unwinding). TextSourcesClear, then the list's own destructor.
void __fastcall TextSourcesDestruct(TextSourceList* list);
// A-S11. 004DEBC0 (RET 8): the data of file `key` from the first archive that has it (GetFile:
// its size written to `size`), or null. 004DEC40 (RET 8): `out` emptied, then the names matching
// `pattern` of every archive appended in list order (ListFiles; a name in two archives is listed
// twice). Rebirth patches 004DEC40 (ResOverride redirects the ListFiles call): ours is the vanilla
// function and installs in the vanilla launch only.
void* __fastcall TextSourcesFind(TextSourceList* list, void*, const char* key, std::uint32_t* size);
void __fastcall TextSourcesListKeys(TextSourceList* list, void*, mfc::CStringArray* out, const char* pattern);

// 004E1540. Copies an iterator (thiscall: *out = *this).
struct TextSourceIterator {
    CTextManager::SourceNode* node;
    // 004E1540 and its byte-identical copies: *out = node; returns `out` in EAX, which some callers use
    // (the sector lists' End, 0056DEC0).
    CTextManager::SourceNode** CopyTo(CTextManager::SourceNode** out);
};

// 004DEAB0 (thiscall on an stlp::string, not rewritten): frees the string's storage.
void __fastcall FreeStringStorage(void* string);

// 004DC400 (__fastcall, not rewritten): replaces every `from` in `s` by `to`. Returns 1.
BOOL __fastcall ReplaceChar(mfc::CString* s, char from, char to);
