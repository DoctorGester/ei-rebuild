#include "engine/core/asset_text.h"
#include "engine/core/core_streams.h"
#include "engine/data/resource.h"
#include "engine/data/map_folder.h"
#include "engine/mods/mod_folders.h"
#include "engine/models/figure_gltf.h"
#include "engine/models/texture_png.h"

#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "lib/threads.h"
#include "lib/toml.h"
#include "platform/platform.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

int ResourceBase::FindEntry(std::string_view name) const {
    std::uint32_t count = header.count;
    if (count == 0) return -1;

    std::uint32_t hash = 0;
    std::uint32_t length = 0;
    for (; length < name.size(); ++length)
        hash += text::ToLower(static_cast<unsigned char>(name[length]));

    std::uint32_t index = hash % count;
    for (;;) {
        const ResEntry& e = entries[index];
        if (length == e.nameLength && text::CompareIgnoringCase(name.data(), names + e.nameOffset, length) == 0)
            return static_cast<int>(index);
        if (e.next == ResEntry::kChainEnd) return -1;
        index = static_cast<std::uint32_t>(e.next);
    }
}

namespace {

// A name as the loose files are indexed by: ASCII letters in lower case (the archives' own comparison, FindEntry).
std::string LooseKey(std::string_view name) {
    std::string key(name);
    for (char& c : key) c = static_cast<char>(text::ToLower(static_cast<unsigned char>(c)));
    return key;
}

// The file `name` the archive loaded already, shared (one more reference), or null.
void* SharedFile(MappedResource& archive, std::string_view name, std::uint32_t* size) {
    const auto loaded = archive.filesByName.find(name);
    if (loaded == archive.filesByName.end()) return nullptr;
    LoadedFile* file = loaded->second;
    ++file->refCount;
    if (size) *size = file->size;
    return file->bytes.data();
}

// `file` (its name, size and bytes set) kept by the archive with one reference: its bytes.
void* KeptFile(MappedResource& archive, std::unique_ptr<LoadedFile> file, std::uint32_t* size) {
    file->refCount = 1;
    if (file->bytes.empty()) file->bytes.push_back(0);   // an empty file still has an address
    void* data = file->bytes.data();
    if (size) *size = file->size;
    archive.filesByName[file->name] = file.get();
    archive.filesByData[data] = std::move(file);
    return data;
}

// How many bytes of .mmp files made from PNGs an archive keeps after their last close (MappedResource::keptPngs): with
// 128 MB each texture of mod_lost_in_astral_cz_ingos' seven zones (447 textures, 132 MB) is made once.
constexpr std::size_t kKeptPngBytes = 128u << 20;

// A map archive's map file's extension (the landscape and the AI map list "*.mp").
constexpr const char* kMapFileExtension = ".MP";

// The name of a folder's texts (docs/notes/mods.md, "Records as TOML"), as LooseKey gives it.
constexpr std::string_view kTextsToml = "texts.toml";

// Whether `key` (a LooseKey) ends with `extension`.
bool EndsWith(std::string_view key, std::string_view extension) {
    return key.size() > extension.size() && key.substr(key.size() - extension.size()) == extension;
}

// A figure model's glTF (docs/notes/mods.md, "Models as glTF"): `key` (a LooseKey) entered by its name without the
// extension, a .gltf before a .glb of the same name.
void AddModel(std::map<std::string, std::string, std::less<>>& models, const std::string& key) {
    const bool gltf = EndsWith(key, ".gltf");
    if (!gltf && !EndsWith(key, ".glb")) return;
    const auto [found, added] = models.try_emplace(key.substr(0, key.size() - (gltf ? 5 : 4)), key);
    if (!added && gltf) found->second = key;
}

// Whether `key` (a LooseKey) is a figure file a glTF can stand for: .mod, .bon, .lnk, .fig, .anm.
bool IsFigureFile(std::string_view key) {
    for (const std::string_view extension : {".mod", ".bon", ".lnk", ".fig", ".anm"})
        if (EndsWith(key, extension)) return true;
    return false;
}

// The texts of `loose`'s texts.toml read into it; a file that cannot be read or parsed, or a value that is not a
// string, is logged and left out.
void ReadLooseTexts(LooseFiles& loose) {
    std::string bytes;
    toml::Document document;
    if (!platform::ReadLayerFile(loose.textsPath, &bytes) || !document.Parse(bytes, loose.textsPath)) {
        game::Warning("texts: %s does not read: %s", loose.textsPath, document.Error());
        return;
    }
    const toml::Value root = document.Root();
    for (int i = 0; i < root.Size(); ++i) {
        const toml::Value value = root.ValueAt(i);
        if (!value.IsString()) {
            game::Warning("texts: %s line %d: not a string", loose.textsPath, value.Line());
            continue;
        }
        std::string name = text::ToAssetText(root.KeyAt(i));
        const auto [index, added] = loose.textIndex.try_emplace(LooseKey(name), loose.texts.size());
        if (added) loose.texts.emplace_back(std::move(name), value.String());
        else loose.texts[index->second].second = value.String();
    }
}

// The UTF-8 text of `key` (a LooseKey) in `loose`'s texts.toml, or null.
const std::string* LooseText(const LooseFiles& loose, std::string_view key) {
    const auto found = loose.textIndex.find(key);
    return found == loose.textIndex.end() ? nullptr : &loose.texts[found->second].second;
}

} // namespace

MappedResource::~MappedResource() {
    // Rebirth's ResOverride: addon forgets the archive first.
    if (modfolders::Active()) modfolders::OnDestroyResource(this);
    CloseResource();
}

// The archive's reads are File's ReadAt (SetFilePointer, then ReadFile; VC6's _lseek and _read on a binary descriptor
// did the same calls).
bool MappedResource::OpenResource(const std::string& path) {
    // Rebirth's ResOverride: addon loads the mod's archives on the first open and records
    // which archive this is.
    if (modfolders::Active()) modfolders::OnOpenResource(this, path);
    CloseResource();
    FindLooseFiles(path);
    if (!archive.Open(path, platform::FileMode::Read)) {
        // Only the archive's folder: an archive of no entries.
        const std::string folder = LooseFolderOf(path);
        if (!folder.empty() && platform::PathExists(folder)) return true;
        game::FatalExit("CMappedResource::OpenResource()\nUnable to open file \"" + path + "\"");
    }
    const std::string unreadable = "CMappedResource::OpenResource()\nUnable to read file \"" + path + "\"";
    if (!archive.ReadAt(0, &header, sizeof(header))) game::FatalExit(unreadable);
    if (header.magic != kResMagic) game::FatalExit("Wrong format resource file \"" + path + "\"");
    unsigned tableSize = header.count * sizeof(ResEntry);
    entryTable.resize(header.count);
    nameTable.resize(header.namesSize);
    entries = entryTable.data();
    names = nameTable.data();
    if (!archive.ReadAt(static_cast<long>(header.tableOffset), entries, tableSize) ||
        !archive.ReadAt(static_cast<long>(header.tableOffset + tableSize), names, header.namesSize))
        game::FatalExit(unreadable);
    return true;
}

void MappedResource::ReadImage(char* base) {
    header = lib::Load<ResHeader>(base);
    if (header.magic != kResMagic) game::FatalExit("Wrong format resource");
    entryTable.resize(header.count);
    if (header.count != 0)
        std::memcpy(entryTable.data(), base + header.tableOffset, header.count * sizeof(ResEntry));
    entries = entryTable.data();
    names = base + header.tableOffset + header.count * sizeof(ResEntry);
}

void MappedResource::OpenFromMemory(const void* memoryImage) {
    CloseResource();
    // GetFile hands out pointers into the image (writable through them).
    image = const_cast<char*>(static_cast<const char*>(memoryImage));
    ReadImage(image);
}

void MappedResource::CloseResource() {
    FreeAllFiles();
    ForgetLooseFiles();
    archive.Close();
    entries = nullptr;
    names = nullptr;
    entryTable = {};
    nameTable = {};
    image = nullptr;
    header = ResHeader{};
}

void MappedResource::FindLooseFiles(const std::string& path) {
    const std::string folder = LooseFolderOf(path);
    if (folder.empty()) return;
    if (text::CompareIgnoringCase(std::string_view(path).substr(folder.size()), ".mpr") == 0) mapFolder = folder;
    std::vector<platform::FolderLayer> layers = platform::FilesByLayer(folder);
    for (std::size_t i = 0; i < layers.size(); ++i) {
        LooseFiles& loose = i + 1 < layers.size() ? looseOver.emplace_back() : looseOwn;
        loose.folder = std::move(layers[i].folder);
        for (platform::LayerFile& file : layers[i].files) {
            file.name = text::ToAssetText(file.name);
            if (LooseKey(file.name) == kTextsToml) {
                loose.textsPath = file.path;
                continue;
            }
            const auto [kept, added] = loose.byName.try_emplace(LooseKey(file.name), file);
            if (added) {
                AddModel(loose.models, kept->first);
                continue;
            }
            // Two files of one name in a folder (a system that tells their case apart): the first by name is read.
            platform::LayerFile& first = kept->second;
            if (file.name < first.name) std::swap(file, first);
            game::Warning("loose files: %s and %s have one name; the first is read", first.path, file.path);
        }
        if (!loose.textsPath.empty()) ReadLooseTexts(loose);
    }
}

void MappedResource::ForgetLooseFiles() {
    looseOver.clear();
    looseOwn = {};
    mapFolder.clear();
    folderMap.reset();
    folderMapMade = false;
    modelEntries.clear();
    modelEntriesRead = false;
    madeModels.clear();
    wornSets.clear();
    keptPngs.clear();
    keptPngBytes = 0;
}

std::vector<MappedResource::Source> MappedResource::Sources() {
    std::vector<Source> sources;
    std::vector<const std::string*> met;
    auto addFolder = [&](const LooseFiles& loose) {
        if (loose.byName.empty() && loose.texts.empty()) return;
        for (const std::string* folder : met)
            if (text::CompareIgnoringCase(*folder, loose.folder) == 0) return;
        met.push_back(&loose.folder);
        sources.push_back({&loose, nullptr});
    };
    auto addArchive = [&](MappedResource* archive) {
        for (const LooseFiles& loose : archive->looseOver) addFolder(loose);
        addFolder(archive->looseOwn);
        sources.push_back({nullptr, archive});
    };
    for (const LooseFiles& loose : looseOver) addFolder(loose);
    for (MappedResource* mod : modfolders::ModArchivesOf(this)) addArchive(mod);
    if (!modfolders::Ignores(this)) addArchive(this);
    return sources;
}

void* MappedResource::GetFile(std::string_view name, std::uint32_t* size) { return Find(name, size, nullptr); }

void* MappedResource::FindText(std::string_view name, std::uint32_t* size, bool* utf8) {
    *utf8 = false;
    return Find(name, size, utf8);
}

void* MappedResource::Find(std::string_view name, std::uint32_t* size, bool* utf8) {
    void* data = FindFile(name, size, utf8);
    // Rebirth's ResOverride: with a mod a "t\" or "s\" name not found is looked for once more without the
    // prefix.
    if (!data && modfolders::Active() && name.size() >= 2 && (name[0] == 't' || name[0] == 's') && name[1] == '\\')
        data = FindFile(name.substr(2), size, utf8);
    return data;
}

namespace {

// The name of the PNG that stands for the texture `name` (docs/notes/mods.md, "Textures as PNG"): an .mmp's name with
// ".png"; empty for another file.
std::string PngNameOf(std::string_view name) {
    constexpr std::string_view kMmp = ".mmp";
    if (name.size() <= kMmp.size() || text::CompareIgnoringCase(name.substr(name.size() - kMmp.size()), kMmp) != 0) return {};
    return std::string(name.substr(0, name.size() - kMmp.size())) + ".png";
}

} // namespace

bool MappedResource::ReadSource(const Source& source, std::string_view name, std::string* bytes, std::string* where) {
    if (source.loose) {
        const auto file = source.loose->byName.find(LooseKey(name));
        if (file == source.loose->byName.end()) return false;
        *where = file->second.path;
        // The layer's own file: a read through the path map would take a layer over it that has one of its name
        // (a mod's model without animations over the game's with them).
        return platform::ReadLayerFile(file->second.path, bytes);
    }
    if (source.entries->FindEntry(name) == -1) return false;
    std::uint32_t size = 0;
    void* data = source.entries->EntryFile(name, &size);
    if (!data) return false;
    bytes->assign(static_cast<const char*>(data), size);
    source.entries->CloseFile(data);
    *where = name;
    return true;
}

bool MappedResource::SourceHas(const Source& source, std::string_view name) {
    return source.loose ? source.loose->byName.contains(LooseKey(name)) : source.entries->FindEntry(name) != -1;
}

void* MappedResource::KeptPng(std::string_view name, std::uint32_t* size) {
    const auto kept = std::find_if(keptPngs.begin(), keptPngs.end(), [&](const auto& file) { return file->name == name; });
    if (kept == keptPngs.end()) return nullptr;
    std::unique_ptr<LoadedFile> file = std::move(*kept);
    keptPngs.erase(kept);
    keptPngBytes -= file->bytes.size();
    return KeptFile(*this, std::move(file), size);
}

struct MappedResource::PngRead {
    std::string name;       // the .mmp's
    std::string png;
    std::string where;      // the PNG's path or entry, for the warning
    std::string original;   // the .mmp it stands for when the PNG has no settings chunk; empty when none
};

bool MappedResource::ReadPng(std::string_view name, const std::string& pngName, const std::vector<Source>& sources,
                             std::size_t at, PngRead* out) {
    out->name = name;
    if (!ReadSource(sources[at], pngName, &out->png, &out->where)) return false;
    std::string originalWhere;
    if (texture_png::SettingsText(out->png).empty())
        for (std::size_t i = at; i < sources.size(); ++i)
            if (ReadSource(sources[i], name, &out->original, &originalWhere)) break;
    return true;
}

std::unique_ptr<LoadedFile> MappedResource::MakePng(const PngRead& read, std::string* error) {
    MmpHeader originalHeader;
    texture_png::MmpView below;
    if (read.original.size() >= sizeof(MmpHeader)) {
        std::memcpy(&originalHeader, read.original.data(), sizeof(MmpHeader));
        below = {&originalHeader, reinterpret_cast<const std::uint8_t*>(read.original.data()) + sizeof(MmpHeader),
                 read.original.size() - sizeof(MmpHeader)};
    }
    auto file = std::make_unique<LoadedFile>();
    if (!texture_png::FromPng(read.png, below.header ? &below : nullptr, &file->bytes, error)) return nullptr;
    file->name = read.name;
    file->size = static_cast<std::uint32_t>(file->bytes.size());
    file->madeFromPng = true;
    return file;
}

void* MappedResource::PngTexture(std::string_view name, const std::string& pngName, const std::vector<Source>& sources,
                                 std::size_t at, std::uint32_t* size) {
    if (!SourceHas(sources[at], pngName)) return nullptr;
    if (void* data = SharedFile(*this, name, size)) return data;
    if (void* data = KeptPng(name, size)) return data;
    PngRead read;
    if (!ReadPng(name, pngName, sources, at, &read)) return nullptr;
    std::string error;
    std::unique_ptr<LoadedFile> file = MakePng(read, &error);
    if (!file) {
        game::Warning("textures: %s: %s; not read", read.where, error);
        return nullptr;
    }
    return KeptFile(*this, std::move(file), size);
}

void MappedResource::MakePngs(std::span<const std::string> files) {
    const std::vector<Source> sources = Sources();
    // Each name's PNG and the source it is in, FindFile's: the first source with the PNG or the .mmp.
    std::vector<std::pair<const std::string*, std::size_t>> pngs;
    for (const std::string& name : files) {
        const std::string pngName = PngNameOf(name);
        if (pngName.empty() || filesByName.contains(name) ||
            std::ranges::any_of(keptPngs, [&](const auto& file) { return file->name == name; }))
            continue;
        for (std::size_t i = 0; i < sources.size(); ++i) {
            if (SourceHas(sources[i], pngName)) {
                pngs.emplace_back(&name, i);
                break;
            }
            if (SourceHas(sources[i], name)) break;
        }
    }
    // A round of one a processor at a time, up to half of keptPngs' bytes: the files made first are still kept when
    // their GetFile comes (the rest are made there, one at a time).
    const std::size_t round = std::max(1u, std::thread::hardware_concurrency());
    std::size_t madeBytes = 0;
    for (std::size_t first = 0; first < pngs.size() && madeBytes < kKeptPngBytes / 2; first += round) {
        std::vector<PngRead> reads;
        for (std::size_t i = first; i < std::min(first + round, pngs.size()); ++i) {
            const std::string& name = *pngs[i].first;
            if (PngRead read; ReadPng(name, PngNameOf(name), sources, pngs[i].second, &read)) reads.push_back(std::move(read));
        }
        std::vector<std::unique_ptr<LoadedFile>> made(reads.size());
        lib::OnThreads(static_cast<int>(reads.size()), [&](int i) {
            std::string error;
            made[i] = MakePng(reads[i], &error);
        });
        for (std::unique_ptr<LoadedFile>& file : made)
            if (file) {
                madeBytes += file->bytes.size();
                KeepPng(std::move(file));
            }
    }
}

void MappedResource::KeepPng(std::unique_ptr<LoadedFile> file) {
    keptPngBytes += file->bytes.size();
    keptPngs.push_back(std::move(file));
    while (keptPngBytes > kKeptPngBytes) {
        keptPngBytes -= keptPngs.front()->bytes.size();
        keptPngs.pop_front();
    }
}

void* MappedResource::FindFile(std::string_view name, std::uint32_t* size, bool* utf8) {
    if (void* data = FolderMapFile(name, size)) return data;
    const std::string key = LooseKey(name);
    const std::string pngName = PngNameOf(name);
    const std::vector<Source> sources = Sources();
    if (EndsWith(key, ".mod") || EndsWith(key, ".bon"))
        if (void* data = WornSetFile(name, key, sources, size)) return data;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const Source& source = sources[i];
        if (!pngName.empty())
            if (void* data = PngTexture(name, pngName, sources, i, size)) return data;
        if (IsFigureFile(key))
            if (void* data = ModelFile(name, source, size)) return data;
        if (!source.loose) {
            if (void* data = source.entries->EntryFile(name, size)) return data;
            continue;
        }
        if (const auto file = source.loose->byName.find(key); file != source.loose->byName.end())
            if (void* data = LoadLooseFile(name, file->second.path, size)) return data;
        if (utf8)
            if (const std::string* text = LooseText(*source.loose, key)) {
                *utf8 = true;
                if (size) *size = static_cast<std::uint32_t>(text->size());
                return const_cast<char*>(text->data());
            }
    }
    return nullptr;
}

namespace {

// `file` read (its text) and whether it replaces what lies below it (`replace = true`).
bool ReplacesBelow(const TomlFile& file) {
    toml::Document document;
    return document.Parse(file.text, file.path) && document.Root()["replace"].IsBool() &&
           document.Root()["replace"].Bool();
}

} // namespace

bool IsReplaceKey(std::string_view key, const toml::Value& value) { return key == "replace" && value.IsBool(); }

RecordFile MappedResource::FindRecord(std::string_view name) {
    RecordFile record;
    const std::string key = LooseKey(name);
    const std::string tomlName = std::string(name) + ".toml";
    const std::string tomlKey = LooseKey(tomlName);
    for (const Source& source : Sources()) {
        bool original = false;
        if (source.loose) {
            if (const auto toml = source.loose->byName.find(tomlKey); toml != source.loose->byName.end()) {
                TomlFile& file = record.tomls.emplace_back();
                file.path = toml->second.path;
                platform::ReadLayerFile(file.path, &file.text);
                record.replaced = ReplacesBelow(file);
            }
            original = source.loose->byName.contains(key);
        } else {
            std::uint32_t size = 0;
            if (void* data = source.entries->EntryFile(tomlName, &size)) {
                record.tomls.push_back({tomlName, std::string(static_cast<const char*>(data), size)});
                source.entries->CloseFile(data);
                record.replaced = ReplacesBelow(record.tomls.back());
            }
            original = source.entries->FindEntry(name) != -1;
        }
        if (original || record.replaced) break;
    }
    std::uint32_t size = 0;
    if (record.replaced) return record;
    if (void* data = GetFile(name, &size)) {
        record.found = true;
        record.original.assign(static_cast<const char*>(data), size);
        CloseFile(data);
    }
    return record;
}

bool MappedResource::HasFile(std::string_view name) {
    const std::string key = LooseKey(name);
    const std::string pngName = PngNameOf(name);
    for (const Source& source : Sources()) {
        if (SourceHas(source, name)) return true;
        if (!pngName.empty() && SourceHas(source, pngName)) return true;
        if (IsFigureFile(key) && HasModelFile(name, source)) return true;
    }
    return false;
}

// ---- Figure models as glTF (docs/notes/mods.md, "Models as glTF") ----------------------------------------------

namespace {

// The glTF models among an archive's own entries (read at the first use).
const std::map<std::string, std::string, std::less<>>& ModelEntries(MappedResource& archive) {
    if (archive.modelEntriesRead) return archive.modelEntries;
    archive.modelEntriesRead = true;
    if (!archive.entries || !archive.names) return archive.modelEntries;
    for (std::uint32_t i = 0; i < archive.header.count; ++i) {
        const ResEntry& entry = archive.entries[i];
        if (entry.nameOffset > archive.header.namesSize || entry.nameLength > archive.header.namesSize - entry.nameOffset)
            continue;
        AddModel(archive.modelEntries, LooseKey(std::string_view(archive.names + entry.nameOffset, entry.nameLength)));
    }
    return archive.modelEntries;
}

} // namespace

const std::map<std::string, std::string>* MappedResource::MadeModel(const Source& source, const std::string& model) {
    std::string bytes, where;
    const std::string place = source.loose ? source.loose->folder : std::to_string(reinterpret_cast<std::uintptr_t>(source.entries));
    const auto cached = madeModels.find(place + "|" + model);
    if (cached != madeModels.end()) return cached->second ? &*cached->second : nullptr;
    auto& made = madeModels[place + "|" + model];
    if (!ReadSource(source, model, &bytes, &where)) return nullptr;
    // A .gltf's buffers are files of its place, named relative to it.
    const std::size_t slash = model.find_last_of('\\');
    const std::string folder = slash == std::string::npos ? std::string() : model.substr(0, slash + 1);
    const figure_gltf::ReadUri readUri = [&](const std::string& uri, std::string* out) {
        std::string name = folder + uri;
        std::replace(name.begin(), name.end(), '/', '\\');
        std::string ignored;
        return ReadSource(source, text::ToAssetText(name), out, &ignored);
    };
    figure_gltf::Figure figure;
    figure_gltf::Notes notes;
    std::string error;
    if (!figure_gltf::FromGltf(bytes, readUri, &figure, &error, &notes)) {
        game::Warning("models: %s: %s; not read", where, error);
        return nullptr;
    }
    for (const std::string& line : notes.lines) game::Warning("models: %s: %s", where, line);
    const std::size_t dot = model.find_last_of('.');
    made = figure_gltf::Files(figure, model.substr(0, dot));
    return &*made;
}

const std::string* MappedResource::MadeModelFile(const std::string& key, const Source& source) {
    const auto& models = source.loose ? source.loose->models : ModelEntries(*source.entries);
    if (models.empty()) return nullptr;
    const std::string_view stem = std::string_view(key).substr(0, key.size() - 4);
    // The longest model name that begins the file's (a loose figure's <name><part>.fig).
    for (std::size_t length = stem.size(); length > 0; --length) {
        const auto model = models.find(stem.substr(0, length));
        if (model == models.end()) continue;
        const std::map<std::string, std::string>* files = MadeModel(source, model->second);
        if (!files) continue;
        if (const auto file = files->find(key); file != files->end()) return &file->second;
    }
    return nullptr;
}

void* MappedResource::ModelFile(std::string_view name, const Source& source, std::uint32_t* size) {
    const std::string* file = MadeModelFile(LooseKey(name), source);
    if (!file) return nullptr;
    if (void* data = SharedFile(*this, name, size)) return data;
    auto made = std::make_unique<LoadedFile>();
    made->name = name;
    made->size = static_cast<std::uint32_t>(file->size());
    made->bytes.assign(file->begin(), file->end());
    return KeptFile(*this, std::move(made), size);
}

bool MappedResource::HasModelFile(std::string_view name, const Source& source) {
    return MadeModelFile(LooseKey(name), source) != nullptr;
}

// ---- Worn models (docs/notes/mods.md, "Worn models") ----------------------------------------------------------------

void* MappedResource::WornSetFile(std::string_view name, const std::string& key, const std::vector<Source>& sources,
                                  std::uint32_t* size) {
    const std::string figure = key.substr(0, key.size() - 4);
    auto [set, added] = wornSets.try_emplace(figure);
    if (added) set->second = JoinedSet(figure, sources);
    if (!set->second) return nullptr;
    const std::string& bytes = EndsWith(key, ".bon") ? set->second->bon : set->second->mod;
    if (bytes.empty()) return nullptr;
    if (void* data = SharedFile(*this, name, size)) return data;
    auto made = std::make_unique<LoadedFile>();
    made->name = name;
    made->size = static_cast<std::uint32_t>(bytes.size());
    made->bytes.assign(bytes.begin(), bytes.end());
    return KeptFile(*this, std::move(made), size);
}

std::optional<MappedResource::WornSetFiles> MappedResource::JoinedSet(const std::string& figure,
                                                                 const std::vector<Source>& sources) {
    struct Worn {
        const Source* source = nullptr;
        std::string name, file;
    };
    std::vector<Worn> worn;
    for (const Source& source : sources)
        for (const auto& [model, file] : source.loose ? source.loose->models : ModelEntries(*source.entries))
            if (figure_gltf::IsWornModel(model) &&
                std::ranges::none_of(worn, [&](const Worn& w) { return w.name == model; }))
                worn.push_back({&source, model, file});
    if (worn.empty()) return std::nullopt;
    // The set's files as FindFile reads them: a glTF's made file, else a folder's file or an archive's entry.
    const auto setFile = [&](const std::string& extension, std::string* bytes) {
        const std::string key = figure + extension;
        for (const Source& source : sources) {
            if (const std::string* made = MadeModelFile(key, source)) {
                *bytes = *made;
                return true;
            }
            std::string where;
            if (ReadSource(source, key, bytes, &where)) return true;
        }
        return false;
    };
    WornSetFiles set;
    if (!setFile(".mod", &set.mod)) return std::nullopt;
    setFile(".bon", &set.bon);
    std::size_t joined = 0;
    for (const Worn& w : worn) {
        const std::map<std::string, std::string>* files = MadeModel(*w.source, w.file);
        if (!files) continue;
        const auto mod = files->find(w.name + ".mod");
        const auto bon = files->find(w.name + ".bon");
        if (mod == files->end()) continue;
        joined += figure_gltf::JoinWorn(figure, &set.mod, &set.bon, w.name, mod->second,
                                        bon == files->end() ? std::string_view() : std::string_view(bon->second))
                      .size();
    }
    if (joined == 0) return std::nullopt;
    return set;
}

void* MappedResource::LoadLooseFile(std::string_view name, const std::string& path, std::uint32_t* size) {
    if (void* data = SharedFile(*this, name, size)) return data;
    std::string bytes;
    if (!platform::ReadWholeFile(path, &bytes)) return nullptr;
    auto file = std::make_unique<LoadedFile>();
    file->name = name;
    file->size = static_cast<std::uint32_t>(bytes.size());
    file->bytes.assign(bytes.begin(), bytes.end());
    return KeptFile(*this, std::move(file), size);
}

void* MappedResource::EntryFile(std::string_view name, std::uint32_t* size) {
    if (!entries || !names) return nullptr;
    if (void* data = SharedFile(*this, name, size)) return data;

    int index = FindEntry(name);
    if (index == -1) return nullptr;
    const ResEntry& entry = entries[index];

    // An archive image in memory hands out pointers into the image (size is not reported).
    if (image) return image + entry.offset;

    auto file = std::make_unique<LoadedFile>();
    file->name = name;
    file->size = entry.size;
    file->bytes.resize(std::max(entry.size, 1u));
    if (!archive.ReadAt(static_cast<long>(entry.offset), file->bytes.data(), entry.size)) return nullptr;
    return KeptFile(*this, std::move(file), size);
}

bool MappedResource::CloseFile(void* data) {
    if (image) return true;
    if (!data) return false;

    auto byData = filesByData.find(data);
    if (byData == filesByData.end()) {
        // Rebirth's ResOverride: a file one of the mod's archives gave in this one's place goes back to it.
        if (modfolders::Active())
            if (MappedResource* mod = modfolders::ArchiveHolding(this, data)) return mod->CloseFile(data);
        game::DebugLog("CMappedResource::CloseFile()\nFile already closed\n");
        return false;
    }
    if (--byData->second->refCount != 0) return true;

    std::unique_ptr<LoadedFile> file = std::move(byData->second);
    filesByName.erase(file->name);
    filesByData.erase(byData);
    if (file->madeFromPng) KeepPng(std::move(file));
    return true;
}

void MappedResource::FreeAllFiles() {
    if (image) return;
    filesByName.clear();
    filesByData.clear();
}

bool MMIOResource::OpenResource(const std::string& path) {
    CloseResource();
    FindLooseFiles(path);
    if (!file.Open(path)) {
        // Only the archive's folder: an archive of no entries.
        const std::string folder = LooseFolderOf(path);
        if (!folder.empty() && platform::PathExists(folder)) return true;
        game::DebugLog("CMappedFile::Open().\nUnable to open file \"%s\".\n", path);
        return false;
    }
    OpenFromMemory(file.Data());
    return true;
}

void MMIOResource::OpenFromMemory(const void* memoryImage) {
    mappedImage = const_cast<char*>(static_cast<const char*>(memoryImage));
    ReadImage(mappedImage);
}

void MMIOResource::CloseResource() {
    file.Close();
    ForgetLooseFiles();
    filesByName.clear();
    filesByData.clear();
    entries = nullptr;
    names = nullptr;
    entryTable = {};
    mappedImage = nullptr;
    header = ResHeader{};
}

void* MMIOResource::EntryFile(std::string_view name, std::uint32_t* size) {
    if (!entries || !names) return nullptr;
    int index = FindEntry(name);
    if (index == -1) return nullptr;
    if (size) *size = entries[index].size;
    return mappedImage + entries[index].offset;
}

bool MMIOResource::CloseFile(void*) { return true; }

void MMIOResource::FreeAllFiles() {}

RecordFile FindRecordFile(const std::string& path, bool readOriginal) {
    RecordFile record;
    const std::size_t slash = path.find_last_of("\\/");
    const std::string folder = slash == std::string::npos ? std::string(".") : path.substr(0, slash);
    const std::string name = path.substr(slash + 1);
    const std::string tomlName = name + ".toml";
    for (const platform::FolderLayer& layer : platform::FilesByLayer(folder, {name, tomlName})) {
        bool original = false;
        for (const platform::LayerFile& file : layer.files) {
            if (file.name != tomlName) {
                original = true;
                continue;
            }
            TomlFile& toml = record.tomls.emplace_back();
            toml.path = file.path;
            platform::ReadLayerFile(file.path, &toml.text);
            record.replaced = ReplacesBelow(toml);
        }
        if (original || record.replaced) break;
    }
    if (!record.replaced && readOriginal) record.found = platform::ReadWholeFile(path, &record.original);
    return record;
}

void MergeTomls(const RecordFile& file, const std::function<void(const toml::Value& root, const std::string& path)>& merge) {
    for (auto toml = file.tomls.rbegin(); toml != file.tomls.rend(); ++toml) {
        toml::Document document;
        if (!document.Parse(toml->text, toml->path)) {
            game::Warning("records: %s", document.Error());
            continue;
        }
        merge(document.Root(), toml->path);
    }
}

std::string LooseFolderOf(std::string_view path) {
    const std::size_t dot = path.find_last_of('.');
    const std::size_t slash = path.find_last_of("\\/");
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) return {};
    for (const std::string_view kind : {".res", ".mpr", ".mq"})
        if (text::CompareIgnoringCase(path.substr(dot), kind) == 0) return std::string(path.substr(0, dot));
    return {};
}

bool ResourceExists(std::string_view path) {
    if (platform::PathExists(path)) return true;
    const std::string folder = LooseFolderOf(path);
    return !folder.empty() && platform::PathExists(folder);
}

// --- Archive name listing ------------------------------------------------------------------------
// See data/resource.h and docs/notes/models.md.

namespace {

// A name's parts as _splitpath gives them: the drive ("C:"), the folder up to its last separator (either slash), the
// name, and the extension from the name's last dot.
struct PathParts {
    std::string drive, directory, name, extension;
};

PathParts SplitPath(std::string_view path) {
    PathParts parts;
    if (path.size() >= 2 && path[1] == ':') {
        parts.drive = path.substr(0, 2);
        path.remove_prefix(2);
    }
    const std::size_t folderEnd = path.find_last_of("\\/") + 1;   // 0 without a separator
    parts.directory = path.substr(0, folderEnd);
    path.remove_prefix(folderEnd);
    const std::size_t dot = path.find_last_of('.');
    parts.name = path.substr(0, dot);
    if (dot != std::string_view::npos) parts.extension = path.substr(dot);
    return parts;
}

// The listing's drive and directory tests: an empty pattern matches anything.
bool PartMatches(std::string_view name, std::string_view pattern) {
    return pattern.empty() || WildcardMatch(name, pattern, 0) != 0;
}

// Whether `name` matches the listing's pattern (its parts; null: every name matches).
bool NameMatches(std::string_view name, const PathParts* pattern) {
    if (!pattern) return true;
    const PathParts parts = SplitPath(name);
    // An empty name pattern only matches an empty name; an empty extension pattern anything.
    return PartMatches(parts.drive, pattern->drive) && PartMatches(parts.directory, pattern->directory) &&
           WildcardMatch(parts.name, pattern->name, 0) != 0 && WildcardMatch(parts.extension, pattern->extension, 1) != 0;
}

// The name of the archive's entry `index` as stored, up to a zero byte, cut at the end of the names block (the
// original copied it into 1 KB with no check).
std::string EntryName(const MappedResource& archive, std::uint32_t index) {
    const ResHeader& header = archive.header;
    const ResEntry& entry = archive.entries[index];
    const std::uint32_t offset = entry.nameOffset < header.namesSize ? entry.nameOffset : header.namesSize;
    const std::size_t stored = std::min<std::size_t>(entry.nameLength, header.namesSize - offset);
    return std::string(archive.names + offset, str::BoundedLength(archive.names + offset, stored));
}

} // namespace


// --- Archive listing -----------------------------------------------------------------------------

int WildcardMatch(std::string_view name, std::string_view pattern, int emptyPatternResult) {
    if (pattern.empty()) return name.empty() ? 1 : emptyPatternResult;
    while (!name.empty() && !pattern.empty()) {
        if (pattern.front() == '*') return 1;
        if (pattern.front() != '?' && text::ToUpper(pattern.front()) != text::ToUpper(name.front())) return 0;
        name.remove_prefix(1);
        pattern.remove_prefix(1);
    }
    for (const char c : pattern)
        if (c != '?' && c != '*') return 0;
    return name.empty() ? 1 : 0;
}

void MappedResource::ListFiles(std::vector<std::string>* fileNames, const char* pattern) {
    // Rebirth's ResOverride appends to the caller's list.
    if (!modfolders::Active()) fileNames->clear();
    const PathParts patternParts = pattern ? SplitPath(pattern) : PathParts{};
    const PathParts* parts = pattern ? &patternParts : nullptr;
    std::vector<std::string> listed;
    for (const Source& source : Sources()) {
        if (source.loose) {
            const LooseFiles& loose = *source.loose;
            for (const auto& [key, file] : loose.byName)
                if (NameMatches(file.name, parts)) listed.push_back(file.name);
            // The texts.toml's names the folder has no file of.
            for (const auto& [name, text] : loose.texts)
                if (NameMatches(name, parts) && !loose.byName.contains(LooseKey(name))) listed.push_back(name);
            continue;
        }
        const MappedResource& owner = *source.entries;
        for (std::uint32_t i = 0; i < owner.header.count; ++i) {
            std::string name = EntryName(owner, i);
            const std::string key = LooseKey(name);
            if (NameMatches(name, parts) && !owner.looseOwn.byName.contains(key) &&
                !owner.looseOwn.textIndex.contains(key))
                listed.push_back(std::move(name));
        }
    }
    // A map made from the folder's files lists its map file and sectors first (those not listed already), so that the
    // landscape and the AI map read its name.
    if (const map_folder::MapData* map = FolderMap()) {
        std::vector<std::string> made = {map->name + kMapFileExtension};
        for (int sy = 0; sy < map->header.sectorsY; ++sy)
            for (int sx = 0; sx < map->header.sectorsX; ++sx) made.push_back(map_folder::SectorName(map->name, sx, sy));
        for (std::string& name : made) {
            const bool there = std::ranges::any_of(
                listed, [&](const std::string& other) { return text::CompareIgnoringCase(other, name) == 0; });
            if (!there && NameMatches(name, parts)) fileNames->push_back(std::move(name));
        }
    }
    fileNames->insert(fileNames->end(), std::make_move_iterator(listed.begin()), std::make_move_iterator(listed.end()));
}

// ---- Maps as folders --------------------------------------------------------------------------------------------

const map_folder::MapData* MappedResource::FolderMap() {
    if (mapFolder.empty()) return nullptr;
    if (folderMapMade) return folderMap.get();
    folderMapMade = true;
    const PathParts mapFiles = SplitPath(std::string("*") + kMapFileExtension);
    std::vector<map_folder::Place> places;
    for (const Source& source : Sources()) {
        map_folder::Place& place = places.emplace_back();
        if (const LooseFiles* loose = source.loose) {
            place.has = [loose](std::string_view name) { return loose->byName.contains(LooseKey(name)); };
            place.read = [loose](std::string_view name, std::string* bytes) {
                const auto file = loose->byName.find(LooseKey(name));
                return file != loose->byName.end() && platform::ReadLayerFile(file->second.path, bytes);
            };
            for (const auto& [key, file] : loose->byName)
                if (NameMatches(file.name, &mapFiles)) {
                    place.mapFile = file.name;
                    break;
                }
            continue;
        }
        MappedResource* owner = source.entries;
        place.has = [owner](std::string_view name) { return owner->FindEntry(name) != -1; };
        place.read = [owner](std::string_view name, std::string* bytes) {
            std::uint32_t size = 0;
            void* data = owner->EntryFile(name, &size);
            if (!data) return false;
            bytes->assign(static_cast<const char*>(data), size);
            owner->CloseFile(data);
            return true;
        };
        for (std::uint32_t i = 0; i < owner->header.count && place.mapFile.empty(); ++i) {
            std::string name = EntryName(*owner, i);
            if (NameMatches(name, &mapFiles) && !owner->looseOwn.byName.contains(LooseKey(name)))
                place.mapFile = std::move(name);
        }
    }
    const std::size_t slash = mapFolder.find_last_of("\\/");
    std::vector<std::string> warnings;
    std::optional<map_folder::MapData> map = map_folder::Assemble(
        places, std::string_view(mapFolder).substr(slash == std::string::npos ? 0 : slash + 1), &warnings);
    for (const std::string& line : warnings) game::Warning("maps: %s: %s", mapFolder, line);
    if (map) folderMap = std::make_shared<const map_folder::MapData>(std::move(*map));
    return folderMap.get();
}

void* MappedResource::FolderMapFile(std::string_view name, std::uint32_t* size) {
    const map_folder::MapData* map = FolderMap();
    if (!map) return nullptr;
    // The map file, or a sector <name>XXXYYY.SEC (sx, sy; -1 for the map file).
    int sx = -1, sy = -1;
    if (text::CompareIgnoringCase(name, map->name + kMapFileExtension) != 0) {
        const std::string_view stem = name.substr(0, name.find_last_of('.'));
        if (stem.size() != map->name.size() + 6) return nullptr;
        const std::string_view digits = stem.substr(map->name.size());
        if (!std::ranges::all_of(digits, [](char c) { return c >= '0' && c <= '9'; })) return nullptr;
        sx = std::stoi(std::string(digits.substr(0, 3)));
        sy = std::stoi(std::string(digits.substr(3)));
        if (sx >= map->header.sectorsX || sy >= map->header.sectorsY ||
            text::CompareIgnoringCase(name, map_folder::SectorName(map->name, sx, sy)) != 0)
            return nullptr;
    }
    if (void* data = SharedFile(*this, name, size)) return data;
    const std::string bytes = sx < 0 ? map_folder::EncodeMap(*map) : map_folder::EncodeSector(*map, sx, sy);
    auto file = std::make_unique<LoadedFile>();
    file->name = name;
    file->size = static_cast<std::uint32_t>(bytes.size());
    file->bytes.assign(bytes.begin(), bytes.end());
    return KeptFile(*this, std::move(file), size);
}
