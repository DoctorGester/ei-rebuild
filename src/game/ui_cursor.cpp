// The cursor images' constructor (the cursor object, see docs/notes/ui.md, "Cursor"), which builds each image's mask
// from its picture's pixels in the .mmp file (the holder has no memory). (The software cursor's blit, which combined
// image, mask and background through Locks of the holders' surfaces, has no caller of ours since: the new renderer
// draws the cursor, ui_cursor_frame.cpp.)
#include "ui_cursor.h"

#include "game_singletons.h"
#include "model.h"
#include "surface.h"
#include "texture.h"
#include "world.h"

#include <cstdint>
#include <cstring>

// --- a cursor image ------------------------------------------------------------------------------

namespace {

// The mask of the 8888 picture at `bitsPerPixel` (16 or 32) a pixel, its rows one after another: all ones where the
// pixel's colour in the screen's format is 0 (16 bits 5:6:5: the whole pixel; 32 bits: its low 24 bits), else 0.
void BuildMask(const MmpHeader* picture, int bitsPerPixel, std::uint8_t* mask) {
    ChannelLayout layout;
    layout.Split(ScreenFormat(bitsPerPixel));
    const std::size_t bytes = static_cast<std::size_t>(bitsPerPixel / 8);
    const std::uint8_t* pixels = reinterpret_cast<const std::uint8_t*>(picture) + sizeof(MmpHeader) + picture->MipOffset(0);
    const std::size_t count = static_cast<std::size_t>(picture->width) * picture->height;
    for (std::size_t i = 0; i < count; ++i) {
        std::uint32_t argb;
        std::memcpy(&argb, pixels + i * 4, sizeof(argb));
        std::memset(mask + i * bytes, layout.ToNative(argb) == 0 ? 0xFF : 0, bytes);
    }
}

} // namespace

CursorImage::CursorImage(const char* name, int hotSpotX, int hotSpotY) {
    holder = nullptr;
    frame = 0;
    CTextureManager* manager = g_clientWorld.textureManager_;
    MmpHeader* picture = manager->GetFileData(name);
    // (A throw passes the exception on; that ends the game.)
    holder = new Surface(g_sceneRenderer->device_, picture);
    ++holder->refs_;

    // The holder took the picture's size and the screen's pixel size or threw.
    std::uint32_t pictureWidth = static_cast<std::uint32_t>(holder->width), pictureHeight = static_cast<std::uint32_t>(holder->height);
    std::uint32_t count = pictureWidth / pictureHeight;
    height = static_cast<LONG>(pictureHeight);
    frames = static_cast<int>(count);
    width = static_cast<LONG>(pictureWidth / count);
    hotX = static_cast<int>(g_cursorWidth) * hotSpotX / 32;
    hotY = static_cast<int>(g_cursorHeight) * hotSpotY / 32;
    std::uint32_t maskBytes = (static_cast<std::uint32_t>(holder->bitsPerPixel) * pictureHeight * pictureWidth) >> 3;
    saved = new std::uint8_t[maskBytes];
    BuildMask(picture, holder->bitsPerPixel, saved);
    manager->CloseFile(picture);
}
