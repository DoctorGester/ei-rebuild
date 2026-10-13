// FigureMeshDrawer::Animate (005A7160, A-W12): an animated figure mesh's texture offset per frame.
// The drawer's lifetime (A-W14): its constructor, destructor bodies and slots 0, 2 and 3.
// See link_figure.h.
#include "link_figure.h"

#include "engine_object.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <bit>
#include <cstdint>

namespace {

// The mesh types (CFigureMesh +0x15C) whose texture is a sheet of frames.
constexpr std::uint32_t kTwoRows = 13;     // 8 frames, 4 per row, two rows (v 0 / 0.5)
constexpr std::uint32_t kSheet4x4 = 14;    // 16 frames
constexpr std::uint32_t kSheet8x4 = 16;    // 32 frames

std::uint32_t Bits(float value) { return std::bit_cast<std::uint32_t>(value); }

// The drawer as the engine object it is.
CEngineObject* AsObject(FigureMeshDrawer* drawer) { return reinterpret_cast<CEngineObject*>(drawer); }

} // namespace

void FigureMeshDrawer::Animate(float time) {
    CFigureMesh* mesh = mesh_;
    const std::uint32_t type = mesh->unknown_15C;
    if (type == kTwoRows) {
        int frame = static_cast<int>(gamecrt::Ftol(time) & 7);
        if (frame < 4) {
            mesh->unknown_0CC = Bits(static_cast<float>(frame) * 0.25f);
            mesh_->unknown_0D0 = 0;
        } else {
            mesh->unknown_0CC = Bits(static_cast<float>(frame - 4) * 0.25f);
            mesh_->unknown_0D0 = Bits(0.5f);
        }
    } else if (type == kSheet4x4) {
        int frame = static_cast<int>(gamecrt::Ftol(time) & 15);
        mesh->unknown_0CC = Bits(static_cast<float>(frame % 4) * 0.25f);
        mesh_->unknown_0D0 = Bits(1.0f - static_cast<float>(frame / 4) * 0.25f);
    } else if (type == kSheet8x4) {
        int frame = static_cast<int>(gamecrt::Ftol(time) & 31);
        mesh->unknown_0CC = Bits(static_cast<float>(frame % 8) * 0.125f);
        mesh_->unknown_0D0 = Bits(1.0f - static_cast<float>(frame / 8) * 0.25f);
    }
}
EI_REPLACE_METHOD(0x005A7160, FigureMeshDrawer::Animate);

// ---- Lifetime (A-W14) ---------------------------------------------------------------------------

FigureMeshDrawer* FigureMeshDrawer::Construct() {   // 005A7010
    AsObject(this)->ClearFields();
    mesh_ = nullptr;
    ei::SetVtable(this, kVtableAddress);
    return this;
}
EI_REPLACE_METHOD(0x005A7010, FigureMeshDrawer::Construct);

void FigureMeshDrawer::Destruct() {   // 005A7080
    ei::SetVtable(this, kBaseVtableAddress);
    flags_ = 0;
    mesh_ = nullptr;
    AsObject(this)->DestructBase();
}
EI_REPLACE_METHOD(0x005A7080, FigureMeshDrawer::Destruct);

void FigureMeshDrawer::DestructBase() {   // 005A70C0
    ei::SetVtable(this, kBaseVtableAddress);
    mesh_ = nullptr;
    AsObject(this)->DestructBase();
}
EI_REPLACE_METHOD(0x005A70C0, FigureMeshDrawer::DestructBase);

FigureMeshDrawer* FigureMeshDrawer::DeletingDestruct(unsigned flags) {   // 005A7060
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x005A7060, FigureMeshDrawer::DeletingDestruct);

FigureMeshDrawer* FigureMeshDrawer::DeletingDestructBase(unsigned flags) {   // 005A70A0
    DestructBase();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x005A70A0, FigureMeshDrawer::DeletingDestructBase);

int FigureMeshDrawer::Create() {   // 005A70E0
    flags_ = 0;
    unknown_30 = Vector3{1.0f, 1.0f, 1.0f};
    return 1;
}
EI_REPLACE_METHOD(0x005A70E0, FigureMeshDrawer::Create);

void FigureMeshDrawer::Destroy() {   // 005A7130
    mesh_ = nullptr;
    flags_ = 0;
}
EI_REPLACE_METHOD(0x005A7130, FigureMeshDrawer::Destroy);
