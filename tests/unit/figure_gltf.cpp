// game/models/figure_gltf.h: figure models as glTF and back (docs/notes/mods.md, "Models as glTF"). A model set and a
// loose figure of every record kind (FIG8 with morph corners a float displacement cannot hold, FIG of one version,
// the older record with its skipped block; padding lanes, unused slots, a normal's w, normals not of unit length,
// words of their own, an entry of another kind) come back as the same files; a glTF of another tool's (no extras) is
// numbered by value, its bounds fitted, its axes turned; lib/json.h's reader.
#include "game/models/figure_gltf.h"

#include "lib/json.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    if (ok) return;
    ++g_failures;
    std::printf("ei_unit_check: figure_gltf: %s\n", what.c_str());
}

using figure_gltf::Figure;
using figure_gltf::Part;
using figure_gltf::Record;

// A record of `versions` with `used` position slots in 4-lane packets (the padding lanes copies of slot 0 but for
// one lane), a corner per used slot, two triangles per four corners.
Record MakeRecord(int versions, int used, bool old) {
    Record r;
    r.old = old;
    r.versions = versions;
    r.magic = versions == 8 ? 0x38474946u : 0x31474946u;
    r.materialValue = 0x13;
    r.meshType = 8;
    for (int i = 0; i < 10 * versions; ++i) r.bounds.push_back(0.25f * static_cast<float>(i) - 3.0f);
    const int slots = (used + 3) / 4 * 4;
    for (int s = 0; s < slots; ++s)
        for (int k = 0; k < versions; ++k)
            for (int c = 0; c < 3; ++c) {
                const int from = s < used ? s : (s == slots - 1 ? s : 0);
                // Corner k of a slot: one a float displacement from corner 0 does not hold (docs/notes/mods.md).
                float value = static_cast<float>(from) * 0.5f + static_cast<float>(c);
                if (k > 0) value = c == 0 ? 0.23675106465816498f + static_cast<float>(k) : value * 1.1f + static_cast<float>(k);
                if (k == 0 && c == 0) value = 0.10616640001535416f + static_cast<float>(from);
                r.positions.push_back(value);
            }
    const int normalSlots = slots;
    for (int s = 0; s < normalSlots; ++s) {
        const float n[4] = {0.0f, static_cast<float>(s % 3 == 0), static_cast<float>(s % 3 != 0), s == 1 ? 0.5f : 1.0f};
        r.normals.insert(r.normals.end(), n, n + 4);
    }
    for (int t = 0; t < used + 1; ++t) {   // the last no corner names
        r.texCoords.push_back(static_cast<float>(t) / 8.0f);
        r.texCoords.push_back(1.0f - static_cast<float>(t) / 16.0f);
    }
    for (int i = 0; i < used; ++i)
        r.corners.push_back({static_cast<std::uint16_t>(i), static_cast<std::uint16_t>(used - 1 - i), static_cast<std::uint16_t>(i)});
    for (int q = 0; q + 3 < used; q += 4) {
        for (const int i : {0, 1, 2, 0, 2, 3}) r.indices.push_back(static_cast<std::uint16_t>(q + i));
    }
    for (int s = 0; s < normalSlots; ++s) r.words.push_back(static_cast<std::uint32_t>(s < used ? s : used - 1) | static_cast<std::uint32_t>(s) << 16);
    if (old) {
        r.words.push_back(7);   // words of their own
        r.skipped.assign(2 * 0x34, '\x5A');
    }
    return r;
}

Part MakePart(const std::string& name, bool linked, const std::string& parent, int boneVersions) {
    Part part;
    part.name = name;
    part.linked = linked;
    part.parent = parent;
    for (int i = 0; i < boneVersions * 3; ++i) part.bone.push_back(static_cast<float>(i) * 0.125f - 1.0f);
    return part;
}

// The figure as a .glb, read back, made into its files: the same files as the figure's own.
void CheckRoundTrip(const Figure& figure, const std::string& name) {
    const std::map<std::string, std::string> files = figure_gltf::Files(figure, name);
    Figure fromFiles;
    std::string error;
    Check(figure_gltf::FromFiles(name, figure.layout, files, &fromFiles, &error), name + ": FromFiles: " + error);
    Check(figure_gltf::Files(fromFiles, name) == files, name + ": its files read and written again differ");
    const std::string glb = figure_gltf::ToGlb(fromFiles, name);
    Figure back;
    figure_gltf::Notes notes;
    Check(figure_gltf::FromGltf(glb, {}, &back, &error, &notes), name + ": FromGltf: " + error);
    Check(notes.lines.empty(), name + ": notes: " + (notes.lines.empty() ? std::string() : notes.lines[0]));
    const std::map<std::string, std::string> again = figure_gltf::Files(back, name);
    Check(again.size() == files.size(), name + ": a file count differs");
    for (const auto& [file, bytes] : files) {
        const auto found = again.find(file);
        Check(found != again.end() && found->second == bytes, name + ": " + file + " differs");
    }
}

void CheckSet() {
    Figure figure;
    figure.layout = figure_gltf::Layout::Set;
    Part root = MakePart("body", true, "", 8);
    root.hasMesh = true;
    root.mesh = MakeRecord(8, 6, false);
    Part arm = MakePart("rh.arm", true, "body", 6);
    arm.hasMesh = true;
    arm.mesh = MakeRecord(8, 9, false);
    arm.mesh.words.pop_back();   // fewer words than normal slots
    Part armour = MakePart("rh.arm.armor01", false, "", 8);
    armour.hasMesh = true;
    armour.mesh = MakeRecord(8, 4, false);
    // A zero normal and one not of unit length (a glTF's NORMAL holds neither: straight up and normalised there).
    for (int a = 0; a < 3; ++a) armour.mesh.normals[a] = 0.0f;
    armour.mesh.normals[2 * 4 + 1] = 0.6f;
    armour.mesh.normals[2 * 4 + 2] = 0.6f;
    Part empty = MakePart("crown", true, "body", 0);   // linked, no record, no bone
    figure.parts = {root, arm, empty, armour};
    figure.raw.push_back({"leftover", figure_gltf::RawFile::In::Mod, std::string("\x01\0\0\0\x06\0\0\0base2\0\0\0\0\0", 18)});
    CheckRoundTrip(figure, "unTest");

    // A set whose .mod has no link list and that has no .bon; one whose .bon is no archive (Lost in Astral has both).
    Figure unlinked;
    unlinked.link = false;
    unlinked.bones = false;
    Part wall = MakePart("wall", false, "", 0);
    wall.hasMesh = true;
    wall.mesh = MakeRecord(8, 4, false);
    unlinked.parts = {wall};
    CheckRoundTrip(unlinked, "unTest2");
    Figure badBones = unlinked;
    badBones.link = true;
    badBones.parts[0].linked = true;
    badBones.raw.push_back({"untest3.bon", figure_gltf::RawFile::In::Folder, "not an archive"});
    CheckRoundTrip(badBones, "unTest3");
}

void CheckLoose() {
    Figure figure;
    figure.layout = figure_gltf::Layout::Loose;
    Part a = MakePart("arrow", true, "", 6);
    a.hasMesh = true;
    a.mesh = MakeRecord(6, 5, true);
    Part b = MakePart("flag", true, "arrow", 8);
    b.hasMesh = true;
    b.mesh = MakeRecord(1, 7, false);
    Part c = MakePart("gipat01", false, "", 8);   // a part no link names
    c.hasMesh = true;
    c.mesh = MakeRecord(8, 3, false);
    c.mesh.indices.clear();
    figure.parts = {a, b, c};
    CheckRoundTrip(figure, "efcu");
    figure.link = false;   // a .fig of its own (no .lnk)
    figure.parts = {c};
    figure.parts[0].name = "";
    CheckRoundTrip(figure, "initarlg6object02");
}

std::string Base64(const std::string& bytes) {
    static constexpr char kDigits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        std::uint32_t n = static_cast<std::uint8_t>(bytes[i]) << 16;
        if (i + 1 < bytes.size()) n |= static_cast<std::uint8_t>(bytes[i + 1]) << 8;
        if (i + 2 < bytes.size()) n |= static_cast<std::uint8_t>(bytes[i + 2]);
        out += kDigits[n >> 18 & 63];
        out += kDigits[n >> 12 & 63];
        out += i + 1 < bytes.size() ? kDigits[n >> 6 & 63] : '=';
        out += i + 2 < bytes.size() ? kDigits[n & 63] : '=';
    }
    return out;
}

// A .gltf of another tool's: a quad of four vertices, its buffer external or a data URI, no extras.
void CheckOtherTools() {
    const float positions[] = {1, 3, -2, 1, 3, -2, 0, 0, 0, 2, 1, 0};   // two the same: one slot
    const float normals[] = {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1};
    const float uvs[] = {0, 0, 0, 0, 1, 0, 1, 1};
    const std::uint16_t indices[] = {0, 2, 3, 1, 3, 2};
    std::string bin(reinterpret_cast<const char*>(positions), sizeof(positions));
    bin.append(reinterpret_cast<const char*>(normals), sizeof(normals));
    bin.append(reinterpret_cast<const char*>(uvs), sizeof(uvs));
    bin.append(reinterpret_cast<const char*>(indices), sizeof(indices));
    for (const bool external : {true, false}) {
        const std::string uri = external ? "parts/quad%20data.bin" : "data:application/octet-stream;base64," + Base64(bin);
        const std::string gltf =
            "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
            "\"nodes\":[{\"name\":\"plate\",\"mesh\":0,\"translation\":[1,3,-2],\"children\":[1]},{\"name\":\"rivet\"}],"
            "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},\"indices\":3,"
            "\"material\":0}]}],\"materials\":[{\"name\":\"wood 2\"}],"
            "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\",\"min\":[0,0,-2],"
            "\"max\":[2,3,0]},{\"bufferView\":1,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
            "{\"bufferView\":2,\"componentType\":5126,\"count\":4,\"type\":\"VEC2\"},"
            "{\"bufferView\":3,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"}],"
            "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":48},{\"buffer\":0,\"byteOffset\":48,\"byteLength\":48},"
            "{\"buffer\":0,\"byteOffset\":96,\"byteLength\":32},{\"buffer\":0,\"byteOffset\":128,\"byteLength\":12}],"
            "\"buffers\":[{\"byteLength\":140,\"uri\":\"" + uri + "\"}]}";
        std::string asked;
        const figure_gltf::ReadUri read = [&](const std::string& name, std::string* out) {
            asked = name;
            *out = bin;
            return true;
        };
        Figure figure;
        std::string error;
        const std::string what = external ? "a .gltf with its .bin" : "a .gltf with a data URI";
        Check(figure_gltf::FromGltf(gltf, read, &figure, &error), what + ": " + error);
        Check(!external || asked == "parts/quad data.bin", what + ": the buffer asked as " + asked);
        Check(figure.layout == figure_gltf::Layout::Set && figure.parts.size() == 2, what + ": the parts");
        if (figure.parts.size() != 2) continue;
        const Part& plate = figure.parts[0];
        const Record& r = plate.mesh;
        Check(plate.name == "plate" && plate.linked && plate.parent.empty() && figure.parts[1].parent == "plate",
              what + ": the tree");
        Check(plate.bone.size() == 24 && plate.bone[0] == 1 && plate.bone[1] == 2 && plate.bone[2] == 3,
              what + ": the bone from the translation, in the game's axes");
        Check(plate.hasMesh && r.versions == 8 && r.Slots() == 4 && r.corners.size() == 4, what + ": the slots");
        if (!plate.hasMesh || r.positions.size() < 96) continue;
        Check(r.positions[0] == 1 && r.positions[1] == 2 && r.positions[2] == 3 && r.positions[21] == 1,
              what + ": the first position in the game's axes, every version the same");
        Check(r.corners[0].position == 0 && r.corners[1].position == 0 && r.corners[3].position == 2,
              what + ": equal positions share a slot");
        Check(r.positions[3 * 24] == 1 && r.positions[3 * 24 + 1] == 2, what + ": the padding lane a copy of the first slot");
        Check(r.normals.size() == 16 && r.normals[2] == 1 && r.normals[3] == 1 && r.normals[4 + 1] == -1 &&
                  r.normals[7] == 1,
              what + ": two normals (y up is the game's z, z the game's -y), w 1, padded");
        Check(r.materialValue == 0x12 && r.meshType == 2, what + ": the material from its name");
        Check(r.indices.size() == 6 && r.words.size() == 4 && r.words[3] == (1u | 3u << 16), what + ": indices and words");
        // Fitted: x 0..2, y -0..2 (the game's y is glTF's -z: 2, 0, 0), z 0..3.
        Check(r.bounds.size() == 80 && r.bounds[0] == 1 && r.bounds[2] == 1.5f && r.bounds[24] == -1 &&
                  r.bounds[48] == 1 && r.bounds[50] == 1.5f,
              what + ": the bounds fitted");
    }
}

void CheckJson() {
    json::Value v;
    std::string error;
    Check(json::Parse("{\"a\":[1,-2.5e3,true,null,\"x\\u00e9\\n\\ud83d\\ude00\"],\"b\":{}}", &v, &error), "json: " + error);
    Check(v["a"].Size() == 5 && v["a"].At(1).Number() == -2500.0 && v["a"].At(2).Bool() && v["b"].IsObject(),
          "json: the values");
    Check(v["a"].At(4).String() == "x\xc3\xa9\n\xf0\x9f\x98\x80", "json: the escapes as UTF-8");
    Check(!json::Parse("{\"a\":1,}", &v, &error) && !json::Parse("[1 2]", &v, &error) && !json::Parse("01x", &v, &error),
          "json: broken documents refused");
    for (const float f : {0.1f, -0.0f, 1e-30f, 3.4028235e38f, 0.10616640001535416f, 1.0f / 3.0f, INFINITY}) {
        json::Value back;
        const bool parsed = json::Parse(json::Float(f), &back, &error);
        const float read = json::FloatOf(back);
        Check(parsed && std::memcmp(&f, &read, sizeof(float)) == 0, "json: a float read back: " + json::Float(f));
    }
    Check(json::String("a\"\\\x01") == "\"a\\\"\\\\\\u0001\"", "json: a string escaped");
}

} // namespace

int RunFigureGltfChecks() {
    g_failures = 0;
    CheckJson();
    CheckSet();
    CheckLoose();
    CheckOtherTools();
    return g_failures;
}
