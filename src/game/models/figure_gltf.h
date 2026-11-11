#pragma once

// Figure models as glTF 2.0 (docs/notes/mods.md, "Models as glTF" and "Animations as glTF"): a figure's files (a model
// set's <name>.mod, <name>.bon and <name>.anm archives, or a loose figure's <name>.lnk, <name><part>.fig and
// <name><part>.bon) read into a Figure, written as one .glb, and a .gltf / .glb read back into the Figure and made into
// the files the game would have read in its place. The part tree is the glTF's nodes, each part's mesh a glTF mesh whose
// 7 morph targets are the FIG8 record's other corners, each animation a glTF animation of the nodes' rotations
// (figure_gltf_animation.cpp); what glTF has no place for is in each object's `extras.ei`. No game state: the resource
// layer, ei-convert and the unit check use it as is.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace figure_gltf {

// A part's mesh record (docs/notes/models.md, "Link figures"): a FIG record (FigHeader, its magic's versions) or the
// older headerless one (6 versions, a count before every array), every field as the file holds it.
struct Record {
    bool old = false;                     // the older record
    std::uint32_t magic = 0x38474946;     // a FIG record's ("FIG8": 8 versions; another "FIG" one: 1)
    std::uint32_t headerTail = 0;         // FigHeader's last dword
    std::uint32_t materialValue = 0;
    std::uint32_t meshType = 0;
    int versions = 8;                     // 8, 1 or 6 (old)
    // The bounds: the versions' centres (3 floats each), then their box minima, box maxima, then their radii.
    std::vector<float> bounds;
    // The positions by slot (the packets' lanes; a multiple of 4): slot s, version k, component c at
    // (s * versions + k) * 3 + c.
    std::vector<float> positions;
    std::vector<float> normals;           // by slot (a multiple of 4), x, y, z, w
    std::vector<float> texCoords;         // u, v
    struct Corner {
        std::uint16_t position = 0, normal = 0, texCoord = 0;
        bool operator==(const Corner&) const = default;
    };
    std::vector<Corner> corners;
    std::vector<std::uint16_t> indices;   // into corners, 3 a triangle
    std::vector<std::uint32_t> words;
    std::string skipped;                  // the older record's block of 0x34-byte records, as it is

    int Slots() const { return static_cast<int>(positions.size() / (3 * static_cast<std::size_t>(versions))); }
};

// A part of a figure: its name, its place in the link list, its mesh record and its bone (each optional).
struct Part {
    std::string name;
    bool linked = false;                  // in the link list
    std::string parent;                   // its parent there (empty: a root)
    bool hasMesh = false;
    Record mesh;
    std::vector<float> bone;              // the .bon's floats: 6 or 8 versions of an offset (empty: no bone file)
};

// A part's track of an animation (an entry of the animation's archive, named as the part): its keys as
// AnimationTrack::LoadKeyFrames reads them (docs/notes/map_objects.md, "Skeleton"), every field as the file holds it.
struct Track {
    std::string name;
    std::vector<float> rotations;         // 4 a key: w, x, y, z (the game's axes; a child's turned after its parent's)
    std::vector<float> positions;         // 3 a key (the game reads a root's alone)
    int frames = 0;                       // vertex frames
    int slots = 0;                        // vertices a frame (the part's position slots)
    std::vector<float> vertices;          // frames * slots * 3: each frame's offset of every slot

    bool operator==(const Track&) const = default;
};

// An animation of a set's <name>.anm: the archive's entry named as the game names the animation, itself an archive of
// its tracks.
struct Animation {
    std::string name;
    std::vector<Track> tracks;
};

// The figure's files: a model set (<name>.mod with the link list as its entry <name>, <name>.bon, <name>.anm), or loose
// ones (<name>.lnk when `link`, <name><part>.fig, <name><part>.bon).
enum class Layout { Set, Loose };

// A file of the figure that is no record, bone, link list or animation a glTF can hold (an archive entry of another
// kind, a record too short for its counts or without corners, an animation whose tracks do not parse or whose glTF
// animation would have no channel): carried as it is.
struct RawFile {
    enum class In { Mod, Bon, Anm, Folder };   // an entry of the set's .mod, .bon or .anm, or a loose file of that name
    std::string name;
    In in = In::Folder;
    std::string bytes;
};

struct Figure {
    Layout layout = Layout::Set;
    bool link = true;                     // a link list: a loose figure's <name>.lnk, a set's entry <name>
    bool bones = true;                    // a set has a <name>.bon (made when a part has a bone, else empty)
    bool anm = false;                     // a set has a <name>.anm archive
    std::vector<Part> parts;              // the linked ones first, in the link list's order
    std::vector<Animation> animations;    // the .anm's, in its order
    std::vector<RawFile> raw;
};

// ---- The files ----------------------------------------------------------------------------------------------------

// A record from its bytes; false with `*error` set when they are short or a count is out of range.
bool ReadRecord(std::string_view bytes, Record* out, std::string* error);
std::string WriteRecord(const Record& record);
// A link list (a count, then (child, parent) names of four-byte length and their characters up to a zero byte).
// A track from its bytes (an animation archive's entry): false unless they read to their end.
bool ReadTrack(std::string_view bytes, Track* out);
std::string WriteTrack(const Track& track);
bool ReadLinks(std::string_view bytes, std::vector<std::pair<std::string, std::string>>* links);
std::string WriteLinks(const std::vector<std::pair<std::string, std::string>>& links);
// A .res archive's entries (name, bytes) from its image, in their data's order; false when it is not one.
bool ReadArchive(std::string_view image, std::vector<std::pair<std::string, std::string_view>>* entries);
// The image of an archive of these entries (the names' hash chains as the game's lookup walks them).
std::string WriteArchive(const std::vector<std::pair<std::string, std::string>>& entries);

// The figure `name` from its files (`files`: lower-case name to bytes; a set's <name>.mod, <name>.bon and <name>.anm, or
// the loose ones). False with `*error` set when a file fails to parse.
bool FromFiles(std::string_view name, Layout layout, const std::map<std::string, std::string>& files, Figure* out,
               std::string* error);
// The files the figure stands for, by lower-case name (the archives, the link list and records written anew; a set's
// .bon when it had one, its .anm when it had one or has animations).
std::map<std::string, std::string> Files(const Figure& figure, std::string_view name);

// ---- glTF ---------------------------------------------------------------------------------------------------------

// The figure as a .glb named `name` (the scene's name).
std::string ToGlb(const Figure& figure, std::string_view name);

// The bytes of an external file a .gltf names by `uri` (relative to the .gltf, percent-decoded); false when there
// is none.
using ReadUri = std::function<bool(const std::string& uri, std::string* bytes)>;

// What FromGltf saw that a reader should hear of (a line each), the figure being made anyway.
struct Notes {
    std::vector<std::string> lines;
};

// A .gltf or .glb read into a figure. The records and the animations' keys come back exactly when their `extras.ei`
// agree with the glTF's own data (ei-convert's files, and others that kept them); otherwise they are built from the
// glTF's data alone.
// False with `*error` set when cgltf cannot read it.
bool FromGltf(std::string_view bytes, const ReadUri& readUri, Figure* out, std::string* error, Notes* notes = nullptr);

} // namespace figure_gltf
