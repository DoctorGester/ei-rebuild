// The draw lists' order without addresses (address_order.h, DrawBefore). The harness sorts game.exe's draw
// lists the same way over its own views of game.exe's objects (tests/verify/harness_address_order.cpp): a
// change of the order here changes it there too.
#include "game/address_order.h"

#include "game/core/engine_object.h"
#include "game/link_figure.h"
#include "game/map_object.h"
#include "game/map_object_figures.h"
#include "game/map_object_shadow.h"
#include "game/texture.h"


#include <bit>
#include <cstdint>
#include <cstring>
#include <utility>

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
    const char* aName = a->name.c_str();
    const char* bName = b->name.c_str();
    return std::strcmp(aName ? aName : "", bName ? bName : "");
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
