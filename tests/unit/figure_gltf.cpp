// engine/models/figure_gltf.h: figure models as glTF and back (docs/notes/mods.md, "Models as glTF" and "Animations as
// glTF"). A model set and a loose figure of every record kind (FIG8 with morph corners a float displacement cannot
// hold, FIG of one version, the older record with its skipped block; padding lanes, unused slots, a normal's w, normals
// not of unit length, words of their own, an entry of another kind) and a set's animations (a root's and children's
// keys, keys not of unit length, vertex frames, a track of no part, an animation of no channel, an entry that is no
// archive) come back as the same files; a glTF of another tool's (no extras) is numbered by value, its bounds fitted,
// its axes turned, its animation's keys sampled at the ticks and its rotations turned into the game's; lib/json.h's
// reader.
#include "engine/models/figure_gltf.h"

#include "lib/json.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    if (ok) return;
    ++g_failures;
    std::printf("ei_unit_check: figure_gltf: %s\n", what.c_str());
}

using figure_gltf::Animation;
using figure_gltf::Figure;
using figure_gltf::Part;
using figure_gltf::Record;
using figure_gltf::Track;

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

// A track of `keys` rotations and positions: turns about a varying axis (with `odd` the last key of no length and the
// one before it twice the unit length), positions moving along x.
Track MakeTrack(const std::string& name, int keys, bool odd) {
    Track t;
    t.name = name;
    for (int k = 0; k < keys; ++k) {
        const float angle = 0.3f * static_cast<float>(k + 1);
        float q[4] = {std::cos(angle), std::sin(angle) * 0.6f, std::sin(angle) * 0.8f * (k % 2 ? 1.0f : -1.0f), 0.0f};
        if (odd && k == keys - 1) q[0] = q[1] = q[2] = 0.0f;
        if (odd && k == keys - 2)
            for (float& c : q) c *= 2.0f;
        t.rotations.insert(t.rotations.end(), q, q + 4);
        t.positions.insert(t.positions.end(), {0.1f * static_cast<float>(k), 0.25f, 1.0f / 3.0f});
    }
    return t;
}

void CheckAnimations() {
    Figure figure;
    figure.layout = figure_gltf::Layout::Set;
    figure.anm = true;
    figure.parts = {MakePart("hp", true, "", 8), MakePart("bd", true, "hp", 8), MakePart("hd", true, "bd", 8),
                    MakePart("lh1.armor02", false, "", 8)};
    Animation idle;
    idle.name = "cidle01";
    // A child with fewer keys than its parent, a track of no part, an unlinked part's.
    idle.tracks = {MakeTrack("hp", 4, false), MakeTrack("bd", 4, true), MakeTrack("HD", 2, false), MakeTrack("ghost", 1, false),
                   MakeTrack("lh1.armor02", 3, false)};
    Track& body = idle.tracks[1];   // vertex frames: 3 of 5 slots
    body.frames = 3;
    body.slots = 5;
    for (int i = 0; i < 3 * 5 * 3; ++i) body.vertices.push_back(0.01f * static_cast<float>(i) - 0.2f);
    Track still;
    still.name = "hd";
    still.frames = 1;
    still.slots = 2;
    still.vertices = {1, 2, 3, 4, 5, 6};
    Animation face;
    face.name = "uface";
    face.tracks = {still};   // no rotation or position: no channel, carried as it is
    Animation empty;
    empty.name = "cempty";
    figure.animations = {idle, face, empty};
    figure.raw.push_back({"broken", figure_gltf::RawFile::In::Anm, "not an archive"});
    CheckRoundTrip(figure, "unTest4");

    const std::map<std::string, std::string> files = figure_gltf::Files(figure, "unTest4");
    std::vector<std::pair<std::string, std::string_view>> entries;
    Check(files.contains("untest4.anm") && figure_gltf::ReadArchive(files.at("untest4.anm"), &entries) && entries.size() == 4,
          "animations: the .anm made with its four entries");
    Track read;
    Check(!figure_gltf::ReadTrack(std::string("\x01\0\0\0", 4), &read), "animations: a short track refused");
    Check(figure_gltf::ReadTrack(figure_gltf::WriteTrack(body), &read) && read.rotations == body.rotations &&
              read.vertices == body.vertices,
          "animations: a track read back");
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

// A .gltf of another tool's with an animation: a root turning a quarter about glTF's y (the game's z) and moving up, its
// child held a quarter about x (STEP), keys at 0 and 0.11 s (two ticks: the middle one interpolated), and a scale
// channel (left out).
void CheckOtherToolsAnimation() {
    const float s = std::sqrt(0.5f);
    const float data[] = {0.0f, 0.11f,                       // times
                          0, 0, 0, 1, 0, s, 0, s,            // the root's rotations (x, y, z, w)
                          s, 0, 0, s, s, 0, 0, s,            // the child's
                          0, 0, 0, 0, 1, 0,                  // the root's translations
                          1, 1, 1, 2, 2, 2};                 // a scale
    const std::string bin(reinterpret_cast<const char*>(data), sizeof(data));
    const std::string gltf =
        "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],"
        "\"nodes\":[{\"name\":\"a\",\"children\":[1]},{\"name\":\"b\",\"translation\":[0,1,0]}],"
        "\"animations\":[{\"name\":\"walk\",\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"rotation\"}},"
        "{\"sampler\":1,\"target\":{\"node\":1,\"path\":\"rotation\"}},{\"sampler\":2,\"target\":{\"node\":0,\"path\":\"translation\"}},"
        "{\"sampler\":3,\"target\":{\"node\":1,\"path\":\"scale\"}}],"
        "\"samplers\":[{\"input\":0,\"output\":1},{\"input\":0,\"output\":2,\"interpolation\":\"STEP\"},{\"input\":0,\"output\":3},"
        "{\"input\":0,\"output\":4}]}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":2,\"type\":\"SCALAR\",\"min\":[0],\"max\":[0.11]},"
        "{\"bufferView\":1,\"componentType\":5126,\"count\":2,\"type\":\"VEC4\"},"
        "{\"bufferView\":2,\"componentType\":5126,\"count\":2,\"type\":\"VEC4\"},"
        "{\"bufferView\":3,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"},"
        "{\"bufferView\":4,\"componentType\":5126,\"count\":2,\"type\":\"VEC3\"}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":8},{\"buffer\":0,\"byteOffset\":8,\"byteLength\":32},"
        "{\"buffer\":0,\"byteOffset\":40,\"byteLength\":32},{\"buffer\":0,\"byteOffset\":72,\"byteLength\":24},"
        "{\"buffer\":0,\"byteOffset\":96,\"byteLength\":24}],"
        "\"buffers\":[{\"byteLength\":120,\"uri\":\"data:application/octet-stream;base64," + Base64(bin) + "\"}]}";
    Figure figure;
    std::string error;
    figure_gltf::Notes notes;
    const std::string what = "a .gltf's animation";
    Check(figure_gltf::FromGltf(gltf, {}, &figure, &error, &notes), what + ": " + error);
    Check(notes.lines.size() == 1, what + ": a note of the scale channel");
    Check(figure.anm && figure.animations.size() == 1 && figure.animations[0].name == "walk" &&
              figure.animations[0].tracks.size() == 2,
          what + ": one animation of two tracks");
    if (figure.animations.size() != 1 || figure.animations[0].tracks.size() != 2) return;
    const Track& a = figure.animations[0].tracks[0];
    const Track& b = figure.animations[0].tracks[1];
    const auto near = [](const std::vector<float>& values, std::size_t at, std::initializer_list<float> expected) {
        std::size_t i = at;
        for (const float e : expected)
            if (i >= values.size() || std::fabs(values[i++] - e) > 1e-5f) return false;
        return true;
    };
    Check(a.name == "a" && a.rotations.size() == 12 && a.positions.size() == 9 && b.name == "b" && b.rotations.size() == 12 &&
              b.positions.empty(),
          what + ": three keys a track (two ticks' time), the child's positions none");
    // The root (w, x, y, z): the identity, an eighth about the game's z (slerped), a quarter; up the game's z.
    const float eighth = 0.39269908f;
    Check(near(a.rotations, 0, {1, 0, 0, 0}) && near(a.rotations, 4, {std::cos(eighth), 0, 0, std::sin(eighth)}) &&
              near(a.rotations, 8, {s, 0, 0, s}),
          what + ": the root's keys in the game's axes, the middle one slerped");
    Check(near(a.positions, 3, {0, 0, 0.5f}) && near(a.positions, 6, {0, 0, 1}), what + ": the root's positions");
    // The child: a quarter about its parent's x, which the parent turns: about the game's x, (s, s, 0), then y.
    Check(near(b.rotations, 0, {s, s, 0, 0}) && near(b.rotations, 4, {s, 0.5f, 0.5f, 0}) && near(b.rotations, 8, {s, 0, s, 0}),
          what + ": the child's keys turned after its parent's");
    const std::map<std::string, std::string> files = figure_gltf::Files(figure, "unwalk");
    std::vector<std::pair<std::string, std::string_view>> entries;
    Check(files.contains("unwalk.anm") && figure_gltf::ReadArchive(files.at("unwalk.anm"), &entries) && entries.size() == 1,
          what + ": the .anm made");
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
    CheckAnimations();
    CheckOtherToolsAnimation();
    return g_failures;
}
