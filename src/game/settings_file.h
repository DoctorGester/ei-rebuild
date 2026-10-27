#pragma once

// The standalone's settings file (docs/notes/standalone.md, "Settings file"). The game keeps its settings
// under HKEY_CURRENT_USER: Software\Nival Interactive\EvilIslands (the options, options.cpp) and Rebirth's
// Software\Gipat.Ru\EI_Starter (its game settings and the starter's switches). ei_standalone.exe keeps the same
// keys and values in ei_settings.ini next to the exe instead: CWinRegistry (win_registry.cpp) and the direct
// readers (ReadValue) use it there, and never write the registry. When the file is missing, the first use reads
// both trees from the registry once (read only) and writes the file. game.exe (the original game) keeps the
// registry.
// The file: a section per key (its path below HKEY_CURRENT_USER), a line per value, as reg export writes them:
//   [Software\Nival Interactive\EvilIslands\General Settings]
//   "FullScreen"=dword:00000001          REG_DWORD
//   "VideoResolution"="1920x1080"        REG_SZ (\\ and \" escaped)
//   "Brightness"=float:0.5               a 4-byte REG_BINARY holding a float (CWinRegistry's floats)
//   "Data"=hex:01,02                     REG_BINARY
//   "Lines"=hex(7):61,00,00              any other type, or a string the quoted form cannot hold
// "@" names a key's default value; lines starting with ';' are comments. Every value round-trips bit for bit:
// the bytes are what the game's ANSI registry call gave (UTF-8 with EI_UTF8).
// Harness runs (verify build, tests/verify/harness_settings.h) pin a file of their own (Pin): written from the
// committed registry snapshot, read only (or writable, to check the options save), every read of a value it lacks
// reported. The one-time import reads a RegistrySource, the registry's or a test's stand-in (SetImportSource).

#include <deque>
#include <string>
#include <vector>

namespace settings_file {

inline constexpr const char* kFileName = "ei_settings.ini";

// One value as RegQueryValueEx gives it: the type (REG_DWORD, REG_SZ, ...) and the bytes (a string's with
// its terminator).
struct Value {
    std::string name;
    unsigned long type = 0;
    std::string data;
};

struct Key {
    std::string path;   // below HKEY_CURRENT_USER, parts joined by one '\'
    std::vector<Value> values;
};

// A settings file's keys and values, in file order. Paths and names compare without case, as the registry's.
class Document {
public:
    Key* FindKey(const std::string& path);
    Key& AddKey(const std::string& path);   // the key, made when missing
    static Value* FindValue(Key& key, const std::string& name);
    void Set(const std::string& path, const std::string& name, unsigned long type, std::string data);

    std::string Text() const;
    // Reads `text` into the document; each line not understood is skipped and described in `problems`.
    void Parse(const std::string& text, std::vector<std::string>* problems);

    std::deque<Key> keys;   // a deque: a key stays where it is as others are added (CWinRegistry holds one)
};

// Where the one-time import reads from: the key `path` (below HKEY_CURRENT_USER) as the game's ANSI registry calls
// read it. The registry's is in settings_file.cpp; DocumentSource and the harness's stand in for it.
class RegistrySource {
public:
    virtual ~RegistrySource() = default;
    // false when the key does not exist; else its values and the names of its subkeys (a value that cannot be
    // read is left out).
    virtual bool ReadKey(const std::string& path, std::vector<Value>* values, std::vector<std::string>* subkeys) = 0;
};

// A source over a Document (its keys' paths below HKEY_CURRENT_USER): a key exists when it or one below it is in
// the document.
class DocumentSource : public RegistrySource {
public:
    explicit DocumentSource(const Document& document) : document_(document) {}
    bool ReadKey(const std::string& path, std::vector<Value>* values, std::vector<std::string>* subkeys) override;

private:
    const Document& document_;
};

// The trees the game and Rebirth keep their settings in: what the import copies.
inline constexpr const char* kImportedTrees[] = {"Software\\Nival Interactive\\EvilIslands", "Software\\Gipat.Ru\\EI_Starter"};

// Copies kImportedTrees (every key below them, in the source's order) from `source` into `document`; the number
// of values copied.
int ImportTrees(Document& document, RegistrySource& source);

// `sub` below `path` as a key path: the parts of both, empty ones (doubled or outer '\') skipped.
std::string JoinPath(const char* path, const char* sub);

// ---- The process's settings (the standalone; game.exe uses the registry) ----

// Opens the key `path` (below HKEY_CURRENT_USER), making it when missing as RegCreateKeyEx does: the handle
// CWinRegistry holds.
void* CreateKey(const char* path);
// RegQueryValueEx of the value `name` of `sub` below `key` (a CreateKey handle, or null: below
// HKEY_CURRENT_USER): 0, ERROR_MORE_DATA (the size needed in *size) or ERROR_FILE_NOT_FOUND (key or value
// missing).
long Query(const void* key, const char* sub, const char* name, unsigned long* type, void* data, unsigned long* size);
// RegSetValueEx of `name` of `sub` below `key` (made when missing): 0. Kept until Flush.
long Set(const void* key, const char* sub, const char* name, unsigned long type, const void* data, unsigned long size);
// Writes the file when a value changed.
void Flush();

// RegQueryValueEx of the value `name` of HKEY_CURRENT_USER\<path>, for the readers that open, query and close
// a key directly: the settings file's.
long ReadValue(const char* path, const char* name, unsigned long* type, void* data, unsigned long* size);

// A change of each value read (the harness's and ei_hook.dll's redirects of the game paths, hook/paths.cpp):
// called in the order added with the value's name, type and bytes, which it may change.
using ReadFilter = void (*)(const char* name, unsigned long type, std::string& data);
void AddReadFilter(ReadFilter filter);

// Harness runs: `file` in place of the one next to the exe, and each read of a key or value it lacks passed to
// `missing` (key path, value name). `readOnly`: writes are dropped and a missing file is not imported; else the
// file is written as in the product (Flush at CWinRegistry::Close, .tmp then moved) and a missing file is
// imported from the import source. Before the first use.
using MissingHandler = void (*)(const char* path, const char* name);
void Pin(const char* file, MissingHandler missing, bool readOnly = true);
// Harness runs: the import reads `source` (which must outlive the run) in place of the registry.
void SetImportSource(RegistrySource* source);

} // namespace settings_file
