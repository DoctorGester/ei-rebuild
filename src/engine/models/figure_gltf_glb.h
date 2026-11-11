#pragma once

// The pieces figure_gltf.cpp (the meshes) and figure_gltf_animation.cpp (the animations) share: names as the archives
// look them up, the axes, the .glb's writer and the glTF reader's accessors and extras (docs/notes/mods.md, "Models as
// glTF"). Only those two include it.

#include "engine/core/asset_text.h"
#include "engine/models/figure_gltf.h"
#include "lib/json.h"
#include "lib/vecmath.h"

#include "cgltf/cgltf.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace figure_gltf {

// The vector of three floats at `first` of a flat array.
inline Vector3 VectorAt(const std::vector<float>& values, std::size_t first) {
    return {values[first], values[first + 1], values[first + 2]};
}

inline char Lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

// A name as the archives look it up: ASCII letters in lower case.
inline std::string Key(std::string_view name) {
    std::string key(name);
    for (char& c : key) c = Lower(c);
    return key;
}

inline bool SameName(std::string_view a, std::string_view b) { return Key(a) == Key(b); }

inline std::uint32_t Bits(float value) { return std::bit_cast<std::uint32_t>(value); }

// The game's axes to glTF's: the game's z is up and its x, y, z right-handed; glTF's y is up. (x, y, z) -> (x, z, -y),
// a rotation (no mirror: the triangles keep their winding). Blender's import turns glTF's y-up back to its own z-up,
// so it shows the game's coordinates as they are.
inline Vector3 ToGltfAxes(const Vector3& game) { return {game.x, game.z, -game.y}; }
inline Vector3 ToGameAxes(const Vector3& gltf) { return {gltf.x, -gltf.z, gltf.y}; }

inline std::string Num(std::int64_t value) { return std::to_string(value); }

inline std::string Floats(const float* values, std::size_t count) {
    std::string out = "[";
    for (std::size_t i = 0; i < count; ++i) out += (i ? "," : "") + json::Float(values[i]);
    return out + "]";
}
inline std::string Floats(const Vector3& v) { return "[" + json::Float(v.x) + "," + json::Float(v.y) + "," + json::Float(v.z) + "]"; }

// The arrays of a .glb's binary chunk, each its own buffer view, and their accessors' JSON.
class GlbWriter {
public:
    enum class Component { UnsignedShort = 5123, UnsignedInt = 5125, Float = 5126 };
    enum class Target { None = 0, Vertices = 34962, Indices = 34963 };

    // A buffer view of the bytes; its index.
    int View(const void* data, std::size_t bytes, Target target) {
        while (bin.size() % 4) bin.push_back('\0');
        std::string view = "{\"buffer\":0,\"byteOffset\":" + Num(static_cast<std::int64_t>(bin.size())) +
                           ",\"byteLength\":" + Num(static_cast<std::int64_t>(bytes));
        if (target != Target::None) view += ",\"target\":" + Num(static_cast<int>(target));
        views.push_back(view + "}");
        bin.append(static_cast<const char*>(data), bytes);
        return static_cast<int>(views.size()) - 1;
    }
    // An accessor of `count` elements of `type` (SCALAR, VEC2, ...) in a new view of `data` (none: all zero);
    // `bounds` the min and max JSON when it has them. Its index.
    int Accessor(const void* data, std::size_t bytes, Component component, std::size_t count, const char* type,
                 Target target, const std::string& bounds = {}) {
        std::string accessor = "{";
        if (data) accessor += "\"bufferView\":" + Num(View(data, bytes, target)) + ",";
        accessor += "\"componentType\":" + Num(static_cast<int>(component)) + ",\"count\":" + Num(static_cast<std::int64_t>(count)) +
                    ",\"type\":\"" + type + "\"" + bounds + "}";
        accessors.push_back(accessor);
        return static_cast<int>(accessors.size()) - 1;
    }
    // VEC3 floats with their min and max (a POSITION's must have them), all zero ones without a view.
    int Vec3(const std::vector<float>& values, Target target) {
        Vector3 low, high;
        for (std::size_t i = 0; i + 3 <= values.size(); i += 3) {
            const Vector3 v = VectorAt(values, i);
            low = i == 0 ? v : Min(low, v);
            high = i == 0 ? v : Max(high, v);
        }
        const bool zero = std::all_of(values.begin(), values.end(), [](float v) { return Bits(v) == 0; });
        const std::string bounds = ",\"min\":" + Floats(low) + ",\"max\":" + Floats(high);
        return Accessor(zero ? nullptr : values.data(), values.size() * sizeof(float), Component::Float, values.size() / 3,
                        "VEC3", target, bounds);
    }

    std::string bin;
    std::vector<std::string> views, accessors;
    std::map<std::size_t, int> keyTimes;   // the animations' key times' accessors by their count (shared)
};

inline std::string Join(const std::vector<std::string>& items) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) out += (i ? "," : "") + items[i];
    return out;
}

// An object's extras parsed (a missing value when it has none or they do not parse).
inline json::Value Extras(const cgltf_data* data, const cgltf_extras& extras) {
    std::string_view text;
    if (extras.data) text = extras.data;
    else if (extras.end_offset > extras.start_offset && data->json && extras.end_offset <= data->json_size)
        text = std::string_view(data->json + extras.start_offset, extras.end_offset - extras.start_offset);
    json::Value value;
    std::string error;
    if (text.empty() || !json::Parse(text, &value, &error)) return json::Value();
    return value;
}

inline std::string NameOf(const char* name) { return name ? text::ToAssetText(name) : std::string(); }

// An accessor's floats (its count times its components), or none.
inline std::vector<float> Floats(const cgltf_accessor* accessor) {
    std::vector<float> out;
    if (!accessor) return out;
    out.resize(cgltf_accessor_unpack_floats(accessor, nullptr, 0));
    if (cgltf_accessor_unpack_floats(accessor, out.data(), out.size()) != out.size()) out.clear();
    return out;
}

// An accessor's integers (its count times its components), or none.
inline std::vector<std::uint32_t> Uints(const cgltf_accessor* accessor) {
    std::vector<std::uint32_t> out;
    if (!accessor || accessor->is_sparse) return out;
    const std::size_t components = cgltf_num_components(accessor->type);
    out.resize(accessor->count * components);
    for (std::size_t i = 0; i < accessor->count; ++i)
        if (!cgltf_accessor_read_uint(accessor, i, &out[i * components], components)) return {};
    return out;
}

// The accessor an extras value names by index, or null.
inline const cgltf_accessor* AccessorAt(const cgltf_data* data, const json::Value& index) {
    if (!index.IsNumber() || index.Number() < 0 || index.Number() >= static_cast<double>(data->accessors_count))
        return nullptr;
    return &data->accessors[static_cast<std::size_t>(index.Int())];
}

// ---- The animations (figure_gltf_animation.cpp) -------------------------------------------------------------------

// `animation` as a glTF animation's JSON, its accessors added to `glb`; the nodes are `parts` (their index), each
// node's parent node in `parentOf` (-1: a root). Empty when it would have no channel (the caller carries it as it is).
std::string AnimationJson(const Animation& animation, const std::vector<Part>& parts, const std::vector<int>& parentOf,
                          GlbWriter& glb);
// The glTF's animations, each made into the tracks the game reads, appended to `animations`.
void ReadAnimations(const cgltf_data* data, std::vector<Animation>* animations, Notes* notes);

} // namespace figure_gltf
