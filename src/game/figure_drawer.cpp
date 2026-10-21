// FigureMeshDrawer::Animate (005A7160, A-W12): an animated figure mesh's texture offset per frame.
// The drawer's lifetime (A-W14): its constructor, destructor bodies and slots 0, 2 and 3.
// See link_figure.h.
#include "link_figure.h"

#include "engine_object.h"

#include "world_class_slots.h"   // FigureClientSpecificClassName / ClassId

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstdint>
#include <new>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {

// The mesh types (CFigureMesh +0x15C) whose texture is a sheet of frames.
constexpr std::uint32_t kTwoRows = 13;     // 8 frames, 4 per row, two rows (v 0 / 0.5)
constexpr std::uint32_t kSheet4x4 = 14;    // 16 frames
constexpr std::uint32_t kSheet8x4 = 16;    // 32 frames

} // namespace

void FigureMeshDrawer::Animate(float time) {
    CFigureMesh* mesh = mesh_;
    const std::uint32_t type = mesh->meshType_;
    if (type == kTwoRows) {
        int frame = static_cast<int>(gamecrt::Ftol(time) & 7);
        if (frame < 4) {
            mesh->textureOffsetU_ = static_cast<float>(frame) * 0.25f;
            mesh_->textureOffsetV_ = 0.0f;
        } else {
            mesh->textureOffsetU_ = static_cast<float>(frame - 4) * 0.25f;
            mesh_->textureOffsetV_ = 0.5f;
        }
    } else if (type == kSheet4x4) {
        int frame = static_cast<int>(gamecrt::Ftol(time) & 15);
        mesh->textureOffsetU_ = static_cast<float>(frame % 4) * 0.25f;
        mesh_->textureOffsetV_ = 1.0f - static_cast<float>(frame / 4) * 0.25f;
    } else if (type == kSheet8x4) {
        int frame = static_cast<int>(gamecrt::Ftol(time) & 31);
        mesh->textureOffsetU_ = static_cast<float>(frame % 8) * 0.125f;
        mesh_->textureOffsetV_ = 1.0f - static_cast<float>(frame / 8) * 0.25f;
    }
}
EI_REWRITTEN(0x005A7160, FigureMeshDrawer::Animate);

// ---- Lifetime (A-W14) ---------------------------------------------------------------------------

FigureDrawerBase::FigureDrawerBase() {
    ClearFields();
    mesh_ = nullptr;
}

// The original's 005A7080 clears the flags, then its inlined base part the mesh.
FigureDrawerBase::~FigureDrawerBase() { mesh_ = nullptr; }

void FigureDrawerBase::DestructDrawerBase() { this->FigureDrawerBase::~FigureDrawerBase(); }
EI_REWRITTEN(0x005A70C0, FigureDrawerBase::DestructDrawerBase);

FigureDrawerBase* FigureDrawerBase::DeletingDestructBase(unsigned flags) {   // 005A70A0
    this->FigureDrawerBase::~FigureDrawerBase();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x005A70A0, FigureDrawerBase::DeletingDestructBase);

FigureMeshDrawer::FigureMeshDrawer() = default;

FigureMeshDrawer* FigureMeshDrawer::Construct() { return new (this) FigureMeshDrawer(); }
EI_REWRITTEN(0x005A7010, FigureMeshDrawer::Construct);

FigureMeshDrawer::~FigureMeshDrawer() { flags_ = 0; }

void FigureMeshDrawer::Destruct() { this->FigureMeshDrawer::~FigureMeshDrawer(); }
EI_REWRITTEN(0x005A7080, FigureMeshDrawer::Destruct);

FigureMeshDrawer* FigureMeshDrawer::DeletingDestruct(unsigned flags) {   // 005A7060
    this->FigureMeshDrawer::~FigureMeshDrawer();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x005A7060, FigureMeshDrawer::DeletingDestruct);

int FigureMeshDrawer::CreateDrawer() {   // 005A70E0
    flags_ = 0;
    spare30_ = Vector3{1.0f, 1.0f, 1.0f};
    return 1;
}
EI_REWRITTEN(0x005A70E0, FigureMeshDrawer::CreateDrawer);

void FigureMeshDrawer::DestroyDrawer() {   // 005A7130
    mesh_ = nullptr;
    flags_ = 0;
}
EI_REWRITTEN(0x005A7130, FigureMeshDrawer::DestroyDrawer);

int FigureMeshDrawer::Create() { return CreateDrawer(); }
void FigureMeshDrawer::Destroy() { DestroyDrawer(); }
const char* FigureMeshDrawer::GetClassName() { return FigureClientSpecificClassName(this); }
int FigureMeshDrawer::GetClassId() { return FigureClientSpecificClassId(this); }
