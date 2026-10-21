// The titles window's frame and close (0062BD80, 0062BD00) and its script lines' destructor
// (0062BCB0). The window's Create is in ui_layouts.cpp, its input in ui_screens.cpp. See
// ui_screens.h (CCreditsScreen) and docs/notes/ui.md ("Main menu, dialogs, escape menu, credits").
#include "app_helpers.h"
#include "bink_video.h"
#include "model.h"
#include "ui_screens.h"

#include "world.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_library.h"
#include "originals_render.h"
#include "originals_ui.h"
#include "ui_cursor.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

#include <cstdint>

namespace {

constexpr const char* kOutroSuffix = "fout";    // 00794044
constexpr std::uint32_t kHeaderColour = 0xFEFEFE;
constexpr UINT kSetFontSmoothing = 0x4B;        // SPI_SETFONTSMOOTHING

// 00455E20: the movie flag (00784752; 1: movies shown at their own size). __cdecl, AL.
// CBinkVideo's frame step (005DBD80, RET 4: AL whether the movie still plays; rewritten in
// bink_video.cpp) and close (005DBF70, plain RET).


} // namespace

// --- 0062BFD0: a window above the titles closed (slot 27) ---------------------------------------------

void CCreditsScreen::OnMessageResult(int) { player_.Close(); }
EI_REPLACE_METHOD(0x0062BFD0, CCreditsScreen::OnMessageResult);

// --- 0065EF30: the scrolling surface's load (ui_scrolling_surface.cpp), through its address -------

// --- 0062BCB0: a script line's destructor ---------------------------------------------------------

void TitleLine::Destroy() {
    CStringDestroy(&style);
    CStringDestroy(&text);
}
EI_REPLACE_METHOD(0x0062BCB0, TitleLine::Destroy);

// --- 0062BD80: the frame -------------------------------------------------------------------------

BOOL CCreditsScreen::Draw(int pass) {
    EI_NAN_AUDIT(textSurface_->speed_);
    if (GameTime() > nextLineAt_ && textSurface_->speed_ == 0.0f) {
        nextLineAt_ = 0.0f;
        textSurface_->Freeze(0, 0);
        headerSurface_->Freeze(0, 0);
        if (currentLine_ == lines_.End()) {
            finished_ = true;
        } else {
            const TitleLine& line = currentLine_->data;
            textSurface_->Load(reinterpret_cast<CMappedResource*>(archive_), line.style);
            textSurface_->Start(0.0f, 0);
            if (line.scroll) {
                textSurface_->Start(line.speed, 1);
                UiSurfaceGetDC(headerSurface_);
                SurfaceClearOriginal(headerSurface_);
                RECT rect = {0, 0, 0, 0};
                SurfaceDrawText(headerSurface_, nullptr, line.text, &rect, 2, kHeaderColour, 1, 1);
                UiSurfaceReleaseDC(headerSurface_);
                headerSurface_->Freeze(1, 0);
            } else {
                nextLineAt_ = GameTime() + line.speed;
            }
            textSurface_->Freeze(1, 0);
            currentLine_ = currentLine_->next;
        }
    }
    if (!player_.Frame(app::MovieMode() != 1) && finished_) {
        player_.Close();
        PlayMoviesOriginal(mfc::operator+(*reinterpret_cast<const mfc::CString*>(&name_), kOutroSuffix), 0, 0, 0, 0);
        InterfaceHasScreen() = false;
        Close();
        return 1;
    }
    return reinterpret_cast<CUiContainer*>(this)->DrawContainer(pass);
}
EI_REPLACE_METHOD(0x0062BD80, CCreditsScreen::Draw);

// --- 0062BD00: the close ---------------------------------------------------------------------------

void CCreditsScreen::OnClose() {
    lines_.ClearWith([](TitleLine* line) { line->Destroy(); });
    gameapi::SystemParametersInfoA(kSetFontSmoothing, static_cast<UINT>(fontSmoothing_), nullptr, 0);
    SceneRenderer()->fogColour_ = static_cast<std::uint32_t>(savedDisplay_);
    InterfaceHasScreen() = true;
    CursorShowOriginal(TheCursor(), nullptr, 1);
}
EI_REPLACE_METHOD(0x0062BD00, CCreditsScreen::OnClose);
