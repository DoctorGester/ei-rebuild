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

void CCreditsScreen::MessageResult(int) { player.Close(); }

// --- the frame -----------------------------------------------------------------------------------

void CCreditsScreen::Draw(CSceneRenderer* pass) {
    EI_NAN_AUDIT(textSurface->speed);
    if (GameTime() > nextLineAt && textSurface->speed == 0.0f) {
        nextLineAt = 0.0f;
        textSurface->Hide(false);
        headerSurface->Hide(false);
        if (currentLine == lines.end()) {
            finished = true;
        } else {
            const TitleLine& line = *currentLine;
            textSurface->Load(&archive, line.style.c_str());
            textSurface->Start(0.0f, false);
            if (line.scroll) {
                textSurface->Start(line.speed, true);
                headerSurface->Clear();
                // Centred over the whole surface with its shadow, without smoothing: the original switched the
                // system's font smoothing off for the titles.
                const UiFont& font = ui->fonts[kHeaderFont];
                const RECT& at = headerSurface->rect;
                const RECT whole = {0, 0, at.right - at.left, at.bottom - at.top};
                headerSurface->DrawTextPixels(UiFont{font.face, font.pixels, true}, line.text, whole, DT_CENTER,
                                               kHeaderColour, true);
                headerSurface->Show();
            } else {
                nextLineAt = GameTime() + line.speed;
            }
            textSurface->Show();
            ++currentLine;
        }
    }
    if (!player.Frame() && finished) {
        player.Close();
        app::PlayMovies((name + kOutroSuffix), false);
        app::g_interfaceHasScreen = 0;
        Close();
        return;
    }
    CUiContainer::Draw(pass);
}

// --- the close -----------------------------------------------------------------------------------

void CCreditsScreen::CloseScreen() {
    lines.clear();
    g_sceneRenderer.fogColour = static_cast<std::uint32_t>(savedDisplay);
    app::g_interfaceHasScreen = 1;
    g_cursor.ShowImage(1);
}
