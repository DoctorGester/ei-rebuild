#pragma once

// The software mouse cursor (the object at 007C7BD8; 005E4E00-005E6990, see docs/notes/ui.md,
// "Cursor"). Only the members the rewritten draws use; the object is created by original code.

#include <cstddef>
#include "originals_ui.h"

#include "lib/containers.h"

#include <cstdint>

#include <windows.h>

class CTextureDevice;
struct CDXSurface;

// The cursor's image (what +0x10 points at; made by 005E4EB0 for every cursor shape, from 005E63F0):
// its surface holder (the picture: `frames` square frames side by side), a frame's size, the frame
// shown, the hot spot and the mask.
struct CursorImage {
    // 005E4EB0 (RET 0x10): the image of the picture "<name>" (the file's name with ".mmp", from the
    // texture manager; loaded into a holder by 004EFC60 with `flag`), the hot spot given in the
    // 32-pixel layout. Returns this.
    CursorImage* Construct(const char* name, int hotSpotX, int hotSpotY, int flag);

    CDXSurface* holder;              // +0x00 surface holder
    LONG width;                      // +0x04 a frame's width
    LONG height;                     // +0x08
    int frames;                      // +0x0C frames side by side (the picture's width / height)
    int frame;                       // +0x10 the frame shown (005E52E0 steps it every 125 ms)
    int hotX;                        // +0x14 the hot spot, scaled to the cursor's size
    int hotY;                        // +0x18
    std::uint8_t* saved;             // +0x1C the mask over the whole picture (its pixel size): all
                                     //       ones where the picture is black, else 0
};
static_assert(offsetof(CursorImage, width) == 0x04);
static_assert(offsetof(CursorImage, frames) == 0x0C);
static_assert(offsetof(CursorImage, hotX) == 0x14);
static_assert(offsetof(CursorImage, saved) == 0x1C);
static_assert(sizeof(CursorImage) == 0x20);

class CCursor {
public:
    // 005E5C40 (RET 0xC). Locks `from` of the image, `to` of the target surface (+0x64) and the
    // from-sized top left of the background copy (+0x60), and writes over `to`
    // (background & saved) | image per pixel (16 or 32 bits, by the image's bit depth).
    void Blit(CursorImage* image, RECT* to, RECT* from);
    // 005E5200 (RET 4, not rewritten). Selects cursor shape `shape`; returns the previous one (-1
    // when the cursor is not set up).
    int SetShape(int shape) { return CursorSetShapeOriginal(this, nullptr, shape); }

    // 005E60B0 (RET 0x10; ui_cursor_setup.cpp). Sets the cursor up on `device` (a reference
    // taken, the old one's dropped) in `mode` with `context`; `smallCursors` (low byte) picks 16-pixel
    // cursors instead of 32 (the size globals 007C7C90 / 007C7C94). Makes the end event, the
    // bounds (the render target's size when there is a primary surface) and the background and
    // composition surfaces (cursor size and twice that, through 004EF7B0).
    void Init(CTextureDevice* device, int mode, int context, int smallCursors);
    // 005E63F0 (RET 0x10; ui_cursor_setup.cpp). Adds the shape made from the picture "<name>.mmp"
    // with the hot spot (hotX, hotY) and `flag` (CursorImage::Construct; null when the allocation
    // fails) to the shape list.
    void AddShape(const char* name, int hotX, int hotY, int flag);

    int mode_;                       // +0x00 2: the background is saved from the primary without an image
    CTextureDevice* device_;         // +0x04 the device object (its +0x0C IDirectDraw7), referenced
    int context_;                    // +0x08 Init's context
    int imageShown_;                 // +0x0C an image is shown (005E5270)
    CursorImage* image_;             // +0x10
    std::uint32_t unknown_14;        // +0x14 cleared by Init
    int shape_;                      // +0x18 the shape SetShape selected
    HANDLE stopped_;                 // +0x1C set when the cursor thread ends
    RECT drawn_;                     // +0x20 the rectangle drawn last
    RECT rect_;                      // +0x30 the cursor's screen rectangle (005E52E0)
    RECT from_;                      // +0x40 the part of the image shown
    RECT bounds_;                    // +0x50
    CDXSurface* under_;              // +0x60 surface holder: the background under the cursor
    CDXSurface* target_;             // +0x64 surface holder: where the cursor is drawn
    lib::AlignedArray<CursorImage*> images_;   // +0x68 the shapes (AddShape)
    POINT position_;                 // +0x78 the position drawn last (005E52E0)
    POINT heldPosition_;             // +0x80 the position kept while held
    POINT motion_;                   // +0x88 the mouse motion summed by 005E52E0
    std::uint8_t held_;              // +0x90 the cursor stays at +0x80 (005E67B0 sets it)
    std::uint8_t enabled_;           // +0x91 the cursor exists (created)
    std::uint8_t unknown_92[2];
    DWORD frameTime_;                // +0x94 when the image's frame last stepped
    std::uint32_t unknown_98;        // +0x98 cleared by Init
    int stop_;                       // +0x9C asks the cursor thread to end
    CRITICAL_SECTION lock_;          // +0xA0
};
static_assert(offsetof(CCursor, imageShown_) == 0x0C);
static_assert(offsetof(CCursor, shape_) == 0x18);
static_assert(offsetof(CCursor, stopped_) == 0x1C);
static_assert(offsetof(CCursor, drawn_) == 0x20);
static_assert(offsetof(CCursor, bounds_) == 0x50);
static_assert(offsetof(CCursor, under_) == 0x60);
static_assert(offsetof(CCursor, enabled_) == 0x91 && offsetof(CCursor, images_) == 0x68 && offsetof(CCursor, motion_) == 0x88);
static_assert(offsetof(CCursor, frameTime_) == 0x94 && offsetof(CCursor, unknown_98) == 0x98);
static_assert(offsetof(CCursor, stop_) == 0x9C);
static_assert(offsetof(CCursor, lock_) == 0xA0);

// The game's cursor (the object at 007C7BD8).
inline CCursor* GameCursor() { return reinterpret_cast<CCursor*>(0x007C7BD8); }
