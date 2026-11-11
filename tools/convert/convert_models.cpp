// ei-convert's figure models: convert_models.h. The layout and why: docs/notes/mods.md, "Models as glTF".
#include "convert_models.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <set>

namespace convert_models {

namespace {

std::string Key(std::string name) {
    for (char& c : name)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return name;
}

std::string ExtensionOf(const std::string& key) { return key.size() > 4 ? key.substr(key.size() - 4) : std::string(); }
std::string StemOf(const std::string& key) { return key.substr(0, key.size() - 4); }

struct Measures {
    int figures = 0, sets = 0, loose = 0;
    int parts = 0, linked = 0, bones = 0, raw = 0;
    std::map<std::string, int> records;   // by kind: FIG8, FIG, old
    long long corners = 0, slots = 0, triangles = 0;
    int animations = 0, tracks = 0, unplaced = 0, morphed = 0;   // unplaced: tracks of no part; morphed: with vertex frames
    long long rotationKeys = 0, positionKeys = 0, vertexKeys = 0;
    long long originalBytes = 0, glbBytes = 0;
    double readSeconds = 0;               // FromGltf and Files: the game's work on a converted figure
    int failed = 0;
};
Measures g_measures;

// The first field of two records that differs (empty: none).
std::string FirstDifference(const figure_gltf::Record& a, const figure_gltf::Record& b) {
    const auto same = [](const auto& x, const auto& y) {
        return x.size() == y.size() && (x.empty() || std::memcmp(x.data(), y.data(), x.size() * sizeof(x[0])) == 0);
    };
    if (a.old != b.old || a.magic != b.magic || a.versions != b.versions) return "the kind";
    if (a.headerTail != b.headerTail) return "the header's last dword";
    if (a.materialValue != b.materialValue || a.meshType != b.meshType) return "the material or mesh type";
    if (!same(a.bounds, b.bounds)) return "the bounds";
    if (a.positions.size() != b.positions.size()) return "the position count";
    if (!same(a.positions, b.positions)) {
        int differing = 0;
        for (std::size_t i = 0; i < a.positions.size(); ++i)
            differing += std::memcmp(&a.positions[i], &b.positions[i], sizeof(float)) != 0;
        return "the positions (" + std::to_string(differing) + " floats)";
    }
    if (!same(a.normals, b.normals)) return "the normals";
    if (!same(a.texCoords, b.texCoords)) return "the texture coordinates";
    if (a.corners != b.corners) return "the corners";
    if (a.indices != b.indices) return "the indices";
    if (a.words != b.words) return "the words";
    if (a.skipped != b.skipped) return "the skipped block";
    return {};
}

// A made file against its original as the game reads it: an archive entry by entry, a link list by its names, a
// record or bone byte for byte (the writer writes every field the reader read); the differences into `lines`.
void CompareFile(const std::string& name, const std::string& original, const std::string& made, const std::string& linkName,
                 std::vector<std::string>* lines);

void CompareBytes(const std::string& what, std::string_view original, std::string_view made, std::vector<std::string>* lines) {
    if (original == made) return;
    figure_gltf::Record a, b;
    std::string error;
    std::string field;
    if (figure_gltf::ReadRecord(original, &a, &error) && figure_gltf::ReadRecord(made, &b, &error))
        field = FirstDifference(a, b);
    lines->push_back(what + " differs" + (field.empty() ? std::string() : ": " + field));
}

void CompareLinks(const std::string& what, std::string_view original, std::string_view made, std::vector<std::string>* lines) {
    std::vector<std::pair<std::string, std::string>> a, b;
    if (!figure_gltf::ReadLinks(original, &a) || !figure_gltf::ReadLinks(made, &b) || a != b)
        lines->push_back(what + ": the link list differs");
}

// An animation (an .anm's entry) track by track, each byte for byte.
void CompareAnimation(const std::string& what, std::string_view original, std::string_view made, std::vector<std::string>* lines) {
    std::vector<std::pair<std::string, std::string_view>> a, b;
    if (!figure_gltf::ReadArchive(original, &a)) return CompareBytes(what, original, made, lines);
    if (!figure_gltf::ReadArchive(made, &b)) return lines->push_back(what + ": not an archive");
    std::map<std::string, std::string_view> byKey;
    for (const auto& [entry, bytes] : b) byKey[Key(entry)] = bytes;
    if (a.size() != b.size()) lines->push_back(what + ": the track count differs");
    for (const auto& [entry, bytes] : a) {
        const auto found = byKey.find(Key(entry));
        if (found == byKey.end()) lines->push_back(what + ": " + entry + " is missing");
        else if (bytes != found->second) lines->push_back(what + ": " + entry + " differs");
    }
}

void CompareFile(const std::string& name, const std::string& original, const std::string& made, const std::string& linkName,
                 std::vector<std::string>* lines) {
    const std::string extension = ExtensionOf(Key(name));
    if (extension == ".lnk") return CompareLinks(name, original, made, lines);
    std::vector<std::pair<std::string, std::string_view>> a, b;
    if ((extension == ".mod" || extension == ".bon" || extension == ".anm") && figure_gltf::ReadArchive(original, &a)) {
        if (!figure_gltf::ReadArchive(made, &b)) return lines->push_back(name + ": not an archive");
        std::map<std::string, std::string_view> byKey;
        for (const auto& [entry, bytes] : b) byKey[Key(entry)] = bytes;
        if (a.size() != b.size()) lines->push_back(name + ": the entry count differs");
        for (const auto& [entry, bytes] : a) {
            const auto found = byKey.find(Key(entry));
            if (found == byKey.end()) {
                lines->push_back(name + ": " + entry + " is missing");
            } else if (extension == ".mod" && Key(entry) == linkName) {
                CompareLinks(name + ": " + entry, bytes, found->second, lines);
            } else if (extension == ".anm") {
                CompareAnimation(name + ": " + entry, bytes, found->second, lines);
            } else {
                CompareBytes(name + ": " + entry, bytes, found->second, lines);
            }
        }
        return;
    }
    CompareBytes(name, original, made, lines);
}

} // namespace

bool IsFigureFile(const std::string& name) {
    const std::string extension = ExtensionOf(Key(name));
    return extension == ".mod" || extension == ".bon" || extension == ".lnk" || extension == ".fig" || extension == ".anm";
}

std::vector<Group> Groups(const std::vector<std::string>& names, std::vector<std::string>* left) {
    std::map<std::string, std::string> byKey;   // the figure files by key
    std::set<std::string> mods, lnks;
    for (const std::string& name : names) {
        if (!IsFigureFile(name)) continue;
        const std::string key = Key(name);
        byKey[key] = name;
        if (ExtensionOf(key) == ".mod") mods.insert(StemOf(key));
        if (ExtensionOf(key) == ".lnk") lnks.insert(StemOf(key));
    }
    std::map<std::string, Group> groups;   // by key
    std::set<std::string> taken;
    for (const std::string& stem : mods) {
        Group& group = groups[stem];
        group.name = StemOf(byKey[stem + ".mod"]);
        group.layout = figure_gltf::Layout::Set;
        for (const char* extension : {".mod", ".bon", ".anm"})
            if (byKey.contains(stem + extension)) {
                group.files.push_back(byKey[stem + extension]);
                taken.insert(stem + extension);
            }
    }
    for (const std::string& stem : lnks) {
        if (mods.contains(stem)) {
            left->push_back(byKey[stem + ".lnk"] + ": a .lnk beside a .mod of its name (the game reads the .mod)");
            taken.insert(stem + ".lnk");
            continue;
        }
        Group& group = groups[stem];
        group.name = StemOf(byKey[stem + ".lnk"]);
        group.layout = figure_gltf::Layout::Loose;
        group.files.push_back(byKey[stem + ".lnk"]);
        taken.insert(stem + ".lnk");
    }
    for (const auto& [key, name] : byKey) {
        if (taken.contains(key)) continue;
        const std::string stem = StemOf(key);
        if (ExtensionOf(key) == ".anm") {
            left->push_back(name + ": an .anm without a .mod of its name (the game reads a set's .anm with its .mod)");
            continue;
        }
        // The longest .lnk name that begins it.
        std::string owner;
        for (const std::string& lnk : lnks)
            if (stem.size() > lnk.size() && stem.compare(0, lnk.size(), lnk) == 0 && lnk.size() > owner.size()) owner = lnk;
        if (!owner.empty() && mods.contains(owner)) {
            left->push_back(name + ": a part of " + owner + ".lnk, beside " + owner + ".mod");
            continue;
        }
        if (owner.empty() && mods.contains(stem)) {
            left->push_back(name + ": its name is a set's (" + stem + ".mod)");
            continue;
        }
        if (owner.empty()) {
            owner = stem;
            Group& group = groups[owner];
            group.name = StemOf(name);
            group.layout = figure_gltf::Layout::Loose;
        }
        groups[owner].files.push_back(name);
    }
    std::vector<Group> out;
    for (auto& [key, group] : groups) out.push_back(std::move(group));
    return out;
}

std::vector<std::string> RoundTrip(const Group& group, const std::map<std::string, std::string>& files, std::string* glb) {
    std::vector<std::string> lines;
    figure_gltf::Figure figure, back;
    std::string error;
    if (!figure_gltf::FromFiles(group.name, group.layout, files, &figure, &error)) return {error};
    *glb = figure_gltf::ToGlb(figure, group.name);
    const auto start = std::chrono::steady_clock::now();
    figure_gltf::Notes notes;
    const bool read = figure_gltf::FromGltf(*glb, {}, &back, &error, &notes);
    const std::map<std::string, std::string> made = figure_gltf::Files(back, group.name);
    g_measures.readSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (!read) return {"the glTF does not read: " + error};
    for (const std::string& line : notes.lines) lines.push_back("a note: " + line);

    Measures& m = g_measures;
    ++m.figures;
    ++(group.layout == figure_gltf::Layout::Set ? m.sets : m.loose);
    m.raw += static_cast<int>(figure.raw.size());
    for (const figure_gltf::Part& part : figure.parts) {
        ++m.parts;
        m.linked += part.linked;
        m.bones += !part.bone.empty();
        if (!part.hasMesh) continue;
        const figure_gltf::Record& r = part.mesh;
        ++m.records[r.old ? "old" : r.versions == 8 ? "FIG8" : "FIG"];
        m.corners += static_cast<long long>(r.corners.size());
        m.slots += r.Slots();
        m.triangles += static_cast<long long>(r.indices.size() / 3);
    }
    std::set<std::string> partKeys;
    for (const figure_gltf::Part& part : figure.parts) partKeys.insert(Key(part.name));
    for (const figure_gltf::Animation& animation : figure.animations) {
        ++m.animations;
        for (const figure_gltf::Track& t : animation.tracks) {
            ++m.tracks;
            m.unplaced += !partKeys.contains(Key(t.name));
            m.morphed += t.frames > 0;
            m.rotationKeys += static_cast<long long>(t.rotations.size() / 4);
            m.positionKeys += static_cast<long long>(t.positions.size() / 3);
            m.vertexKeys += static_cast<long long>(t.frames) * t.slots;
        }
    }
    m.glbBytes += static_cast<long long>(glb->size());

    const std::string linkName = Key(group.name);
    std::set<std::string> originalKeys;
    for (const auto& [name, bytes] : files) {
        m.originalBytes += static_cast<long long>(bytes.size());
        const std::string key = Key(name);
        originalKeys.insert(key);
        const auto found = made.find(key);
        if (found == made.end()) {
            lines.push_back(name + " is not made");
            continue;
        }
        CompareFile(name, bytes, found->second, linkName, &lines);
    }
    for (const auto& [key, bytes] : made)
        if (!originalKeys.contains(key)) lines.push_back(key + " is made but was not there");
    m.failed += !lines.empty();
    return lines;
}

void PrintMeasures() {
    const Measures& m = g_measures;
    if (m.figures == 0) return;
    std::string records;
    for (const auto& [kind, count] : m.records) records += (records.empty() ? "" : ", ") + std::to_string(count) + " " + kind;
    std::printf("ei-convert: models: %d figures (%d sets, %d loose), %d parts (%d linked, %d with bones), records %s, "
                "%d files carried as they are; %lld corners on %lld slots, %lld triangles; %.1f MB of originals as "
                "%.1f MB of .glb; read back and made into files in %.2f s\n",
                m.figures, m.sets, m.loose, m.parts, m.linked, m.bones, records.c_str(), m.raw, m.corners, m.slots,
                m.triangles, static_cast<double>(m.originalBytes) / 1e6, static_cast<double>(m.glbBytes) / 1e6,
                m.readSeconds);
    if (m.animations)
        std::printf("ei-convert: animations: %d in the sets' .anm, %d tracks (%d of no part, %d with vertex frames), "
                    "%lld rotation keys, %lld position keys, %lld frame vertices\n",
                    m.animations, m.tracks, m.unplaced, m.morphed, m.rotationKeys, m.positionKeys, m.vertexKeys);
}

} // namespace convert_models
