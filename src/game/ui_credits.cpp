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

#include <cstdint>
#include "ui_cursor.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

#include <memory>

namespace {

constexpr const char* kOutroSuffix = "fout";
constexpr std::uint32_t kHeaderColour = 0xFEFEFE;
constexpr int kHeaderFont = 2;

} // namespace

// --- a window above the titles closed ------------------------------------------------------------

void CCreditsScreen::MessageResult(int) { player_.Close(); }

// --- the frame -----------------------------------------------------------------------------------

void CCreditsScreen::Draw(CSceneRenderer* pass) {
    EI_NAN_AUDIT(textSurface_->speed_);
    if (GameTime() > nextLineAt_ && textSurface_->speed_ == 0.0f) {
        nextLineAt_ = 0.0f;
        textSurface_->Hide(false);
        headerSurface_->Hide(false);
        if (currentLine_ == lines_.end()) {
            finished_ = true;
        } else {
            const TitleLine& line = *currentLine_;
            textSurface_->Load(&archive_, line.style.c_str());
            textSurface_->Start(0.0f, false);
            if (line.scroll) {
                textSurface_->Start(line.speed, true);
                headerSurface_->Clear();
                // Centred over the whole surface with its shadow, without smoothing: the original switched the
                // system's font smoothing off for the titles.
                const UiFont& font = interface_->fonts_[kHeaderFont];
                const RECT& at = headerSurface_->rect_;
                const RECT whole = {0, 0, at.right - at.left, at.bottom - at.top};
                headerSurface_->DrawTextPixels(UiFont{font.face, font.pixels, true}, line.text, whole, DT_CENTER,
                                               kHeaderColour, true);
                headerSurface_->Show();
            } else {
                nextLineAt_ = GameTime() + line.speed;
            }
            textSurface_->Show();
            ++currentLine_;
        }
    }
    if (!player_.Frame() && finished_) {
        player_.Close();
        app::PlayMovies((name_ + kOutroSuffix), false);
        app::g_interfaceHasScreen = 0;
        Close();
        return;
    }
    CUiContainer::Draw(pass);
}

// --- the close -----------------------------------------------------------------------------------

void CCreditsScreen::CloseScreen() {
    lines_.clear();
    g_sceneRenderer.fogColour_ = static_cast<std::uint32_t>(savedDisplay_);
    app::g_interfaceHasScreen = 1;
    g_cursor.ShowImage(1);
}
