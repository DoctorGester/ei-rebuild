// FogGrid (effects.h): the squares a seer sees, rows of bytes. Clear 005C8B60, Read 005C8BF0 and
// Write 005C8CC0 (thiscall, RET 4 for the stream ones); used by CEffectShowVisibility
// (world_effect_visibility.cpp). See docs/notes/world.md.
#include "effects.h"

#include "core_streams.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>

namespace {

constexpr int kNoOrigin = 0x7FFFFFFF;

// The cells and the row table freed and made again for `width` x `height` (not cleared).
void Reallocate(FogGrid* grid, int width, int height) {
    gamecrt::operator_delete(grid->cells);
    gamecrt::operator_delete(grid->rows);
    grid->width = width;
    grid->height = height;
    grid->cells = static_cast<std::uint8_t*>(gamecrt::operator_new(static_cast<std::size_t>(width * height)));
    grid->rows = static_cast<std::uint8_t**>(gamecrt::operator_new(static_cast<std::size_t>(grid->height) * 4));
    for (int row = 0; row < grid->height; ++row) grid->rows[row] = grid->cells + grid->width * row;
}

} // namespace

void FogGrid::Clear() {
    x0 = kNoOrigin;
    y0 = kNoOrigin;
    if (width != 1 || height != 1) Reallocate(this, 1, 1);
    std::memset(cells, 0, static_cast<std::size_t>(height * width));
}

void FogGrid::Read(CMemoryStream* stream) {
    stream->Read(&x0, 4);
    stream->Read(&y0, 4);
    // The original's locals hold the stream and the grid pointers when a read fails.
    int newWidth = 0;
    int newHeight = 0;
    stream->Read(&newWidth, 4);
    stream->Read(&newHeight, 4);
    if (width != newWidth || height != newHeight) Reallocate(this, newWidth, newHeight);
    stream->Read(cells, static_cast<std::uint32_t>(newHeight * newWidth));
}

void FogGrid::Write(CMemoryStream* stream) {
    int value = x0;
    stream->Write(&value, 4);
    value = y0;
    stream->Write(&value, 4);
    const int w = width;
    const int h = height;
    value = w;
    stream->Write(&value, 4);
    value = h;
    stream->Write(&value, 4);
    stream->Write(cells, static_cast<std::uint32_t>(h * w));
}

EI_REPLACE_METHOD(0x005C8B60, FogGrid::Clear);
EI_REPLACE_METHOD(0x005C8BF0, FogGrid::Read);
EI_REPLACE_METHOD(0x005C8CC0, FogGrid::Write);
