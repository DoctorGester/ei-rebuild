#pragma once

// The software mouse cursor (the object at 007C7BD8; 005E4E00-005E6990, see docs/notes/ui.md,
// "Cursor"). Only the members the rewritten blit uses; the object is created by original code.

#include <cstdint>

#include <windows.h>

// The cursor's image (what +0x10 points at): its surface holder and a copy of its pixels.
struct CursorImage {
    std::uint8_t* holder;            // +0x00 surface holder (+0x08 result, +0x10 surface, +0x1C bits)
    std::uint8_t unknown_04[0x1C - 0x04];
    std::uint8_t* saved;             // +0x1C the image's pixels (its full width)
};

class CCursor {
public:
    // 005E5C40 (RET 0xC). Locks `from` of the image, `to` of the target surface (+0x64) and the
    // from-sized top left of the background copy (+0x60), and writes over `to`
    // (background & saved) | image per pixel (16 or 32 bits, by the image's bit depth).
    void Blit(CursorImage* image, RECT* to, RECT* from);

    std::uint8_t unknown_00[0x60];
    std::uint8_t* under_;            // +0x60 surface holder: the background under the cursor
    std::uint8_t* target_;           // +0x64 surface holder: where the cursor is drawn
};
