// Layout builders (the Create virtuals) of the escape menu, the main menu, the message box, the difficulty question and
// the credits: the regions, sprites, surfaces and 3D model each window is made of. Rebirth patches the sprite placement
// and UiRegion::SetRect for its widescreen layout. The save / load and options windows' are in ui_saveload.cpp and
// ui_options.cpp. See docs/notes/ui.md.
#include "app.h"
#include "asset_text.h"
#include "options.h"
#include "bink_video.h"
#include "sound.h"
#include "text_helpers.h"
#include "map_object_figures.h"
#include "ui.h"
#include "ui_menu_pieces.h"
#include "ui_mesh.h"
#include "ui_scrollbar.h"
#include "ui_edit_field.h"
#include "ui_saveload.h"
#include "ui_options.h"
#include "ui_message_box.h"

#include "console.h"
#include "text_manager.h"
#include "lib/vecmath.h"
#include "model.h"
#include "net_second_master.h"
#include "resource.h"
#include "ui_cursor.h"
#include "ui_screens.h"
#include "world.h"

#include "lib/rebirth.h"
#include "lib/containers.h"
#include "lib/rect.h"
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include "lib/text_case.h"
#include "terrain.h"
#include "game_singletons.h"
#include "figure_manager.h"
#include "lib/str.h"

#include <cstdlib>
#include <new>
#include <format>

// --- Helpers ------------------------------------------------------------------------------------

namespace {

// The window's 3D model: the stone menu (`escMenu00`) turned about x.
// The menu models' tilts about x (static initialisers: pi * 0.5).
constexpr float kEscMenuTilt = /*007C7FB0*/ 1.5707964f;

struct TitlesTokenizer : TextTokenizer {
    // The current word, or "" past the last one (the original assigned null to a string, which keeps "").
    const char* WordOrEmpty() const { return position != -1 ? word.c_str() : ""; }
};

} // namespace

// --- Esc menu ------------------------------------------------------------------------

void EscMenu::CreateScreen() {
    rect = Rect{0, 0, 800, 600};
    UiModel* model = AddModel();
    model->Load("unmoco1", "escMenu00", "escMenu00labels");
    model->SetScreenPosition(400.0f, 700.0f, 4.8f);
    Vector3 axis = {1.0f, 0.0f, 0.0f};
    Quaternion turn = Quaternion::AxisAngle(axis, kEscMenuTilt);
    model->object->localRotation = turn;

    // The stones from the menus.reg rectangles; the original goes through SetRect with a RECT by value.
    const Rect* rects = g_menuRects + kMainMenuRects;
    for (int stone = 0; stone < kStones; ++stone) {
        stones[stone] = AddRegion();
        stones[stone]->SetRect(rects[stone]);
    }
    models[0]->Play(SkillAnimation::Briefing02);
    g_interface.PlayInterfaceSound("buttons\\menu\\shift.wav");
}

// --- Esc menu clicks -------------------------------------------------------------------

void EscMenu::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: LeftDown(event); break;
    case UiEventKind::MouseMove: MouseMove(event); break;
    case UiEventKind::KeyDown: KeyDown(event); break;
    default: UiContainer::OnEvent(event); break;
    }
}

void EscMenu::LeftDown(UiEvent& event) {
    const int stone = RegionIndex(stones, RegionUnder(event.at));
    if (stone >= 0) {
        g_interface.PlayInterfaceSound("buttons\\menu\\ok.wav");
        event.handled = true;
    }
    std::unique_ptr<UiContainer> opened;
    switch (stone) {
    case kSave:
    case kLoad:
        if (!IsMultiplayerClient()) opened = std::make_unique<SaveLoadWindow>(stone == kSave, true);
        break;
    case kResume:
        g_interface.SetPendingValue(0);
        break;
    case kQuit:
        opened = std::make_unique<MessageWindow>("quit", "quit_msg", MessageQuestion::First, MessageButtons::OkCancel);
        break;
    case kOptions:
        opened = std::make_unique<OptionsWindow>(true);
        break;
    default:
        break;
    }
    if (opened) g_interface.PushWindow(std::move(opened), WindowBackdrop::FirstWindow);
    UiContainer::OnEvent(event);
}

// --- Main menu clicks ------------------------------------------------------------

void MainMenu::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: LeftDown(event); break;
    case UiEventKind::MouseMove: MouseMove(event); break;
    case UiEventKind::KeyDown: KeyDown(event); break;
    default: UiContainer::OnEvent(event); break;
    }
}

void MainMenu::LeftDown(UiEvent& event) {
    const char* okSound = "buttons\\menu\\ok.wav";
    switch (RegionIndex(buttons, RegionUnder(event.at))) {
    case kNewGame: {
        // A new game: the difficulty question (a message box with its own vtables).
        g_interface.PlayInterfaceSound(okSound);
        g_interface.PushWindow(std::make_unique<DifficultyBox>(), WindowBackdrop::FirstWindow);
        break;
    }
    case kLoad: {
        g_interface.PlayInterfaceSound(okSound);
        g_interface.PushWindow(std::make_unique<SaveLoadWindow>(false, false), WindowBackdrop::FirstWindow);
        break;
    }
    case kExit: {
        {
            std::string path = g_dataFolder + "Movies\\Exit.bik";
            PlayMovie(path, false, true, true);
        }
        platform::PostQuit(0);
        break;
    }
    case kOptions: {
        g_interface.PlayInterfaceSound(okSound);
        g_interface.PushWindow(std::make_unique<OptionsWindow>(false), WindowBackdrop::FirstWindow);
        break;
    }
    case kTitles: {
        // The titles (opened over a cleared menu, not as a dialog).
        g_clientWorld.sounds->StopAll(false);
        g_clientWorld.terrain->StopAmbience();
        g_interface.PlayInterfaceSound(okSound);
        ClearSurfaces();
        g_interface.PushWindow(std::make_unique<CreditsScreen>(true, "Titles"), WindowBackdrop::None);
        break;
    }
    case kMultiplayer: {
        // Multiplayer: the hero screen (a server session starts). EI_Plugin's NewMaster first looks up its
        // second master server.
        if (ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNewMaster)) net::second_master::OnMultiplayerClick();
        std::string text = g_clientWorld.texts->TextManager::GetString("reserved_04");
        g_interface.ShowStatusText(text);
        g_serverWorld.SetServerPaused(1);
        g_interface.PlayInterfaceSound(okSound);
        g_interface.PushWindow(std::make_unique<MpHeroScreen>(), WindowBackdrop::FirstWindow);
        break;
    }
    default:
        break;
    }
    UiContainer::OnEvent(event);
}

// --- Message box -----------------------------------------------------------------------

void MessageWindow::CreateScreen() {
    LoadTexture("saveload", TextureKind::Interface);
    UiSurface* surface = AddSurface(NewSurface(Rect{200, 50, 600, 500}));
    surface->PaintWith([this](UiSurface& out) { PaintLabels(out, labels); });
    labels.clear();
    int font = question != MessageQuestion::Exit ? 2 : 1;
    // Measure the title (font 2) and the text (DT_CALCRECT | DT_WORDBREAK | DT_CENTER, 370 wide).
    Rect area = {15, 0, 385, 200};
    int titleHeight = surface->DrawTextIn(title, area, TextStyle{2, 0, render::text::Format::Center | render::text::Format::WordBreak | render::text::Format::CalcRect});
    area = Rect{15, 0, 385, 400};
    int textHeight = surface->DrawTextIn(text, area, TextStyle{font, 0, render::text::Format::Center | render::text::Format::WordBreak | render::text::Format::CalcRect});
    // An edit field takes one line of the font (the extent of "A").
    const bool editing = buttons == MessageButtons::OkCancelEdit;
    if (editing) textHeight = g_interface.TextExtent("A", font).h;
    int height = titleHeight + textHeight;
    int top = (0x226 - (height + 0x3C)) / 2;   // the block centred in 550 pixels
    labels.push_back({title, Rect{15, top - 35, 385, 450}, {2, 0x31B3FF, render::text::Format::Center | render::text::Format::WordBreak}});
    if (editing) {
        edit = AddChild(std::make_unique<UiEditField>(Rect{215, top + titleHeight + 30, 585, height + top + 30}));
        edit->SetLook(15, 0, font, 0x32, kUiTextColour, 0x31B3FF);
        edit->SetText(editText);
    } else {
        labels.push_back({text, Rect{15, top + titleHeight - 20, 385, 450}, {font, kUiTextColour, render::text::Format::Center | render::text::Format::WordBreak}});
    }

    int bottom = top + height + 0x3C;
    Shade(this, 3.0f, 200.0f, static_cast<float>(top), 600.0f, static_cast<float>(bottom));
    Frame(this, 3.0f, 195.0f, static_cast<float>(top - 5), 605.0f, static_cast<float>(bottom + 5));

    // The buttons, 48 high below the block: OK (texels 0x51..0x9B) and cancel (0xA0..0xEA).
    float buttonTop = static_cast<float>(bottom + 0x1A);
    float buttonBottom = static_cast<float>(bottom + 0x4A);
    if (buttons == MessageButtons::OkCancel || editing) {
        AddBlankSprite()->SetRectangle(kDialogDepth, kSpriteScale, 263.0f, buttonTop, 337.0f, buttonBottom, 0x51, 2, 0x9B, 0x32);
        AddBlankSprite()->SetRectangle(kDialogDepth, kSpriteScale, 463.0f, buttonTop, 537.0f, buttonBottom, 0xA0, 2, 0xEA, 0x32);
        okButton = AddRect(this, 263, bottom + 0x1A, 337, bottom + 0x4A);
        okButton->SetTip(0xC3B4);
        cancelButton = AddRect(this, 463, bottom + 0x1A, 537, bottom + 0x4A);
        cancelButton->SetTip(0xC3B5);
    } else if (buttons == MessageButtons::Ok) {
        AddBlankSprite()->SetRectangle(kDialogDepth, kSpriteScale, 363.0f, buttonTop, 437.0f, buttonBottom, 0x51, 2, 0x9B, 0x32);
        okButton = AddRect(this, 363, bottom + 0x1A, 437, bottom + 0x4A);
        okButton->SetTip(0xC3B4);
    } else {
        // Cancel only: an empty region first, so the button is the second region as with two buttons (the
        // harness's region steps count regions).
        AddBlankSprite()->SetRectangle(kDialogDepth, kSpriteScale, 363.0f, buttonTop, 437.0f, buttonBottom, 0xA0, 2, 0xEA, 0x32);
        AddRegion();
        cancelButton = AddRect(this, 363, bottom + 0x1A, 437, bottom + 0x4A);
        cancelButton->SetTip(0xC3B5);
    }
}

// --- Difficulty question ---------------------------------------------------------------

void DifficultyBox::CreateScreen() {
    LoadTexture("saveload", TextureKind::Interface);
    UiSurface* surface = AddSurface(NewSurface(Rect{200, 100, 600, 500}));
    surface->PaintWith([this](UiSurface& out) { PaintLabels(out, labels); });
    labels.clear();
    // Measure the title (font 2) and the explanation (font 1, DT_CALCRECT | DT_WORDBREAK).
    Rect area = {15, 0, 385, 200};
    int titleHeight = surface->DrawTextIn(title, area, TextStyle{2, 0, render::text::Format::Center | render::text::Format::WordBreak | render::text::Format::CalcRect});
    std::string explanation;
    int textHeight = 0;
    if (g_clientWorld.texts->Open("tip option_difficulty") != 0) {
        explanation = g_clientWorld.texts->ReadToHash();
        str::TrimRight(explanation, "\r\n");
        area = Rect{15, 0, 385, 300};
        textHeight = surface->DrawTextIn(explanation, area, TextStyle{1, 0, render::text::Format::WordBreak | render::text::Format::CalcRect});
    }
    int total = textHeight + titleHeight + 0x6C;
    int top = (0x226 - total) / 2;   // the block centred in 550 pixels
    labels.push_back({title, Rect{15, top - 0x55, 385, 450}, {2, 0x31B3FF, render::text::Format::Center | render::text::Format::WordBreak}});
    labels.push_back({explanation, Rect{15, titleHeight + top - 7, 385, 450}, {1, kUiTextColour, render::text::Format::WordBreak}});

    // The two choices, their labels centred on 430 (surface x 230) by their mean width.
    std::string newEasy;
    std::string hard;
    newEasy = g_clientWorld.texts->GetString("difficulty_easy");
    hard = g_clientWorld.texts->GetString("difficulty_hard");
    Size easySize = g_interface.TextExtent(newEasy, 2);
    Size hardSize = g_interface.TextExtent(hard, 2);
    int half = ((easySize.w + hardSize.w) / 2 + 0x1E) / 2;
    int row = titleHeight + top + 0x1E;   // layout y of the easy row (the surface starts at 100)
    labels.push_back({newEasy, Rect{0xE6 - half, row - 100, 385, 450}, {2, kUiTextColour}});
    labels.push_back({hard, Rect{0xE6 - half, row - 0x4C, 385, 450}, {2, kUiTextColour}});
    easy = 0;

    // The check boxes (texels 0xD1..0xE6 x 0x37..0x4C); the hard one checked (its texture shifted by
    // the sprite's U offset, as the button handler does).
    int boxLeft = 400 - half;
    int boxRight = 0x1A5 - half;
    easyBox = AddBlankSprite();
    easyBox->SetRectangle(2.5f, kSpriteScale, static_cast<float>(boxLeft), static_cast<float>(row),
                          static_cast<float>(boxRight), static_cast<float>(row + 0x15), 0xD1, 0x37, 0xE6, 0x4C);
    hardBox = AddBlankSprite();
    hardBox->SetRectangle(2.5f, kSpriteScale, static_cast<float>(boxLeft), static_cast<float>(row + 0x18),
                          static_cast<float>(boxRight), static_cast<float>(row + 0x2D), 0xD1, 0x37, 0xE6, 0x4C);
    hardBox->textureOffsetU = kCheckedOffset;

    int bottom = top + total;
    Shade(this, 3.0f, 200.0f, static_cast<float>(top), 600.0f, static_cast<float>(bottom));
    Frame(this, 3.0f, 195.0f, static_cast<float>(top - 5), 605.0f, static_cast<float>(bottom + 5));
    float buttonTop = static_cast<float>(bottom + 0x1A);
    float buttonBottom = static_cast<float>(bottom + 0x4A);
    AddBlankSprite()->SetRectangle(kDialogDepth, kSpriteScale, 263.0f, buttonTop, 337.0f, buttonBottom, 0x51, 2, 0x9B, 0x32);
    AddBlankSprite()->SetRectangle(kDialogDepth, kSpriteScale, 463.0f, buttonTop, 537.0f, buttonBottom, 0xA0, 2, 0xEA, 0x32);
    okButton = AddRect(this, 263, bottom + 0x1A, 337, bottom + 0x4A);
    okButton->SetTip(0xC3B4);
    cancelButton = AddRect(this, 463, bottom + 0x1A, 537, bottom + 0x4A);
    cancelButton->SetTip(0xC3B5);
    easyArea = AddRect(this, boxLeft, row, boxRight, row + 0x15);
    hardArea = AddRect(this, boxLeft, row + 0x18, boxRight, row + 0x2D);
}

// --- Titles ----------------------------------------------------------------------------

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
                std::string first = words.WordOrEmpty();
                record.scroll = text::CompareIgnoringCase(first,
                                                "#scroll") ==
                                0;
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
    player.Open((g_dataFolder + "Movies\\" + currentName + ".bik").c_str(), false);
    finished = false;
}
