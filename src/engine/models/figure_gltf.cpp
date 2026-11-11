// Figure models as glTF: see figure_gltf.h and docs/notes/mods.md, "Models as glTF" (the layout of the .glb, the
// extras, the coordinates).
#include "engine/models/figure_gltf.h"
#include "engine/models/figure_gltf_glb.h"

#include "engine/core/asset_text.h"
#include "lib/bytes.h"
#include "lib/float_select.h"
#include "lib/json.h"
#include "lib/vecmath.h"

#include "cgltf/cgltf.h"

#include <algorithm>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unordered_map>

namespace figure_gltf {

namespace {

// ---- Bytes --------------------------------------------------------------------------------------------------------

constexpr std::uint32_t kFig8Magic = 0x38474946;   // "FIG8"
constexpr std::uint32_t kFigMagic = 0x474946;      // "FIG": a FIG record's magic's low three bytes
constexpr int kFig8Versions = 8;
constexpr int kOldVersions = 6;
constexpr int kBoundsFloats = 10;                  // a version's centre, box minimum, box maximum, radius
constexpr std::size_t kSkippedRecordBytes = 0x34;  // the older record's skipped block's records
constexpr std::size_t kBoneCube = 8 * 3 * sizeof(float);    // a .bon of 8 versions (any size but the prism's)
constexpr std::size_t kBonePrism = 6 * 3 * sizeof(float);   // of 6 (the prism)

constexpr std::uint32_t kResMagic = 0x019CE23C;
constexpr std::size_t kResHeaderBytes = 16;
constexpr std::size_t kResEntryBytes = 22;

bool IsFig(std::uint32_t magic) { return (magic & 0xFFFFFF) == kFigMagic; }

bool Fail(std::string* error, const char* what) {
    *error = what;
    return false;
}

bool SameBits(const Vector3& a, const Vector3& b) { return Bits(a.x) == Bits(b.x) && Bits(a.y) == Bits(b.y) && Bits(a.z) == Bits(b.z); }

// A vector of three floats into a flat array at `first`.
void PutVector(std::vector<float>* values, std::size_t first, const Vector3& v) {
    (*values)[first] = v.x;
    (*values)[first + 1] = v.y;
    (*values)[first + 2] = v.z;
}
void AppendVector(std::vector<float>* values, const Vector3& v) { values->insert(values->end(), {v.x, v.y, v.z}); }

// How far a normal's length may be from 1 in a glTF's NORMAL (the Khronos validator allows 0.00674).
constexpr float kUnitSlack = 0.005f;

// A normal as a glTF's NORMAL holds it: of unit length. A shipped normal that is not (a zero one, some of the trees'
// and the hero's) goes in normalised, or as straight up when it has no direction; its own value then in the patch.
Vector3 GltfNormal(const Vector3& n) {
    const float length = Length(n);
    if (std::fabs(length - 1.0f) <= kUnitSlack) return n;
    if (std::isfinite(length) && length > 0.0f) return n / length;
    return {0.0f, 0.0f, 1.0f};
}

// Reads little-endian values at a moving offset; a read past the end leaves `ok` false (and the value zero).
struct Cursor {
    std::string_view bytes;
    std::size_t at = 0;
    bool ok = true;

    bool Has(std::size_t n) const { return ok && n <= bytes.size() - at; }
    template <class T>
    T Get() {
        T value{};
        if (!Has(sizeof(T))) {
            ok = false;
            return value;
        }
        value = lib::Load<T>(bytes.data() + at);
        at += sizeof(T);
        return value;
    }
    // `count` elements of T appended to `out` (the cursor moved past them).
    template <class T>
    void Array(std::size_t count, std::vector<T>* out) {
        if (!Has(count * sizeof(T)) || count > bytes.size()) {
            ok = false;
            return;
        }
        const std::size_t first = out->size();
        out->resize(first + count);
        for (std::size_t i = 0; i < count; ++i) (*out)[first + i] = lib::Load<T>(bytes.data() + at + i * sizeof(T));
        at += count * sizeof(T);
    }
    // A count the file holds before an array (a negative one is out of range).
    std::size_t Count() {
        const std::int32_t n = Get<std::int32_t>();
        if (n < 0) ok = false;
        return ok ? static_cast<std::size_t>(n) : 0;
    }
};

template <class T>
void Put(std::string* out, T value) {
    out->append(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <class T>
void PutArray(std::string* out, const std::vector<T>& values) {
    if (!values.empty()) out->append(reinterpret_cast<const char*>(values.data()), values.size() * sizeof(T));
}

// The packets' floats (per packet: 3 blocks x, y, z of `versions` rows of 4 lanes) as positions by slot, and back.
void PacketsToSlots(const std::vector<float>& file, int packets, int versions, std::vector<float>* positions) {
    positions->assign(static_cast<std::size_t>(packets) * 4 * versions * 3, 0.0f);
    for (int p = 0; p < packets; ++p)
        for (int b = 0; b < 3; ++b)
            for (int k = 0; k < versions; ++k)
                for (int lane = 0; lane < 4; ++lane)
                    (*positions)[((static_cast<std::size_t>(p) * 4 + lane) * versions + k) * 3 + b] =
                        file[((static_cast<std::size_t>(p) * 3 + b) * versions + k) * 4 + lane];
}

std::vector<float> SlotsToPackets(const Record& r) {
    const int packets = r.Slots() / 4;
    std::vector<float> file(r.positions.size());
    for (int p = 0; p < packets; ++p)
        for (int b = 0; b < 3; ++b)
            for (int k = 0; k < r.versions; ++k)
                for (int lane = 0; lane < 4; ++lane)
                    file[((static_cast<std::size_t>(p) * 3 + b) * r.versions + k) * 4 + lane] =
                        r.positions[((static_cast<std::size_t>(p) * 4 + lane) * r.versions + k) * 3 + b];
    return file;
}

// A normal packet's rows x, y, z, w of 4 lanes as normals by slot, and back.
void NormalPacketsToSlots(const std::vector<float>& file, std::vector<float>* normals) {
    normals->assign(file.size(), 0.0f);
    for (std::size_t p = 0; p < file.size() / 16; ++p)
        for (int row = 0; row < 4; ++row)
            for (int lane = 0; lane < 4; ++lane) (*normals)[(p * 4 + lane) * 4 + row] = file[p * 16 + row * 4 + lane];
}

std::vector<float> NormalSlotsToPackets(const std::vector<float>& normals) {
    std::vector<float> file(normals.size());
    for (std::size_t p = 0; p < normals.size() / 16; ++p)
        for (int row = 0; row < 4; ++row)
            for (int lane = 0; lane < 4; ++lane) file[p * 16 + row * 4 + lane] = normals[(p * 4 + lane) * 4 + row];
    return file;
}

void CornersFrom(const std::vector<std::uint16_t>& words, std::vector<Record::Corner>* corners) {
    corners->resize(words.size() / 3);
    for (std::size_t i = 0; i < corners->size(); ++i)
        (*corners)[i] = {words[i * 3], words[i * 3 + 1], words[i * 3 + 2]};
}

void PutCorners(std::string* out, const std::vector<Record::Corner>& corners) {
    for (const Record::Corner& c : corners) {
        Put(out, c.position);
        Put(out, c.normal);
        Put(out, c.texCoord);
    }
}

} // namespace

bool ReadRecord(std::string_view bytes, Record* out, std::string* error) {
    Record r;
    Cursor cursor{bytes};
    std::vector<float> packets, normals;
    std::vector<std::uint16_t> cornerWords;
    if (bytes.size() >= 4 && IsFig(Cursor{bytes}.Get<std::uint32_t>())) {
        r.magic = cursor.Get<std::uint32_t>();
        r.versions = r.magic == kFig8Magic ? kFig8Versions : 1;
        std::int32_t counts[6];
        for (std::int32_t& n : counts) n = cursor.Get<std::int32_t>();
        r.headerTail = cursor.Get<std::uint32_t>();
        for (const std::int32_t n : counts)
            if (n < 0) cursor.ok = false;
        if (!cursor.ok) return Fail(error, "a count is out of range");
        const auto [vertexCount, normalCount, texCoordCount, indexCount, cornerCount, wordCount] = counts;
        r.materialValue = cursor.Get<std::uint32_t>();
        r.meshType = cursor.Get<std::uint32_t>();
        cursor.Array(static_cast<std::size_t>(kBoundsFloats) * r.versions, &r.bounds);
        cursor.Array(static_cast<std::size_t>(vertexCount) * 3 * r.versions * 4, &packets);
        cursor.Array(static_cast<std::size_t>(normalCount) * 16, &normals);
        cursor.Array(static_cast<std::size_t>(texCoordCount) * 2, &r.texCoords);
        cursor.Array(static_cast<std::size_t>(indexCount), &r.indices);
        cursor.Array(static_cast<std::size_t>(cornerCount) * 3, &cornerWords);
        cursor.Array(static_cast<std::size_t>(wordCount), &r.words);
        if (cursor.ok) PacketsToSlots(packets, vertexCount, r.versions, &r.positions);
    } else {
        r.old = true;
        r.versions = kOldVersions;
        r.materialValue = cursor.Get<std::uint32_t>();
        r.meshType = cursor.Get<std::uint32_t>();
        cursor.Array(static_cast<std::size_t>(kBoundsFloats) * r.versions, &r.bounds);
        const std::size_t vertexCount = cursor.Count();
        cursor.Array(vertexCount * 3 * r.versions * 4, &packets);
        cursor.Array(cursor.Count() * 16, &normals);
        cursor.Array(cursor.Count() * 2, &r.texCoords);
        const std::size_t skipped = cursor.Count();
        if (cursor.Has(skipped * kSkippedRecordBytes) && skipped <= bytes.size()) {
            r.skipped.assign(bytes.substr(cursor.at, skipped * kSkippedRecordBytes));
            cursor.at += skipped * kSkippedRecordBytes;
        } else {
            cursor.ok = false;
        }
        cursor.Array(cursor.Count() * 3, &cornerWords);
        cursor.Array(cursor.Count(), &r.words);
        cursor.Array(cursor.Count(), &r.indices);
        if (cursor.ok) PacketsToSlots(packets, static_cast<int>(vertexCount), r.versions, &r.positions);
    }
    if (!cursor.ok) return Fail(error, "shorter than its counts");
    NormalPacketsToSlots(normals, &r.normals);
    CornersFrom(cornerWords, &r.corners);
    *out = std::move(r);
    return true;
}

std::string WriteRecord(const Record& r) {
    std::string out;
    const auto count = [](std::size_t n) { return static_cast<std::int32_t>(n); };
    const std::vector<float> packets = SlotsToPackets(r);
    const std::vector<float> normals = NormalSlotsToPackets(r.normals);
    if (!r.old) {
        Put(&out, r.magic);
        for (const std::size_t n : {packets.size() / (12 * static_cast<std::size_t>(r.versions)), normals.size() / 16,
                                    r.texCoords.size() / 2, r.indices.size(), r.corners.size(), r.words.size()})
            Put(&out, count(n));
        Put(&out, r.headerTail);
        Put(&out, r.materialValue);
        Put(&out, r.meshType);
        PutArray(&out, r.bounds);
        PutArray(&out, packets);
        PutArray(&out, normals);
        PutArray(&out, r.texCoords);
        PutArray(&out, r.indices);
        PutCorners(&out, r.corners);
        PutArray(&out, r.words);
        return out;
    }
    Put(&out, r.materialValue);
    Put(&out, r.meshType);
    PutArray(&out, r.bounds);
    Put(&out, count(packets.size() / (12 * static_cast<std::size_t>(r.versions))));
    PutArray(&out, packets);
    Put(&out, count(normals.size() / 16));
    PutArray(&out, normals);
    Put(&out, count(r.texCoords.size() / 2));
    PutArray(&out, r.texCoords);
    Put(&out, count(r.skipped.size() / kSkippedRecordBytes));
    out += r.skipped;
    Put(&out, count(r.corners.size()));
    PutCorners(&out, r.corners);
    Put(&out, count(r.words.size()));
    PutArray(&out, r.words);
    Put(&out, count(r.indices.size()));
    PutArray(&out, r.indices);
    return out;
}

bool ReadTrack(std::string_view bytes, Track* out) {
    Track t;
    Cursor cursor{bytes};
    cursor.Array(cursor.Count() * 4, &t.rotations);
    cursor.Array(cursor.Count() * 3, &t.positions);
    const std::int32_t frames = cursor.Get<std::int32_t>();
    const std::int32_t slots = cursor.Get<std::int32_t>();
    if (!cursor.ok || frames < 0 || slots < 0) return false;
    t.frames = frames;
    t.slots = slots;
    cursor.Array(static_cast<std::size_t>(frames) * static_cast<std::size_t>(slots) * 3, &t.vertices);
    if (!cursor.ok || cursor.at != bytes.size()) return false;
    t.name = out->name;
    *out = std::move(t);
    return true;
}

std::string WriteTrack(const Track& t) {
    std::string out;
    Put(&out, static_cast<std::int32_t>(t.rotations.size() / 4));
    PutArray(&out, t.rotations);
    Put(&out, static_cast<std::int32_t>(t.positions.size() / 3));
    PutArray(&out, t.positions);
    Put(&out, static_cast<std::int32_t>(t.frames));
    Put(&out, static_cast<std::int32_t>(t.slots));
    PutArray(&out, t.vertices);
    return out;
}

bool ReadLinks(std::string_view bytes, std::vector<std::pair<std::string, std::string>>* links) {
    Cursor cursor{bytes};
    const std::uint32_t count = cursor.Get<std::uint32_t>();
    links->clear();
    for (std::uint32_t i = 0; i < count && cursor.ok; ++i) {
        std::string names[2];
        for (std::string& name : names) {
            const std::uint32_t length = cursor.Get<std::uint32_t>();
            if (!cursor.Has(length)) return false;
            const std::string_view text = bytes.substr(cursor.at, length);
            name.assign(text.substr(0, std::min(text.find('\0'), text.size())));
            cursor.at += length;
        }
        links->emplace_back(std::move(names[0]), std::move(names[1]));
    }
    return cursor.ok;
}

std::string WriteLinks(const std::vector<std::pair<std::string, std::string>>& links) {
    std::string out;
    Put(&out, static_cast<std::uint32_t>(links.size()));
    for (const auto& [child, parent] : links)
        for (const std::string* name : {&child, &parent}) {
            // An empty name has no characters, not even the zero byte (a root's parent).
            Put(&out, static_cast<std::uint32_t>(name->empty() ? 0 : name->size() + 1));
            if (!name->empty()) out.append(name->c_str(), name->size() + 1);
        }
    return out;
}

bool ReadArchive(std::string_view image, std::vector<std::pair<std::string, std::string_view>>* entries) {
    Cursor cursor{image};
    const std::uint32_t magic = cursor.Get<std::uint32_t>();
    const std::uint32_t count = cursor.Get<std::uint32_t>();
    const std::uint32_t table = cursor.Get<std::uint32_t>();
    const std::uint32_t namesSize = cursor.Get<std::uint32_t>();
    if (!cursor.ok || magic != kResMagic) return false;
    const std::size_t names = table + static_cast<std::size_t>(count) * kResEntryBytes;
    if (table > image.size() || names > image.size() || namesSize > image.size() - names) return false;
    entries->clear();
    for (std::uint32_t i = 0; i < count; ++i) {
        Cursor entry{image, table + static_cast<std::size_t>(i) * kResEntryBytes};
        entry.Get<std::int32_t>();
        const std::uint32_t size = entry.Get<std::uint32_t>();
        const std::uint32_t offset = entry.Get<std::uint32_t>();
        entry.Get<std::uint32_t>();
        const std::uint16_t nameLength = entry.Get<std::uint16_t>();
        const std::uint32_t nameOffset = entry.Get<std::uint32_t>();
        if (!entry.ok || offset > image.size() || size > image.size() - offset || nameOffset > namesSize ||
            nameLength > namesSize - nameOffset)
            return false;
        std::string_view name = image.substr(names + nameOffset, nameLength);
        name = name.substr(0, std::min(name.find('\0'), name.size()));
        entries->emplace_back(std::string(name), image.substr(offset, size));
    }
    // In their data's order (the table is in the hash's): the order WriteArchive was given them.
    std::stable_sort(entries->begin(), entries->end(),
                     [&](const auto& a, const auto& b) { return a.second.data() < b.second.data(); });
    return true;
}

std::string WriteArchive(const std::vector<std::pair<std::string, std::string>>& entries) {
    const std::size_t count = entries.size();
    // The lookup (ResourceBase::FindEntry): from slot (sum of the lower-cased bytes) % count along the `next` chain.
    std::vector<int> slotOf(count, -1), entryAt(count, -1), next(count, -1);
    std::vector<std::uint32_t> home(count);
    for (std::size_t i = 0; i < count; ++i) {
        std::uint32_t hash = 0;
        for (const char c : entries[i].first) hash += static_cast<unsigned char>(Lower(c));
        home[i] = hash % static_cast<std::uint32_t>(count);
        if (entryAt[home[i]] == -1) {
            entryAt[home[i]] = static_cast<int>(i);
            slotOf[i] = static_cast<int>(home[i]);
        }
    }
    std::size_t free = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (slotOf[i] != -1) continue;
        while (entryAt[free] != -1) ++free;
        entryAt[free] = static_cast<int>(i);
        slotOf[i] = static_cast<int>(free);
        int chain = static_cast<int>(home[i]);
        while (next[static_cast<std::size_t>(chain)] != -1) chain = next[static_cast<std::size_t>(chain)];
        next[static_cast<std::size_t>(chain)] = static_cast<int>(free);
    }
    std::string data, names;
    std::vector<std::uint32_t> offsets(count), nameOffsets(count);
    for (std::size_t i = 0; i < count; ++i) {
        offsets[i] = static_cast<std::uint32_t>(kResHeaderBytes + data.size());
        data += entries[i].second;
        nameOffsets[i] = static_cast<std::uint32_t>(names.size());
        names += entries[i].first;
    }
    std::string out;
    Put(&out, kResMagic);
    Put(&out, static_cast<std::uint32_t>(count));
    Put(&out, static_cast<std::uint32_t>(kResHeaderBytes + data.size()));
    Put(&out, static_cast<std::uint32_t>(names.size()));
    out += data;
    for (std::size_t slot = 0; slot < count; ++slot) {
        const std::size_t i = static_cast<std::size_t>(entryAt[slot]);
        Put(&out, static_cast<std::int32_t>(next[slot]));
        Put(&out, static_cast<std::uint32_t>(entries[i].second.size()));
        Put(&out, offsets[i]);
        Put(&out, std::uint32_t{0});
        Put(&out, static_cast<std::uint16_t>(entries[i].first.size()));
        Put(&out, nameOffsets[i]);
    }
    out += names;
    return out;
}

// ---- A figure from its files and back ---------------------------------------------------------------------------

namespace {

// The part of `figure` named `name` (no case), or null.
Part* FindPart(Figure& figure, std::string_view name) {
    for (Part& part : figure.parts)
        if (SameName(part.name, name)) return &part;
    return nullptr;
}

// A mesh record from a file's bytes: false (the bytes kept as they are by the caller) when they are not one a glTF
// mesh can hold: one that reads to its end, with corners, whose corners name slots it has and whose indices name its
// corners.
bool MeshOf(std::string_view bytes, Record* record) {
    std::string error;
    if (!ReadRecord(bytes, record, &error) || WriteRecord(*record).size() != bytes.size() || record->corners.empty())
        return false;
    const std::size_t positions = static_cast<std::size_t>(record->Slots()), normals = record->normals.size() / 4,
                      texCoords = record->texCoords.size() / 2;
    if (positions > 0x10000 || normals > 0x10000 || texCoords > 0x10000) return false;
    for (const Record::Corner& c : record->corners)
        if (c.position >= positions || (normals && c.normal >= normals) || (texCoords && c.texCoord >= texCoords))
            return false;
    for (const std::uint16_t i : record->indices)
        if (i >= record->corners.size()) return false;
    return true;
}

// A bone from a .bon's bytes: false when it is not 6 or 8 versions of an offset.
bool BoneOf(std::string_view bytes, std::vector<float>* bone) {
    if (bytes.size() != kBonePrism && bytes.size() != kBoneCube) return false;
    bone->resize(bytes.size() / sizeof(float));
    std::memcpy(bone->data(), bytes.data(), bytes.size());
    return true;
}

std::string BoneBytes(const Part& part) {
    return std::string(reinterpret_cast<const char*>(part.bone.data()), part.bone.size() * sizeof(float));
}

// An animation from an .anm entry's bytes: false (the bytes kept as they are by the caller) unless they are an archive
// of tracks that read, no two of one name.
bool AnimationOf(std::string_view name, std::string_view bytes, Animation* animation) {
    std::vector<std::pair<std::string, std::string_view>> entries;
    if (!ReadArchive(bytes, &entries)) return false;
    animation->name = name;
    animation->tracks.clear();
    std::vector<std::string> keys;
    for (const auto& [entry, track] : entries) {
        Track& t = animation->tracks.emplace_back();
        t.name = entry;
        if (!ReadTrack(track, &t)) return false;
        keys.push_back(Key(entry));
    }
    std::sort(keys.begin(), keys.end());
    return std::adjacent_find(keys.begin(), keys.end()) == keys.end();
}

std::string AnimationBytes(const Animation& animation) {
    std::vector<std::pair<std::string, std::string>> tracks;
    for (const Track& t : animation.tracks) tracks.emplace_back(t.name, WriteTrack(t));
    return WriteArchive(tracks);
}

// The parts of a link list, in its order.
void AddLinkedParts(Figure* figure, const std::vector<std::pair<std::string, std::string>>& links) {
    for (const auto& [child, parent] : links) {
        Part& part = figure->parts.emplace_back();
        part.name = child;
        part.linked = true;
        part.parent = parent;
    }
}

// The bytes as part `name`'s mesh (`bone` false) or bone: given to the part of that name (a new unlinked one when
// there is none) unless it has one already or they fail to parse as one; false then (the caller keeps them raw).
bool GiveToPart(Figure* figure, std::string_view name, std::string_view bytes, bool bone) {
    Part* part = FindPart(*figure, name);
    if (part && (bone ? !part->bone.empty() : part->hasMesh)) return false;
    Record record;
    std::vector<float> offsets;
    if (bone ? !BoneOf(bytes, &offsets) : !MeshOf(bytes, &record)) return false;
    if (!part) {
        part = &figure->parts.emplace_back();
        part->name = name;
    }
    if (bone) {
        part->bone = std::move(offsets);
    } else {
        part->hasMesh = true;
        part->mesh = std::move(record);
    }
    return true;
}

} // namespace

bool FromFiles(std::string_view name, Layout layout, const std::map<std::string, std::string>& files, Figure* out,
               std::string* error) {
    Figure figure;
    figure.layout = layout;
    std::map<std::string, const std::string*> byKey;
    for (const auto& [file, bytes] : files) byKey[Key(file)] = &bytes;
    const std::string base = Key(name);
    std::vector<std::pair<std::string, std::string>> links;
    if (layout == Layout::Set) {
        const auto mod = byKey.find(base + ".mod");
        std::vector<std::pair<std::string, std::string_view>> entries;
        if (mod == byKey.end() || !ReadArchive(*mod->second, &entries)) return Fail(error, "its .mod is not an archive");
        // The link list (a set without one, one of Lost in Astral's walls: every part unlinked).
        figure.link = false;
        for (const auto& [entry, bytes] : entries)
            if (!figure.link && Key(entry) == base) figure.link = ReadLinks(bytes, &links);
        AddLinkedParts(&figure, links);
        bool linkSeen = false;
        for (const auto& [entry, bytes] : entries) {
            if (figure.link && !linkSeen && Key(entry) == base) {
                linkSeen = true;
                continue;
            }
            if (!GiveToPart(&figure, entry, bytes, false)) figure.raw.push_back({entry, RawFile::In::Mod, std::string(bytes)});
        }
        // The bones (a .bon that is no archive, as one of Lost in Astral's, carried as it is).
        const auto bon = byKey.find(base + ".bon");
        figure.bones = bon != byKey.end() && ReadArchive(*bon->second, &entries);
        if (figure.bones) {
            for (const auto& [entry, bytes] : entries)
                if (!GiveToPart(&figure, entry, bytes, true)) figure.raw.push_back({entry, RawFile::In::Bon, std::string(bytes)});
        } else if (bon != byKey.end()) {
            figure.raw.push_back({base + ".bon", RawFile::In::Folder, *bon->second});
        }
        // The animations (an .anm that is no archive carried as it is).
        const auto anm = byKey.find(base + ".anm");
        figure.anm = anm != byKey.end() && ReadArchive(*anm->second, &entries);
        if (figure.anm) {
            for (const auto& [entry, bytes] : entries) {
                Animation animation;
                if (AnimationOf(entry, bytes, &animation)) figure.animations.push_back(std::move(animation));
                else figure.raw.push_back({entry, RawFile::In::Anm, std::string(bytes)});
            }
        } else if (anm != byKey.end()) {
            figure.raw.push_back({base + ".anm", RawFile::In::Folder, *anm->second});
        }
    } else {
        const auto lnk = byKey.find(base + ".lnk");
        figure.link = lnk != byKey.end();
        if (figure.link && !ReadLinks(*lnk->second, &links)) return Fail(error, "its .lnk does not read");
        AddLinkedParts(&figure, links);
        for (const auto& [file, bytes] : files) {
            const std::string key = Key(file);
            if (key == base + ".lnk") continue;
            if (key.size() < base.size() + 4 || key.compare(0, base.size(), base) != 0) {
                figure.raw.push_back({file, RawFile::In::Folder, bytes});
                continue;
            }
            const std::string extension = key.substr(key.size() - 4);
            const std::string_view part = std::string_view(file).substr(base.size(), file.size() - base.size() - 4);
            const bool read = (extension == ".fig" || extension == ".bon") && GiveToPart(&figure, part, bytes, extension == ".bon");
            if (!read) figure.raw.push_back({file, RawFile::In::Folder, bytes});
        }
    }
    *out = std::move(figure);
    return true;
}

std::map<std::string, std::string> Files(const Figure& figure, std::string_view name) {
    std::map<std::string, std::string> files;
    std::vector<std::pair<std::string, std::string>> links;
    for (const Part& part : figure.parts)
        if (part.linked) links.emplace_back(part.name, part.parent);
    if (figure.layout == Layout::Set) {
        std::vector<std::pair<std::string, std::string>> mod, bon, anm;
        if (figure.link) mod.emplace_back(std::string(name), WriteLinks(links));
        for (const Animation& animation : figure.animations) anm.emplace_back(animation.name, AnimationBytes(animation));
        for (const Part& part : figure.parts) {
            if (part.hasMesh) mod.emplace_back(part.name, WriteRecord(part.mesh));
            if (!part.bone.empty()) bon.emplace_back(part.name, BoneBytes(part));
        }
        for (const RawFile& raw : figure.raw) {
            if (raw.in == RawFile::In::Mod) mod.emplace_back(raw.name, raw.bytes);
            if (raw.in == RawFile::In::Bon) bon.emplace_back(raw.name, raw.bytes);
            if (raw.in == RawFile::In::Anm) anm.emplace_back(raw.name, raw.bytes);
        }
        files[Key(name) + ".mod"] = WriteArchive(mod);
        if (figure.bones || !bon.empty()) files[Key(name) + ".bon"] = WriteArchive(bon);
        // The animations by name: a glTF holds those of no channel apart from the others.
        std::stable_sort(anm.begin(), anm.end(), [](const auto& a, const auto& b) { return Key(a.first) < Key(b.first); });
        if (figure.anm || !anm.empty()) files[Key(name) + ".anm"] = WriteArchive(anm);
    }
    if (figure.layout == Layout::Loose) {
        if (figure.link) files[Key(name) + ".lnk"] = WriteLinks(links);
        for (const Part& part : figure.parts) {
            if (part.hasMesh) files[Key(name) + Key(part.name) + ".fig"] = WriteRecord(part.mesh);
            if (!part.bone.empty()) files[Key(name) + Key(part.name) + ".bon"] = BoneBytes(part);
        }
    }
    for (const RawFile& raw : figure.raw)
        if (raw.in == RawFile::In::Folder) files[Key(raw.name)] = raw.bytes;
    return files;
}

// ---- Writing the .glb ---------------------------------------------------------------------------------------------

namespace {


// The FIG8 corners' names as morph targets (corner k's bits: 1 s, 2 t, 4 u; corner 0 is the base), and the older
// record's prism (c1 s, c2 t, c3 u, c4 s and u, c5 t and u).
constexpr const char* kCubeTargets[] = {"s", "t", "st", "u", "su", "tu", "stu"};
constexpr const char* kPrismTargets[] = {"s", "t", "u", "su", "tu"};

const char* const* TargetNames(int versions) { return versions == kOldVersions ? kPrismTargets : kCubeTargets; }

// The material kinds with a file of their own (material.cpp's table) by name; 0x19 shares metal's file.
struct MaterialName {
    std::uint32_t value = 0;
    const char* name = nullptr;
};
constexpr MaterialName kMaterialNames[] = {{0x11, "leather"}, {0x12, "wood"},  {0x13, "metal"}, {0x14, "cloth"},
                                           {0x15, "skin"},    {0x16, "stone"}, {0x17, "magic"}};
// A part without a material (a glTF made elsewhere): stone, the part's texture kind 1 (an object's own texture).
constexpr std::uint32_t kDefaultMaterial = 0x16;
constexpr std::uint32_t kDefaultMeshType = 1;

// "wood 8": the material kind's name (or its number when it has none of its own) and the texture kind.
std::string MaterialText(std::uint32_t value, std::uint32_t meshType) {
    std::string name = std::to_string(value);
    for (const MaterialName& known : kMaterialNames)
        if (known.value == value) name = known.name;
    return name + " " + std::to_string(meshType);
}


std::string Hex(std::string_view bytes) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    for (const char c : bytes) {
        out.push_back(kDigits[static_cast<unsigned char>(c) >> 4]);
        out.push_back(kDigits[static_cast<unsigned char>(c) & 15]);
    }
    return out;
}

// A slot's version-k position as the loader works it out from the glTF: the base plus the target's displacement.
Vector3 Reconstructed(const Vector3& base, const Vector3& displacement) { return base + displacement; }

// The words the loader makes when the glTF gives none: one per normal slot, its own slot in the high half and in the
// low half the slot (a padding lane: the last normal a corner names), as every shipped record but a few has them.
std::vector<std::uint32_t> DerivedWords(const Record& r) {
    const std::size_t slots = r.normals.size() / 4;
    std::uint32_t used = 0;
    for (const Record::Corner& c : r.corners) used = std::max<std::uint32_t>(used, c.normal + 1u);
    std::vector<std::uint32_t> words(slots);
    for (std::size_t i = 0; i < slots; ++i) {
        const std::uint32_t low = used == 0 ? 0 : std::min<std::uint32_t>(static_cast<std::uint32_t>(i), used - 1);
        words[i] = low | static_cast<std::uint32_t>(i) << 16;
    }
    return words;
}

// The JSON of a part's mesh (its accessors added to `glb`), and the `extras.ei` of what glTF has no place for.
std::string MeshJson(const Part& part, int material, GlbWriter& glb) {
    const Record& r = part.mesh;
    const std::size_t n = r.corners.size();
    const int versions = r.versions;
    const bool hasNormals = !r.normals.empty();
    const bool hasTexCoords = !r.texCoords.empty();
    std::vector<float> positions(n * 3), normals, texCoords;
    std::vector<std::vector<float>> targets(static_cast<std::size_t>(versions - 1), std::vector<float>(n * 3));
    std::vector<std::uint16_t> slots(n * 3);
    std::vector<bool> positionGiven(static_cast<std::size_t>(r.Slots())), normalGiven(r.normals.size() / 4),
        texCoordGiven(r.texCoords.size() / 2), positionExact(static_cast<std::size_t>(r.Slots()), true);
    if (hasNormals) normals.resize(n * 3);
    if (hasTexCoords) texCoords.resize(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        const Record::Corner& c = r.corners[i];
        slots[i * 3] = c.position;
        slots[i * 3 + 1] = c.normal;
        slots[i * 3 + 2] = c.texCoord;
        const std::size_t p = c.position;
        if (p < positionGiven.size()) {
            positionGiven[p] = true;
            const Vector3 v0 = VectorAt(r.positions, p * versions * 3);
            PutVector(&positions, i * 3, ToGltfAxes(v0));
            for (int k = 1; k < versions; ++k) {
                const Vector3 vk = VectorAt(r.positions, (p * versions + k) * 3);
                const Vector3 delta = vk - v0;
                if (!SameBits(Reconstructed(v0, delta), vk)) positionExact[p] = false;
                PutVector(&targets[static_cast<std::size_t>(k - 1)], i * 3, ToGltfAxes(delta));
            }
        }
        if (hasNormals && c.normal < normalGiven.size()) {
            normalGiven[c.normal] = true;
            PutVector(&normals, i * 3, ToGltfAxes(GltfNormal(VectorAt(r.normals, static_cast<std::size_t>(c.normal) * 4))));
        }
        if (hasTexCoords && c.texCoord < texCoordGiven.size()) {
            texCoordGiven[c.texCoord] = true;
            texCoords[i * 2] = r.texCoords[static_cast<std::size_t>(c.texCoord) * 2];
            texCoords[i * 2 + 1] = r.texCoords[static_cast<std::size_t>(c.texCoord) * 2 + 1];
        }
    }

    std::string attributes = "\"POSITION\":" + Num(glb.Vec3(positions, GlbWriter::Target::Vertices));
    if (hasNormals)
        attributes += ",\"NORMAL\":" + Num(glb.Accessor(normals.data(), normals.size() * 4, GlbWriter::Component::Float, n,
                                                        "VEC3", GlbWriter::Target::Vertices));
    if (hasTexCoords)
        attributes += ",\"TEXCOORD_0\":" + Num(glb.Accessor(texCoords.data(), texCoords.size() * 4, GlbWriter::Component::Float,
                                                            n, "VEC2", GlbWriter::Target::Vertices));
    std::string primitive = "{\"attributes\":{" + attributes + "}";
    if (!r.indices.empty())
        primitive += ",\"indices\":" + Num(glb.Accessor(r.indices.data(), r.indices.size() * 2,
                                                        GlbWriter::Component::UnsignedShort, r.indices.size(), "SCALAR",
                                                        GlbWriter::Target::Indices));
    primitive += ",\"material\":" + Num(material);
    std::vector<std::string> targetJson, names, weights;
    for (int k = 1; k < versions; ++k) {
        targetJson.push_back("{\"POSITION\":" + Num(glb.Vec3(targets[static_cast<std::size_t>(k - 1)], GlbWriter::Target::Vertices)) + "}");
        names.push_back(json::String(TargetNames(versions)[k - 1]));
        weights.push_back("0");
    }
    if (!targetJson.empty()) primitive += ",\"targets\":[" + Join(targetJson) + "]";
    primitive += "}";

    // What the glTF's own data does not give exactly (docs/notes/mods.md, "Models as glTF").
    std::vector<std::string> ei;
    ei.push_back("\"format\":" + json::String(r.old ? "old" : r.magic == kFig8Magic ? "FIG8" : "FIG"));
    if (!r.old && r.magic != kFig8Magic) ei.push_back("\"magic\":" + Num(r.magic));
    if (r.headerTail) ei.push_back("\"unused\":" + Num(r.headerTail));
    {
        std::vector<std::string> centre, low, high, radius;
        for (int k = 0; k < versions; ++k) {
            centre.push_back(Floats(&r.bounds[static_cast<std::size_t>(k) * 3], 3));
            low.push_back(Floats(&r.bounds[static_cast<std::size_t>(versions + k) * 3], 3));
            high.push_back(Floats(&r.bounds[static_cast<std::size_t>(2 * versions + k) * 3], 3));
            radius.push_back(json::Float(r.bounds[static_cast<std::size_t>(9 * versions + k)]));
        }
        ei.push_back("\"bounds\":{\"center\":[" + Join(centre) + "],\"min\":[" + Join(low) + "],\"max\":[" + Join(high) +
                     "],\"radius\":[" + Join(radius) + "]}");
    }
    ei.push_back("\"slots\":" + Num(glb.Accessor(slots.data(), slots.size() * 2, GlbWriter::Component::UnsignedShort, n, "VEC3",
                                                 GlbWriter::Target::None)));
    // The slots the corners do not give exactly, each with all its values: those no corner names (the packets'
    // padding, unused ones), a position whose versions do not come back from a float displacement, a normal whose w
    // is not 1 or that is not of unit length.
    const auto patch = [&](const char* key, std::size_t slotCount, int floats, const char* type, auto given) {
        std::vector<std::uint16_t> at;
        std::vector<float> values;
        for (std::size_t s = 0; s < slotCount; ++s) {
            const float* value = nullptr;
            if (!given(s, &value)) {
                at.push_back(static_cast<std::uint16_t>(s));
                values.insert(values.end(), value, value + floats);
            }
        }
        if (at.empty()) return;
        const int slotsAccessor = glb.Accessor(at.data(), at.size() * 2, GlbWriter::Component::UnsignedShort, at.size(),
                                               "SCALAR", GlbWriter::Target::None);
        const std::size_t perSlot = std::strcmp(type, "VEC3") == 0 ? static_cast<std::size_t>(floats / 3) : 1;
        const int valuesAccessor = glb.Accessor(values.data(), values.size() * 4, GlbWriter::Component::Float,
                                                at.size() * perSlot, type, GlbWriter::Target::None);
        ei.push_back(std::string("\"") + key + "\":{\"slots\":" + Num(slotsAccessor) + ",\"values\":" +
                     Num(valuesAccessor) + "}");
    };
    patch("positions", positionGiven.size(), versions * 3, "VEC3", [&](std::size_t s, const float** value) {
        *value = &r.positions[s * versions * 3];
        return positionGiven[s] && positionExact[s];
    });
    patch("normals", normalGiven.size(), 4, "VEC4", [&](std::size_t s, const float** value) {
        *value = &r.normals[s * 4];
        const Vector3 normal = VectorAt(r.normals, s * 4);
        return normalGiven[s] && Bits(r.normals[s * 4 + 3]) == Bits(1.0f) && SameBits(GltfNormal(normal), normal);
    });
    patch("texcoords", texCoordGiven.size(), 2, "VEC2", [&](std::size_t s, const float** value) {
        *value = &r.texCoords[s * 2];
        return texCoordGiven[s];
    });
    if (r.words != DerivedWords(r))
        ei.push_back("\"words\":" + Num(r.words.empty() ? -1
                                                         : glb.Accessor(r.words.data(), r.words.size() * 4,
                                                                        GlbWriter::Component::UnsignedInt, r.words.size(),
                                                                        "SCALAR", GlbWriter::Target::None)));
    if (r.indices.empty()) ei.push_back("\"indices\":false");
    if (!r.skipped.empty()) ei.push_back("\"skipped\":" + Num(glb.View(r.skipped.data(), r.skipped.size(), GlbWriter::Target::None)));

    std::string mesh = "{\"name\":" + json::String(text::FromAssetText(part.name)) + ",\"primitives\":[" + primitive + "]";
    if (!weights.empty()) mesh += ",\"weights\":[" + Join(weights) + "]";
    mesh += ",\"extras\":{";
    if (!names.empty()) mesh += "\"targetNames\":[" + Join(names) + "],";
    return mesh + "\"ei\":{" + Join(ei) + "}}}";
}

} // namespace

std::string ToGlb(const Figure& figure, std::string_view name) {
    GlbWriter glb;
    std::vector<std::string> nodes, meshes, materials;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> materialKeys;
    // Each linked part's node index, for the children lists.
    std::vector<std::vector<int>> children(figure.parts.size());
    std::vector<int> roots;
    for (std::size_t i = 0; i < figure.parts.size(); ++i) {
        const Part& part = figure.parts[i];
        bool child = false;
        if (part.linked && !part.parent.empty())
            for (std::size_t p = 0; p < figure.parts.size() && !child; ++p)
                if (p != i && figure.parts[p].linked && SameName(figure.parts[p].name, part.parent)) {
                    children[p].push_back(static_cast<int>(i));
                    child = true;
                }
        if (!child) roots.push_back(static_cast<int>(i));
    }
    for (std::size_t i = 0; i < figure.parts.size(); ++i) {
        const Part& part = figure.parts[i];
        std::string node = "{\"name\":" + json::String(text::FromAssetText(part.name));
        if (part.bone.size() >= 3) {
            node += ",\"translation\":" + Floats(ToGltfAxes(VectorAt(part.bone, 0)));
        }
        if (part.hasMesh) {
            const std::pair key{part.mesh.materialValue, part.mesh.meshType};
            auto found = std::find(materialKeys.begin(), materialKeys.end(), key);
            if (found == materialKeys.end()) {
                materialKeys.push_back(key);
                materials.push_back("{\"name\":" + json::String(MaterialText(key.first, key.second)) +
                                    ",\"pbrMetallicRoughness\":{\"metallicFactor\":0.0,\"roughnessFactor\":1.0}"
                                    ",\"extras\":{\"ei\":{\"material\":" + Num(key.first) + ",\"kind\":" +
                                    Num(key.second) + "}}}");
                found = materialKeys.end() - 1;
            }
            meshes.push_back(MeshJson(part, static_cast<int>(found - materialKeys.begin()), glb));
            node += ",\"mesh\":" + Num(static_cast<std::int64_t>(meshes.size()) - 1);
        }
        if (!children[i].empty()) {
            std::vector<std::string> list;
            for (const int c : children[i]) list.push_back(Num(c));
            node += ",\"children\":[" + Join(list) + "]";
        }
        std::vector<std::string> ei;
        if (!part.bone.empty()) {
            std::vector<std::string> versions;
            for (std::size_t k = 0; k + 3 <= part.bone.size(); k += 3) versions.push_back(Floats(&part.bone[k], 3));
            ei.push_back("\"bone\":[" + Join(versions) + "]");
        }
        if (!part.linked) ei.push_back("\"linked\":false");
        // A parent the link list names that is no linked part: kept by name (the node is a root).
        if (part.linked && !part.parent.empty() &&
            std::find(roots.begin(), roots.end(), static_cast<int>(i)) != roots.end())
            ei.push_back("\"parent\":" + json::String(text::FromAssetText(part.parent)));
        nodes.push_back(node + ",\"extras\":{\"ei\":{" + Join(ei) + "}}}");
    }
    // The animations (figure_gltf_animation.cpp); one that would have no channel is carried as it is.
    std::vector<int> parentOf(figure.parts.size(), -1);
    for (std::size_t p = 0; p < figure.parts.size(); ++p)
        for (const int c : children[p]) parentOf[static_cast<std::size_t>(c)] = static_cast<int>(p);
    std::vector<RawFile> rawFiles = figure.raw;
    std::vector<std::string> animations;
    for (const Animation& animation : figure.animations) {
        std::string json = AnimationJson(animation, figure.parts, parentOf, glb);
        if (json.empty()) rawFiles.push_back({animation.name, RawFile::In::Anm, AnimationBytes(animation)});
        else animations.push_back(std::move(json));
    }
    std::vector<std::string> rootList, raw;
    for (const int r : roots) rootList.push_back(Num(r));
    for (const RawFile& file : rawFiles) {
        const char* in = file.in == RawFile::In::Mod   ? "mod"
                         : file.in == RawFile::In::Bon ? "bon"
                         : file.in == RawFile::In::Anm ? "anm"
                                                       : "folder";
        raw.push_back("{\"name\":" + json::String(text::FromAssetText(file.name)) + ",\"in\":\"" + in +
                      "\",\"view\":" +
                      Num(file.bytes.empty() ? -1 : glb.View(file.bytes.data(), file.bytes.size(), GlbWriter::Target::None)) + "}");
    }
    std::string sceneEi = std::string("\"layout\":\"") + (figure.layout == Layout::Set ? "set" : "loose") + "\"";
    if (!figure.link) sceneEi += ",\"link\":false";
    if (figure.layout == Layout::Set && !figure.bones) sceneEi += ",\"bones\":false";
    if (figure.anm) sceneEi += ",\"anm\":true";
    if (!raw.empty()) sceneEi += ",\"files\":[" + Join(raw) + "]";

    while (glb.bin.size() % 4) glb.bin.push_back('\0');
    std::string jsonText = "{\"asset\":{\"version\":\"2.0\",\"generator\":\"ei-convert (docs/notes/mods.md, Models as glTF)\"}"
                           ",\"scene\":0,\"scenes\":[{\"name\":" + json::String(text::FromAssetText(name)) +
                           ",\"nodes\":[" + Join(rootList) + "],\"extras\":{\"ei\":{" + sceneEi + "}}}]";
    if (!nodes.empty()) jsonText += ",\"nodes\":[" + Join(nodes) + "]";
    if (!meshes.empty()) jsonText += ",\"meshes\":[" + Join(meshes) + "]";
    if (!materials.empty()) jsonText += ",\"materials\":[" + Join(materials) + "]";
    if (!animations.empty()) jsonText += ",\"animations\":[" + Join(animations) + "]";
    if (!glb.accessors.empty()) jsonText += ",\"accessors\":[" + Join(glb.accessors) + "]";
    if (!glb.views.empty()) jsonText += ",\"bufferViews\":[" + Join(glb.views) + "]";
    if (!glb.bin.empty()) jsonText += ",\"buffers\":[{\"byteLength\":" + Num(static_cast<std::int64_t>(glb.bin.size())) + "}]";
    jsonText += "}";
    while (jsonText.size() % 4) jsonText.push_back(' ');

    std::string out;
    const std::size_t total = 12 + 8 + jsonText.size() + (glb.bin.empty() ? 0 : 8 + glb.bin.size());
    Put(&out, std::uint32_t{0x46546C67});   // "glTF"
    Put(&out, std::uint32_t{2});
    Put(&out, static_cast<std::uint32_t>(total));
    Put(&out, static_cast<std::uint32_t>(jsonText.size()));
    Put(&out, std::uint32_t{0x4E4F534A});   // "JSON"
    out += jsonText;
    if (!glb.bin.empty()) {
        Put(&out, static_cast<std::uint32_t>(glb.bin.size()));
        Put(&out, std::uint32_t{0x4E4942});   // "BIN\0"
        out += glb.bin;
    }
    return out;
}

// ---- Reading a .gltf / .glb ---------------------------------------------------------------------------------------

namespace {

// cgltf's file reads go to the caller's ReadUri (the .gltf's place: its folder or archive), never to the disk.
cgltf_result ReadFile(const cgltf_memory_options*, const cgltf_file_options* options, const char* path, cgltf_size* size,
                      void** data) {
    const ReadUri& read = *static_cast<const ReadUri*>(options->user_data);
    std::string bytes;
    if (!read || !read(path, &bytes)) return cgltf_result_file_not_found;
    *data = std::malloc(std::max<std::size_t>(bytes.size(), 1));
    if (!*data) return cgltf_result_out_of_memory;
    if (!bytes.empty()) std::memcpy(*data, bytes.data(), bytes.size());
    *size = bytes.size();
    return cgltf_result_success;
}

void ReleaseFile(const cgltf_memory_options*, const cgltf_file_options*, void* data) { std::free(data); }

const char* ResultText(cgltf_result result) {
    switch (result) {
    case cgltf_result_data_too_short: return "too short";
    case cgltf_result_unknown_format: return "not glTF";
    case cgltf_result_invalid_json: return "its JSON does not read";
    case cgltf_result_invalid_gltf: return "not valid glTF";
    case cgltf_result_file_not_found: return "a buffer's file is not there";
    case cgltf_result_io_error: return "a buffer does not read";
    case cgltf_result_out_of_memory: return "out of memory";
    case cgltf_result_legacy_gltf: return "glTF 1.0";
    default: return "cgltf cannot read it";
    }
}

// The bytes of the buffer view an extras value names by index (empty when there is none).
std::string ViewBytes(const cgltf_data* data, const json::Value& index) {
    if (!index.IsNumber() || index.Number() < 0 || index.Number() >= static_cast<double>(data->buffer_views_count))
        return {};
    const cgltf_buffer_view& view = data->buffer_views[static_cast<std::size_t>(index.Int())];
    const auto* bytes = reinterpret_cast<const char*>(cgltf_buffer_view_data(&view));
    return bytes ? std::string(bytes, view.size) : std::string();
}

const cgltf_accessor* Attribute(const cgltf_attribute* attributes, std::size_t count, cgltf_attribute_type type) {
    for (std::size_t i = 0; i < count; ++i)
        if (attributes[i].type == type && attributes[i].index == 0) return attributes[i].data;
    return nullptr;
}

// Whether a version's position `value` is the base plus its displacement (`made`) but for the sum's rounding: at most
// two steps of the larger of the base and the value apart (the displacement's subtraction and the sum round once each).
bool WithinRounding(float value, float made, float base) {
    if (Bits(value) == Bits(made)) return true;
    if (!std::isfinite(value) || !std::isfinite(made) || !std::isfinite(base)) return false;
    const float larger = std::max(std::fabs(base), std::fabs(value));
    const double step = static_cast<double>(std::nextafter(larger, INFINITY)) - static_cast<double>(larger);
    return std::fabs(static_cast<double>(value) - static_cast<double>(made)) <= 2 * step;
}

// The glTF vertices of a mesh, every primitive's after another, in the game's axes.
struct Vertices {
    std::size_t count = 0;
    int versions = kFig8Versions;
    std::vector<float> positions;   // vertex i, version k, component c at (i * versions + k) * 3 + c
    std::vector<float> normals;     // empty: none
    std::vector<float> texCoords;   // empty: none
    std::vector<std::uint32_t> indices;
    const cgltf_material* material = nullptr;
};

bool ReadVertices(const cgltf_mesh& mesh, int versions, Vertices* out, Notes* notes, const std::string& part) {
    out->versions = versions;
    const char* const* targetNames = TargetNames(versions);
    bool someNormals = false, someTexCoords = false;
    for (std::size_t p = 0; p < mesh.primitives_count; ++p) {
        const cgltf_primitive& primitive = mesh.primitives[p];
        if (primitive.type != cgltf_primitive_type_triangles) {
            if (notes) notes->lines.push_back(part + ": a primitive that is not triangles is left out");
            continue;
        }
        const cgltf_accessor* position = Attribute(primitive.attributes, primitive.attributes_count, cgltf_attribute_type_position);
        if (!position) continue;
        const std::vector<float> base = Floats(position);
        const std::vector<float> normal = Floats(Attribute(primitive.attributes, primitive.attributes_count, cgltf_attribute_type_normal));
        const std::vector<float> uv = Floats(Attribute(primitive.attributes, primitive.attributes_count, cgltf_attribute_type_texcoord));
        const std::size_t n = position->count;
        if (base.size() != n * 3) continue;
        someNormals = someNormals || normal.size() == n * 3;
        someTexCoords = someTexCoords || uv.size() == n * 2;
        // The targets by name (mesh.extras.targetNames, cgltf's target_names), else in their order.
        std::vector<std::vector<float>> displacement(static_cast<std::size_t>(versions - 1));
        for (std::size_t t = 0; t < primitive.targets_count; ++t) {
            int corner = static_cast<int>(t) + 1;
            if (t < mesh.target_names_count && mesh.target_names[t]) {
                corner = 0;
                for (int k = 1; k < versions; ++k)
                    if (std::strcmp(mesh.target_names[t], targetNames[k - 1]) == 0) corner = k;
            }
            if (corner <= 0 || corner >= versions) continue;
            displacement[static_cast<std::size_t>(corner - 1)] = Floats(Attribute(primitive.targets[t].attributes, primitive.targets[t].attributes_count, cgltf_attribute_type_position));
        }
        const std::size_t first = out->count;
        for (std::size_t i = 0; i < n; ++i) {
            const Vector3 v0 = ToGameAxes(VectorAt(base, i * 3));
            for (int k = 0; k < versions; ++k) {
                const std::vector<float>* moved = k > 0 ? &displacement[static_cast<std::size_t>(k - 1)] : nullptr;
                AppendVector(&out->positions,
                             moved && moved->size() == n * 3 ? Reconstructed(v0, ToGameAxes(VectorAt(*moved, i * 3))) : v0);
            }
            AppendVector(&out->normals, normal.size() == n * 3 ? ToGameAxes(VectorAt(normal, i * 3)) : Vector3{});
            out->texCoords.push_back(uv.size() == n * 2 ? uv[i * 2] : 0.0f);
            out->texCoords.push_back(uv.size() == n * 2 ? uv[i * 2 + 1] : 0.0f);
        }
        out->count += n;
        if (primitive.indices) {
            for (std::size_t i = 0; i < primitive.indices->count; ++i)
                out->indices.push_back(static_cast<std::uint32_t>(first + cgltf_accessor_read_index(primitive.indices, i)));
        } else {
            for (std::size_t i = 0; i < n; ++i) out->indices.push_back(static_cast<std::uint32_t>(first + i));
        }
        if (!out->material) out->material = primitive.material;
        else if (primitive.material != out->material && notes)
            notes->lines.push_back(part + ": its primitives have several materials; the first is the part's");
    }
    if (!someNormals) out->normals.clear();
    if (!someTexCoords) out->texCoords.clear();
    return out->count > 0;
}

// The record's slots from ei-convert's extras: the corners' slots, then the slots the corners do not give. False when
// the extras do not agree with the vertices (another tool changed the mesh): the caller numbers by value instead.
bool SlotsFromExtras(const cgltf_data* data, const json::Value& ei, const Vertices& v, Record* r) {
    const std::vector<std::uint32_t> slots = Uints(AccessorAt(data, ei["slots"]));
    if (slots.size() != v.count * 3) return false;
    const int versions = v.versions;
    const std::size_t perPosition = static_cast<std::size_t>(versions) * 3;
    // The patches: their slots and values, checked for their sizes.
    struct Patch {
        std::vector<std::uint32_t> slots;
        std::vector<float> values;
    };
    const auto patchOf = [&](const char* key, std::size_t floats, Patch* patch) {
        const json::Value& value = ei[key];
        if (!value.Exists()) return true;
        patch->slots = Uints(AccessorAt(data, value["slots"]));
        patch->values = Floats(AccessorAt(data, value["values"]));
        return !patch->slots.empty() && patch->values.size() == patch->slots.size() * floats;
    };
    Patch positions, normals, texCoords;
    if (!patchOf("positions", perPosition, &positions) || !patchOf("normals", 4, &normals) ||
        !patchOf("texcoords", 2, &texCoords))
        return false;
    std::uint32_t slotCount[3] = {0, 0, 0};
    for (std::size_t i = 0; i < v.count; ++i)
        for (int a = 0; a < 3; ++a) slotCount[a] = std::max(slotCount[a], slots[i * 3 + a] + 1);
    for (const std::uint32_t s : positions.slots) slotCount[0] = std::max(slotCount[0], s + 1);
    for (const std::uint32_t s : normals.slots) slotCount[1] = std::max(slotCount[1], s + 1);
    for (const std::uint32_t s : texCoords.slots) slotCount[2] = std::max(slotCount[2], s + 1);
    if (slotCount[0] % 4 || (!v.normals.empty() && slotCount[1] % 4) || slotCount[0] > 0x10000 || slotCount[1] > 0x10000 ||
        slotCount[2] > 0x10000)
        return false;
    if (v.normals.empty() && !normals.slots.empty()) return false;
    const std::size_t normalSlots = v.normals.empty() ? 0 : slotCount[1];
    const std::size_t texCoordSlots = v.texCoords.empty() ? 0 : slotCount[2];
    r->positions.assign(slotCount[0] * perPosition, 0.0f);
    r->normals.assign(normalSlots * 4, 0.0f);
    r->texCoords.assign(texCoordSlots * 2, 0.0f);
    std::vector<bool> setPosition(slotCount[0]), setNormal(normalSlots), setTexCoord(texCoordSlots);
    // Every vertex of a slot gives it the same values.
    const auto give = [](std::vector<float>& to, std::vector<bool>& set, std::size_t slot, const float* from, std::size_t count,
                         std::size_t stride) {
        float* at = &to[slot * stride];
        if (set[slot]) return std::memcmp(at, from, count * sizeof(float)) == 0;
        std::memcpy(at, from, count * sizeof(float));
        set[slot] = true;
        return true;
    };
    r->corners.resize(v.count);
    for (std::size_t i = 0; i < v.count; ++i) {
        const std::uint32_t p = slots[i * 3], n = slots[i * 3 + 1], t = slots[i * 3 + 2];
        r->corners[i] = {static_cast<std::uint16_t>(p), static_cast<std::uint16_t>(n), static_cast<std::uint16_t>(t)};
        if (!give(r->positions, setPosition, p, &v.positions[i * perPosition], perPosition, perPosition)) return false;
        if (normalSlots) {
            const float normal[4] = {v.normals[i * 3], v.normals[i * 3 + 1], v.normals[i * 3 + 2], 1.0f};
            if (!give(r->normals, setNormal, n, normal, 4, 4)) return false;
        }
        if (texCoordSlots && !give(r->texCoords, setTexCoord, t, &v.texCoords[i * 2], 2, 2)) return false;
    }
    // A patch of a slot the corners give: its values the same (a position's but for the displacement's rounding, a
    // normal's w its own, a normal's the one its NORMAL was made of).
    for (std::size_t j = 0; j < positions.slots.size(); ++j) {
        const std::size_t s = positions.slots[j];
        const float* value = &positions.values[j * perPosition];
        float* at = &r->positions[s * perPosition];
        if (setPosition[s])
            for (std::size_t c = 0; c < perPosition; ++c)
                if (!WithinRounding(value[c], at[c], at[c % 3])) return false;
        std::memcpy(at, value, perPosition * sizeof(float));
    }
    for (std::size_t j = 0; j < normals.slots.size(); ++j) {
        const std::size_t s = normals.slots[j];
        const float* value = &normals.values[j * 4];
        const Vector3 given = VectorAt(r->normals, s * 4), patched{value[0], value[1], value[2]};
        if (setNormal[s] && !SameBits(given, patched) && !SameBits(given, GltfNormal(patched))) return false;
        std::memcpy(&r->normals[s * 4], value, 4 * sizeof(float));
    }
    for (std::size_t j = 0; j < texCoords.slots.size(); ++j) {
        const std::size_t s = texCoords.slots[j];
        if (setTexCoord[s]) return false;
        std::memcpy(&r->texCoords[s * 2], &texCoords.values[j * 2], 2 * sizeof(float));
    }
    return true;
}

// The record's slots numbered by value: each new position (all its versions), normal and texture coordinate in the
// order the vertices first have it; the positions padded to the packets' four lanes with copies of the first, the
// normals with copies of the last (as the shipped records are).
void SlotsByValue(const Vertices& v, Record* r) {
    const std::size_t perPosition = static_cast<std::size_t>(v.versions) * 3;
    std::unordered_map<std::string, std::uint16_t> positions, normals, texCoords;
    const auto slotOf = [](std::unordered_map<std::string, std::uint16_t>& map, std::vector<float>& to, const float* value,
                           std::size_t floats) {
        const std::string key(reinterpret_cast<const char*>(value), floats * sizeof(float));
        const auto [found, added] = map.try_emplace(key, static_cast<std::uint16_t>(map.size()));
        if (added) to.insert(to.end(), value, value + floats);
        return found->second;
    };
    r->positions.clear();
    r->normals.clear();
    r->texCoords.clear();
    r->corners.resize(v.count);
    for (std::size_t i = 0; i < v.count; ++i) {
        Record::Corner& c = r->corners[i];
        c.position = slotOf(positions, r->positions, &v.positions[i * perPosition], perPosition);
        if (!v.normals.empty()) {
            const float normal[4] = {v.normals[i * 3], v.normals[i * 3 + 1], v.normals[i * 3 + 2], 1.0f};
            c.normal = slotOf(normals, r->normals, normal, 4);
        }
        if (!v.texCoords.empty()) c.texCoord = slotOf(texCoords, r->texCoords, &v.texCoords[i * 2], 2);
    }
    const std::vector<float> first(r->positions.begin(), r->positions.begin() + static_cast<std::ptrdiff_t>(perPosition));
    while (r->positions.size() % (perPosition * 4)) r->positions.insert(r->positions.end(), first.begin(), first.end());
    if (!r->normals.empty()) {
        const std::vector<float> last(r->normals.end() - 4, r->normals.end());
        while (r->normals.size() % 16) r->normals.insert(r->normals.end(), last.begin(), last.end());
    }
}

// The bounds fitted to every slot's version k as ClipBounds::Fit fits them (the slots from the last): the centre
// midway between the extremes, the box around it, the radius the box's half diagonal.
void FitBounds(Record* r) {
    const std::size_t versions = static_cast<std::size_t>(r->versions);
    r->bounds.assign(kBoundsFloats * versions, 0.0f);
    for (std::size_t k = 0; k < versions; ++k) {
        Vector3 low = {FLT_MAX, FLT_MAX, FLT_MAX};
        Vector3 high = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
        for (std::size_t s = static_cast<std::size_t>(r->Slots()); s-- > 0;) {
            const Vector3 p = VectorAt(r->positions, (s * versions + k) * 3);
            high = MaxKeepB(high, p);
            low = MinKeepA(p, low);
        }
        const Vector3 centre = (high + low) * 0.5f;
        PutVector(&r->bounds, k * 3, centre);
        PutVector(&r->bounds, (versions + k) * 3, low - centre);
        PutVector(&r->bounds, (2 * versions + k) * 3, high - centre);
        r->bounds[9 * versions + k] = Length(high - centre);
    }
}

// The bounds ei-convert wrote (`ei.bounds`: every version's centre, min, max, radius); false when they are not all
// there.
bool BoundsFromExtras(const json::Value& bounds, Record* r) {
    const int versions = r->versions;
    std::vector<float> out(static_cast<std::size_t>(kBoundsFloats) * versions);
    const char* keys[3] = {"center", "min", "max"};
    for (int part = 0; part < 3; ++part) {
        const json::Value& list = bounds[keys[part]];
        if (list.Size() != versions) return false;
        for (int k = 0; k < versions; ++k) {
            if (list.At(k).Size() != 3) return false;
            for (int a = 0; a < 3; ++a)
                out[static_cast<std::size_t>(part * versions + k) * 3 + a] = json::FloatOf(list.At(k).At(a));
        }
    }
    const json::Value& radius = bounds["radius"];
    if (radius.Size() != versions) return false;
    for (int k = 0; k < versions; ++k) out[static_cast<std::size_t>(9 * versions + k)] = json::FloatOf(radius.At(k));
    r->bounds = std::move(out);
    return true;
}

// The material and texture kinds of a glTF material: its extras (ei-convert's), else its name ("wood 8", "25 6"),
// else the defaults.
void MaterialOf(const cgltf_data* data, const cgltf_material* material, Record* r) {
    r->materialValue = kDefaultMaterial;
    r->meshType = kDefaultMeshType;
    if (!material) return;
    const json::Value ei = Extras(data, material->extras)["ei"];
    if (ei["material"].IsNumber() && ei["kind"].IsNumber()) {
        r->materialValue = static_cast<std::uint32_t>(ei["material"].Int());
        r->meshType = static_cast<std::uint32_t>(ei["kind"].Int());
        return;
    }
    const std::string name = material->name ? material->name : "";
    const std::size_t space = name.find(' ');
    if (space == std::string::npos) return;
    const std::string first = name.substr(0, space), second = name.substr(space + 1);
    char* end = nullptr;
    const unsigned long kind = std::strtoul(second.c_str(), &end, 10);
    if (second.empty() || *end != '\0') return;
    for (const MaterialName& known : kMaterialNames)
        if (first == known.name) r->materialValue = known.value;
    if (!first.empty() && first.find_first_not_of("0123456789") == std::string::npos)
        r->materialValue = static_cast<std::uint32_t>(std::strtoul(first.c_str(), nullptr, 10));
    r->meshType = static_cast<std::uint32_t>(kind);
}

// A part's record from its glTF mesh.
bool RecordOf(const cgltf_data* data, const cgltf_mesh& mesh, Record* r, Notes* notes, const std::string& part) {
    const json::Value extras = Extras(data, mesh.extras);
    const json::Value& ei = extras["ei"];
    const std::string format = ei["format"].IsString() ? ei["format"].String() : "FIG8";
    r->old = format == "old";
    r->magic = format == "FIG" && ei["magic"].IsNumber() ? static_cast<std::uint32_t>(ei["magic"].Int()) : kFig8Magic;
    if (!r->old && !IsFig(r->magic)) r->magic = kFig8Magic;
    r->versions = r->old ? kOldVersions : r->magic == kFig8Magic ? kFig8Versions : 1;
    r->headerTail = static_cast<std::uint32_t>(ei["unused"].Int());
    Vertices v;
    if (!ReadVertices(mesh, r->versions, &v, notes, part)) return false;
    if (v.count > 0x10000) {
        if (notes) notes->lines.push_back(part + ": more than 65,536 vertices");
        return false;
    }
    MaterialOf(data, v.material, r);
    const bool exact = ei.IsObject() && SlotsFromExtras(data, ei, v, r);
    if (!exact) {
        if (ei.IsObject() && notes)
            notes->lines.push_back(part + ": its extras do not agree with its mesh; built from the glTF's data alone");
        SlotsByValue(v, r);
    }
    r->indices.clear();
    if (!(exact && ei["indices"].IsBool() && !ei["indices"].Bool()))
        for (const std::uint32_t i : v.indices) r->indices.push_back(static_cast<std::uint16_t>(i));
    r->words = DerivedWords(*r);
    if (exact && ei["words"].Exists())
        r->words = ei["words"].Int() < 0 ? std::vector<std::uint32_t>() : Uints(AccessorAt(data, ei["words"]));
    if (!(exact && BoundsFromExtras(ei["bounds"], r))) FitBounds(r);
    r->skipped = r->old ? ViewBytes(data, ei["skipped"]) : std::string();
    if (r->skipped.size() % kSkippedRecordBytes) r->skipped.clear();
    return true;
}

} // namespace

bool FromGltf(std::string_view bytes, const ReadUri& readUri, Figure* out, std::string* error, Notes* notes) {
    cgltf_options options{};
    options.file.read = ReadFile;
    options.file.release = ReleaseFile;
    options.file.user_data = const_cast<ReadUri*>(&readUri);
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse(&options, bytes.data(), bytes.size(), &data);
    // A buffer's file is looked up by its URI alone: the path "" leaves it as it is.
    if (result == cgltf_result_success) result = cgltf_load_buffers(&options, data, "");
    if (result == cgltf_result_success) result = cgltf_validate(data);
    if (result != cgltf_result_success) {
        cgltf_free(data);
        *error = ResultText(result);
        return false;
    }
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> held(data, cgltf_free);

    Figure figure;
    const cgltf_scene* scene = data->scene ? data->scene : data->scenes_count ? &data->scenes[0] : nullptr;
    const json::Value sceneEi = scene ? Extras(data, scene->extras)["ei"] : json::Value();
    figure.layout = sceneEi["layout"].IsString() && sceneEi["layout"].String() == "loose" ? Layout::Loose : Layout::Set;
    figure.link = !(sceneEi["link"].IsBool() && !sceneEi["link"].Bool());
    figure.bones = !(sceneEi["bones"].IsBool() && !sceneEi["bones"].Bool());
    figure.anm = sceneEi["anm"].IsBool() && sceneEi["anm"].Bool();

    // The nodes in the scene (their trees).
    std::vector<bool> inScene(data->nodes_count);
    const auto mark = [&](const cgltf_node* node, auto& self) -> void {
        inScene[cgltf_node_index(data, node)] = true;
        for (std::size_t c = 0; c < node->children_count; ++c) self(node->children[c], self);
    };
    if (scene)
        for (std::size_t i = 0; i < scene->nodes_count; ++i) mark(scene->nodes[i], mark);
    std::vector<json::Value> nodeEi(data->nodes_count);
    std::vector<bool> linked(data->nodes_count);
    for (std::size_t i = 0; i < data->nodes_count; ++i) {
        nodeEi[i] = Extras(data, data->nodes[i].extras)["ei"];
        linked[i] = inScene[i] && !(nodeEi[i]["linked"].IsBool() && !nodeEi[i]["linked"].Bool());
    }
    // The linked parts in the nodes' order, each after its parent (the link list names a parent first).
    std::vector<std::size_t> order;
    std::vector<bool> placed(data->nodes_count);
    const auto place = [&](std::size_t i, auto& self) -> void {
        if (placed[i]) return;
        placed[i] = true;
        const cgltf_node* parent = data->nodes[i].parent;
        if (linked[i] && parent && linked[cgltf_node_index(data, parent)]) self(cgltf_node_index(data, parent), self);
        order.push_back(i);
    };
    for (std::size_t i = 0; i < data->nodes_count; ++i)
        if (linked[i]) place(i, place);
    for (std::size_t i = 0; i < data->nodes_count; ++i)
        if (!linked[i]) order.push_back(i);

    for (const std::size_t i : order) {
        const cgltf_node& node = data->nodes[i];
        const json::Value& ei = nodeEi[i];
        Part& part = figure.parts.emplace_back();
        part.name = NameOf(node.name);
        part.linked = linked[i];
        if (part.linked && node.parent && linked[cgltf_node_index(data, node.parent)])
            part.parent = NameOf(node.parent->name);
        else if (part.linked && ei["parent"].IsString())
            part.parent = text::ToAssetText(ei["parent"].String());
        if (node.has_rotation || node.has_scale || node.has_matrix)
            if (notes) notes->lines.push_back(part.name + ": its node's rotation and scale are left out");
        // The bone: ei-convert's versions; a node without them (another tool's) has one of its translation.
        if (ei.IsObject()) {
            const json::Value& bone = ei["bone"];
            for (int k = 0; k < bone.Size(); ++k)
                for (int a = 0; a < 3; ++a) part.bone.push_back(json::FloatOf(bone.At(k).At(a)));
            if (part.bone.size() != kBonePrism / sizeof(float) && part.bone.size() != kBoneCube / sizeof(float))
                part.bone.clear();
        } else {
            const Vector3 offset =
                node.has_translation ? ToGameAxes({node.translation[0], node.translation[1], node.translation[2]}) : Vector3{};
            for (int k = 0; k < kFig8Versions; ++k) AppendVector(&part.bone, offset);
        }
        if (node.mesh) part.hasMesh = RecordOf(data, *node.mesh, &part.mesh, notes, part.name);
    }
    for (int i = 0; i < sceneEi["files"].Size(); ++i) {
        const json::Value& file = sceneEi["files"].At(i);
        RawFile raw;
        raw.name = text::ToAssetText(file["name"].String());
        const std::string& in = file["in"].String();
        raw.in = in == "mod"   ? RawFile::In::Mod
                 : in == "bon" ? RawFile::In::Bon
                 : in == "anm" ? RawFile::In::Anm
                               : RawFile::In::Folder;
        raw.bytes = ViewBytes(data, file["view"]);
        figure.raw.push_back(std::move(raw));
    }
    ReadAnimations(data, &figure.animations, notes);
    figure.anm = figure.anm || !figure.animations.empty();
    for (const RawFile& raw : figure.raw) figure.anm = figure.anm || raw.in == RawFile::In::Anm;
    *out = std::move(figure);
    return true;
}

} // namespace figure_gltf
