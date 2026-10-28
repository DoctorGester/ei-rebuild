// The cursor images' constructor (the cursor object, see docs/notes/ui.md, "Cursor"), which builds each
// image's mask from its picture: the picture's holder has no DirectDraw surface, and the mask is built from
// the new renderer's memory (DirectPixels). (The software cursor's blit, which combined image, mask and
// background through Locks of the holders' surfaces, has no caller of ours since: the new renderer draws the
// cursor, ui_cursor_frame.cpp.)
#include "app.h"
#include "ui_cursor.h"
#include "game_strings.h"
#include "dx_surface.h"
#include "texture.h"

#include "lib/crt.h"
#include "lib/gameapi.h"

#include <cstdint>
#include <cstring>
#include "game_data.h"
#include "game_singletons.h"

// --- a cursor image ------------------------------------------------------------------------------

namespace {

// The cursor's size (the hot spots are given for a 32-pixel cursor).


// The mask of `count` pixels of a row: all ones where the pixel's colour is 0 (16 bits: the whole
// pixel; 32 bits: its low 24 bits), else 0.
template <typename T>
void MaskRow(const std::uint8_t* row, T* mask, std::uint32_t count, T colourBits) {
    for (std::uint32_t x = 0; x < count; ++x) {
        T pixel;
        std::memcpy(&pixel, row + x * sizeof(T), sizeof(T));
        mask[x] = (pixel & colourBits) == 0 ? static_cast<T>(~T{0}) : T{0};
    }
}

} // namespace

CursorImage::CursorImage(const char* name, int hotSpotX, int hotSpotY, int flag) {
    holder = nullptr;
    frame = 0;
    CTextureManager* manager = TheTextureManager();
    MmpHeader* picture = manager->GetFileData(name);
    // (A throw frees the holder's memory and passes the exception on; that ends the game.)
    holder = new CDXSurface(TheRenderDevice(), picture, flag);
    if (holder) ++holder->refs_;
    // (A holder kept before here would be released; there is none: it was cleared above.)
    manager->CloseFile(picture);

    // The picture's size and pixels: the new renderer's memory (DirectDraw: GetSurfaceDesc, then a
    // Lock). gave the holder that memory or threw.
    SurfacePixels image;
    DirectPixels(holder, &image);
    std::uint32_t pictureWidth = static_cast<std::uint32_t>(image.width), pictureHeight = static_cast<std::uint32_t>(image.height);
    std::uint32_t count = pictureWidth / pictureHeight;
    height = static_cast<LONG>(pictureHeight);
    frames = static_cast<int>(count);
    width = static_cast<LONG>(pictureWidth / count);
    hotX = static_cast<int>(g_cursorWidth) * hotSpotX / 32;
    hotY = static_cast<int>(g_cursorHeight) * hotSpotY / 32;
    std::uint32_t maskBytes = (static_cast<std::uint32_t>(holder->bitsPerPixel) * pictureHeight * pictureWidth) >> 3;
    saved = new std::uint8_t[maskBytes];

    // The mask, under the device lock.
    gameapi::EnterCriticalSection(TheDeviceLock());
    const std::uint8_t* pixels = image.bits;
    std::uint32_t rows = pictureHeight, columns = pictureWidth, pitch = static_cast<std::uint32_t>(image.pitch);
    if (holder->bitsPerPixel == 16) {
        auto* mask = reinterpret_cast<std::uint16_t*>(saved);
        for (std::uint32_t y = 0; y < rows; ++y)
            MaskRow<std::uint16_t>(pixels + y * ((pitch >> 1) * 2), mask + y * columns, columns, 0xFFFF);
    } else if (holder->bitsPerPixel == 32) {
        auto* mask = reinterpret_cast<std::uint32_t*>(saved);
        for (std::uint32_t y = 0; y < rows; ++y)
            MaskRow<std::uint32_t>(pixels + y * ((pitch >> 2) * 4), mask + y * columns, columns, 0xFFFFFF);
    }
    gameapi::LeaveCriticalSection(TheDeviceLock());
}
