// The titles window's frame and close (0062BD80, 0062BD00) and its script lines' destructor
// (0062BCB0). The window's Create is in ui_layouts.cpp, its input in ui_screens.cpp. See
// ui_screens.h (CCreditsScreen) and docs/notes/ui.md ("Main menu, dialogs, escape menu, credits").
#include "ui_screens.h"

#include "world.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_library.h"
#include "originals_render.h"
#include "originals_ui.h"
#include "ui_cursor.h"

#include <cstdint>

namespace {

constexpr const char* kOutroSuffix = "fout";    // 00794044
constexpr std::uint32_t kHeaderColour = 0xFEFEFE;
constexpr UINT kSetFontSmoothing = 0x4B;        // SPI_SETFONTSMOOTHING

// 00455E20: the movie flag (00784752; 1: movies shown at their own size). __cdecl, AL.
__declspec(naked) char __cdecl MovieModeOriginal() EI_ORIGINAL(0x00455E20)
// CBinkVideo's frame step (005DBD80, RET 4: AL whether the movie still plays; rewritten in
// bink_video.cpp) and close (005DBF70, plain RET).
__declspec(naked) char __fastcall MovieFrameOriginal(CBinkVideo*, void*, char /*stretch*/) EI_ORIGINAL(0x005DBD80)
__declspec(naked) void __fastcall MovieCloseOriginal(CBinkVideo*) EI_ORIGINAL(0x005DBF70)

// The display object's (00500960) +0x14: the colour the titles keep cleared.
struct DisplayColour {
    std::uint8_t unknown_00[0x14];
    std::uint32_t colour;            // +0x14
};
DisplayColour* Display() { return static_cast<DisplayColour*>(DisplayObject()); }

} // namespace

// --- 0062BFD0: a window above the titles closed (slot 27) ---------------------------------------------

void CCreditsScreen::OnMessageResult(int) { MovieCloseOriginal(&player_); }
EI_REPLACE_METHOD(0x0062BFD0, CCreditsScreen::OnMessageResult);

// --- 0065EF30 / 0065F1C0: the scrolling surface (ui_scrolling_surface.cpp), through their addresses ---

__declspec(naked) bool CUiScrollingSurface::LoadThroughHook(void*, const char*) EI_ORIGINAL(0x0065EF30)
__declspec(naked) void CUiScrollingSurface::StartThroughHook(float, char) EI_ORIGINAL(0x0065F1C0)

// --- 0062BCB0: a script line's destructor ---------------------------------------------------------

void TitleLine::Destroy() {
    CStringDestroy(&style);
    CStringDestroy(&text);
}
EI_REPLACE_METHOD(0x0062BCB0, TitleLine::Destroy);

// --- 0062BD80: the frame -------------------------------------------------------------------------

BOOL CCreditsScreen::Draw(int pass) {
    if (GameTime() > nextLineAt_ && !(textSurface_->speed_ < 0.0f || textSurface_->speed_ > 0.0f)) {
        nextLineAt_ = 0.0f;
        textSurface_->Freeze(0, 0);
        headerSurface_->Freeze(0, 0);
        if (currentLine_ == lines_.End()) {
            finished_ = true;
        } else {
            const TitleLine& line = currentLine_->data;
            textSurface_->LoadThroughHook(archive_, line.style);
            textSurface_->StartThroughHook(0.0f, 0);
            if (line.scroll) {
                textSurface_->StartThroughHook(line.speed, 1);
                SurfaceGetDC(headerSurface_);
                SurfaceClearOriginal(headerSurface_);
                RECT rect = {0, 0, 0, 0};
                SurfaceDrawText(headerSurface_, nullptr, line.text, &rect, 2, kHeaderColour, 1, 1);
                SurfaceReleaseDC(headerSurface_);
                headerSurface_->Freeze(1, 0);
            } else {
                nextLineAt_ = GameTime() + line.speed;
            }
            textSurface_->Freeze(1, 0);
            currentLine_ = currentLine_->next;
        }
    }
    if (!MovieFrameOriginal(&player_, nullptr, MovieModeOriginal() != 1) && finished_) {
        MovieCloseOriginal(&player_);
        PlayMoviesOriginal(mfc::operator+(*reinterpret_cast<const mfc::CString*>(&name_), kOutroSuffix), 0, 0, 0, 0);
        InterfaceHasScreen() = false;
        Close();
        return 1;
    }
    return reinterpret_cast<CUiContainer*>(this)->DrawContainer(pass);
}
namespace {
BOOL __fastcall CreditsDraw(CCreditsScreen* self, void*, int pass) { return self->Draw(pass); }
} // namespace
EI_REPLACE(0x0062BD80, CreditsDraw);

// --- 0062BD00: the close ---------------------------------------------------------------------------

void CCreditsScreen::OnClose() {
    lines_.ClearWith([](TitleLine* line) { line->Destroy(); });
    gameapi::SystemParametersInfoA(kSetFontSmoothing, static_cast<UINT>(fontSmoothing_), nullptr, 0);
    Display()->colour = static_cast<std::uint32_t>(savedDisplay_);
    InterfaceHasScreen() = true;
    CursorShowOriginal(GameCursor(), nullptr, 1);
}
EI_REPLACE_METHOD(0x0062BD00, CCreditsScreen::OnClose);
