// FogGrid (effects.h): the squares a seer sees, rows of bytes (Clear, Read, Write); used by
// CEffectShowVisibility (world_effect_visibility.cpp). See docs/notes/world.md.
#include "ai_grids.h"
#include "effects.h"

#include "core_streams.h"

#include "lib/crt.h"

#include <cstring>

namespace {

constexpr int kNoOrigin = 0x7FFFFFFF;

// The cells and the row table freed and made again for `width` x `height` (not cleared).
void Reallocate(SearchGrid<std::uint8_t>* grid, int width, int height) {
    FreeGrid(*grid);
    grid->width = width;
    grid->height = height;
    grid->data = new std::uint8_t[static_cast<std::size_t>(width * height)];
    grid->rows = new std::uint8_t*[static_cast<std::size_t>(grid->height)];
    for (int row = 0; row < grid->height; ++row) grid->rows[row] = grid->data + grid->width * row;
}

} // namespace

void FogGrid::Clear() {
    x0 = kNoOrigin;
    y0 = kNoOrigin;
    if (cells.width != 1 || cells.height != 1) Reallocate(&cells, 1, 1);
    std::memset(cells.data, 0, static_cast<std::size_t>(cells.height * cells.width));
}

void FogGrid::Read(CMemoryStream* stream) {
    stream->Read(&x0, 4);
    stream->Read(&y0, 4);
    // Like the original, the locals hold the stream and the grid pointers when a read fails.
    int newWidth = 0;
    int newHeight = 0;
    stream->Read(&newWidth, 4);
    stream->Read(&newHeight, 4);
    if (cells.width != newWidth || cells.height != newHeight) Reallocate(&cells, newWidth, newHeight);
    stream->Read(cells.data, static_cast<std::uint32_t>(newHeight * newWidth));
}

void FogGrid::Write(CMemoryStream* stream) {
    int value = x0;
    stream->Write(&value, 4);
    value = y0;
    stream->Write(&value, 4);
    const int w = cells.width;
    const int h = cells.height;
    value = w;
    stream->Write(&value, 4);
    value = h;
    stream->Write(&value, 4);
    stream->Write(cells.data, static_cast<std::uint32_t>(h * w));
}
