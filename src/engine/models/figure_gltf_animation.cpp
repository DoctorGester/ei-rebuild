// Figure animations as glTF animations: figure_gltf.h and docs/notes/mods.md, "Animations as glTF" (the layout, the
// keys' times, the rotations turned into the nodes' own, the extras).
#include "engine/models/figure_gltf.h"
#include "engine/models/figure_gltf_glb.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <unordered_map>

namespace figure_gltf {

namespace {

// A key per world tick: an animation's time is the world's, a tick of 55 ms at the game's normal speed
// (app::g_updatePeriod).
constexpr double kSecondsPerKey = 0.055;
// A glTF key's time this close to a tick is that tick's key.
constexpr float kTimeSlack = 0.0001f;
// The most keys a track made from a glTF channel gets (a channel's last time past them is cut there).
constexpr int kMostKeys = 100000;
// How far a channel's value may be from the one the extras' keys make (each component) for the extras to agree.
constexpr float kAgreement = 0.00001f;
// How far a rotation's length may be from 1 in a glTF's rotation (the Khronos validator allows 0.00674).
constexpr float kUnitSlack = 0.005f;

constexpr Quaternion kIdentity{1.0f, 0.0f, 0.0f, 0.0f};

float KeyTime(std::size_t key) { return static_cast<float>(static_cast<double>(key) * kSecondsPerKey); }

Quaternion KeyAt(const std::vector<float>& rotations, std::size_t key) {
    const float* q = &rotations[key * 4];
    return {q[0], q[1], q[2], q[3]};
}

Quaternion Conjugate(const Quaternion& q) { return {q.w, -q.x, -q.y, -q.z}; }

// A rotation as glTF holds it: (x, y, z, w), the vector part in glTF's axes (turned as a vector is: the axes' change is
// a rotation), of unit length (a key that is not is normalised; one of no length is the identity).
void PutGltfRotation(std::vector<float>* out, Quaternion q) {
    const float length = std::sqrt(Dot(q, q));
    if (!(std::fabs(length - 1.0f) <= kUnitSlack))
        q = std::isfinite(length) && length > 0.0f ? Quaternion{q.w / length, q.x / length, q.y / length, q.z / length}
                                                    : kIdentity;
    const Vector3 v = ToGltfAxes({q.x, q.y, q.z});
    out->insert(out->end(), {v.x, v.y, v.z, q.w});
}

Quaternion GameRotation(const float* gltf) {
    const Vector3 v = ToGameAxes({gltf[0], gltf[1], gltf[2]});
    return {gltf[3], v.x, v.y, v.z};
}

// The figure-frame rotations of an animation's parts at each key, as FigureBone::Place makes them: a root's is its key,
// a child's its key times its parent's (the child turned by its own key after its parent's turn, not before it as a
// glTF node is); past a track's last key its last, a part without keys the identity. A node's keys are set before any
// child's rotation is asked for.
class Pose {
public:
    explicit Pose(const std::vector<int>& parentOf)
        : parentOf(parentOf), keys(parentOf.size(), nullptr), made(parentOf.size()), known(parentOf.size()) {}

    void SetKeys(int node, const std::vector<float>& rotations) {
        keys[static_cast<std::size_t>(node)] = &rotations;
        made[static_cast<std::size_t>(node)].clear();
        known[static_cast<std::size_t>(node)].clear();
    }

    // The rotation of `node` (-1: none, the identity) at `key`.
    Quaternion At(int node, std::size_t key) {
        if (node < 0) return kIdentity;
        const std::size_t n = static_cast<std::size_t>(node);
        if (key < known[n].size() && known[n][key]) return made[n][key];
        const Quaternion parent = At(parentOf[n], key);
        const std::vector<float>* own = keys[n];
        const Quaternion turn = own && !own->empty() ? KeyAt(*own, std::min(key, own->size() / 4 - 1)) : kIdentity;
        if (key >= known[n].size()) {
            made[n].resize(key + 1);
            known[n].resize(key + 1);
        }
        made[n][key] = turn * parent;
        known[n][key] = true;
        return made[n][key];
    }

    // A node's own rotation as glTF has it from the game's key `turn` at `key`, and back.
    Quaternion Local(int node, std::size_t key, const Quaternion& turn) {
        const Quaternion parent = At(parentOf[static_cast<std::size_t>(node)], key);
        return Conjugate(parent) * turn * parent;
    }
    Quaternion Turn(int node, std::size_t key, const Quaternion& local) {
        const Quaternion parent = At(parentOf[static_cast<std::size_t>(node)], key);
        return parent * local * Conjugate(parent);
    }

private:
    const std::vector<int>& parentOf;
    std::vector<const std::vector<float>*> keys;
    std::vector<std::vector<Quaternion>> made;
    std::vector<std::vector<bool>> known;
};

} // namespace

// ---- Writing ------------------------------------------------------------------------------------------------------

std::string AnimationJson(const Animation& animation, const std::vector<Part>& parts, const std::vector<int>& parentOf,
                          GlbWriter& glb) {
    std::unordered_map<std::string, int> nodeByKey;
    for (std::size_t p = 0; p < parts.size(); ++p) nodeByKey.try_emplace(Key(parts[p].name), static_cast<int>(p));
    std::vector<int> nodeOf;
    Pose pose(parentOf);
    for (const Track& t : animation.tracks) {
        const auto found = nodeByKey.find(Key(t.name));
        nodeOf.push_back(found == nodeByKey.end() ? -1 : found->second);
        if (nodeOf.back() >= 0) pose.SetKeys(nodeOf.back(), t.rotations);
    }

    std::vector<std::string> channels, samplers, tracks;
    std::vector<float> rawRotations, rawPositions, rawVertices;
    const auto times = [&](std::size_t count) {
        if (const auto found = glb.keyTimes.find(count); found != glb.keyTimes.end()) return found->second;
        std::vector<float> keyTimes(count);
        for (std::size_t k = 0; k < count; ++k) keyTimes[k] = KeyTime(k);
        const int accessor = glb.Accessor(keyTimes.data(), count * sizeof(float), GlbWriter::Component::Float, count, "SCALAR",
                                          GlbWriter::Target::None,
                                          ",\"min\":[" + json::Float(keyTimes.front()) + "],\"max\":[" + json::Float(keyTimes.back()) + "]");
        glb.keyTimes.emplace(count, accessor);
        return accessor;
    };
    const auto channel = [&](int node, const char* path, const std::vector<float>& values, const char* type, std::size_t count) {
        const int output = glb.Accessor(values.data(), values.size() * sizeof(float), GlbWriter::Component::Float, count, type,
                                        GlbWriter::Target::None);
        samplers.push_back("{\"input\":" + Num(times(count)) + ",\"output\":" + Num(output) + ",\"interpolation\":\"LINEAR\"}");
        channels.push_back("{\"sampler\":" + Num(static_cast<std::int64_t>(samplers.size()) - 1) + ",\"target\":{\"node\":" +
                           Num(node) + ",\"path\":\"" + path + "\"}}");
    };
    for (std::size_t i = 0; i < animation.tracks.size(); ++i) {
        const Track& t = animation.tracks[i];
        const int node = nodeOf[i];
        const std::size_t rotationKeys = t.rotations.size() / 4, positionKeys = t.positions.size() / 3;
        if (node >= 0 && rotationKeys) {
            std::vector<float> local;
            for (std::size_t k = 0; k < rotationKeys; ++k) PutGltfRotation(&local, pose.Local(node, k, KeyAt(t.rotations, k)));
            channel(node, "rotation", local, "VEC4", rotationKeys);
        }
        // A root's position places the figure; a child's the game never reads (its bone places it).
        if (node >= 0 && parentOf[static_cast<std::size_t>(node)] < 0 && positionKeys) {
            std::vector<float> moved;
            for (std::size_t k = 0; k < positionKeys; ++k) {
                const Vector3 v = ToGltfAxes(VectorAt(t.positions, k * 3));
                moved.insert(moved.end(), {v.x, v.y, v.z});
            }
            channel(node, "translation", moved, "VEC3", positionKeys);
        }
        rawRotations.insert(rawRotations.end(), t.rotations.begin(), t.rotations.end());
        rawPositions.insert(rawPositions.end(), t.positions.begin(), t.positions.end());
        rawVertices.insert(rawVertices.end(), t.vertices.begin(), t.vertices.end());
        std::string track = "{\"name\":" + json::String(text::FromAssetText(t.name));
        if (rotationKeys) track += ",\"rotations\":" + Num(static_cast<std::int64_t>(rotationKeys));
        if (positionKeys) track += ",\"positions\":" + Num(static_cast<std::int64_t>(positionKeys));
        if (t.frames || t.slots) track += ",\"frames\":" + Num(t.frames) + ",\"slots\":" + Num(t.slots);
        tracks.push_back(track + "}");
    }
    if (channels.empty()) return {};

    std::string ei = "\"tracks\":[" + Join(tracks) + "]";
    const auto raw = [&](const char* key, const std::vector<float>& values, const char* type, std::size_t floats) {
        if (values.empty()) return;
        ei += std::string(",\"") + key + "\":" +
              Num(glb.Accessor(values.data(), values.size() * sizeof(float), GlbWriter::Component::Float, values.size() / floats,
                               type, GlbWriter::Target::None));
    };
    raw("rotations", rawRotations, "VEC4", 4);
    raw("positions", rawPositions, "VEC3", 3);
    raw("vertices", rawVertices, "VEC3", 3);
    return "{\"name\":" + json::String(text::FromAssetText(animation.name)) + ",\"channels\":[" + Join(channels) +
           "],\"samplers\":[" + Join(samplers) + "],\"extras\":{\"ei\":{" + ei + "}}}";
}

// ---- Reading ------------------------------------------------------------------------------------------------------

namespace {

// A sampler's keys: their times and values (a CUBICSPLINE's three a key: in-tangent, value, out-tangent).
struct Keys {
    std::vector<float> times, values;
    int components = 0;
    cgltf_interpolation_type interpolation = cgltf_interpolation_type_linear;

    std::size_t PerKey() const { return interpolation == cgltf_interpolation_type_cubic_spline ? 3 : 1; }
    const float* Value(std::size_t key) const {
        return &values[(key * PerKey() + (PerKey() == 3 ? 1 : 0)) * static_cast<std::size_t>(components)];
    }
};

// False when the sampler's accessors do not hold keys of `components` floats at times that rise.
bool KeysOf(const cgltf_animation_sampler& sampler, int components, Keys* keys) {
    keys->times = Floats(sampler.input);
    keys->values = Floats(sampler.output);
    keys->components = components;
    keys->interpolation = sampler.interpolation;
    if (keys->times.empty() || keys->values.size() != keys->times.size() * keys->PerKey() * static_cast<std::size_t>(components))
        return false;
    for (std::size_t k = 1; k < keys->times.size(); ++k)
        if (!(keys->times[k] > keys->times[k - 1])) return false;
    return true;
}

// A channel's value (its first `components` floats): glTF's (x, y, z, w) for a rotation.
using Value = std::array<float, 4>;

Value ValueOf(const float* from, std::size_t components) {
    Value value{};
    std::copy_n(from, components, value.begin());
    return value;
}

// The channel's value at `time` as glTF interpolates it (a rotation's linear one the game's Slerp); before the first key
// the first, after the last the last; a key's own value at its time.
Value ValueAt(const Keys& keys, float time, bool rotation) {
    const std::size_t c = static_cast<std::size_t>(keys.components);
    const auto after = std::upper_bound(keys.times.begin(), keys.times.end(), time + kTimeSlack);
    if (after == keys.times.begin()) return ValueOf(keys.Value(0), c);
    const std::size_t i = static_cast<std::size_t>(after - keys.times.begin()) - 1;
    if (i + 1 == keys.times.size() || std::fabs(time - keys.times[i]) <= kTimeSlack ||
        keys.interpolation == cgltf_interpolation_type_step)
        return ValueOf(keys.Value(i), c);
    const float span = keys.times[i + 1] - keys.times[i];
    const float f = (time - keys.times[i]) / span;
    const float* a = keys.Value(i);
    const float* b = keys.Value(i + 1);
    Value out{};
    if (keys.interpolation == cgltf_interpolation_type_cubic_spline) {
        // Hermite's basis: the two values, key i's out-tangent and key i + 1's in-tangent (glTF's tangents are per second).
        const float* leave = a + c;
        const float* arrive = b - c;
        const float f2 = f * f, f3 = f2 * f;
        const float fromValue = 2 * f3 - 3 * f2 + 1, fromTangent = (f3 - 2 * f2 + f) * span;
        const float toValue = 3 * f2 - 2 * f3, toTangent = (f3 - f2) * span;
        for (std::size_t j = 0; j < c; ++j)
            out[j] = fromValue * a[j] + fromTangent * leave[j] + toValue * b[j] + toTangent * arrive[j];
        if (rotation) {
            const Quaternion q{out[3], out[0], out[1], out[2]};
            const float length = std::sqrt(Dot(q, q));
            if (length > 0.0f)
                for (float& component : out) component /= length;
        }
        return out;
    }
    if (rotation) {
        Quaternion q;
        q.Slerp(f, {a[3], a[0], a[1], a[2]}, {b[3], b[0], b[1], b[2]});
        return {q.x, q.y, q.z, q.w};
    }
    for (std::size_t j = 0; j < c; ++j) out[j] = a[j] + (b[j] - a[j]) * f;
    return out;
}

// The keys a track made from a channel has: one a tick up to the channel's last time.
std::size_t KeyCount(const Keys& keys) {
    const double ticks = std::round(static_cast<double>(keys.times.back()) / kSecondsPerKey);
    return static_cast<std::size_t>(std::clamp(ticks, 0.0, static_cast<double>(kMostKeys - 1))) + 1;
}

// Whether the channel's keys are the extras' (`count` of them) at one a tick.
bool OnTicks(const Keys& keys, std::size_t count) {
    if (keys.times.size() != count) return false;
    for (std::size_t k = 0; k < count; ++k)
        if (!(std::fabs(keys.times[k] - KeyTime(k)) <= kTimeSlack)) return false;
    return true;
}

bool Near(const float* a, const float* b, std::size_t count, float sign) {
    for (std::size_t j = 0; j < count; ++j)
        if (!(std::fabs(a[j] - sign * b[j]) <= kAgreement)) return false;
    return true;
}

// Whether a rotation channel is the one the extras' keys `turns` make (a key or its negation: the same rotation).
bool RotationsAgree(const Keys& keys, const std::vector<float>& turns, Pose& pose, int node) {
    if (!OnTicks(keys, turns.size() / 4)) return false;
    std::vector<float> local;
    for (std::size_t k = 0; k < turns.size() / 4; ++k) {
        local.clear();
        PutGltfRotation(&local, pose.Local(node, k, KeyAt(turns, k)));
        if (!Near(keys.Value(k), local.data(), 4, 1.0f) && !Near(keys.Value(k), local.data(), 4, -1.0f)) return false;
    }
    return true;
}

bool PositionsAgree(const Keys& keys, const std::vector<float>& positions) {
    if (!OnTicks(keys, positions.size() / 3)) return false;
    for (std::size_t k = 0; k < positions.size() / 3; ++k) {
        const Vector3 v = ToGltfAxes(VectorAt(positions, k * 3));
        const Value expected{v.x, v.y, v.z, 0.0f};
        if (!Near(keys.Value(k), expected.data(), 3, 1.0f)) return false;
    }
    return true;
}

// A track's keys made from a rotation channel: its rotation at each tick, turned back into the game's key.
std::vector<float> SampledTurns(const Keys& keys, Pose& pose, int node) {
    std::vector<float> turns;
    for (std::size_t k = 0, n = KeyCount(keys); k < n; ++k) {
        const Value local = ValueAt(keys, KeyTime(k), true);
        const Quaternion q = pose.Turn(node, k, GameRotation(local.data()));
        turns.insert(turns.end(), {q.w, q.x, q.y, q.z});
    }
    return turns;
}

std::vector<float> SampledPositions(const Keys& keys) {
    std::vector<float> positions;
    for (std::size_t k = 0, n = KeyCount(keys); k < n; ++k) {
        const Value value = ValueAt(keys, KeyTime(k), false);
        const Vector3 v = ToGameAxes({value[0], value[1], value[2]});
        positions.insert(positions.end(), {v.x, v.y, v.z});
    }
    return positions;
}

// A track being made: its node (-1: none) and its keys from the extras (empty without them).
struct PlannedTrack {
    Track track;
    int node = -1;
    std::vector<float> rotations, positions;
};

// The extras' tracks (`ei.tracks` with the keys of all in the accessors `ei.rotations`, `ei.positions` and
// `ei.vertices`, one track's after another); false when they do not add up.
bool TracksFromExtras(const cgltf_data* data, const json::Value& ei, const std::unordered_map<std::string, int>& nodeByKey,
                      std::vector<PlannedTrack>* planned) {
    const std::vector<float> rotations = Floats(AccessorAt(data, ei["rotations"]));
    const std::vector<float> positions = Floats(AccessorAt(data, ei["positions"]));
    const std::vector<float> vertices = Floats(AccessorAt(data, ei["vertices"]));
    std::size_t r = 0, p = 0, v = 0;
    const json::Value& list = ei["tracks"];
    for (int i = 0; i < list.Size(); ++i) {
        const json::Value& entry = list.At(i);
        const std::int64_t counts[4] = {entry["rotations"].Int(), entry["positions"].Int(), entry["frames"].Int(),
                                        entry["slots"].Int()};
        if (!entry["name"].IsString() || std::any_of(std::begin(counts), std::end(counts), [](std::int64_t n) { return n < 0 || n > kMostKeys * 1000LL; }))
            return false;
        const std::size_t rotationFloats = static_cast<std::size_t>(counts[0]) * 4, positionFloats = static_cast<std::size_t>(counts[1]) * 3,
                          vertexFloats = static_cast<std::size_t>(counts[2]) * static_cast<std::size_t>(counts[3]) * 3;
        if (rotationFloats > rotations.size() - r || positionFloats > positions.size() - p || vertexFloats > vertices.size() - v)
            return false;
        PlannedTrack& t = planned->emplace_back();
        t.track.name = text::ToAssetText(entry["name"].String());
        if (const auto found = nodeByKey.find(Key(t.track.name)); found != nodeByKey.end()) t.node = found->second;
        t.rotations.assign(rotations.begin() + static_cast<std::ptrdiff_t>(r), rotations.begin() + static_cast<std::ptrdiff_t>(r + rotationFloats));
        t.positions.assign(positions.begin() + static_cast<std::ptrdiff_t>(p), positions.begin() + static_cast<std::ptrdiff_t>(p + positionFloats));
        t.track.frames = static_cast<int>(counts[2]);
        t.track.slots = static_cast<int>(counts[3]);
        t.track.vertices.assign(vertices.begin() + static_cast<std::ptrdiff_t>(v), vertices.begin() + static_cast<std::ptrdiff_t>(v + vertexFloats));
        r += rotationFloats;
        p += positionFloats;
        v += vertexFloats;
    }
    return r == rotations.size() && p == positions.size() && v == vertices.size();
}

} // namespace

void ReadAnimations(const cgltf_data* data, std::vector<Animation>* animations, Notes* notes) {
    const std::size_t nodes = data->nodes_count;
    std::vector<int> parentOf(nodes, -1), depth(nodes, 0);
    std::unordered_map<std::string, int> nodeByKey;
    for (std::size_t i = 0; i < nodes; ++i) {
        const cgltf_node& node = data->nodes[i];
        if (node.parent) parentOf[i] = static_cast<int>(cgltf_node_index(data, node.parent));
        nodeByKey.try_emplace(Key(NameOf(node.name)), static_cast<int>(i));
    }
    for (std::size_t i = 0; i < nodes; ++i)
        for (int p = parentOf[i]; p >= 0 && depth[i] <= static_cast<int>(nodes); p = parentOf[static_cast<std::size_t>(p)]) ++depth[i];

    for (std::size_t a = 0; a < data->animations_count; ++a) {
        const cgltf_animation& source = data->animations[a];
        Animation animation;
        animation.name = NameOf(source.name);
        if (animation.name.empty()) {
            if (notes) notes->lines.push_back("an animation without a name is left out");
            continue;
        }
        // Each node's rotation and translation channels, the nodes in the channels' order.
        struct Channels {
            const cgltf_animation_sampler* rotation = nullptr;
            const cgltf_animation_sampler* translation = nullptr;
        };
        std::unordered_map<int, Channels> channels;
        std::vector<int> channelNodes;
        bool leftOut = false;
        for (std::size_t c = 0; c < source.channels_count; ++c) {
            const cgltf_animation_channel& channel = source.channels[c];
            if (!channel.target_node || !channel.sampler) continue;
            const int node = static_cast<int>(cgltf_node_index(data, channel.target_node));
            if (channel.target_path != cgltf_animation_path_type_rotation &&
                channel.target_path != cgltf_animation_path_type_translation) {
                leftOut = true;
                continue;
            }
            if (!channels.contains(node)) channelNodes.push_back(node);
            Channels& of = channels[node];
            (channel.target_path == cgltf_animation_path_type_rotation ? of.rotation : of.translation) = channel.sampler;
        }
        if (leftOut && notes)
            notes->lines.push_back("animation " + animation.name + ": its scale and morph weight channels are left out");

        std::vector<PlannedTrack> planned;
        const json::Value ei = Extras(data, source.extras)["ei"];
        if (ei.IsObject() && !TracksFromExtras(data, ei, nodeByKey, &planned)) {
            planned.clear();
            if (notes) notes->lines.push_back("animation " + animation.name + ": its extras do not add up; built from the glTF's data alone");
        }
        for (const int node : channelNodes) {
            const bool known = std::any_of(planned.begin(), planned.end(), [&](const PlannedTrack& t) { return t.node == node; });
            if (known) continue;
            PlannedTrack& t = planned.emplace_back();
            t.track.name = NameOf(data->nodes[static_cast<std::size_t>(node)].name);
            t.node = node;
        }

        // A parent's keys before its children's: a child's rotation is turned by its parent's.
        std::vector<std::size_t> order(planned.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::stable_sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) {
            const auto depthOf = [&](const PlannedTrack& t) { return t.node < 0 ? -1 : depth[static_cast<std::size_t>(t.node)]; };
            return depthOf(planned[x]) < depthOf(planned[y]);
        });
        Pose pose(parentOf);
        for (const std::size_t index : order) {
            PlannedTrack& t = planned[index];
            const auto found = t.node >= 0 ? channels.find(t.node) : channels.end();
            const Channels none;
            const Channels& of = found == channels.end() ? none : found->second;
            Keys keys;
            // The rotations: the extras' when they agree with the channel (or there is none), else the channel's.
            const bool rotationChannel = of.rotation && KeysOf(*of.rotation, 4, &keys);
            if (!t.rotations.empty() && (!rotationChannel || RotationsAgree(keys, t.rotations, pose, t.node)))
                t.track.rotations = t.rotations;
            else if (rotationChannel)
                t.track.rotations = SampledTurns(keys, pose, t.node);
            if (t.node >= 0) pose.SetKeys(t.node, t.track.rotations);
            // The positions likewise; a child's the extras' whenever they have them (the game reads a root's alone).
            const bool root = t.node >= 0 && parentOf[static_cast<std::size_t>(t.node)] < 0;
            const bool translationChannel = of.translation && KeysOf(*of.translation, 3, &keys);
            if (!t.positions.empty() && (!translationChannel || !root || PositionsAgree(keys, t.positions)))
                t.track.positions = t.positions;
            else if (translationChannel)
                t.track.positions = SampledPositions(keys);
        }
        for (PlannedTrack& t : planned) animation.tracks.push_back(std::move(t.track));
        if (!animation.tracks.empty()) animations->push_back(std::move(animation));
    }
}

} // namespace figure_gltf
