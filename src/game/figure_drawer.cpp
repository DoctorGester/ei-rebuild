// FigureMeshDrawer::Animate (005A7160, A-W12): an animated figure mesh's texture offset per frame.
// See link_figure.h.
#include "link_figure.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <bit>
#include <cstdint>

namespace {

// The mesh types (CFigureMesh +0x15C) whose texture is a sheet of frames.
constexpr std::uint32_t kTwoRows = 13;     // 8 frames, 4 per row, two rows (v 0 / 0.5)
constexpr std::uint32_t kSheet4x4 = 14;    // 16 frames
constexpr std::uint32_t kSheet8x4 = 16;    // 32 frames

std::uint32_t Bits(float value) { return std::bit_cast<std::uint32_t>(value); }

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
