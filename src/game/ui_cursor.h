#pragma once

// The software mouse cursor (the object at 007C7BD8; 005E4E00-005E6990, see docs/notes/ui.md,
// "Cursor"). Only the members the rewritten draws use; the object is created by original code.

<<<<<<< HEAD
#include <cstddef>
=======
#include "originals_ui.h"

>>>>>>> 8a0f9ad
#include <cstdint>

#include <windows.h>

class CTextureDevice;
struct CDXSurface;

// The cursor's image (what +0x10 points at): its surface holder, size and a copy of its pixels.
struct CursorImage {
    CDXSurface* holder;              // +0x00 surface holder
    LONG width;                      // +0x04
    LONG height;                     // +0x08
    std::uint8_t unknown_0C[0x1C - 0x0C];
    std::uint8_t* saved;             // +0x1C the image's pixels (its full width)
};
static_assert(offsetof(CursorImage, width) == 0x04);
static_assert(offsetof(CursorImage, saved) == 0x1C);

class CCursor {
public:
    // 005E5C40 (RET 0xC). Locks `from` of the image, `to` of the target surface (+0x64) and the
    // from-sized top left of the background copy (+0x60), and writes over `to`
    // (background & saved) | image per pixel (16 or 32 bits, by the image's bit depth).
    void Blit(CursorImage* image, RECT* to, RECT* from);
    // 005E5200 (RET 4, not rewritten). Selects cursor shape `shape`; returns the previous one (-1
    // when the cursor is not set up).
    int SetShape(int shape) { return CursorSetShapeOriginal(this, nullptr, shape); }

<<<<<<< HEAD
    int mode_;                       // +0x00 2: the background is saved from the primary without an image
    CTextureDevice* device_;         // +0x04 the device object (its +0x0C IDirectDraw7)
    std::uint8_t unknown_08[4];
    int imageShown_;                 // +0x0C an image is shown (005E5270)
    CursorImage* image_;             // +0x10
    std::uint8_t unknown_14[0x1C - 0x14];
    HANDLE stopped_;                 // +0x1C set when the cursor thread ends
    RECT drawn_;                     // +0x20 the rectangle drawn last
    RECT rect_;                      // +0x30 the cursor's screen rectangle (005E52E0)
    RECT from_;                      // +0x40 the part of the image shown
    RECT bounds_;                    // +0x50
    CDXSurface* under_;              // +0x60 surface holder: the background under the cursor
    CDXSurface* target_;             // +0x64 surface holder: where the cursor is drawn
    std::uint8_t unknown_68[0x91 - 0x68];
    std::uint8_t enabled_;           // +0x91 the cursor exists (created)
    std::uint8_t unknown_92[0x9C - 0x92];
    int stop_;                       // +0x9C asks the cursor thread to end
    CRITICAL_SECTION lock_;          // +0xA0
};
static_assert(offsetof(CCursor, imageShown_) == 0x0C);
static_assert(offsetof(CCursor, stopped_) == 0x1C);
static_assert(offsetof(CCursor, drawn_) == 0x20);
static_assert(offsetof(CCursor, bounds_) == 0x50);
static_assert(offsetof(CCursor, under_) == 0x60);
static_assert(offsetof(CCursor, enabled_) == 0x91);
static_assert(offsetof(CCursor, stop_) == 0x9C);
static_assert(offsetof(CCursor, lock_) == 0xA0);
=======
    std::uint8_t unknown_00[0x18];
    int shape_;                      // +0x18 the shape SetShape selected
    std::uint8_t unknown_1C[0x60 - 0x1C];
    std::uint8_t* under_;            // +0x60 surface holder: the background under the cursor
    std::uint8_t* target_;           // +0x64 surface holder: where the cursor is drawn
};

// The game's cursor (the object at 007C7BD8).
inline CCursor* GameCursor() { return reinterpret_cast<CCursor*>(0x007C7BD8); }
>>>>>>> 8a0f9ad
