// FigureMeshDrawer::Animate: an animated figure mesh's texture offset per frame. The drawer's lifetime: its
// constructor, destructor bodies and the create / destroy virtuals. See link_figure.h.
#include "engine_object.h"
#include "link_figure.h"

#include <cstdint>
#include <new>

namespace {

// The mesh types (FigureMesh +0x15C) whose texture is a sheet of frames.
constexpr std::uint32_t kTwoRows = 13;     // 8 frames, 4 per row, two rows (v 0 / 0.5)
constexpr std::uint32_t kSheet4x4 = 14;    // 16 frames
constexpr std::uint32_t kSheet8x4 = 16;    // 32 frames

} // namespace

void FigureMeshDrawer::Animate(float time) {
    FigureMesh* currentMesh = mesh;
    const std::uint32_t meshType = currentMesh->meshType;
    if (meshType == kTwoRows) {
        int frame = static_cast<int>(static_cast<std::int64_t>(time) & 7);
        if (frame < 4) {
            currentMesh->textureOffsetU = static_cast<float>(frame) * 0.25f;
            mesh->textureOffsetV = 0.0f;
        } else {
            currentMesh->textureOffsetU = static_cast<float>(frame - 4) * 0.25f;
            mesh->textureOffsetV = 0.5f;
        }
    } else if (meshType == kSheet4x4) {
        int frame = static_cast<int>(static_cast<std::int64_t>(time) & 15);
        currentMesh->textureOffsetU = static_cast<float>(frame % 4) * 0.25f;
        mesh->textureOffsetV = 1.0f - static_cast<float>(frame / 4) * 0.25f;
    } else if (meshType == kSheet8x4) {
        int frame = static_cast<int>(static_cast<std::int64_t>(time) & 31);
        currentMesh->textureOffsetU = static_cast<float>(frame % 8) * 0.125f;
        mesh->textureOffsetV = 1.0f - static_cast<float>(frame / 8) * 0.25f;
    }
}

// ---- Lifetime ---------------------------------------------------------------------------

FigureDrawerBase::FigureDrawerBase() {
    mesh = nullptr;
}

// The original's base clears the flags, then its inlined base part the mesh.
FigureDrawerBase::~FigureDrawerBase() { mesh = nullptr; }

FigureMeshDrawer::FigureMeshDrawer() = default;

FigureMeshDrawer::~FigureMeshDrawer() { flags = 0; }

void FigureMeshDrawer::DestroyDrawer() {
    mesh = nullptr;
    flags = 0;
}

void FigureMeshDrawer::Create() { flags = 0; }
void FigureMeshDrawer::Destroy() { DestroyDrawer(); }
const char* FigureMeshDrawer::ClassName() { return "CFigureClientSpecific"; }
ClassId FigureMeshDrawer::GetClassId() { return ClassId::FigureMeshDrawer; }
