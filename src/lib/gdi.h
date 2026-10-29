#pragma once

// GDI objects owned by the code that makes them (the interface's and the console's fonts, the
// carets' and lines' pens, a frame's brush) and the drawing helper the game took from MFC's CDC. The interface draws
// on plain HDCs (ui_gdi.h); see docs/notes/ui.md, section 3.

#include <windows.h>

#include <utility>

namespace gdi {

// DeleteObject, through the game's import (gameapi.h).
void DeleteGdiObject(HGDIOBJ object);

// A GDI object of type `Handle` (HFONT, HPEN, HBRUSH), deleted with its holder; null when making it failed.
template <class Handle>
class Object {
public:
    constexpr Object() = default;
    explicit Object(Handle handle) : handle_(handle) {}
    Object(Object&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    Object& operator=(Object&& other) noexcept {
        reset(std::exchange(other.handle_, nullptr));
        return *this;
    }
    ~Object() { reset(); }

    Handle get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }
    // The old object deleted, `handle` kept.
    void reset(Handle handle = nullptr) {
        if (handle_) DeleteGdiObject(handle_);
        handle_ = handle;
    }

private:
    Handle handle_ = nullptr;
};

// Kept: the GDI handle classes are the names of the instances of Object.
using Font = Object<HFONT>;
using Pen = Object<HPEN>;
using Brush = Object<HBRUSH>;

// MFC's CFont::CreatePointFont: `face` at `pointSize` tenths of a point, DEFAULT_CHARSET, the height turned into
// logical units of `dc` (the screen's when null). Null when CreateFontIndirectA fails.
Font PointFont(int pointSize, const char* face, HDC dc = nullptr);

// MFC's CDC::FillSolidRect: `rect` filled with `colour` (an opaque ExtTextOut of no text; the background colour stays
// `colour`).
void FillSolidRect(HDC dc, const RECT& rect, COLORREF colour);
inline void FillSolidRect(HDC dc, int x, int y, int width, int height, COLORREF colour) {
    FillSolidRect(dc, RECT{x, y, x + width, y + height}, colour);
}

} // namespace gdi
