// The titles window's frame and close and its script lines' destructor. The window's Create is in
// ui_layouts.cpp, its input in ui_screens.cpp. See ui_screens.h (CreditsScreen) and docs/notes/ui.md ("Main
// menu, dialogs, escape menu, credits").
#include "app.h"
#include "app.h"
#include "bink_video.h"
#include "model.h"
#include "ui.h"
#include "ui_screens.h"

#include "world.h"

#include <cstdint>
#include "ui_cursor.h"
#include "game_singletons.h"
#include "render/text_format.h"

#include <memory>

namespace {

constexpr const char* kOutroSuffix = "fout";
constexpr std::uint32_t kHeaderColour = 0xFEFEFE;
constexpr int kHeaderFont = 2;

} // namespace

// --- a window above the titles closed ------------------------------------------------------------

void CreditsScreen::MessageResult(std::uint32_t) { player.Close(); }

// --- the frame -----------------------------------------------------------------------------------

void CreditsScreen::Draw(SceneRenderer* pass) {
    if (GameTime() > nextLineAt && textSurface->speed == 0.0f) {
        nextLineAt = 0.0f;
        textSurface->Hide();
        headerSurface->Hide();
        if (nextLine >= lines.size()) {
            finished = true;
        } else {
            const TitleLine& line = lines[nextLine];
            textSurface->Load(&archive, line.style.c_str());
            textSurface->Start(0.0f, false);
            if (line.scroll) {
                textSurface->Start(line.speed, true);
                header = line.text;
                headerSurface->Show();
            } else {
                nextLineAt = GameTime() + line.speed;
            }
            textSurface->Show();
            ++nextLine;
        }
    }
    if (!player.Frame() && finished) {
        player.Close();
        app::PlayMovies((name + kOutroSuffix), false);
        app::g_interfaceHasScreen = 0;
        Close();
        return;
    }
    UiContainer::Draw(pass);
}

// Centred over the whole surface with its shadow, without smoothing: the original switched the system's font
// smoothing off for the titles.
void CreditsScreen::PaintHeader(UiSurface& surface) const {
    const UiFont& font = ui->fonts[kHeaderFont];
    const Rect whole = {0, 0, surface.rect.Width(), surface.rect.Height()};
    surface.DrawTextPixels(UiFont{font.face, font.pixels, true}, header, whole, render::text::kCenter, kHeaderColour, true);
}

// --- the close -----------------------------------------------------------------------------------

void CreditsScreen::CloseScreen() {
    lines.clear();
    g_sceneRenderer.fogColour = static_cast<std::uint32_t>(savedDisplay);
    app::g_interfaceHasScreen = 1;
    g_cursor.ShowImage(1);
}
