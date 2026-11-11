// The draw lists' order without addresses (world/object_order.h, DrawBefore). The harness sorts game.exe's draw
// lists the same way over its own views of game.exe's objects (tests/verify/harness_address_order.cpp): a
// change of the order here changes it there too.
#include "game/world/object_order.h"

#include "engine/core/engine_object.h"
#include "engine/models/link_figure.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "game/objects/map_object_shadow.h"
#include "engine/models/texture.h"


#include "lib/intro_sort.h"
#include "lib/str.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>

namespace {

// The first part set's texture of a drawn object's model, null without one.
const Texture* RankTexture(Object3DClientSpecific* model) {
    const TextureParts* rank = model->partSets[0].get();
    return rank ? rank->texture : nullptr;
}

// The texture's and the figure's order without their addresses: null first, then by name, a figure
// then by its morph weights (their bits); 0 for the same keys.
int CompareTextures(const Texture* a, const Texture* b) {
    if (!a || !b) return (a != nullptr) - (b != nullptr);
    return str::Compare(a->name, b->name);
}

int CompareFigures(const LinkMesh* a, const LinkMesh* b) {
    if (!a || !b) return (a != nullptr) - (b != nullptr);
    if (a->name < b->name) return -1;
    if (b->name < a->name) return 1;
    for (const auto [wa, wb] : {std::pair{a->morph.s, b->morph.s}, std::pair{a->morph.t, b->morph.t},
                                std::pair{a->morph.u, b->morph.u}}) {
        const auto bitsA = std::bit_cast<std::uint32_t>(wa);
        const auto bitsB = std::bit_cast<std::uint32_t>(wb);
        if (bitsA != bitsB) return bitsA < bitsB ? -1 : 1;
    }
    return 0;
}

bool DepthBefore(Object3D* a, Object3D* b) {
    return a->worldPosition.z < b->worldPosition.z;
}

} // namespace

bool object_order::DrawBefore(Object3D* a, Object3D* b) {
    if (!a || !b) return false;
    auto* groupA = a->ClientSpecific();
    auto* groupB = b->ClientSpecific();
    if (!groupA || !groupB) return false;
    if (int c = CompareTextures(RankTexture(groupA), RankTexture(groupB))) return c < 0;
    if (int c = CompareFigures(a->figure, b->figure)) return c < 0;
    return DepthBefore(a, b);
}

namespace {

// The ranks of the distinct keys of `keys` under `compare` (equal keys the same rank, null first as compare puts it):
// `ranks[i]` for `keys[i]`; false when there are more than 0xFFFF.
template <typename Key, typename Compare>
bool Ranks(const std::vector<const Key*>& keys, Compare compare, std::vector<std::uint32_t>& ranks) {
    static std::vector<const Key*> distinct;
    distinct.assign(keys.begin(), keys.end());
    std::sort(distinct.begin(), distinct.end(), std::less<const Key*>());
    distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
    static std::vector<const Key*> ordered;
    ordered.assign(distinct.begin(), distinct.end());
    std::sort(ordered.begin(), ordered.end(), [&](const Key* a, const Key* b) { return compare(a, b) < 0; });
    static std::vector<std::pair<const Key*, std::uint32_t>> byAddress;
    byAddress.clear();
    std::uint32_t rank = 0;
    for (std::size_t k = 0; k < ordered.size(); ++k) {
        if (k > 0 && compare(ordered[k - 1], ordered[k]) != 0) ++rank;
        byAddress.emplace_back(ordered[k], rank);
    }
    if (rank > 0xFFFF) return false;
    std::sort(byAddress.begin(), byAddress.end(), [](const auto& a, const auto& b) { return std::less<const Key*>()(a.first, b.first); });
    ranks.resize(keys.size());
    for (std::size_t i = 0; i < keys.size(); ++i) {
        auto at = std::lower_bound(byAddress.begin(), byAddress.end(), keys[i],
                                   [](const auto& e, const Key* key) { return std::less<const Key*>()(e.first, key); });
        ranks[i] = at->second;
    }
    return true;
}

// A view depth as an unsigned key in the same order as `<` (-0 as +0); false for NaN.
bool DepthKey(float z, std::uint32_t* key) {
    if (z != z) return false;
    if (z == 0.0f) z = 0.0f;
    const auto bits = std::bit_cast<std::uint32_t>(z);
    *key = (bits & 0x80000000u) ? ~bits : bits | 0x80000000u;
    return true;
}

struct Keyed {
    std::uint64_t key = 0;
    MapObject* object = nullptr;
};

} // namespace

void object_order::SortForDraw(std::vector<MapObject*>& objects) {
    const auto byComparator = [&objects] {
        lib::IntroSort(objects.data(), objects.data() + objects.size(),
                       [](MapObject* a, MapObject* b) { return DrawBefore(a, b); });
    };
    static std::vector<const Texture*> textures;
    static std::vector<const LinkMesh*> figures;
    static std::vector<std::uint32_t> depths, textureRanks, figureRanks;
    textures.clear();
    figures.clear();
    depths.clear();
    for (MapObject* object : objects) {
        Object3DClientSpecific* group = object ? object->ClientSpecific() : nullptr;
        std::uint32_t depth = 0;
        // DrawBefore is no order over these: its own sort keeps what it does with them.
        if (!group || !DepthKey(object->worldPosition.z, &depth)) return byComparator();
        textures.push_back(RankTexture(group));
        figures.push_back(object->figure);
        depths.push_back(depth);
    }
    if (!Ranks(textures, CompareTextures, textureRanks) || !Ranks(figures, CompareFigures, figureRanks))
        return byComparator();
    // The keys order the objects as DrawBefore does pair by pair: the same sort makes the same permutation.
    static std::vector<Keyed> keyed;
    keyed.clear();
    for (std::size_t i = 0; i < objects.size(); ++i)
        keyed.push_back({static_cast<std::uint64_t>(textureRanks[i]) << 48 | static_cast<std::uint64_t>(figureRanks[i]) << 32 |
                             depths[i],
                         objects[i]});
    lib::IntroSort(keyed.data(), keyed.data() + keyed.size(), [](const Keyed& a, const Keyed& b) { return a.key < b.key; });
    for (std::size_t i = 0; i < objects.size(); ++i) objects[i] = keyed[i].object;
}
