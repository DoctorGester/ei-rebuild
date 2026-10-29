// FogGrid (effects.h): the squares a seer sees, rows of bytes (Clear, Read, Write); used by
// EffectShowVisibility (world_effect_visibility.cpp). See docs/notes/world.md.
#include "ai_map.h"
#include "effects.h"

#include "core_streams.h"

#include "lib/crt.h"

#include <cstring>

namespace {

constexpr int kNoOrigin = 0x7FFFFFFF;

} // namespace

void FogGrid::Clear() {
    x0 = kNoOrigin;
    y0 = kNoOrigin;
    cells.Resize(1, 1);
    cells.data.assign(cells.data.size(), 0);
}

void FogGrid::Read(MemoryStream* stream) {
    stream->ReadValue(x0);
    stream->ReadValue(y0);
    // Like the original, the locals hold the stream and the grid pointers when a read fails.
    int newWidth = 0;
    int newHeight = 0;
    stream->ReadValue(newWidth);
    stream->ReadValue(newHeight);
    cells.Resize(newWidth, newHeight);
    stream->Read(cells.data.data(), static_cast<std::uint32_t>(newHeight * newWidth));
}

void FogGrid::Write(MemoryStream* stream) {
    int value = x0;
    stream->WriteValue(value);
    value = y0;
    stream->WriteValue(value);
    const int w = cells.width;
    const int h = cells.height;
    value = w;
    stream->WriteValue(value);
    value = h;
    stream->WriteValue(value);
    stream->Write(cells.data.data(), static_cast<std::uint32_t>(h * w));
}
