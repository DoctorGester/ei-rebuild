// The draw lists' order without addresses (address_order.h, DrawBefore). The harness sorts game.exe's draw
// lists the same way over its own views of game.exe's objects (tests/verify/harness_address_order.cpp): a
// change of the order here changes it there too.
#include "address_order.h"

#include "engine_object.h"
#include "link_figure.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "texture.h"

#include "lib/nan_audit.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace {

CObject3D* Object3D(EngineObject* object) { return static_cast<CObject3D*>(object); }

// The first part set's texture of a drawn object's model, null without one.
const CTexture* RankTexture(Object3DClientSpecific* model) {
    const TextureParts* rank = model->partSets[0];
    return rank ? rank->texture : nullptr;
}

// The texture's and the figure's order without their addresses: null first, then by name, a figure
// then by its morph weights (their bits); 0 for the same keys.
int CompareTextures(const CTexture* a, const CTexture* b) {
    if (!a || !b) return (a != nullptr) - (b != nullptr);
    const char* aName = a->name.c_str();
    const char* bName = b->name.c_str();
    return std::strcmp(aName ? aName : "", bName ? bName : "");
}

int CompareFigures(const LinkMesh* a, const LinkMesh* b) {
    if (!a || !b) return (a != nullptr) - (b != nullptr);
    if (a->name < b->name) return -1;
    if (b->name < a->name) return 1;
    for (int i = 0; i < 3; ++i) {
        const auto wa = std::bit_cast<std::uint32_t>(a->morphWeights[i]);
        const auto wb = std::bit_cast<std::uint32_t>(b->morphWeights[i]);
        if (wa != wb) return wa < wb ? -1 : 1;
    }
    return 0;
}

bool DepthBefore(EngineObject* a, EngineObject* b) {
    EI_NAN_AUDIT(Object3D(a)->worldPosition.z, Object3D(b)->worldPosition.z);
    return Object3D(a)->worldPosition.z < Object3D(b)->worldPosition.z;
}

} // namespace

bool address_order::DrawBefore(EngineObject* a, EngineObject* b) {
    if (!a || !b) return false;
    auto* groupA = Object3D(a)->ClientSpecific();
    auto* groupB = Object3D(b)->ClientSpecific();
    if (!groupA || !groupB) return false;
    if (int c = CompareTextures(RankTexture(groupA), RankTexture(groupB))) return c < 0;
    if (int c = CompareFigures(Object3D(a)->figure, Object3D(b)->figure)) return c < 0;
    return DepthBefore(a, b);
}
