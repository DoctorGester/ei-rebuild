// Check for the sprite draw (ui_mesh_draw.cpp): 005E6D90 CUiMesh::Draw. Ours only (it draws):
// the result is 1, the view and world stacks are back at their depths, the renderer's colour is
// the one it had, and a visible sprite's placement is its orientation's rotation (the original
// 004D9AA0 into a scratch matrix) with the position in the last column and 0, 0, 0, 1 below.
// Every scenario (every window's sprites, every frame).
#include "verify.h"

#include "../../src/game/model.h"
#include "../../src/game/originals_render.h"
#include "../../src/game/ui.h"

#include <cstring>
#include <vector>

namespace {
void* s_unused;

bool PlacementOk(const CUiMesh* self) {
    float expected[16];
    std::memcpy(expected, &self->placement_, sizeof(expected));
    QuaternionToMatrixOriginal(&self->orientation_, nullptr, expected);
    expected[3] = self->position_.x;
    expected[7] = self->position_.y;
    expected[11] = self->position_.z;
    expected[12] = 0.0f;
    expected[13] = 0.0f;
    expected[14] = 0.0f;
    expected[15] = 1.0f;
    return std::memcmp(expected, &self->placement_, sizeof(expected)) == 0;
}
} // namespace

static int __fastcall SpriteDrawVerify(CUiMesh* self, void*, CSceneRenderer* renderer) {
    CTransformPipeline* pipeline = renderer->pipeline_;
    const int view = pipeline->stacks_[2].count;
    const int world = pipeline->stacks_[1].count;
    const std::uint32_t colour = renderer->colour_;
    const int result = self->Draw(renderer);
    const bool ok = result == 1 && pipeline->stacks_[2].count == view && pipeline->stacks_[1].count == world &&
                    renderer->colour_ == colour && (!self->visible_ || PlacementOk(self));
    ei::verify::Check("CUiMesh::Draw", true, ok, "result %d stacks %d %d -> %d %d colour %08X -> %08X", result, view,
                      world, pipeline->stacks_[2].count, pipeline->stacks_[1].count, colour, renderer->colour_);
    return result;
}
EI_VERIFY(0x005E6D90, SpriteDrawVerify, 0, s_unused);

// 005E6C60 (the sprite's Create, vtable slot 2): ours only (the figure's Create runs first): the
// fields it sets. Every scenario (every sprite a window adds).
static int __fastcall SpriteCreateVerify(CUiMesh* self) {
    const int result = self->CreateSprite();
    const float* m = self->scaleMatrix_.m;
    bool identity = true;
    for (int i = 0; i < 16; ++i) identity = identity && m[i] == ((i % 5 == 0) ? 1.0f : 0.0f);
    const Quaternion& q = *reinterpret_cast<const Quaternion*>(0x007C7CD8);
    const bool ok = result == 1 && identity && !self->material_ && !self->texture_ && self->tint_[0] == 1.0f &&
                    self->tint_[2] == 1.0f && std::memcmp(&self->orientation_, &q, sizeof(q)) == 0 &&
                    self->position_.z == 10.0f && self->colour_ == 0xFF000000 && self->plainColour_ == 0xFF000000 &&
                    self->visible_ == 1;
    ei::verify::Check("CUiMesh::CreateSprite", true, ok, "");
    return result;
}
EI_VERIFY(0x005E6C60, SpriteCreateVerify, 0, s_unused);

// 005EA210 / 005EA600 / 005EA4C0 / 005EA380 (the arrays' sizes): ours only (they allocate): the
// size is `count` within the capacity, the elements before the old size kept (compared with a copy),
// the quads' z 0 and w 1, the vertices' +2 zero. Every scenario (the sprite builders, the minimap).
namespace {
template <typename T, typename Extra>
void CheckSetSize(const char* name, lib::AlignedArray<T>& array, int count, void (CUiMesh::*set)(int), CUiMesh* self,
                  Extra extra) {
    std::vector<T> before(array.data, array.data + (count < array.size ? count : array.size));
    (self->*set)(count);
    bool ok = array.size == count && array.capacity >= count && (count == 0 ? !array.data : array.data != nullptr);
    if (ok && !before.empty()) ok = std::memcmp(before.data(), array.data, before.size() * sizeof(T)) == 0 || !extra.keeps;
    for (int i = 0; ok && i < count; ++i) ok = extra(array.data[i]);
    ei::verify::Check(name, true, ok, "count %d size %d capacity %d", count, array.size, array.capacity);
}
struct NoExtra {
    bool keeps = true;
    template <typename T> bool operator()(const T&) const { return true; }
};
struct QuadExtra {
    bool keeps = false;   // z / w are rewritten in every block
    bool operator()(const UiQuadBlock& q) const {
        for (int k = 0; k < 4; ++k)
            if (q.z[k] != 0.0f || q.w[k] != 1.0f) return false;
        return true;
    }
};
struct VertexExtra {
    bool keeps = false;
    bool operator()(const UiMeshVertex& v) const { return v.normal == 0; }
};
} // namespace

static void __fastcall QuadCountVerify(CUiMesh* self, void*, int count) {
    CheckSetSize("CUiMesh::SetQuadCount", self->Quads(), count, &CUiMesh::SetQuadCount, self, QuadExtra{});
}
EI_VERIFY(0x005EA210, QuadCountVerify, 0, s_unused);
static void __fastcall VertexCountVerify(CUiMesh* self, void*, int count) {
    CheckSetSize("CUiMesh::SetVertexCount", self->Vertices(), count, &CUiMesh::SetVertexCount, self, VertexExtra{});
}
EI_VERIFY(0x005EA600, VertexCountVerify, 0, s_unused);
static void __fastcall IndexCountVerify(CUiMesh* self, void*, int count) {
    CheckSetSize("CUiMesh::SetIndexCount", self->Indices(), count, &CUiMesh::SetIndexCount, self, NoExtra{});
}
EI_VERIFY(0x005EA4C0, IndexCountVerify, 0, s_unused);
static void __fastcall UvCountVerify(CUiMesh* self, void*, int count) {
    CheckSetSize("CUiMesh::SetUvCount", self->Texels(), count, &CUiMesh::SetUvCount, self, NoExtra{});
}
EI_VERIFY(0x005EA380, UvCountVerify, 0, s_unused);
