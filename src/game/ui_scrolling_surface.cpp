// The titles' scrolling text surface (CUiScrollingSurface, ui_screens.h; 0065ED30-0065F21E): a
// window surface with a hidden RichEdit control holding an RTF text, formatted into the surface
// at a scroll offset that moves with the game time. See docs/notes/items_spells.md (A-H17, A-H25).
#include "app.h"
#include "ui.h"
#include "ui_screens.h"
#include "game_strings.h"

#include "resource.h"
#include "ui_gdi.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/compiled_vtable.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "game_data.h"
#include "lib/nan_audit.h"

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


// The block being streamed into the RichEdit (the original reads it through a CMemFile; its
// callback is 0065EF10).
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

int HeightInTwips(const CUiScrollingSurface* surface, const ui_gdi::GameDC* dc) {
    return gameapi::MulDiv(surface->rect_.bottom - surface->rect_.top, kTwipsPerInch,
                           gameapi::GetDeviceCaps(dc->attribDC, LOGPIXELSY));
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
EI_REWRITTEN_SPECIAL(0x0065ED30, CUiScrollingSurface::CUiScrollingSurface);

// Then the surface's (005FD120).
CUiScrollingSurface::~CUiScrollingSurface() {
    gameapi::DestroyWindow(richEdit_);
    if (richEditLibrary_) {
        gameapi::FreeLibrary(richEditLibrary_);
        richEditLibrary_ = nullptr;
    }
}
EI_REWRITTEN_SPECIAL(0x0065EDE0, CUiScrollingSurface::~CUiScrollingSurface);
EI_REWRITTEN_SPECIAL(0x0065EDC0, CUiScrollingSurface::~CUiScrollingSurface);   // deleting

bool CUiScrollingSurface::Load(CMappedResource* archive, const char* name) {
    std::uint32_t size = 0;
    void* data = archive->GetFile(name, &size);
    if (data) LoadRtf(data, size);
    archive->CloseFile(data);
    return data != nullptr;
}
EI_REWRITTEN(0x0065EF30, CUiScrollingSurface::Load);

bool CUiScrollingSurface::LoadRtf(const void* data, std::uint32_t size) {
    RtfSource source = {static_cast<const std::uint8_t*>(data), size, 0};
    EDITSTREAM stream = {reinterpret_cast<DWORD_PTR>(&source), 0, ReadRtf};
    textLength_ = static_cast<int>(
        gameapi::SendMessageA(richEdit_, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&stream)));
    offset_ = 0;
    RenderText();
    return true;
}
EI_REWRITTEN(0x0065EF80, CUiScrollingSurface::LoadRtf);

void CUiScrollingSurface::RenderText() {
    ui_gdi::GameDC* dc = UiSurfaceGetDC(this);
    static_cast<CUiSurface*>(this)->Clear();
    gameapi::SendMessageA(richEdit_, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(background_));
    int width = gameapi::MulDiv(rect_.right - rect_.left, kTwipsPerInch, gameapi::GetDeviceCaps(dc->attribDC, LOGPIXELSX));
    int height = HeightInTwips(this, dc);
    RECT area = {0, 0, width, offset_ + kExtraTwipsBelow + height};
    gameapi::OffsetRect(&area, -offsetX_, -offset_);
    FORMATRANGE range;
    range.hdc = dc->hdc;
    range.hdcTarget = dc->hdc;
    range.rc = area;
    range.rcPage = area;
    range.chrg.cpMin = 0;
    range.chrg.cpMax = -1;
    LRESULT fitted = gameapi::SendMessageA(richEdit_, EM_FORMATRANGE, 1, reinterpret_cast<LPARAM>(&range));
    if (endOffset_ > 0) {
        if (offset_ > endOffset_ + kExtraTwipsBelow + height) {
            speed_ = 0.0f;
            endOffset_ = 0;
        }
    } else if (fitted >= textLength_) {
        endOffset_ = offset_;
    }
    UiSurfaceReleaseDC(this);
}
EI_REWRITTEN(0x0065F020, CUiScrollingSurface::RenderText);

void CUiScrollingSurface::Blit() {
    EI_NAN_AUDIT(speed_);
    if (speed_ != 0.0f) {
        const float now = GameTime();
        offset_ = static_cast<int>(gamecrt::Ftol((static_cast<double>(now) - startTime_) * speed_ + offset_));
        startTime_ = now;
        RenderText();
    }
    CUiSurface::Blit();
}
EI_REWRITTEN(0x0065F170, CUiScrollingSurface::Blit);

void CUiScrollingSurface::Start(float speed, char fromBelow) {
    endOffset_ = 0;
    if (fromBelow) {
        ui_gdi::GameDC* dc = UiSurfaceGetDC(this);
        offset_ = -HeightInTwips(this, dc);
        UiSurfaceReleaseDC(this);
    } else {
        offset_ = 0;
    }
    speed_ = speed;
    startTime_ = GameTime();
}
EI_REWRITTEN(0x0065F1C0, CUiScrollingSurface::Start);
