#pragma once

// Resource archives (*.res): the game's packed file format, used for textures, figures, maps,
// scripts and everything else under Res\. Class names come from the game's own error messages.
// File layout:
//   ResHeader                  at offset 0
//   ResEntry[header.count]     at header.tableOffset
//   char names[header.namesSize] immediately after the table (not NUL-terminated)
// Entries form a hash table: an entry's home slot is
//   (sum of tolower(name[i])) % count,
// and colliding names are chained through ResEntry::next (-1 ends the chain).

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "platform/platform.h"

#include <functional>

namespace toml {
class Value;
}


#pragma pack(push, 1)
struct ResHeader {
    std::uint32_t magic = 0;    // kResMagic
    std::uint32_t count = 0;    // number of entries (also the hash table size)
    std::uint32_t tableOffset = 0;  // file offset of the entry table
    std::uint32_t namesSize = 0;  // bytes of name data after the table
};
static_assert(sizeof(ResHeader) == 16);

struct ResEntry {
    static constexpr std::int32_t kChainEnd = -1;
    std::int32_t next = 0;      // next entry in the hash chain, or kChainEnd
    std::uint32_t size = 0;     // data size in bytes
    std::uint32_t offset = 0;   // data offset from the start of the archive
    std::uint32_t time = 0;     // modification time (time_t)
    std::uint16_t nameLength = 0;
    std::uint32_t nameOffset = 0;  // offset into the names block
};
static_assert(sizeof(ResEntry) == 22);
#pragma pack(pop)

constexpr std::uint32_t kResMagic = 0x019CE23C;

// A file loaded from an archive by MappedResource::GetFile.
struct LoadedFile {
    std::string name;                   // name as requested
    std::vector<std::uint8_t> bytes;    // contents (at least one byte, so that an empty file still has an address)
    std::uint32_t size = 0;
    int refCount = 0;                   // GetFile calls not yet matched by CloseFile
};

// The base of the archive readers (ResourceBaseClass in the game's messages): the archive header and tables. (The game's index table from numbers to
// entries, CreateAccelTable's, is never built: it has no caller.)
class ResourceBase {
public:
    virtual ~ResourceBase() = default;

    // Index of the entry named `name` (case-insensitive), or -1.
    int FindEntry(std::string_view name) const;

    ResHeader header{};
    ResEntry* entries = nullptr;
    char* names = nullptr;
};

// A record file (docs/notes/mods.md, "Records as TOML"): the original, when one is found, and the TOML files of its
// name and ".toml" over it, top first: those of every place it is looked for above the first that has the original,
// and that place's own. A TOML file with `replace = true` (ei-convert writes it) is the whole file: the search ends
// there, nothing below it read (`replaced`).
struct TomlFile {
    std::string path;       // where it was read, for messages
    std::string text;
};
struct RecordFile {
    bool found = false;     // the original
    bool replaced = false;  // the last of `tomls` replaces what lies below it
    std::string original;   // its bytes
    std::vector<TomlFile> tomls;

    bool Any() const { return found || !tomls.empty(); }
};

// The record file at `path` (a game folder's file, or one outside it such as a mod's config.reg) in each layer of the
// path map, top first: the TOML files down to the first layer that has the file, and the file.
RecordFile FindRecordFile(const std::string& path);
// Each of `file`'s TOML files parsed and handed to `merge` with its path, the bottom one first (so the top one's
// records win); one that does not parse is logged and left out.
void MergeTomls(const RecordFile& file, const std::function<void(const toml::Value& root, const std::string& path)>& merge);
// Whether a TOML file's top-level `key` and `value` are its `replace = true` (or false), which the merges skip.
bool IsReplaceKey(std::string_view key, const toml::Value& value);

// An archive's loose files in one folder (docs/notes/mods.md, "Loose files"): the folder in full and its files by
// name as the archive names its entries (the asset code page, ASCII letters in lower case).
struct LooseFiles {
    std::string folder;
    std::map<std::string, platform::LayerFile, std::less<>> byName;
    // The folder's texts.toml (docs/notes/mods.md, "Records as TOML"; not among byName), read with the folder: its
    // keys (in the asset code page, as the entries' names) and UTF-8 texts in the file's order.
    std::string textsPath;
    std::vector<std::pair<std::string, std::string>> texts;
    std::map<std::string, std::size_t, std::less<>> textIndex;   // by LooseKey
};

// Reads an archive with file I/O, loading individual files into memory on request. The folder of the archive's name
// stands over it (docs/notes/mods.md, "Loose files"): Res\textures\ over Res\textures.res, Maps\zone1\ over
// Maps\zone1.mpr; a file there is read in place of the entry of its name.
class MappedResource : public ResourceBase {
public:
    // Rebirth's ResOverride forgets the archive, then CloseResource.
    ~MappedResource() override;
    // True when the archive is open, or only its folder is there (an archive of no entries). A missing or broken
    // archive ends the game (game::FatalExit); the mapped reader (MMIOResource) returns false for a missing one.
    virtual bool OpenResource(const std::string& path);
    virtual void OpenFromMemory(const void* image);
    virtual void CloseResource();
    // The file `name` from the first of Sources() that has it; with a mod a "t\" or "s\" name not found is looked for
    // once more without the prefix (Rebirth's ResOverride). An .mmp's PNG (<name>.png) in a source stands for the .mmp
    // there (docs/notes/mods.md, "Textures as PNG"): the .mmp made from it is handed out.
    void* GetFile(std::string_view name, std::uint32_t* size);
    virtual bool CloseFile(void* data);
    virtual void FreeAllFiles();
    // Whether one of Sources() has `name` (an .mmp's PNG for it).
    bool HasFile(std::string_view name);
    // The record file `name` in Sources() (its TOML files in the folders; the original as GetFile finds it).
    RecordFile FindRecord(std::string_view name);
    // The text `name` (a texts archive's entry, docs/notes/mods.md "Records as TOML") in Sources(): a folder's file
    // of the name, then the folder's texts.toml; an archive's entry. `*utf8` tells which: the TOML's text is UTF-8,
    // a file's the asset code page's.
    void* FindText(std::string_view name, std::uint32_t* size, bool* utf8);

    // Fills `names` with the names in Sources() that match `pattern` (drive, directory, name and extension matched
    // separately with * and ?), or with every name when `pattern` is null: a folder's files by name, an archive's
    // entries in their order but those its own folder has. With a mod the names are appended to `names` (Rebirth's
    // ResOverride), without one they replace it.
    void ListFiles(std::vector<std::string>* names, const char* pattern);
    // The header checked and the entry table (packed 22-byte records) copied from the archive image at `base`,
    // the names pointed at in the image; one that is not an archive ends the game. Both readers' OpenFromMemory.
    void ReadImage(char* base);

    platform::File archive;           // the archive's file
    char* image = nullptr;              // archive image when opened with OpenFromMemory
    std::vector<ResEntry> entryTable;   // the entry table (entries points into it)
    std::vector<char> nameTable;        // the names OpenResource reads (names points into them)
    std::unordered_map<void*, std::unique_ptr<LoadedFile>> filesByData;   // the loaded files, by their contents
    std::map<std::string, LoadedFile*, std::less<>> filesByName;
    // The archive's folder (OpenResource) in each layer of the path map over the game folder, top first, and its own.
    std::vector<LooseFiles> looseOver;
    LooseFiles looseOwn;

protected:
    // The archive's entry `name` (shared when loaded already), or null.
    virtual void* EntryFile(std::string_view name, std::uint32_t* size);
    // looseOver and looseOwn read for the archive at `path`.
    void FindLooseFiles(const std::string& path);
    // The loose files and the files loaded from them forgotten.
    void ForgetLooseFiles();

private:
    // Where a name is looked for, in order: the archive's folder in each layer over the game folder; the mod's
    // archives for this one (Rebirth's ResOverride), each its own folder, then its entries; this archive's own folder
    // and entries, unless the mod ignores it. A folder met before, or one with no files, is left out.
    struct Source {
        const LooseFiles* loose = nullptr;    // a folder's files
        MappedResource* entries = nullptr;    // or an archive's entries
    };
    std::vector<Source> Sources();
    // The bytes of `name` in `source` (a folder's file, `*where` its path, or an archive's entry); false when it has
    // none or reading it fails.
    static bool ReadSource(const Source& source, std::string_view name, std::string* bytes, std::string* where);
    // The .mmp `name` made from its PNG `pngName` in sources[at] (shared when made already); null when that source has
    // no such PNG or it makes none (logged). Docs/notes/mods.md, "Textures as PNG".
    void* PngTexture(std::string_view name, const std::string& pngName, const std::vector<Source>& sources, std::size_t at,
                     std::uint32_t* size);
    // GetFile's search; with `utf8` (FindText) a folder's texts.toml too.
    void* Find(std::string_view name, std::uint32_t* size, bool* utf8);
    void* FindFile(std::string_view name, std::uint32_t* size, bool* utf8);
    // The loose file at `path` loaded as `name` (shared when loaded already); null when it cannot be read.
    void* LoadLooseFile(std::string_view name, const std::string& path, std::uint32_t* size);
};

// Maps the whole archive into memory; an entry is a pointer into the mapping. A loose file stays loaded until the
// archive closes.
class MMIOResource : public MappedResource {
public:
    bool OpenResource(const std::string& path) override;
    void OpenFromMemory(const void* image) override;
    void CloseResource() override;
    bool CloseFile(void* data) override;                              // (nothing to free)
    void FreeAllFiles() override;                                     // (empty)

    platform::MappedFile file;
    char* mappedImage = nullptr;         // base of the mapped archive

protected:
    void* EntryFile(std::string_view name, std::uint32_t* size) override;
};

// The folder that stands over the archive at `path` (docs/notes/mods.md, "Loose files"): the path without its
// extension for an archive (.res, .mpr, .mq), else empty.
std::string LooseFolderOf(std::string_view path);
// Whether the file at `path` is there: the file itself, or for an archive its folder.
bool ResourceExists(std::string_view path);

// The name listing of an archive (the archive readers are above).

// Matches `name` against `pattern` (* ends the match successfully, ? matches any
// character, other characters compare through toupper). An empty pattern matches an empty name,
// and a non-empty name gives `emptyPatternResult`.
int WildcardMatch(std::string_view name, std::string_view pattern, int emptyPatternResult);
