// FigureMeshDrawer::Animate: an animated figure mesh's texture offset per frame. The drawer's lifetime: its
// constructor, destructor bodies and the create / destroy virtuals. See link_figure.h.
#include "link_figure.h"

#include "engine_object.h"

#include "world_class_slots.h"   // FigureClientSpecificClassName / ClassId

#include "lib/crt.h"

#include <cstdint>
#include <new>


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
        int frame = static_cast<int>(libcrt::Ftol(time) & 7);
        if (frame < 4) {
            mesh->textureOffsetU_ = static_cast<float>(frame) * 0.25f;
            mesh_->textureOffsetV_ = 0.0f;
        } else {
            mesh->textureOffsetU_ = static_cast<float>(frame - 4) * 0.25f;
            mesh_->textureOffsetV_ = 0.5f;
        }
    } else if (type == kSheet4x4) {
        int frame = static_cast<int>(libcrt::Ftol(time) & 15);
        mesh->textureOffsetU_ = static_cast<float>(frame % 4) * 0.25f;
        mesh_->textureOffsetV_ = 1.0f - static_cast<float>(frame / 4) * 0.25f;
    } else if (type == kSheet8x4) {
        int frame = static_cast<int>(libcrt::Ftol(time) & 31);
        mesh->textureOffsetU_ = static_cast<float>(frame % 8) * 0.125f;
        mesh_->textureOffsetV_ = 1.0f - static_cast<float>(frame / 8) * 0.25f;
    }
}

// ---- Lifetime ---------------------------------------------------------------------------

FigureDrawerBase::FigureDrawerBase() {
    ClearFields();
    mesh_ = nullptr;
}

// The original's base clears the flags, then its inlined base part the mesh.
FigureDrawerBase::~FigureDrawerBase() { mesh_ = nullptr; }

FigureMeshDrawer::FigureMeshDrawer() = default;

FigureMeshDrawer::~FigureMeshDrawer() { flags_ = 0; }

int FigureMeshDrawer::CreateDrawer() {
    flags_ = 0;
    return 1;
}

void FigureMeshDrawer::DestroyDrawer() {
    mesh_ = nullptr;
    flags_ = 0;
}

int FigureMeshDrawer::Create() { return CreateDrawer(); }
void FigureMeshDrawer::Destroy() { DestroyDrawer(); }
const char* FigureMeshDrawer::ClassName() { return FigureClientSpecificClassName(this); }
ClassId FigureMeshDrawer::GetClassId() { return kClassFigureMeshDrawer; }
