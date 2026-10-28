// GDI objects and the CDC helper (gdi.h).
#include "gdi.h"

#include "gameapi.h"

#include <cstdlib>
#include <cstring>

namespace gdi {

void DeleteGdiObject(HGDIOBJ object) { gameapi::DeleteObject(object); }

// The height: device pixels from LOGPIXELSY (tenths of a point, 720 per inch), then the distance from the origin in
// logical units, negative (a character height).
Font PointFont(int pointSize, const char* face, HDC dc) {
    LOGFONTA font;
    std::memset(&font, 0, sizeof(font));
    font.lfCharSet = DEFAULT_CHARSET;
    gameapi::lstrcpynA(font.lfFaceName, face, LF_FACESIZE);
    const HDC measured = dc ? dc : gameapi::GetDC(nullptr);
    POINT height{0, gameapi::GetDeviceCaps(measured, LOGPIXELSY) * pointSize / 720};
    gameapi::DPtoLP(measured, &height, 1);
    POINT origin{0, 0};
    gameapi::DPtoLP(measured, &origin, 1);
    font.lfHeight = -std::abs(height.y - origin.y);
    if (!dc) gameapi::ReleaseDC(nullptr, measured);
    return Font(gameapi::CreateFontIndirectA(&font));
}

void FillSolidRect(HDC dc, const RECT& rect, COLORREF colour) {
    gameapi::SetBkColor(dc, colour);
    gameapi::ExtTextOutA(dc, 0, 0, ETO_OPAQUE, &rect, nullptr, 0, nullptr);
}

} // namespace gdi
