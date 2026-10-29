// The titles' scrolling text surface (CUiScrollingSurface, ui_screens.h): a window surface with a hidden
// RichEdit control holding an RTF text, formatted at a scroll offset that moves with the game time into a page of
// the surface's size on the CPU, whose pixels are then written onto the surface's canvas (the interface's last GDI:
// docs/notes/ui-native.md, "P8 done"). See docs/notes/items_spells.md.
#include "app.h"
#include "ui.h"
#include "ui_screens.h"
#include "game_strings.h"

#include "resource.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/nan_audit.h"
#include "render/render_ui.h"

#include <richedit.h>

#include <algorithm>
#include <cstring>

namespace {
constexpr int kTwipsPerInch = 1440;
constexpr int kExtraTwipsBelow = 1000;   // formatted below the visible height
constexpr int kSurfaceFormat = 5;
constexpr COLORREF kWhite = 0xFFFFFF;

const char* const kRichEditLibrary = "Riched20.dll";
const char* const kRichEditClass = "RichEdit20A";


// The block being streamed into the RichEdit (the original reads it through a CMemFile).
struct RtfSource {
    const std::uint8_t* data;
    std::uint32_t size;
    std::uint32_t position;
};

DWORD CALLBACK ReadRtf(DWORD_PTR cookie, LPBYTE buffer, LONG wanted, LONG* read) {
    RtfSource* source = reinterpret_cast<RtfSource*>(cookie);
    const std::uint32_t count = (std::min)(static_cast<std::uint32_t>(wanted), source->size - source->position);
    std::memcpy(buffer, source->data + source->position, count);
    source->position += count;
    *read = static_cast<LONG>(count);
    return 0;
}

// The page the RichEdit formats into: a memory DC on a 32-bit DIB section, kept for the largest surface so far.
class RichEditPage {
public:
    RichEditPage() = default;
    RichEditPage(const RichEditPage&) = delete;
    RichEditPage& operator=(const RichEditPage&) = delete;
    ~RichEditPage() { Release(); }

    // The page of at least `width` x `height` pixels; false when it cannot be made.
    bool Ensure(int width, int height) {
        if (dc_ && width <= width_ && height <= height_) return true;
        Release();
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        bitmap_ = gameapi::CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        dc_ = bitmap_ ? ::CreateCompatibleDC(nullptr) : nullptr;
        if (!dc_ || !bits) {
            Release();
            return false;
        }
        old_ = gameapi::SelectObject(dc_, bitmap_);
        bits_ = static_cast<std::uint32_t*>(bits);
        width_ = width;
        height_ = height;
        return true;
    }

    HDC Dc() const { return dc_; }
    int Pitch() const { return width_; }
    // The page's pixels as 0x00RRGGBB, `Pitch()` apart (GDI's drawing flushed).
    const std::uint32_t* Pixels() const {
        ::GdiFlush();
        return bits_;
    }
    // `width` x `height` from the top left filled with `colour` (a COLORREF).
    void Fill(int width, int height, COLORREF colour) {
        const std::uint32_t rgb = ((colour & 0xFF) << 16) | (colour & 0xFF00) | ((colour >> 16) & 0xFF);
        ::GdiFlush();
        for (int y = 0; y < height; ++y) std::fill_n(bits_ + static_cast<std::size_t>(y) * width_, width, rgb);
    }

private:
    void Release() {
        if (dc_) {
            gameapi::SelectObject(dc_, old_);
            gameapi::DeleteDC(dc_);
        }
        if (bitmap_) gameapi::DeleteObject(bitmap_);
        dc_ = nullptr;
        bitmap_ = nullptr;
        bits_ = nullptr;
        width_ = height_ = 0;
    }

    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ old_ = nullptr;
    std::uint32_t* bits_ = nullptr;
    int width_ = 0, height_ = 0;
};

RichEditPage& Page() {
    static RichEditPage page;
    return page;
}

int HeightInTwips(const CUiScrollingSurface* surface, HDC dc) {
    return gameapi::MulDiv(surface->rect_.bottom - surface->rect_.top, kTwipsPerInch,
                           gameapi::GetDeviceCaps(dc, LOGPIXELSY));
}
} // namespace

CUiScrollingSurface::CUiScrollingSurface(CInterface3D* ui, const RECT* rect) : CUiSurface(ui, rect, kSurfaceFormat) {
    richEditLibrary_ = nullptr;
    richEditLibrary_ = gameapi::LoadLibraryA(kRichEditLibrary);
    richEdit_ = gameapi::CreateWindowExA(0, kRichEditClass, nullptr, WS_CHILD | ES_MULTILINE, 0, 0, 0, 0, app::g_mainWindow,
                                         nullptr, app::g_instance, nullptr);
    offsetX_ = 0;
    background_ = kWhite;
    speed_ = 0.0f;
    offset_ = 0;
    endOffset_ = 0;
}

// Then the surface's.
CUiScrollingSurface::~CUiScrollingSurface() {
    gameapi::DestroyWindow(richEdit_);
    if (richEditLibrary_) {
        gameapi::FreeLibrary(richEditLibrary_);
        richEditLibrary_ = nullptr;
    }
}

bool CUiScrollingSurface::Load(CMappedResource* archive, const char* name) {
    std::uint32_t size = 0;
    void* data = archive->GetFile(name, &size);
    if (data) LoadRtf(data, size);
    archive->CloseFile(data);
    return data != nullptr;
}

bool CUiScrollingSurface::LoadRtf(const void* data, std::uint32_t size) {
    RtfSource source = {static_cast<const std::uint8_t*>(data), size, 0};
    EDITSTREAM stream = {reinterpret_cast<DWORD_PTR>(&source), 0, ReadRtf};
    textLength_ = static_cast<int>(
        gameapi::SendMessageA(richEdit_, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&stream)));
    offset_ = 0;
    RenderText();
    return true;
}

void CUiScrollingSurface::RenderText() {
    RichEditPage& page = Page();
    if (!holder_ || !page.Ensure(width_, height_)) return;
    HDC dc = page.Dc();
    page.Fill(width_, height_, background_);
    gameapi::SendMessageA(richEdit_, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(background_));
    int width = gameapi::MulDiv(rect_.right - rect_.left, kTwipsPerInch, gameapi::GetDeviceCaps(dc, LOGPIXELSX));
    int height = HeightInTwips(this, dc);
    RECT area = {0, 0, width, offset_ + kExtraTwipsBelow + height};
    gameapi::OffsetRect(&area, -offsetX_, -offset_);
    FORMATRANGE range;
    range.hdc = dc;
    range.hdcTarget = dc;
    range.rc = area;
    range.rcPage = area;
    range.chrg.cpMin = 0;
    range.chrg.cpMax = -1;
    LRESULT fitted = gameapi::SendMessageA(richEdit_, EM_FORMATRANGE, 1, reinterpret_cast<LPARAM>(&range));
    render::ui::Image(holder_, render::Rect{0, 0, width_, height_}, page.Pixels(), page.Pitch());
    if (endOffset_ > 0) {
        if (offset_ > endOffset_ + kExtraTwipsBelow + height) {
            speed_ = 0.0f;
            endOffset_ = 0;
        }
    } else if (fitted >= textLength_) {
        endOffset_ = offset_;
    }
}

void CUiScrollingSurface::Blit() {
    EI_NAN_AUDIT(speed_);
    if (speed_ != 0.0f) {
        const float now = GameTime();
        offset_ = static_cast<int>(libcrt::Ftol((static_cast<double>(now) - startTime_) * speed_ + offset_));
        startTime_ = now;
        RenderText();
    }
    CUiSurface::Blit();
}

void CUiScrollingSurface::Start(float speed, char fromBelow) {
    endOffset_ = 0;
    if (fromBelow) {
        RichEditPage& page = Page();
        offset_ = page.Ensure(width_, height_) ? -HeightInTwips(this, page.Dc()) : 0;
    } else {
        offset_ = 0;
    }
    speed_ = speed;
    startTime_ = GameTime();
}
