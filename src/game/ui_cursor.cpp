// The cursor images' constructor (005E4EB0; the cursor object at 007C7BD8, see docs/notes/ui.md,
// "Cursor"), which builds each image's mask from its picture: the picture's holder has no DirectDraw
// surface (ticket R7), and the mask is built from the new renderer's memory (DirectPixels). (The
// software cursor's blit 005E5C40, which combined image, mask and background through Locks of the
// holders' surfaces, has no caller of ours since R11a: the new renderer draws the cursor,
// ui_cursor_frame.cpp; R11b removed it.)
#include "app.h"
#include "ui_cursor.h"
#include "game_strings.h"
#include "dx_surface.h"
#include "texture.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"

#include <cstdint>
#include <cstring>
#include "originals_engine.h"
#include "originals_render.h"
#include "game_data.h"
#include "game_singletons.h"

// --- 005E4EB0: a cursor image ------------------------------------------------------------------------

namespace {

// The cursor's size (the hot spots are given for a 32-pixel cursor).
LONG CursorWidth() { return ei::GameData<LONG>(0x007C7C90); }
LONG CursorHeight() { return ei::GameData<LONG>(0x007C7C94); }


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

CursorImage* CursorImage::Construct(const char* name, int hotSpotX, int hotSpotY, int flag) {
    holder = nullptr;
    frame = 0;
    CTextureManager* manager = TheTextureManager();
    void* picture = manager->GetFile(name, nullptr);
    // (The original frees the holder's memory when 004EFC60 throws and passes the exception on; that
    // ends the game.)
    void* memory = gamecrt::operator_new(sizeof(CDXSurface));
    holder = memory ? static_cast<CDXSurface*>(static_cast<CDXSurface*>(memory)->ConstructFromPicture(TheRenderDevice(), picture, flag))
                    : nullptr;
    if (holder) ++holder->refCount;
    // (The original releases a holder kept before here; there is none: it was cleared above.)
    manager->CloseFile(picture);

    // The picture's size and pixels: the new renderer's memory (the original: GetSurfaceDesc, then a
    // Lock). 004EFC60 gave the holder that memory or threw.
    SurfacePixels image;
    DirectPixels(holder, &image);
    std::uint32_t pictureWidth = static_cast<std::uint32_t>(image.width), pictureHeight = static_cast<std::uint32_t>(image.height);
    std::uint32_t count = pictureWidth / pictureHeight;
    height = static_cast<LONG>(pictureHeight);
    frames = static_cast<int>(count);
    width = static_cast<LONG>(pictureWidth / count);
    hotX = static_cast<int>(CursorWidth()) * hotSpotX / 32;
    hotY = static_cast<int>(CursorHeight()) * hotSpotY / 32;
    std::uint32_t maskBytes = (static_cast<std::uint32_t>(holder->bitsPerPixel) * pictureHeight * pictureWidth) >> 3;
    saved = static_cast<std::uint8_t*>(gamecrt::operator_new(maskBytes));

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
    return this;
}
EI_REPLACE_METHOD(0x005E4EB0, CursorImage::Construct);
