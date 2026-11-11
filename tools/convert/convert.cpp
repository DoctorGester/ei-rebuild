// ei-convert: the record files of an install written as TOML (docs/notes/mods.md, "Records as TOML"), read with the
// game's own loaders, and each one's round trip checked.
// usage: ei-convert <install or archive> <target folder>   write the TOML files into the target
//        ei-convert --check <install or archive>...        only the round trips
//        ei-convert --scripts <install>... [options]       the script survey and Lua check (convert_scripts.cpp)
//        ei-convert --lua-check                             the scripts' Lua bindings on a small world (lua_check.cpp)
//        ei-convert --lua-api <file>                        the Lua API's LuaLS definition file (lua_api.h)
//        ei-convert --lua-api-docs <repository> [--check]   the same and docs/modding.md's API tables written into the
//                                                           repository, or compared with it (lua_api.h)
//        ei-convert --effect-check [--record <file>] [--dump <file>]
//                                                           the original particle types against their recorded C++
//                                                           (effect_check.h)
//        ei-convert --example-mod <game folder> [<mods folder>] the example mod made from the install (example_mod.h),
//                                                           into <game folder>\starter\Mods by default
// An install is walked for its archives (Res\<name>.res, or the folder of an unpacked one under a Res folder) and its
// loose registries (*.reg; not those of Maps\, whose quest archives are not converted) and camera paths (*.cam). Of an
// archive: its registries, materials (.mat), animation databases (.adb), acks.db, prints.db and the game databases
// (convert_databases.h: units.udb, perks.pdb, spells.sdb, items.idb, quests.qdb, Levers.ldb), each as
// <archive folder>\<name>.toml; a texts archive's (texts.res, textslmp.res) texts as <archive folder>\texts.toml; its
// textures (.mmp) as <archive folder>\<name>.png (convert_textures.cpp, docs/notes/mods.md "Textures as PNG"); its
// figure models (.mod, .bon, .lnk, .fig, .anm) as <archive folder>\<figure>.glb (convert_models.cpp, "Models as glTF",
// "Animations as glTF"). A map archive (Maps\<zone>.mpr, or the folder of an unpacked one under a Maps folder) as its
// folder's map.toml and PNG layers (convert_maps.cpp, docs/notes/mods.md "Maps as folders"). A .mob's script as
// <zone>.lua beside it (convert_scripts.h, docs/notes/lua-scripting.md "Converted scripts"), its other chunks as
// <zone>.mob.toml (convert_mobs.cpp, docs/notes/mods.md "Map objects as TOML"; the .mob stays for its scripts and AI
// graph, so it is not listed as converted). A loose file as <name>.toml beside it. A record file's TOML has
// `replace = true`: it stands for the whole original, nothing below it read (a mod's original hides the game's file
// too). The target mirrors the source's layout (MF1's loose folders); converted.txt there lists the converted originals
// (relative to the source), which tools/convert_install.py removes from its copy.
// Each file's round trip: the original read by the game's loader, written as TOML, the TOML read back by the game's
// loader, every field compared (floats by their bits). Exit code 0 when every round trip holds.
#include "engine/data/registry.h"
#include "engine/data/resource.h"
#include "engine/core/asset_text.h"
#include "engine/models/material.h"
#include "game/objects/object3d.h"
#include "game/world/camera.h"
#include "game/terrain/ground_print.h"
#include "game/units/acks_db.h"
#include "lib/text_case.h"
#include "lib/toml.h"

#include "convert_databases.h"
#include "convert_maps.h"
#include "convert_mobs.h"
#include "convert_models.h"
#include "convert_scripts.h"
#include "lua_api.h"
#include "lua_check.h"
#include "convert_textures.h"
#include "effect_check.h"
#include "example_mod.h"

#include <bit>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_logLines = 0;

// The counts of a format: files converted, round trips that failed.
struct Count {
    int files = 0;
    int failed = 0;
};
std::map<std::string, Count> g_counts;
std::vector<std::string> g_converted;   // the originals converted, relative to the source

// ---- Comparing ----------------------------------------------------------------------------------------------------

// The differences of one round trip, each a line.
struct Differences {
    std::vector<std::string> lines;

    template <class T>
    void Same(const T& a, const T& b, const std::string& what) {
        if (!(a == b)) lines.push_back(what);
    }
    void SameBits(float a, float b, const std::string& what) {
        if (std::bit_cast<std::uint32_t>(a) != std::bit_cast<std::uint32_t>(b)) lines.push_back(what);
    }
    void SameBits(const Vector4& a, const Vector4& b, const std::string& what) {
        SameBits(a.x, b.x, what + ".x");
        SameBits(a.y, b.y, what + ".y");
        SameBits(a.z, b.z, what + ".z");
        SameBits(a.w, b.w, what + ".w");
    }
};

void CompareRegistries(const FileRegistry& a, const FileRegistry& b, Differences& d) {
    d.Same(a.sections.size(), b.sections.size(), "section count");
    for (std::size_t i = 0; i < a.sections.size() && i < b.sections.size(); ++i) {
        const RegistrySection& x = a.sections[i];
        const RegistrySection& y = b.sections[i];
        d.Same(x.name, y.name, "section " + x.name);
        d.Same(x.values.size(), y.values.size(), x.name + ": key count");
        for (std::size_t k = 0; k < x.values.size() && k < y.values.size(); ++k) {
            const RegistryValue& v = x.values[k];
            const RegistryValue& w = y.values[k];
            const std::string at = x.name + "." + v.name;
            d.Same(v.name, w.name, at + ": name");
            d.Same(v.type, w.type, at + ": type");
            d.Same(v.words, w.words, at + ": numbers");
            d.Same(v.strings, w.strings, at + ": strings");
        }
    }
}

void CompareMaterials(const MaterialFile& a, const MaterialFile& b, Differences& d) {
    d.Same(a.unreadName, b.unreadName, "name");
    d.SameBits(a.colors.diffuse, b.colors.diffuse, "diffuse");
    d.SameBits(a.colors.ambient, b.colors.ambient, "ambient");
    d.SameBits(a.colors.specular, b.colors.specular, "specular");
    d.SameBits(a.colors.emissive, b.colors.emissive, "emissive");
    d.SameBits(a.colors.power, b.colors.power, "power");
}

std::string_view BoundedText(const char* text, std::size_t room) {
    std::size_t length = 0;
    while (length < room && text[length] != '\0') ++length;
    return {text, length};
}

void CompareAnimations(const AnimationDatabase& a, const AnimationDatabase& b, Differences& d) {
    d.Same(a.unreadName, b.unreadName, "name");
    d.SameBits(a.start, b.start, "start");
    d.SameBits(a.length, b.length, "length");
    d.SameBits(a.end, b.end, "end");
    d.Same(a.animations.size(), b.animations.size(), "animation count");
    for (std::size_t i = 0; i < a.animations.size() && i < b.animations.size(); ++i) {
        const FigureAnimation& x = a.animations[i];
        const FigureAnimation& y = b.animations[i];
        const std::string at = "animation " + std::to_string(i);
        d.Same(BoundedText(x.name, sizeof(x.name)), BoundedText(y.name, sizeof(y.name)), at + ": name");
        d.Same(x.index, y.index, at + ": index");
        d.Same(x.key, y.key, at + ": key");
        d.Same(x.flags, y.flags, at + ": flags");
        d.Same(x.chance, y.chance, at + ": chance");
        d.Same(x.frameCount, y.frameCount, at + ": frames");
        d.SameBits(x.stride, y.stride, at + ": stride");
        d.Same(x.arrowFrom, y.arrowFrom, at + ": arrow from");
        d.Same(x.arrowTo, y.arrowTo, at + ": arrow to");
        for (int k = 0; k < 4; ++k) d.Same(x.stepFrames[k], y.stepFrames[k], at + ": steps");
        d.Same(x.moment, y.moment, at + ": moment");
        d.Same(x.soundFrame, y.soundFrame, at + ": sound frame");
        for (int k = 0; k < 4; ++k) d.Same(x.sounds[k], y.sounds[k], at + ": sounds");
        d.Same(y.database == &b, true, at + ": its database");
    }
}

void CompareSpeeches(const std::vector<SpeechRecord>& a, const std::vector<SpeechRecord>& b, const std::string& at, Differences& d) {
    d.Same(a.size(), b.size(), at + ": speech count");
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        d.Same(a[i].chance, b[i].chance, at + ": chance");
        d.Same(a[i].sound, b[i].sound, at + ": sound");
        d.Same(a[i].text, b[i].text, at + ": text");
        d.Same(a[i].name, b[i].name, at + ": name");
    }
}

// A record's kind map: each kind's list as its place among the record's lists (-1 for null).
template <class Record>
std::map<int, std::ptrdiff_t> KindMap(const Record& record) {
    std::map<int, std::ptrdiff_t> map;
    for (const auto& [kind, list] : record.head.speeches)
        map[kind] = list ? reinterpret_cast<const char*>(list) - reinterpret_cast<const char*>(&record) : -1;
    return map;
}

template <class Record, class CompareLists>
void CompareTable(const acks::AckTable<Record>& a, const acks::AckTable<Record>& b, const std::string& table,
                  Differences& d, CompareLists lists) {
    d.Same(a.records.size(), b.records.size(), table + ": record count");
    for (std::size_t i = 0; i < a.records.size() && i < b.records.size(); ++i) {
        const Record& x = a.records[i];
        const Record& y = b.records[i];
        const std::string at = table + " " + x.head.name;
        d.Same(x.head.name, y.head.name, at + ": name");
        d.Same(KindMap(x), KindMap(y), at + ": kind map");
        lists(x, y, at);
    }
    std::set<std::string> namesA, namesB;
    for (const auto& [name, record] : a.byName) namesA.insert(name);
    for (const auto& [name, record] : b.byName) namesB.insert(name);
    d.Same(namesA, namesB, table + ": names");
    for (const auto& [name, record] : b.byName)
        d.Same(static_cast<std::size_t>(record - b.records.data()),
               static_cast<std::size_t>(a.byName.at(name) - a.records.data()), table + ": index of " + name);
}

void CompareAcks(const acks::AckDatabase& a, const acks::AckDatabase& b, Differences& d) {
    CompareTable(a.units, b.units, "unit", d, [&](const acks::UnitAcks& x, const acks::UnitAcks& y, const std::string& at) {
        for (int i = 0; i < 33; ++i) CompareSpeeches(x.lists[i].records, y.lists[i].records, at + " list " + std::to_string(i), d);
        CompareSpeeches(x.namedSpeeches.records, y.namedSpeeches.records, at + " named", d);
        const auto& e = x.environmentSpeeches.entries;
        const auto& f = y.environmentSpeeches.entries;
        d.Same(e.size(), f.size(), at + ": environment count");
        for (std::size_t i = 0; i < e.size() && i < f.size(); ++i) {
            CompareSpeeches({e[i].speech}, {f[i].speech}, at + " environment", d);
            d.Same(e[i].environment, f[i].environment, at + ": environment flags");
        }
    });
    CompareTable(a.models, b.models, "model", d, [&](const acks::ModelAcks& x, const acks::ModelAcks& y, const std::string& at) {
        for (int i = 0; i < 5; ++i) CompareSpeeches(x.lists[i].records, y.lists[i].records, at + " list " + std::to_string(i), d);
    });
    CompareTable(a.extras, b.extras, "extra", d, [&](const acks::ExtraAcks& x, const acks::ExtraAcks& y, const std::string& at) {
        for (int i = 0; i < 2; ++i) CompareSpeeches(x.lists[i].records, y.lists[i].records, at + " list " + std::to_string(i), d);
    });
}

template <class Record>
void ComparePrintTable(const std::vector<Record>& a, const std::vector<Record>& b, const std::string& table, Differences& d) {
    d.Same(a.size(), b.size(), table + ": record count");
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        const std::string at = table + " " + a[i].name;
        d.Same(a[i].name, b[i].name, at + ": name");
        for (const auto& [x, y, what] : {std::tuple(&a[i].dryLook, &b[i].dryLook, "dry"), std::tuple(&a[i].wetLook, &b[i].wetLook, "wet")}) {
            d.SameBits(x->alpha, y->alpha, at + ": " + what + " alpha");
            d.Same(x->life, y->life, at + ": " + what + " life");
            d.Same(x->lifetime, y->lifetime, at + ": " + what + " lifetime");
        }
        if constexpr (std::is_same_v<Record, PrintRecord2>)
            for (int k = 0; k < 3; ++k) d.Same(a[i].unreadChunk[k], b[i].unreadChunk[k], at + ": unread");
    }
}

void ComparePrints(const PrintsDatabase& a, const PrintsDatabase& b, Differences& d) {
    ComparePrintTable(a.blood, b.blood, "blood", d);
    ComparePrintTable(a.footprints, b.footprints, "footprint", d);
    ComparePrintTable(a.scorch, b.scorch, "scorch", d);
    d.Same(a.bloodByName.size(), b.bloodByName.size(), "blood names");
    d.Same(a.footprintsByName.size(), b.footprintsByName.size(), "footprint names");
    d.Same(a.scorchByName.size(), b.scorchByName.size(), "scorch names");
}

void CompareCameraKeys(const std::vector<CameraKey>& a, const std::vector<CameraKey>& b, Differences& d) {
    d.Same(a.size(), b.size(), "key count");
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        const std::string at = "key " + std::to_string(i);
        d.SameBits(a[i].time, b[i].time, at + ": time");
        d.SameBits(Vector4{a[i].position.x, a[i].position.y, a[i].position.z, 0.0f},
                   Vector4{b[i].position.x, b[i].position.y, b[i].position.z, 0.0f}, at + ": position");
        d.SameBits(Vector4{a[i].rotation.w, a[i].rotation.x, a[i].rotation.y, a[i].rotation.z},
                   Vector4{b[i].rotation.w, b[i].rotation.x, b[i].rotation.y, b[i].rotation.z}, at + ": rotation");
    }
}

// ---- Converting ---------------------------------------------------------------------------------------------------

struct Options {
    fs::path source;
    fs::path target;   // empty: only the checks
};
Options g_options;

RecordFile Original(const std::string& bytes) {
    RecordFile file;
    file.found = true;
    file.original = bytes;
    return file;
}

RecordFile Toml(const std::string& text, const std::string& path) {
    RecordFile file;
    file.tomls.push_back({path, text});
    return file;
}

// One file's round trip: `convert(original, &toml)` reads the original and writes its TOML, then reads the TOML back
// and compares; the TOML goes to `output` (relative to the target), the original noted as converted.
template <class Convert>
void ConvertFile(const std::string& format, const std::string& name, const std::string& bytes, const fs::path& output,
                 const std::string& original, Convert convert) {
    Count& count = g_counts[format];
    ++count.files;
    Differences differences;
    std::string toml;
    const int logged = g_logLines;
    convert(Original(bytes), name, &toml, differences);
    if (g_logLines != logged) differences.lines.push_back("the loaders logged");
    if (!differences.lines.empty()) {
        ++count.failed;
        std::printf("FAILED %s %s:\n", format.c_str(), name.c_str());
        for (std::size_t i = 0; i < differences.lines.size() && i < 10; ++i)
            std::printf("    %s\n", differences.lines[i].c_str());
    }
    if (g_options.target.empty()) return;
    const fs::path path = g_options.target / output;
    fs::create_directories(path.parent_path());
    fs::remove(path);   // a link into another copy (tools/convert_install.py) is not written through
    std::ofstream(path, std::ios::binary) << toml;
    g_converted.push_back(original);
}

// A converted file's TOML: the whole file, standing for the original (`replace = true`: nothing below it is read).
std::string WholeFile(const std::string& toml) {
    return "# ei-convert: the whole file; replace = true: nothing below it is read (docs/notes/mods.md, \"Records as "
           "TOML\")\nreplace = true\n\n" + toml;
}

void ConvertRegistry(const RecordFile& original, const std::string& name, std::string* toml, Differences& d) {
    FileRegistry a, b;
    if (!a.Load(original)) {
        d.lines.push_back("not a registry");
        return;
    }
    *toml = WholeFile(a.ToToml());
    if (!b.Load(Toml(*toml, name + ".toml"))) d.lines.push_back("the TOML did not load");
    CompareRegistries(a, b, d);
}

void ConvertMaterial(const RecordFile& original, const std::string& name, std::string* toml, Differences& d) {
    MaterialFile a, b;
    a.Read(original);
    *toml = WholeFile(a.ToToml());
    b.Read(Toml(*toml, name + ".toml"));
    CompareMaterials(a, b, d);
}

void ConvertAnimations(const RecordFile& original, const std::string& name, std::string* toml, Differences& d) {
    const auto a = ReadAnimationDatabase(original);
    *toml = WholeFile(a->ToToml());
    const auto b = ReadAnimationDatabase(Toml(*toml, name + ".toml"));
    CompareAnimations(*a, *b, d);
}

void ConvertAcks(const RecordFile& original, const std::string& name, std::string* toml, Differences& d) {
    acks::AckDatabase a, b;
    a.Read(original);
    *toml = WholeFile(a.ToToml());
    b.Read(Toml(*toml, name + ".toml"));
    CompareAcks(a, b, d);
}

void ConvertPrints(const RecordFile& original, const std::string& name, std::string* toml, Differences& d) {
    PrintsDatabase a, b;
    a.Read(original);
    *toml = WholeFile(a.ToToml());
    b.Read(Toml(*toml, name + ".toml"));
    ComparePrints(a, b, d);
}

// A texture as PNG (convert_textures.cpp).
void ConvertTexture(const RecordFile& original, const std::string&, std::string* png, Differences& d) {
    d.lines = convert_textures::RoundTrip(original.original, png);
}

void ConvertCamera(const RecordFile& original, const std::string& name, std::string* toml, Differences& d) {
    const std::vector<CameraKey> a = ReadCameraKeys(original);
    *toml = WholeFile(CameraKeysToml(a));
    const std::vector<CameraKey> b = ReadCameraKeys(Toml(*toml, name + ".toml"));
    CompareCameraKeys(a, b, d);
}

std::string Lower(std::string text) {
    for (char& c : text) c = static_cast<char>(text::ToLower(static_cast<unsigned char>(c)));
    return text;
}

std::string Extension(const std::string& name) {
    const std::size_t dot = name.find_last_of('.');
    const std::size_t slash = name.find_last_of("\\/");
    return dot == std::string::npos || (slash != std::string::npos && dot < slash) ? std::string() : Lower(name.substr(dot));
}

// A path as UTF-8 text (the game's), and back: the system's narrow text is the ANSI code page's.
std::string Utf8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

// A path from the game's names, which join folders with '\': as '/' so a Linux or macOS build makes folders too.
fs::path PathOf(const std::string& utf8) {
    std::u8string text(utf8.begin(), utf8.end());
    for (char8_t& c : text)
        if (c == u8'\\') c = u8'/';
    return fs::path(text);
}

bool ReadFile(const fs::path& path, std::string* bytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    bytes->assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

// The path below the source, with backslashes.
std::string Relative(const fs::path& path) {
    std::string relative = Utf8(fs::relative(path, g_options.source).generic_u8string());
    for (char& c : relative)
        if (c == '/') c = '\\';
    return relative;
}

// The figure models among an archive's names, each figure as one .glb (convert_models.cpp), its round trip checked.
void ConvertModels(MappedResource& archive, const std::vector<std::string>& names, const std::string& folderRelative) {
    std::vector<std::string> left;
    for (const convert_models::Group& group : convert_models::Groups(names, &left)) {
        std::map<std::string, std::string> files;
        for (const std::string& name : group.files) {
            std::uint32_t size = 0;
            void* data = archive.GetFile(name, &size);
            if (!data) continue;
            files[name] = std::string(static_cast<const char*>(data), size);
            archive.CloseFile(data);
        }
        Count& count = g_counts["model"];
        ++count.files;
        std::string glb;
        const std::vector<std::string> lines = convert_models::RoundTrip(group, files, &glb);
        const std::string where = folderRelative + "\\" + text::FromAssetText(group.name);
        if (!lines.empty()) {
            ++count.failed;
            std::printf("FAILED model %s:\n", where.c_str());
            for (std::size_t i = 0; i < lines.size() && i < 10; ++i) std::printf("    %s\n", lines[i].c_str());
            continue;
        }
        if (g_options.target.empty()) continue;
        const fs::path path = g_options.target / PathOf(where + ".glb");
        fs::create_directories(path.parent_path());
        fs::remove(path);
        std::ofstream(path, std::ios::binary) << glb;
        for (const std::string& name : group.files) g_converted.push_back(folderRelative + "\\" + text::FromAssetText(name));
    }
    for (const std::string& line : left) std::printf("ei-convert: left as it is: %s\\%s\n", folderRelative.c_str(), line.c_str());
    g_counts["model files left as they are"].files += static_cast<int>(left.size());
}

// The record entries of an archive (its texts too for a texts archive) converted.
void ConvertArchive(const fs::path& archivePath) {
    const fs::path folder = fs::path(archivePath).replace_extension();
    const std::string folderRelative = Relative(folder);
    MappedResource archive;
    if (!archive.OpenResource(Utf8(archivePath))) return;
    std::vector<std::string> names;
    archive.ListFiles(&names, nullptr);
    const std::string stem = Lower(Utf8(folder.filename()));
    const bool texts = stem == "texts" || stem == "textslmp";
    toml::Writer textsToml;
    std::vector<std::pair<std::string, std::string>> textEntries;   // the entries' names and UTF-8 texts
    ConvertModels(archive, names, folderRelative);
    for (const std::string& name : names) {
        if (convert_models::IsFigureFile(name)) continue;
        std::uint32_t size = 0;
        void* data = archive.GetFile(name, &size);
        if (!data) continue;
        const std::string bytes(static_cast<const char*>(data), size);
        archive.CloseFile(data);
        const std::string original = folderRelative + "\\" + text::FromAssetText(name);
        const fs::path output = PathOf(original + ".toml");
        const std::string extension = Extension(name);
        const std::string lower = Lower(name);
        if (texts) {
            textEntries.emplace_back(name, text::FromAssetText(bytes));
            textsToml.Pair(text::FromAssetText(name), toml::Text(textEntries.back().second));
            g_converted.push_back(original);
        } else if (extension == ".reg") {
            ConvertFile("registry", original, bytes, output, original, ConvertRegistry);
        } else if (extension == ".mat") {
            ConvertFile("material", original, bytes, output, original, ConvertMaterial);
        } else if (extension == ".adb") {
            ConvertFile("adb", original, bytes, output, original, ConvertAnimations);
        } else if (lower == "acks.db") {
            ConvertFile("acks.db", original, bytes, output, original, ConvertAcks);
        } else if (lower == "prints.db") {
            ConvertFile("prints.db", original, bytes, output, original, ConvertPrints);
        } else if (IsDatabase(lower)) {
            ConvertFile(lower, original, bytes, output, original,
                        [&](const RecordFile& file, const std::string& path, std::string* toml, Differences& d) {
                            ConvertDatabase(lower, file, path, WholeFile, toml, d.lines);
                        });
        } else if (extension == ".mmp") {
            ConvertFile(convert_textures::CountName(bytes), original, bytes, PathOf(original.substr(0, original.size() - 4) + ".png"),
                        original, ConvertTexture);
        }
    }
    if (!texts) return;
    // The texts' round trip: the TOML's texts by name (as the loose folder's reader files them) against the entries.
    Count& count = g_counts["texts"];
    count.files += static_cast<int>(textEntries.size());
    toml::Document document;
    std::map<std::string, std::string> byName;
    if (document.Parse(textsToml.text, folderRelative + "\\texts.toml")) {
        const toml::Value root = document.Root();
        for (int i = 0; i < root.Size(); ++i)
            byName[Lower(text::ToAssetText(root.KeyAt(i)))] = std::string(root.ValueAt(i).String());
    } else {
        std::printf("FAILED texts %s: %s\n", folderRelative.c_str(), document.Error().c_str());
    }
    for (const auto& [name, utf8] : textEntries) {
        const auto found = byName.find(Lower(name));
        if (found != byName.end() && found->second == utf8) continue;
        ++count.failed;
        std::printf("FAILED texts %s\\%s\n", folderRelative.c_str(), name.c_str());
    }
    if (g_options.target.empty()) return;
    const fs::path path = g_options.target / PathOf(folderRelative) / "texts.toml";
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << textsToml.text;
}

// A map archive (an .mpr, or the folder of an unpacked one) as the map's folder: its files written only when the round
// trip holds, its map file and sectors listed as converted.
void ConvertMapArchive(const fs::path& archivePath) {
    const fs::path folder = fs::path(archivePath).replace_extension();
    const std::string folderRelative = Relative(folder);
    MappedResource archive;
    if (!archive.OpenResource(Utf8(archivePath))) return;
    Count& count = g_counts["map"];
    ++count.files;
    const int logged = g_logLines;
    convert_maps::Converted converted = convert_maps::RoundTrip(archive, Utf8(folder.filename()));
    if (g_logLines != logged) converted.differences.push_back("the loaders logged");
    if (!converted.differences.empty()) {
        ++count.failed;
        std::printf("FAILED map %s:\n", folderRelative.c_str());
        for (std::size_t i = 0; i < converted.differences.size() && i < 10; ++i)
            std::printf("    %s\n", converted.differences[i].c_str());
        return;
    }
    if (g_options.target.empty()) return;
    for (const auto& [name, bytes] : converted.files) {
        const fs::path path = g_options.target / PathOf(folderRelative) / PathOf(name);
        fs::create_directories(path.parent_path());
        fs::remove(path);   // a link into another copy (tools/convert_install.py) is not written through
        std::ofstream(path, std::ios::binary) << bytes;
    }
    for (const std::string& original : converted.originals)
        g_converted.push_back(folderRelative + "\\" + text::FromAssetText(original));
}

// A .mob's chunks but its scripts as <name>.mob.toml beside it, written only when the round trip holds. The .mob is not
// listed as converted: the game still reads its scripts and AI graph.
void ConvertMob(const fs::path& path) {
    const std::string relative = Relative(path);
    Count& count = g_counts["map objects"];
    ++count.files;
    const int logged = g_logLines;
    convert_mobs::Converted converted = convert_mobs::RoundTrip(path);
    if (g_logLines != logged) converted.differences.push_back("the loaders logged");
    if (!converted.differences.empty()) {
        ++count.failed;
        std::printf("FAILED map objects %s:\n", relative.c_str());
        for (std::size_t i = 0; i < converted.differences.size() && i < 10; ++i)
            std::printf("    %s\n", converted.differences[i].c_str());
        return;
    }
    if (g_options.target.empty()) return;
    const fs::path output = g_options.target / PathOf(relative + ".toml");
    fs::create_directories(output.parent_path());
    fs::remove(output);   // a link into another copy (tools/convert_install.py) is not written through
    std::ofstream(output, std::ios::binary) << converted.toml;
}

// Whether `folder` holds a map file (.mp): an unpacked map archive's folder.
bool HoldsMapFile(const fs::path& folder) {
    for (const fs::directory_entry& entry : fs::directory_iterator(folder))
        if (entry.is_regular_file() && Extension(Utf8(entry.path().filename())) == ".mp") return true;
    return false;
}

bool UnderFolderNamed(const fs::path& relative, const std::string& name) {
    for (const fs::path& part : relative.parent_path())
        if (Lower(Utf8(part)) == name) return true;
    return false;
}

void ConvertInstall(const fs::path& source) {
    std::set<std::string> archives;   // lower case, full paths without the extension
    std::vector<fs::path> archivePaths, mapPaths, mobPaths;
    auto addArchive = [&](const fs::path& path) {
        if (archives.insert(Lower(Utf8(fs::path(path).replace_extension()))).second) archivePaths.push_back(path);
    };
    auto addMap = [&](const fs::path& path) {
        if (archives.insert(Lower(Utf8(fs::path(path).replace_extension()))).second) mapPaths.push_back(path);
    };
    std::vector<fs::path> loose;
    for (auto entry = fs::recursive_directory_iterator(source); entry != fs::recursive_directory_iterator(); ++entry) {
        const fs::path relative = fs::relative(entry->path(), source);
        if (entry->is_directory()) {
            // An unpacked archive's folder: one below a Res folder.
            const std::string parent = Lower(Utf8(entry->path().parent_path().filename()));
            if (parent == "res") {
                addArchive(fs::path(entry->path()).replace_extension(".res"));
                entry.disable_recursion_pending();
            } else if (parent == "maps" && HoldsMapFile(entry->path())) {
                addMap(fs::path(entry->path()) += ".mpr");
                entry.disable_recursion_pending();
            }
            continue;
        }
        const std::string extension = Extension(Utf8(entry->path().filename()));
        if (extension == ".res") addArchive(entry->path());
        else if (extension == ".mpr") addMap(entry->path());
        else if ((extension == ".reg" && !UnderFolderNamed(relative, "maps")) || extension == ".cam") loose.push_back(entry->path());
        else if (extension == ".mob") mobPaths.push_back(entry->path());
    }
    for (const fs::path& archive : archivePaths) ConvertArchive(archive);
    for (const fs::path& map : mapPaths) ConvertMapArchive(map);
    for (const fs::path& mob : mobPaths) ConvertMob(mob);
    for (const fs::path& path : loose) {
        std::string bytes;
        if (!ReadFile(path, &bytes)) continue;
        const std::string relative = Relative(path);
        const fs::path output = PathOf(relative + ".toml");
        if (Extension(relative) == ".cam") {
            ConvertFile("camera", relative, bytes, output, relative, ConvertCamera);
        } else if (bytes.size() >= 4 && std::memcmp(bytes.data(), &kRegistryMagic, 4) == 0) {
            ConvertFile("registry", relative, bytes, output, relative, ConvertRegistry);
        }
    }
}

// Each source (an install, or an archive given by its path) converted; its originals listed in the target's
// converted.txt.
void ConvertAll(const std::vector<fs::path>& sources) {
    for (const fs::path& source : sources) {
        if (!fs::exists(source)) throw std::runtime_error("no " + Utf8(source));
        g_converted.clear();
        const std::string extension = Extension(Utf8(source.filename()));
        const bool archive = extension == ".res" || extension == ".mpr";
        g_options.source = archive ? source.parent_path() : source;
        if (extension == ".res") {
            ConvertArchive(source);
        } else if (extension == ".mpr") {
            ConvertMapArchive(source);
        } else {
            ConvertInstall(source);
            const convert_scripts::MissionCounts scripts = convert_scripts::WriteMissions(source, g_options.target);
            g_counts["mission script"].files += scripts.files;
            g_counts["mission script"].failed += scripts.failed;
        }
        if (g_options.target.empty()) continue;
        fs::create_directories(g_options.target);
        std::ofstream list(g_options.target / "converted.txt", std::ios::binary);
        for (const std::string& original : g_converted) list << original << '\n';
    }
}

int Usage() {
    std::printf("usage: ei-convert <install or archive> <target folder>\n"
                "       ei-convert --check <install or archive>...\n"
                "       ei-convert --scripts <install>... [--scenarios <folder>] [--dump <folder>] [--lua <folder>]\n"
                "                  [--quiet]\n"
                "       ei-convert --lua-check\n"
                "       ei-convert --lua-api <file>\n"
                "       ei-convert --lua-api-docs <repository> [--check]\n");
    return 2;
}

} // namespace

namespace ei {
void Log(const char* format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (lua_check::g_logCapture) lua_check::g_logCapture->push_back(text);
    if (lua_check::g_logMuted) return;
    ++g_logLines;
    std::printf("  log: %s\n", text);
}
std::string OutputPath(const char* name) { return name; }
} // namespace ei

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::vector<fs::path> sources;
    if (argc >= 2 && std::strcmp(argv[1], "--scripts") == 0) return convert_scripts::Main({argv + 2, argv + argc});
    if (argc == 2 && std::strcmp(argv[1], "--lua-check") == 0) return lua_check::Main();
    if (argc >= 2 && std::strcmp(argv[1], "--lua-api") == 0) return lua_api::WriteDefinitions({argv + 2, argv + argc});
    if (argc >= 2 && std::strcmp(argv[1], "--lua-api-docs") == 0) return lua_api::WriteDocs({argv + 2, argv + argc});
    if (argc >= 2 && std::strcmp(argv[1], "--effect-check") == 0) {
        std::string record, dump;
        for (int i = 2; i < argc; ++i) {
            if (i + 1 < argc && std::strcmp(argv[i], "--record") == 0) record = argv[++i];
            else if (i + 1 < argc && std::strcmp(argv[i], "--dump") == 0) dump = argv[++i];
            else return Usage();
        }
        return effect_check::Main(record, dump);
    }
    if ((argc == 3 || argc == 4) && std::strcmp(argv[1], "--example-mod") == 0) {
        const fs::path install = fs::absolute(argv[2]);
        return example_mod::Write(install, argc == 4 ? fs::absolute(argv[3]) : install / "starter" / "Mods");
    }
    if (argc >= 3 && std::strcmp(argv[1], "--check") == 0) {
        for (int i = 2; i < argc; ++i) sources.push_back(fs::absolute(argv[i]));
    } else if (argc == 3) {
        sources.push_back(fs::absolute(argv[1]));
        g_options.target = fs::absolute(argv[2]);
    } else {
        return Usage();
    }
    try {
        ConvertAll(sources);
    } catch (const std::exception& error) {
        std::printf("ei-convert: %s\n", error.what());
        return 1;
    }
    convert_textures::PrintMeasures();
    convert_maps::PrintMeasures();
    convert_mobs::PrintMeasures();
    convert_models::PrintMeasures();
    int failed = 0;
    for (const auto& [format, count] : g_counts) {
        std::printf("ei-convert: %s: %d files, %d failed\n", format.c_str(), count.files, count.failed);
        failed += count.failed;
    }
    return failed == 0 ? 0 : 1;
}
