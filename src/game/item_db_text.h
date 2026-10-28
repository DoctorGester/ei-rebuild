#pragma once

// Item names and descriptions from the game's text resources. A text manager holds a list of resource
// archives (CMappedResource) and a read cursor into the last text it found. A text is looked up by key
// ("string format_item1", "weapon SWORD IRON", ...): the first archive that knows the key wins, and the cursor
// is set to the start of the file's data. Then one line is read from the cursor (the name) or up to a '#' (the
// description). Item keys are the record's name, spaces replaced by '_' and upper-cased, after a prefix that
// depends on the item kind.

#include "item.h"

#include <cstdint>
#include <string>

#include <list>
#include <vector>


// An archive the texts are looked up in.
struct TextSource {
    std::string name;
    CMappedResource* source;
};

// The archive list (CTextManager::sources_).
struct TextSources {
    // Add: unless an archive of that exact name is listed (then 1), appends a new
    // CMappedResource under `path` and returns its OpenResource(path, writable); a failed open stays in the
    // list. Remove: closes, deletes and unlinks the archive named `path`, if any. Clear: closes and deletes
    // every archive, then frees the nodes.
    BOOL Add(const char* path, BOOL writable);
    void Remove(const char* path);
    void Clear();
    // The data of file `key` from the first archive that has it (GetFile: its size written to `size`, which
    // must not be null), or null: the file's bytes as they are. FindKeyText (ours): the same, as text: its
    // UTF-8 copy, the size that copy's. ListKeys: `out` emptied, then the names matching `pattern`
    // of every archive appended in list order (a name in two archives is listed twice); Rebirth patches it
    // (ResOverride redirects the call).
    void* Find(const char* key, std::uint32_t* size);
    void* FindKeyText(const char* key, std::uint32_t* size);
    void ListKeys(std::vector<std::string>* out, const char* pattern);

    std::list<TextSource> archives;
};

class CTextManager : public CEngineObject {
public:
    CTextManager();
    ~CTextManager() override;

    // setlocale(LC_CTYPE, the "Font CharSet" option, "english" by default), then the C runtime's multibyte
    // code page set to the locale's; returns 1. The constructor calls it too.
    int Create() override;
    void Destroy() override { ClearSources(); }
    const char* GetClassName() override;
    int GetClassId() override;              // class id 0x2A

    // The first word at the cursor (leading spaces and line ends skipped; it ends at a
    // space, tab, CR or LF) into `out`; the cursor does not move. 0 when the cursor is at the end.
    BOOL PeekWord(std::string* out);

    // Whether `p` is at or past the end of the current text (signed compare).
    BOOL IsPastEnd(const char* p);

    // Looks up "string " + name and reads it (up to a '#', trailing CR/LF trimmed) into
    // `out`. When there is no such text `out` becomes `name` itself (unless it already holds that
    // very buffer) and the result is 0. Either way the cursor is cleared afterwards.
    BOOL GetString(const char* name, std::string* out);

    // The name, description and kind ("item_weapon", ...) of an item, each looked up
    // in the texts; parts that are not found stay empty. Always returns 1.
    BOOL GetItemTexts(CItem* item, std::string* name, std::string* description, std::string* kind);

    // The item's display name: "format_item1" (or "format_item2" with the stack count when count_ is above
    // 1) applied to the kind and the name.
    BOOL FormatItemName(CItem* item, std::string* out);

    // The line reader.
    // Makes the text `key` current (the first archive whose GetFile knows it), cursor at its
    // start. True when found and not empty.
    BOOL Open(const char* key);
    // Ours: Open for a file that is not text (a quest registry read from the cursor): its bytes as they
    // are, never converted to UTF-8.
    BOOL OpenData(const char* key);
    // The rest of the current line (up to CR or LF) into `out`; with `advance` the cursor
    // then moves to the next line that is not a comment. 0 when the cursor is at the end.
    BOOL ReadLine(std::string* out, BOOL advance);
    // The text up to the next '#' into `out`; with `advance` the cursor moves there.
    BOOL ReadToHash(std::string* out, BOOL advance);
    // The current line's words (separated by spaces and tabs; a word still open when the
    // text ends is dropped) into `words`; with `advance` the cursor moves to the next line that is not
    // a comment. 0 when the cursor is at the end.
    BOOL Split(std::vector<std::string>* words, BOOL advance);
    // Moves the cursor to the line that starts with `key`: null finds the next "#..." header
    // (not a "##" comment); one word compares the line's first word; several words (split at spaces)
    // compare the line's words, lower-cased, with them. 1 when found, else 0 (cursor at the end).
    BOOL FindLine(const char* key);

    BOOL AddSource(const char* path);
    void RemoveSource(const char* path);
    void ClearSources();
    // The names matching `pattern` ("pers *") of every archive into `out`.
    void ListKeys(std::vector<std::string>* out, const char* pattern);

    TextSources sources_;
    const char* cursor_;    // read position in the current text
    const char* text_;      // start of the current text (null: none)
    std::uint32_t length_;  // size of the current text, written by GetFile
};
