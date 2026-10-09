// Checks for the fake unit scene helpers: the figure registry (scene_registry.cpp), the mesh
// corner tables (scene_meshes.cpp) and the skeleton's bones (scene_bones.cpp).
#include "verify.h"

#include "../../src/game/figure.h"
#include "../../src/game/scene_loader.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace scene {
int __fastcall LoadFigureRegistry(FakeUnitScene* unit, const mfc::CString* path, VtArray* links, VtArray* names);
}

namespace {

template <typename T>
T& At(void* base, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

// What the call changes: every mesh's texture coordinates and material fields, the records added to
// the two arrays.
struct Outcome {
    int result = 0;
    std::vector<std::vector<std::uint8_t>> texCoords;
    std::vector<std::uint32_t> material;
    std::vector<std::uint32_t> links;
    std::vector<std::pair<int, std::string>> names;
};

std::vector<std::vector<std::uint8_t>> TexCoords(char* unit) {
    std::vector<std::vector<std::uint8_t>> out;
    char* meshes = At<char*>(unit, 0x548);
    for (int m = 0; m < At<int>(unit, 0x54C); ++m) {
        char* mesh = meshes + m * scene::mesh::kSize;
        const auto* data = At<const std::uint8_t*>(mesh, scene::mesh::kTexCoords);
        int count = At<int>(mesh, scene::mesh::kTexCoords + 4);
        out.emplace_back(data, data + (data ? count * 8 : 0));
    }
    return out;
}

void RestoreTexCoords(char* unit, const std::vector<std::vector<std::uint8_t>>& saved) {
    char* meshes = At<char*>(unit, 0x548);
    for (int m = 0; m < At<int>(unit, 0x54C); ++m) {
        char* mesh = meshes + m * scene::mesh::kSize;
        if (!saved[m].empty()) std::memcpy(At<std::uint8_t*>(mesh, scene::mesh::kTexCoords), saved[m].data(), saved[m].size());
    }
}

Outcome Capture(int result, char* unit, scene::VtArray* links, int linksFrom, scene::VtArray* names, int namesFrom) {
    Outcome o;
    o.result = result;
    o.texCoords = TexCoords(unit);
    char* meshes = At<char*>(unit, 0x548);
    for (int m = 0; m < At<int>(unit, 0x54C); ++m) {
        o.material.push_back(At<std::uint32_t>(meshes + m * scene::mesh::kSize, 0x184));
        o.material.push_back(At<std::uint32_t>(meshes + m * scene::mesh::kSize, 0x188));
    }
    const auto* pairs = reinterpret_cast<const std::uint32_t*>(links->data);
    for (int i = linksFrom; i < links->size; ++i) {
        o.links.push_back(pairs[i * 2]);
        o.links.push_back(pairs[i * 2 + 1]);
    }
    const auto* records = reinterpret_cast<const char*>(names->data);
    for (int i = namesFrom; i < names->size; ++i) {
        o.names.emplace_back(At<int>(const_cast<char*>(records), i * 8),
                             static_cast<const char*>(At<mfc::CString>(const_cast<char*>(records), i * 8 + 4)));
    }
    return o;
}

} // namespace

// 0044B980: PUSH -1; PUSH 007136AF (7 bytes). The original runs first; its results are recorded,
// the texture coordinates restored and the two arrays cut back to their sizes (the original's new
// name records leak), then ours runs and everything is compared.
static int(__fastcall* s_registry)(char*, const mfc::CString*, scene::VtArray*, scene::VtArray*);
static int __fastcall LoadFigureRegistryVerify(char* unit, const mfc::CString* path, scene::VtArray* links,
                                               scene::VtArray* names) {
    auto saved = TexCoords(unit);
    int linksSize = links->size;
    int namesSize = names->size;
    Outcome expected = Capture(s_registry(unit, path, links, names), unit, links, linksSize, names, namesSize);
    RestoreTexCoords(unit, saved);
    links->size = linksSize;
    names->size = namesSize;
    Outcome actual = Capture(scene::LoadFigureRegistry(reinterpret_cast<scene::FakeUnitScene*>(unit), path, links, names), unit, links, linksSize, names,
                             namesSize);
    ei::verify::Check("LoadFigureRegistry.result", expected.result, actual.result, "");
    ei::verify::Check("LoadFigureRegistry.texCoords", true, expected.texCoords == actual.texCoords, "meshes %d",
                      static_cast<int>(saved.size()));
    ei::verify::Check("LoadFigureRegistry.material", true, expected.material == actual.material, "");
    ei::verify::Check("LoadFigureRegistry.links", true, expected.links == actual.links, "added %d",
                      static_cast<int>(expected.links.size() / 2));
    ei::verify::Check("LoadFigureRegistry.names", true, expected.names == actual.names, "added %d",
                      static_cast<int>(expected.names.size()));
    return actual.result;
}
EI_VERIFY(0x0044B980, LoadFigureRegistryVerify, 7, s_registry);

// --- 0044CFF0 ------------------------------------------------------------------------------------

namespace scene {
void __fastcall IndexMeshCorners(VtArray* meshes);
}

namespace {
// Every mesh's corner table (+0x108, 6 bytes each), pairs (+0x118, 4) and corner indices (+0x128, 2).
std::vector<std::vector<std::uint8_t>> MeshTables(scene::VtArray* meshes) {
    std::vector<std::vector<std::uint8_t>> out;
    for (int m = 0; m < meshes->size; ++m) {
        char* mesh = reinterpret_cast<char*>(meshes->data) + m * scene::mesh::kSize;
        const std::pair<std::size_t, int> arrays[] = {{0x108, 6}, {0x118, 4}, {0x128, 2}};
        for (const auto& [offset, element] : arrays) {
            const auto* data = At<const std::uint8_t*>(mesh, offset);
            int count = At<int>(mesh, offset + 4);
            out.emplace_back(data, data + (data ? count * element : 0));
        }
    }
    return out;
}
} // namespace

// 0044CFF0: PUSH -1; PUSH 0071374C (7 bytes). The tables are rebuilt from the faces alone, so the
// original runs first, then ours over its results; the tables are compared.
static void(__fastcall* s_corners)(scene::VtArray*);
static void __fastcall IndexMeshCornersVerify(scene::VtArray* meshes) {
    s_corners(meshes);
    auto expected = MeshTables(meshes);
    scene::IndexMeshCorners(meshes);
    auto actual = MeshTables(meshes);
    ei::verify::Check("IndexMeshCorners", true, expected == actual, "meshes %d", meshes->size);
}
EI_VERIFY(0x0044CFF0, IndexMeshCornersVerify, 7, s_corners);

// --- 004492F0 ------------------------------------------------------------------------------------

// 004492F0 adds the bone to the skin and builds its children (new bones): ours only. Checked
// after the whole subtree: the name, the frame count, every array's size against the node's
// counts, and the children's parent links.
static void* s_bone;
static void __fastcall BuildBoneVerify(scene::SceneSkin* skin, CAnimationNode* bone, const scene::SceneNode* node, int frames) {
    scene::BuildBone(skin, bone, node, frames);
    int morphKeys = node->morphKeys.size;
    bool ok = std::strcmp(bone->name_, node->name) == 0 && bone->frameCount_ == static_cast<short>(frames) &&
              bone->translationKeys_.size == node->rotationKeys.size &&
              bone->rotationKeys_.size == node->positionKeys.size && bone->normals_.size == node->normals.size &&
              bone->morphFrames_.size == (morphKeys ? node->positionKeys.size : 0) &&
              bone->children_.size == node->children.size;
    for (int i = 0; ok && i < bone->children_.size; ++i) ok = bone->children_.data[i]->parent_ == bone;
    ei::verify::Check("BuildBone", true, ok, "node %s keys %d/%d morph %d children %d", static_cast<const char*>(node->name),
                      node->positionKeys.size, node->rotationKeys.size, morphKeys, node->children.size);
}
EI_VERIFY(0x004492F0, BuildBoneVerify, 0, s_bone);
