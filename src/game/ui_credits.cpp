// The titles window's frame and close and its script lines' destructor. The window's Create is in
// ui_layouts.cpp, its input in ui_screens.cpp. See ui_screens.h (CCreditsScreen) and docs/notes/ui.md ("Main
// menu, dialogs, escape menu, credits").
#include "app.h"
#include "app_helpers.h"
#include "bink_video.h"
#include "model.h"
#include "ui.h"
#include "ui_screens.h"

#include "world.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "ui_cursor.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

#include <cstdint>
#include <memory>

namespace {

constexpr const char* kOutroSuffix = "fout";
constexpr std::uint32_t kHeaderColour = 0xFEFEFE;
constexpr UINT kSetFontSmoothing = 0x4B;        // SPI_SETFONTSMOOTHING

} // namespace

// --- a window above the titles closed ------------------------------------------------------------

void CCreditsScreen::OnMessageResult(int) { player_.Close(); }
EI_REWRITTEN(0x0062BFD0, CCreditsScreen::OnMessageResult);

// --- a script line's destructor ------------------------------------------------------------------

void TitleLine::Destroy() {
    std::destroy_at(&style);
    std::destroy_at(&text);
}
EI_REWRITTEN(0x0062BCB0, TitleLine::Destroy);

// --- the frame -----------------------------------------------------------------------------------

BOOL CCreditsScreen::Draw(CSceneRenderer* pass) {
    EI_NAN_AUDIT(textSurface_->speed_);
    if (GameTime() > nextLineAt_ && textSurface_->speed_ == 0.0f) {
        nextLineAt_ = 0.0f;
        textSurface_->Freeze(0, 0);
        headerSurface_->Freeze(0, 0);
        if (currentLine_ == lines_.End()) {
            finished_ = true;
        } else {
            const TitleLine& line = currentLine_->data;
            textSurface_->Load(archive_.Get(), line.style);
            textSurface_->Start(0.0f, 0);
            if (line.scroll) {
                textSurface_->Start(line.speed, 1);
                UiSurfaceGetDC(headerSurface_);
                headerSurface_->Clear();
                RECT rect = {0, 0, 0, 0};
                headerSurface_->DrawTextIn(line.text, &rect, 2, kHeaderColour, 1, 1);
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
        app::PlayMovies(mfc::operator+(name_, kOutroSuffix), false, false, 0, 0);
        app::g_interfaceHasScreen = 0;
        Close();
        return 1;
    }
    return CUiContainer::Draw(pass);
}
EI_REWRITTEN(0x0062BD80, CCreditsScreen::Draw);

// --- the close -----------------------------------------------------------------------------------

void CCreditsScreen::OnClose() {
    lines_.ClearWith([](TitleLine* line) { line->Destroy(); });
    gameapi::SystemParametersInfoA(kSetFontSmoothing, static_cast<UINT>(fontSmoothing_), nullptr, 0);
    SceneRenderer()->fogColour_ = static_cast<std::uint32_t>(savedDisplay_);
    app::g_interfaceHasScreen = 1;
    TheCursor()->ShowImage(1);
}
EI_REWRITTEN(0x0062BD00, CCreditsScreen::OnClose);
