// The titles window (CreditsScreen): its build from credits.scr, the frame, the input and the close. See
// ui_credits.h and docs/notes/ui.md ("Main menu, dialogs, escape menu, credits").
#include "game/ui/ui_credits.h"

#include "game/app/app.h"
#include "game/app/game_flow.h"
#include "game/app/movies.h"
#include "game/data/options.h"
#include "game/app/singletons.h"
#include "game/core/asset_text.h"
#include "game/core/text_split.h"
#include "game/scene/scene_renderer.h"
#include "game/sound/sound.h"
#include "game/ui/ui.h"
#include "game/ui/ui_cursor.h"
#include "game/world/world.h"

#include "lib/str.h"
#include "lib/text_case.h"
#include "render/text_format.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

namespace {

constexpr const char* kOutroSuffix = "fout";
constexpr std::uint32_t kHeaderColour = 0xFEFEFE;
constexpr int kHeaderFont = 2;

struct TitlesTokenizer : TextTokenizer {
    // The current word, or "" past the last one (the original assigned null to a string, which keeps "").
    std::string_view WordOrEmpty() const { return position != -1 ? std::string_view(word) : ""; }
};

} // namespace

// --- the build -----------------------------------------------------------------------------------

CreditsScreen::CreditsScreen(bool newFromGame, const char* newName)
    : name(newName), fromGame(newFromGame) {}

void CreditsScreen::CreateScreen() {
    const std::string& currentName = name;
    if (!fromGame) {
        g_clientWorld.sounds->StopAll(true);
        g_clientWorld.music->Stop();
    }
    g_cursor.ShowImage(0);
    app::PlayMovies((currentName + "fin"), false);
    nextLineAt = 0.0f;

    MappedResource* currentArchive = &archive;
    currentArchive->OpenResource((options::g_options.resourcesPath + "outro.res"));
    std::uint32_t size = 0;
    void* data = currentArchive->GetFile("credits.scr", &size);
    if (!data) {
        nextLine = lines.size();
    } else {
        std::string script;
        std::memmove(str::SetLength(script, static_cast<int>(size)), data, size);
        str::CutAtNull(script);
        text::FromAssetText(&script);   // the headers' text in UTF-8 (the pages are RTF)
        RemoveCarriageReturns(&script);
        TitlesTokenizer newLines{{0, &script, "\n"}};
        newLines.Start();
        while (newLines.position != -1) {
            std::string line = newLines.word;
            TitlesTokenizer words{{0, &line, " "}};
            words.Start();
            TitleLine record{};
            {
                record.scroll = text::CompareIgnoringCase(words.WordOrEmpty(), "#scroll") == 0;
            }
            words.Next();
            record.speed = str::ToFloat(words.word);
            words.Next();
            record.style = words.WordOrEmpty();
            words.Next();
            record.text = words.WordOrEmpty();
            lines.push_back(record);
            newLines.Next();
        }
        nextLine = 0;
        currentArchive->CloseFile(data);
    }

    // The scrolling text (10..790 x 150..450) and the header (10..790 x 110..140), frozen.
    {
        Rect area = {10, 0x96, 0x316, 0x1C2};
        textSurface = AddSurface(std::make_unique<UiScrollingSurface>(&area));
        textSurface->Hide();
    }
    {
        Rect area = {10, 0x6E, 0x316, 0x8C};
        headerSurface = AddSurface(std::make_unique<UiSurface>(&area, SurfaceBlend::Keyed));
        headerSurface->PaintWith([this](UiSurface& out) { PaintHeader(out); });
        headerSurface->Hide();
    }
    if (fromGame) g_serverWorld.SetServerPaused(0);
    savedDisplay = static_cast<int>(g_sceneRenderer.fogColour);
    g_sceneRenderer.fogColour = 0;
    player.Open(g_dataFolder + "Movies\\" + currentName + ".bik", false);
    finished = false;
}

// --- a window above the titles closed ------------------------------------------------------------

void CreditsScreen::MessageResult(std::uint32_t) { player.Close(); }

// --- the frame -----------------------------------------------------------------------------------

void CreditsScreen::DrawSprites(SceneRenderer* pass) {
    if (GameTime() > nextLineAt && textSurface->speed == 0.0f) {
        nextLineAt = 0.0f;
        textSurface->Hide();
        headerSurface->Hide();
        if (nextLine >= lines.size()) {
            finished = true;
        } else {
            const TitleLine& line = lines[nextLine];
            textSurface->Load(&archive, line.style);
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
    UiContainer::DrawSprites(pass);
}

// Centred over the whole surface with its shadow, without smoothing: the original switched the system's font
// smoothing off for the titles.
void CreditsScreen::PaintHeader(UiSurface& surface) const {
    const UiFont& font = g_interface.fonts[kHeaderFont];
    const Rect whole = {0, 0, surface.rect.Width(), surface.rect.Height()};
    surface.DrawTextPixels(UiFont{font.face, font.pixels, true}, header, whole, render::text::Format::Center, kHeaderColour, true);
}

// --- the input -----------------------------------------------------------------------------------

void CreditsScreen::Close() {
    if (fromGame) g_interface.SetPendingValue(0xCCCCCCCC);
    else g_gameFlow.CloseSessions();
}

void CreditsScreen::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: Close(); break;
    case UiEventKind::KeyDown: KeyDown(event); break;
    default: UiContainer::OnEvent(event); break;
    }
}

void CreditsScreen::KeyDown(UiEvent& event) {
    const platform::Key key = event.key;
    if (key == platform::Key::Return || key == platform::Key::Escape || key == platform::Key::Space) Close();
}

// --- the close -----------------------------------------------------------------------------------

void CreditsScreen::CloseScreen() {
    lines.clear();
    g_sceneRenderer.fogColour = static_cast<std::uint32_t>(savedDisplay);
    app::g_interfaceHasScreen = 1;
    g_cursor.ShowImage(1);
}
