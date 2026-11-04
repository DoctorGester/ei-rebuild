// Layout builders of the windows opened from the Esc menu (their Create virtual): the regions, sprites, surfaces,
// scroll bar and 3D model each window is made of. Rebirth patches the sprite placement and UiRegion::SetRect for its
// widescreen layout. See docs/notes/ui.md.
#include "app.h"
#include "asset_text.h"
#include "options.h"
#include "bink_video.h"
#include "sound.h"
#include "text_helpers.h"
#include "map_object_figures.h"
#include "ui.h"

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

UiRegion* AddRect(UiWindow* window, int left, int top, int right, int bottom) {
    UiRegion* region = window->AddRegion();
    region->SetRect(left, top, right, bottom);
    return region;
}

UiScrollbar* NewScrollbar(Interface3D* ui, int left, int top, int right, int bottom) {
    return new UiScrollbar(ui, Rect{left, top, right, bottom}, 3.0f, 0, 1.0f);
}

void Frame(UiWindow* window, float depth, float left, float top, float right, float bottom) {
    window->AddBlankSprite()->SetFrame(depth, 100.0f, left, top, right, bottom, 5.0f, 2, 0xFE, 0xB4, 0xF9);
}

void Shade(UiWindow* window, float depth, float left, float top, float right, float bottom) {
    UiMesh* sprite = window->AddBlankSprite();
    sprite->SetRectangle(depth, 100.0f, left, top, right, bottom, 0, 0, kUiTextureSize, kUiTextureSize);
    sprite->texture = nullptr;
    sprite->flags |= FigureMeshFlag::Flat;
    sprite->flatColour = 0xA0000000;
}

// The window's 3D model: the stone menu (`escMenu00`) turned about x.
// The menu models' tilts about x (static initialisers: pi * 0.5).
constexpr float kEscMenuTilt = /*007C7FB0*/ 1.5707964f;
constexpr float kOptionsTilt = /*007C81A0*/ 1.5707964f;
constexpr float kSaveLoadTilt = /*007C81C8*/ 1.5707964f;

UiModel* AddMenuModel(UiWindow* window, float angle) {
    UiModel* model = window->AddModel();
    model->Load("unmoco1", "escMenu00", "escMenu00labels");
    Vector3 axis = {1.0f, 0.0f, 0.0f};
    Quaternion turn = Quaternion::AxisAngle(axis, angle);
    model->object->localRotation = turn;
    return model;
}

constexpr int kOptionGroups = 11;
const char* OptionGroupName(int index) {
    static constexpr const char* kNames[13] = /*007947A8*/ {"video",  "sound",  "sens",   "game",    "select",
                                                            "actions", "camera", "items",  "spells",  "cshots",
                                                            "windows", "debug1", "debug2"};
    return kNames[index];
}


struct TitlesTokenizer : TextTokenizer {
    // The current word, or "" past the last one (the original assigned null to a string, which keeps "").
    const char* WordOrEmpty() const { return position != -1 ? word.c_str() : ""; }
};

} // namespace

// --- Save/load window -------------------------------------------------------------

void SaveLoadWindow::CreateScreen() {
    rect = Rect{0, 0, 800, 600};
    LoadTexture("saveload", TextureKind::Interface);

    AddBlankSprite()->SetRectangle(kDialogDepth, kSpriteScale, 163.0f, 526.0f, 237.0f, 574.0f, 0x51, 2, 0x9B, 0x32);
    AddBlankSprite()->SetRectangle(kDialogDepth, kSpriteScale, 563.0f, 526.0f, 637.0f, 574.0f, 0xA0, 2, 0xEA, 0x32);
    AddBlankSprite()->SetRectangle(kDialogDepth, kSpriteScale, 363.0f, 526.0f, 437.0f, 574.0f, 2, 2, 0x4C, 0x32);
    AddRect(this, 163, 526, 237, 574)->SetTip(isSave ? 0xEAC4 : 0xEAC5);
    AddRect(this, 563, 526, 637, 574)->SetTip(0xEAC7);
    AddRect(this, 363, 526, 437, 574)->SetTip(0xEAC6);

    // The picture of the selected save (ShowPicture gives it its texture and texels).
    UiMesh* pictureSprite = AddBlankSprite();
    pictureSprite->SetRectangle(kDialogDepth, kSpriteScale, kPictureRect, 0, 0x100, 0x100, 0);
    pictureSprite->texture = nullptr;
    UiMesh* highlight = AddBlankSprite();
    highlight->SetRectangle(kDialogDepth, kSpriteScale, 108.0f, 108.0f, 370.0f, 132.0f, 0, 0, kUiTextureSize, kUiTextureSize);
    highlight->texture = nullptr;
    highlight->flatColour = 0xC8A06800;
    highlight->flags |= FigureMeshFlag::Flat;
    Frame(this, 2.0f, 95.0f, 95.0f, 395.0f, 505.0f);     // the list
    Frame(this, 2.0f, 405.0f, 95.0f, 705.0f, 325.0f);    // the picture
    Frame(this, 2.0f, 405.0f, 335.0f, 705.0f, 505.0f);   // the description
    Shade(this, 5.0f, 100.0f, 100.0f, 390.0f, 500.0f);
    Shade(this, 5.0f, 410.0f, 100.0f, 700.0f, 320.0f);
    Shade(this, 5.0f, 410.0f, 340.0f, 700.0f, 500.0f);

    for (int top = 108; top < 492; top += 24) AddRect(this, 108, top, 370, top + 24);

    UiSurface* list = NewSurface(ui, Rect{100, 100, 700, 500});
    AddSurface(list);
    list->PaintWith([this](UiSurface& out) { PaintList(out); });
    scrollbar = NewScrollbar(ui, 108, 108, 384, 492);
    AddChild(scrollbar);

    UiModel* model = AddMenuModel(this, kSaveLoadTilt);
    model->SetScreenPosition(400.0f, 700.0f, 4.8f);
    model->ShowAllParts(0);
    if (isSave) {
        model->ShowPart("save", 1);
        model->ShowPart("savelabel", 1);
    } else {
        model->ShowPart("load", 1);
        model->ShowPart("loadlabel", 1);
    }

    dialog = nullptr;
    picture = nullptr;
    if (sliding) slide = 0.0f;
    FillEntries();
    if (isSave) {
        // Saving: the first save is selected and its title offered as the name.
        selected = 0;
        if (EntryCount() != 0) {
            nameTyped = false;
            name = entries.front().title;
            cursor = lib::Count(name);
        }
    } else {
        selected = EntryCount() != 0 ? 0 : -1;
    }
    RefreshListVanilla();
    ShowPicture();
    if (!sliding) ui->OpenHelp(false);
}

// --- Options window ------------------------------------------------------------------

void OptionsWindow::CreateScreen() {
    rect = Rect{0, 0, 800, 600};
    LoadTexture("saveload", TextureKind::Interface);
    CreateTexture(0x100, 0x100, TextureKind::Blank565);

    AddBlankSprite()->SetRectangle(kDialogDepth, kSpriteScale, 213.0f, 526.0f, 287.0f, 574.0f, 0x51, 2, 0x9B, 0x32);
    AddBlankSprite()->SetRectangle(kDialogDepth, kSpriteScale, 513.0f, 526.0f, 587.0f, 574.0f, 0xA0, 2, 0xEA, 0x32);
    AddRect(this, 213, 526, 287, 574)->SetTip(0x111D4);
    AddRect(this, 513, 526, 587, 574)->SetTip(0x111D5);
    UiMesh* highlight = AddBlankSprite();
    highlight->SetRectangle(kDialogDepth, kSpriteScale, 250.0f, 108.0f, 692.0f, 132.0f, 0, 0, kUiTextureSize, kUiTextureSize);
    highlight->texture = nullptr;
    highlight->flatColour = 0xC8A06800;
    highlight->flags |= FigureMeshFlag::Flat;

    // Regions 2-12: the option groups, each with its button sprite, tooltip and title.
    std::string text;
    for (int k = 0, top = 156; k < kOptionGroups; ++k, top += 24) {
        UiRegion* region = AddRect(this, 100, top, 250, top + 24);
        AddBlankSprite()->SetRectangle(kDialogDepth, kSpriteScale, 110.0f, static_cast<float>(top + 2), 240.0f,
                                   static_cast<float>(top + 21), 0x7A, 0x6C, 0xFC, 0x7F);
        text = std::format("tip option_group_{}", OptionGroupName(k));
        if (g_clientWorld.texts->Open(text) != 0) text = g_clientWorld.texts->ReadToHash();
        region->SetTipString(text);
        {
            std::string prefix("option_group_");
            std::string key = prefix + OptionGroupName(k);
            text = g_clientWorld.texts->GetString(key);
        }
        groupNames.push_back(text);
    }

    Frame(this, 2.0f, 95.0f, 95.0f, 705.0f, 505.0f);
    Shade(this, 5.0f, 100.0f, 100.0f, 700.0f, 500.0f);
    for (int top = 156; top < 492; top += 24) AddRect(this, 250, top, 520, top + 24);
    UiSurface* surface = NewSurface(ui, Rect{100, 100, 700, 500});
    AddSurface(surface);
    surface->PaintWith([this](UiSurface& out) { PaintRows(out); });

    UiModel* model = AddMenuModel(this, kOptionsTilt);
    model->SetScreenPosition(400.0f, 700.0f, 4.8f);
    model->ShowAllParts(0);
    model->ShowPart("options", 1);
    model->ShowPart("optionslabel", 1);

    if (sliding) slide = 0.0f;
    waitingForKey = false;
    pressedKey = nullptr;
    BuildSettings();
    LoadSettings();
    BuildBindings();
    shadowsChanged = false;
    ShowGroup(0);
    if (!sliding) ui->OpenHelp(false);
}

// --- Esc menu ------------------------------------------------------------------------

void EscMenu::CreateScreen() {
    rect = Rect{0, 0, 800, 600};
    UiModel* model = AddModel();
    model->Load("unmoco1", "escMenu00", "escMenu00labels");
    model->SetScreenPosition(400.0f, 700.0f, 4.8f);
    Vector3 axis = {1.0f, 0.0f, 0.0f};
    Quaternion turn = Quaternion::AxisAngle(axis, kEscMenuTilt);
    model->object->localRotation = turn;

    // Regions 0-4 (save, load, resume, quit, options) from the menus.reg rectangles; the
    // original goes through SetRect with a RECT by value.
    const Rect* rects = g_menuRects + kMainMenuRects;
    for (const Rect* source = rects; source < rects + 5; ++source) {
        UiRegion* region = AddRegion();
        region->SetRect(*source);
    }
    models[0]->Play(SkillAnimation::Briefing02);
    ui->PlayInterfaceSound("buttons\\menu\\shift.wav");
}

// --- Esc menu clicks -------------------------------------------------------------------

void EscMenu::LeftDown(Point point) {
    Interface3D* currentUi = ui;
    int region = RegionAt(point);
    if (region >= 0) {
        currentUi->PlayInterfaceSound("buttons\\menu\\ok.wav");
        handled = true;
    }
    UiContainer* opened = nullptr;
    bool open = true;
    switch (region) {
    case 0:   // save
    case 1: { // load
        if (IsMultiplayerClient()) {
            open = false;
            break;
        }
        opened = new SaveLoadWindow(currentUi, region == 0, true);
        break;
    }
    case 2:   // resume
        currentUi->SetPendingValue(0);
        open = false;
        break;
    case 3: { // quit: the message box
        opened = new MessageWindow(currentUi, "quit", "quit_msg", MessageQuestion::First, MessageButtons::OkCancel);
        break;
    }
    case 4: { // options
        opened = new OptionsWindow(currentUi, true);
        break;
    }
    default:
        open = false;
        break;
    }
    if (open) currentUi->PushWindow(opened, WindowBackdrop::FirstWindow);
    UiContainer::LeftDown(point);
}

// --- Main menu clicks ------------------------------------------------------------

void MainMenu::LeftDown(Point point) {
    Interface3D* currentUi = ui;
    const char* okSound = "buttons\\menu\\ok.wav";
    switch (RegionAt(point)) {
    case 0: {
        // A new game: the difficulty question (a message box with its own vtables).
        currentUi->PlayInterfaceSound(okSound);
        currentUi->PushWindow(new DifficultyBox(currentUi), WindowBackdrop::FirstWindow);
        break;
    }
    case 1: {
        currentUi->PlayInterfaceSound(okSound);
        currentUi->PushWindow(new SaveLoadWindow(currentUi, false, false), WindowBackdrop::FirstWindow);
        break;
    }
    case 2: {
        {
            std::string path = g_dataFolder + "Movies\\Exit.bik";
            PlayMovie(path, false, true, true);
        }
        platform::PostQuit(0);
        break;
    }
    case 3: {
        currentUi->PlayInterfaceSound(okSound);
        currentUi->PushWindow(new OptionsWindow(currentUi, false), WindowBackdrop::FirstWindow);
        break;
    }
    case 4: {
        // The titles (opened over a cleared menu, not as a dialog).
        g_clientWorld.sounds->StopAll(false);
        g_clientWorld.terrain->StopAmbience();
        currentUi->PlayInterfaceSound(okSound);
        ClearSurfaces();
        currentUi->PushWindow(new CreditsScreen(currentUi, true, "Titles"), WindowBackdrop::None);
        break;
    }
    case 5: {
        // Multiplayer: the hero screen (a server session starts). EI_Plugin's NewMaster first looks up its
        // second master server.
        if (ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNewMaster)) net::second_master::OnMultiplayerClick();
        std::string text = g_clientWorld.texts->TextManager::GetString("reserved_04");
        g_interface.ShowStatusText(text);
        g_serverWorld.SetServerPaused(1);
        currentUi->PlayInterfaceSound(okSound);
        currentUi->PushWindow(new MpHeroScreen(currentUi), WindowBackdrop::FirstWindow);
        break;
    }
    default:
        break;
    }
    UiContainer::LeftDown(point);
}

// --- Message box -----------------------------------------------------------------------

void MessageWindow::CreateScreen() {
    LoadTexture("saveload", TextureKind::Interface);
    UiSurface* surface = NewSurface(ui, Rect{200, 50, 600, 500});
    AddSurface(surface)->PaintWith([this](UiSurface& out) { PaintLabels(out, labels); });
    labels.clear();
    int font = question != MessageQuestion::Exit ? 2 : 1;
    // Measure the title (font 2) and the text (DT_CALCRECT | DT_WORDBREAK | DT_CENTER, 370 wide).
    Rect area = {15, 0, 385, 200};
    int titleHeight = surface->DrawTextIn(title, area, TextStyle{2, 0, render::text::kCenter | render::text::kWordBreak | render::text::kCalcRect});
    area = Rect{15, 0, 385, 400};
    int textHeight = surface->DrawTextIn(text, area, TextStyle{font, 0, render::text::kCenter | render::text::kWordBreak | render::text::kCalcRect});
    // An edit field takes one line of the font (the extent of "A").
    const bool editing = buttons == MessageButtons::OkCancelEdit;
    if (editing) textHeight = ui->TextExtent("A", font).h;
    int height = titleHeight + textHeight;
    int top = (0x226 - (height + 0x3C)) / 2;   // the block centred in 550 pixels
    labels.push_back({title, Rect{15, top - 35, 385, 450}, {2, 0x31B3FF, render::text::kCenter | render::text::kWordBreak}});
    if (editing) {
        edit = new UiEditField(ui, Rect{215, top + titleHeight + 30, 585, height + top + 30});
        edit->SetLook(15, 0, font, 0x32, kUiTextColour, 0x31B3FF);
        edit->SetText(editText);
        AddChild(edit);
    } else {
        labels.push_back({text, Rect{15, top + titleHeight - 20, 385, 450}, {font, kUiTextColour, render::text::kCenter | render::text::kWordBreak}});
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
        AddRect(this, 263, bottom + 0x1A, 337, bottom + 0x4A)->SetTip(0xC3B4);
        AddRect(this, 463, bottom + 0x1A, 537, bottom + 0x4A)->SetTip(0xC3B5);
    } else if (buttons == MessageButtons::Ok) {
        AddBlankSprite()->SetRectangle(kDialogDepth, kSpriteScale, 363.0f, buttonTop, 437.0f, buttonBottom, 0x51, 2, 0x9B, 0x32);
        AddRect(this, 363, bottom + 0x1A, 437, bottom + 0x4A)->SetTip(0xC3B4);
    } else {
        // Cancel only: region 0 stays empty, so the button is region 1 as with two buttons.
        AddBlankSprite()->SetRectangle(kDialogDepth, kSpriteScale, 363.0f, buttonTop, 437.0f, buttonBottom, 0xA0, 2, 0xEA, 0x32);
        AddRegion();
        AddRect(this, 363, bottom + 0x1A, 437, bottom + 0x4A)->SetTip(0xC3B5);
    }
}

// --- Difficulty question ---------------------------------------------------------------

void DifficultyBox::CreateScreen() {
    LoadTexture("saveload", TextureKind::Interface);
    UiSurface* surface = NewSurface(ui, Rect{200, 100, 600, 500});
    AddSurface(surface)->PaintWith([this](UiSurface& out) { PaintLabels(out, labels); });
    labels.clear();
    // Measure the title (font 2) and the explanation (font 1, DT_CALCRECT | DT_WORDBREAK).
    Rect area = {15, 0, 385, 200};
    int titleHeight = surface->DrawTextIn(title, area, TextStyle{2, 0, render::text::kCenter | render::text::kWordBreak | render::text::kCalcRect});
    std::string explanation;
    int textHeight = 0;
    if (g_clientWorld.texts->Open("tip option_difficulty") != 0) {
        explanation = g_clientWorld.texts->ReadToHash();
        str::TrimRight(explanation, "\r\n");
        area = Rect{15, 0, 385, 300};
        textHeight = surface->DrawTextIn(explanation, area, TextStyle{1, 0, render::text::kWordBreak | render::text::kCalcRect});
    }
    int total = textHeight + titleHeight + 0x6C;
    int top = (0x226 - total) / 2;   // the block centred in 550 pixels
    labels.push_back({title, Rect{15, top - 0x55, 385, 450}, {2, 0x31B3FF, render::text::kCenter | render::text::kWordBreak}});
    labels.push_back({explanation, Rect{15, titleHeight + top - 7, 385, 450}, {1, kUiTextColour, render::text::kWordBreak}});

    // The two choices, their labels centred on 430 (surface x 230) by their mean width.
    std::string newEasy;
    std::string hard;
    newEasy = g_clientWorld.texts->GetString("difficulty_easy");
    hard = g_clientWorld.texts->GetString("difficulty_hard");
    Size easySize = ui->TextExtent(newEasy, 2);
    Size hardSize = ui->TextExtent(hard, 2);
    int half = ((easySize.w + hardSize.w) / 2 + 0x1E) / 2;
    int row = titleHeight + top + 0x1E;   // layout y of the easy row (the surface starts at 100)
    labels.push_back({newEasy, Rect{0xE6 - half, row - 100, 385, 450}, {2, kUiTextColour}});
    labels.push_back({hard, Rect{0xE6 - half, row - 0x4C, 385, 450}, {2, kUiTextColour}});
    easy = 0;

    // The check boxes (texels 0xD1..0xE6 x 0x37..0x4C); the hard one checked (its texture shifted by
    // the sprite's U offset, as the button handler does).
    int boxLeft = 400 - half;
    int boxRight = 0x1A5 - half;
    AddBlankSprite()->SetRectangle(2.5f, kSpriteScale, static_cast<float>(boxLeft), static_cast<float>(row),
                               static_cast<float>(boxRight), static_cast<float>(row + 0x15), 0xD1, 0x37, 0xE6, 0x4C);
    UiMesh* hardBox = AddBlankSprite();
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
    AddRect(this, 263, bottom + 0x1A, 337, bottom + 0x4A)->SetTip(0xC3B4);
    AddRect(this, 463, bottom + 0x1A, 537, bottom + 0x4A)->SetTip(0xC3B5);
    AddRect(this, boxLeft, row, boxRight, row + 0x15);
    AddRect(this, boxLeft, row + 0x18, boxRight, row + 0x2D);
}

// --- Titles ----------------------------------------------------------------------------

void CreditsScreen::CreateScreen() {
    Interface3D* currentUi = ui;
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
        textSurface = new UiScrollingSurface(currentUi, &area);
        AddSurface(textSurface);
        textSurface->Hide();
    }
    {
        Rect area = {10, 0x6E, 0x316, 0x8C};
        headerSurface = AddSurface(new UiSurface(currentUi, &area, SurfaceBlend::Keyed));
        headerSurface->PaintWith([this](UiSurface& out) { PaintHeader(out); });
        headerSurface->Hide();
    }
    if (fromGame) g_serverWorld.SetServerPaused(0);
    savedDisplay = static_cast<int>(g_sceneRenderer.fogColour);
    g_sceneRenderer.fogColour = 0;
    player.Open((g_dataFolder + "Movies\\" + currentName + ".bik").c_str(), false);
    finished = false;
}
