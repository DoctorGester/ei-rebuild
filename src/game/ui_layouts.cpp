// Layout builders of the windows opened from the Esc menu (their Create virtual): the regions, sprites, surfaces,
// scroll bar and 3D model each window is made of. Rebirth patches the sprite placement and CUiRegion::SetRect for its
// widescreen layout. See docs/notes/ui.md.
#include "app_helpers.h"
#include "asset_text.h"
#include "options.h"
#include "bink_video.h"
#include "sound.h"
#include "text_helpers.h"
#include "ui.h"

#include "console.h"
#include "item_db_text.h"
#include "maths.h"
#include "model.h"
#include "net_second_master.h"
#include "resource.h"
#include "ui_cursor.h"
#include "ui_screens.h"
#include "world.h"

#include "ei/rebirth.h"
#include "lib/gameapi.h"
#include <cstdint>
#include <cstring>
#include <string>
#include "lib/text_case.h"
#include "world_glue.h"
#include "game_data.h"
#include "game_singletons.h"
#include "figure_manager.h"
#include "lib/str.h"

#include <cstdlib>
#include <new>

// --- Helpers ------------------------------------------------------------------------------------

namespace {

CUiRegion* AddRect(CUiWindow* window, int left, int top, int right, int bottom) {
    CUiRegion* region = window->AddRegion();
    region->SetRect(left, top, right, bottom);
    return region;
}

CUiSurface* NewSurface(CInterface3D* ui, int left, int top, int right, int bottom) {
        return new CUiSurface(ui, left, top, right, bottom, SurfaceBlend::Keyed, true);
}

CUiScrollbar* NewScrollbar(CInterface3D* ui, int left, int top, int right, int bottom) {
    return new CUiScrollbar(ui, RECT{left, top, right, bottom}, 3.0f, 0, 1.0f);
}

void Frame(CUiWindow* window, float depth, float left, float top, float right, float bottom) {
    window->AddBlankSprite()->SetFrame(depth, 100.0f, left, top, right, bottom, 5.0f, 2, 0xFE, 0xB4, 0xF9);
}

void Shade(CUiWindow* window, float depth, float left, float top, float right, float bottom) {
    CUiMesh* sprite = window->AddBlankSprite();
    sprite->SetRectangle(depth, 100.0f, left, top, right, bottom, 0, 0, 0x100, 0x100);
    sprite->texture_ = nullptr;
    sprite->flags_ |= FigureMeshFlag::Flat;
    sprite->flatColour_ = 0xA0000000;
}

// The window's 3D model: the stone menu (`escMenu00`) turned about x.
// The menu models' tilts about x (static initialisers: pi * 0.5).
constexpr float kEscMenuTilt = /*007C7FB0*/ 1.5707964f;
constexpr float kOptionsTilt = /*007C81A0*/ 1.5707964f;
constexpr float kSaveLoadTilt = /*007C81C8*/ 1.5707964f;

CUiModel* AddMenuModel(CUiWindow* window, float angle) {
    CUiModel* model = window->AddModel();
    model->Load("unmoco1", "escMenu00", "escMenu00labels");
    Vector3 axis = {1.0f, 0.0f, 0.0f};
    Quaternion turn;
    turn.SetAxisAngle(axis, angle);
    model->object_->localRotation_ = turn;
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

void CSaveLoadWindow::CreateScreen() {
    rect_ = RECT{0, 0, 800, 600};
    LoadTexture("saveload", 3);
    // The selected save's picture (5:6:5).
    CreateTexture(0x100, 0x100, 0x1E);

    AddBlankSprite()->SetRectangle(3.0f, 100.0f, 163.0f, 526.0f, 237.0f, 574.0f, 0x51, 2, 0x9B, 0x32);
    AddBlankSprite()->SetRectangle(3.0f, 100.0f, 563.0f, 526.0f, 637.0f, 574.0f, 0xA0, 2, 0xEA, 0x32);
    AddBlankSprite()->SetRectangle(3.0f, 100.0f, 363.0f, 526.0f, 437.0f, 574.0f, 2, 2, 0x4C, 0x32);
    AddRect(this, 163, 526, 237, 574)->SetTip(isSave_ ? 0xEAC4 : 0xEAC5);
    AddRect(this, 563, 526, 637, 574)->SetTip(0xEAC7);
    AddRect(this, 363, 526, 437, 574)->SetTip(0xEAC6);

    // The picture of the selected save (texture of the window's second texture object).
    CUiMesh* picture = AddBlankSprite();
    picture->SetRectangle(3.0f, 100.0f, 410.0f, 100.0f, 700.0f, 320.0f, 0, 0x100, 0x100, 0x40);
    picture->texture_ = nullptr;
    CUiMesh* highlight = AddBlankSprite();
    highlight->SetRectangle(3.0f, 100.0f, 108.0f, 108.0f, 370.0f, 132.0f, 0, 0, 0x100, 0x100);
    highlight->texture_ = nullptr;
    highlight->flatColour_ = 0xC8A06800;
    highlight->flags_ |= FigureMeshFlag::Flat;
    Frame(this, 2.0f, 95.0f, 95.0f, 395.0f, 505.0f);     // the list
    Frame(this, 2.0f, 405.0f, 95.0f, 705.0f, 325.0f);    // the picture
    Frame(this, 2.0f, 405.0f, 335.0f, 705.0f, 505.0f);   // the description
    Shade(this, 5.0f, 100.0f, 100.0f, 390.0f, 500.0f);
    Shade(this, 5.0f, 410.0f, 100.0f, 700.0f, 320.0f);
    Shade(this, 5.0f, 410.0f, 340.0f, 700.0f, 500.0f);

    for (int top = 108; top < 492; top += 24) AddRect(this, 108, top, 370, top + 24);

    CUiSurface* list = NewSurface(interface_, 100, 100, 700, 500);
    AddSurface(list);
    scrollbar_ = NewScrollbar(interface_, 108, 108, 384, 492);
    AddChild(scrollbar_);

    CUiModel* model = AddMenuModel(this, kSaveLoadTilt);
    model->SetScreenPosition(400.0f, 700.0f, 4.8f);
    model->ShowAllParts(0);
    if (isSave_) {
        model->ShowPart("save", 1);
        model->ShowPart("savelabel", 1);
    } else {
        model->ShowPart("load", 1);
        model->ShowPart("loadlabel", 1);
    }

    dialog_ = nullptr;
    if (sliding_) slide_ = 0.0f;
    FillEntries();
    if (isSave_) {
        // Saving: the first save is selected and its title offered as the name.
        selected_ = 0;
        if (EntryCount() != 0) {
            nameTyped_ = false;
            name_ = entries_.front().title_;
            cursor_ = name_.size();
        }
    } else {
        selected_ = EntryCount() != 0 ? 0 : -1;
    }
    RefreshListVanilla();
    ShowPicture();
    if (!sliding_) interface_->OpenHelp(0);
}

// --- Options window ------------------------------------------------------------------

void COptionsWindow::CreateScreen() {
    rect_ = RECT{0, 0, 800, 600};
    LoadTexture("saveload", 3);
    CreateTexture(0x100, 0x100, 0x1E);

    AddBlankSprite()->SetRectangle(3.0f, 100.0f, 213.0f, 526.0f, 287.0f, 574.0f, 0x51, 2, 0x9B, 0x32);
    AddBlankSprite()->SetRectangle(3.0f, 100.0f, 513.0f, 526.0f, 587.0f, 574.0f, 0xA0, 2, 0xEA, 0x32);
    AddRect(this, 213, 526, 287, 574)->SetTip(0x111D4);
    AddRect(this, 513, 526, 587, 574)->SetTip(0x111D5);
    CUiMesh* highlight = AddBlankSprite();
    highlight->SetRectangle(3.0f, 100.0f, 250.0f, 108.0f, 692.0f, 132.0f, 0, 0, 0x100, 0x100);
    highlight->texture_ = nullptr;
    highlight->flatColour_ = 0xC8A06800;
    highlight->flags_ |= FigureMeshFlag::Flat;

    // Regions 2-12: the option groups, each with its button sprite, tooltip and title.
    std::string text;
    for (int k = 0, top = 156; k < kOptionGroups; ++k, top += 24) {
        CUiRegion* region = AddRect(this, 100, top, 250, top + 24);
        AddBlankSprite()->SetRectangle(3.0f, 100.0f, 110.0f, static_cast<float>(top + 2), 240.0f,
                                   static_cast<float>(top + 21), 0x7A, 0x6C, 0xFC, 0x7F);
        text = str::Format("tip option_group_%s", OptionGroupName(k));
        if (g_clientWorld->texts_->Open(text) != 0) g_clientWorld->texts_->ReadToHash(&text, 1);
        region->SetTipString(text);
        {
            std::string prefix("option_group_");
            std::string key = prefix + OptionGroupName(k);
            g_clientWorld->texts_->GetString(key, &text);
        }
        groupNames_.push_back(text);
    }

    Frame(this, 2.0f, 95.0f, 95.0f, 705.0f, 505.0f);
    Shade(this, 5.0f, 100.0f, 100.0f, 700.0f, 500.0f);
    for (int top = 156; top < 492; top += 24) AddRect(this, 250, top, 520, top + 24);
    CUiSurface* surface = NewSurface(interface_, 100, 100, 700, 500);
    AddSurface(surface);

    CUiModel* model = AddMenuModel(this, kOptionsTilt);
    model->SetScreenPosition(400.0f, 700.0f, 4.8f);
    model->ShowAllParts(0);
    model->ShowPart("options", 1);
    model->ShowPart("optionslabel", 1);

    if (sliding_) slide_ = 0.0f;
    waitingForKey_ = false;
    pressedKey_ = nullptr;
    BuildSettings();
    LoadSettings();
    BuildBindings();
    shadowsChanged_ = false;
    ShowGroup(0);
    if (!sliding_) interface_->OpenHelp(0);
}

// --- Esc menu ------------------------------------------------------------------------

void CEscMenu::CreateScreen() {
    rect_ = RECT{0, 0, 800, 600};
    CUiModel* model = AddModel();
    model->Load("unmoco1", "escMenu00", "escMenu00labels");
    model->SetScreenPosition(400.0f, 700.0f, 4.8f);
    Vector3 axis = {1.0f, 0.0f, 0.0f};
    Quaternion turn;
    turn.SetAxisAngle(axis, kEscMenuTilt);
    model->object_->localRotation_ = turn;

    // Regions 0-4 (save, load, resume, quit, options) from the menus.reg rectangles; the
    // original goes through SetRect with a RECT by value.
    const RECT* rects = g_menuRects + kMainMenuRects;
    for (const RECT* source = rects; source < rects + 5; ++source) {
        CUiRegion* region = AddRegion();
        RECT rect;
        gameapi::CopyRect(&rect, source);
        region->SetRect(rect.left, rect.top, rect.right, rect.bottom);
    }
    models_[0]->Play(1);
    interface_->PlaySound("buttons\\menu\\shift.wav");
}

// --- Esc menu clicks -------------------------------------------------------------------

void CEscMenu::LeftDown(int buttons, int x, int y) {
    CInterface3D* ui = interface_;
    int region = RegionAt(x, y);
    if (region >= 0) {
        ui->PlaySound("buttons\\menu\\ok.wav");
        handled_ = true;
    }
    CUiWindow* opened = nullptr;
    bool open = true;
    switch (region) {
    case 0:   // save
    case 1: { // load
        if (IsMultiplayerClient()) {
            open = false;
            break;
        }
        opened = new CSaveLoadWindow(ui, region == 0, true);
        break;
    }
    case 2:   // resume
        ui->SetPendingValue(0);
        open = false;
        break;
    case 3: { // quit: the message box
        opened = new CMessageBox(ui, "quit", "quit_msg", 0x10000, 3, 1);
        break;
    }
    case 4: { // options
        opened = new COptionsWindow(ui, true);
        break;
    }
    default:
        open = false;
        break;
    }
    if (open) ui->PushWindow(opened, 1, 1);
    CUiContainer::LeftDown(buttons, x, y);
}

// --- Main menu clicks ------------------------------------------------------------

void CMainMenu::LeftDown(int buttons, int x, int y) {
    CInterface3D* ui = interface_;
    const char* okSound = "buttons\\menu\\ok.wav";
    switch (RegionAt(x, y)) {
    case 0: {
        // A new game: the difficulty question (a message box with its own vtables).
        ui->PlaySound(okSound);
        ui->PushWindow(new CDifficultyBox(ui), 1, 1);
        break;
    }
    case 1: {
        ui->PlaySound(okSound);
        ui->PushWindow(new CSaveLoadWindow(ui, false, false), 1, 1);
        break;
    }
    case 2: {
        {
            std::string path = g_discFolder + "Movies\\Exit.bik";
            PlayMovie(path, 0, 0, 0, 1, 1);
        }
        app::QuitGame(0);
        break;
    }
    case 3: {
        ui->PlaySound(okSound);
        ui->PushWindow(new COptionsWindow(ui, false), 1, 1);
        break;
    }
    case 4: {
        // The titles (opened over a cleared menu, not as a dialog).
        GameStopSounds(0);
        GameStopAmbience();
        ui->PlaySound(okSound);
        ClearSurfaces();
        ui->PushWindow(new CCreditsScreen(ui, true, "Titles"), 0, 1);
        break;
    }
    case 5: {
        // Multiplayer: the hero screen (a server session starts). EI_Plugin's NewMaster first looks up its
        // second master server.
        if (ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNewMaster)) net::second_master::OnMultiplayerClick();
        std::string text;
        g_clientWorld->texts_->CTextManager::GetString("reserved_04", &text);
        g_interface.ShowStatusText(text);
        GameSetServerPaused(1);
        ui->PlaySound(okSound);
        ui->PushWindow(new CMpHeroScreen(ui), 1, 1);
        break;
    }
    default:
        break;
    }
    CUiContainer::LeftDown(buttons, x, y);
}

// --- Message box -----------------------------------------------------------------------

void CMessageBox::CreateScreen() {
    LoadTexture("saveload", 3);
    CUiSurface* surface = NewSurface(interface_, 200, 50, 600, 500);
    AddSurface(surface);
    int font = result_ != 0xFFFF0000u ? 2 : 1;
    // Measure the title (font 2) and the text (DT_CALCRECT | DT_WORDBREAK | DT_CENTER, 370 wide).
    RECT rect = {15, 0, 385, 200};
    int titleHeight = surface->DrawTextIn(title_, &rect, 2, 0, 0x411, 1);
    rect = RECT{15, 0, 385, 400};
    int textHeight = surface->DrawTextIn(text_, &rect, font, 0, 0x411, 1);
    // An edit field takes one line of the font (the extent of "A").
    if (flags_ & 4)
        textHeight =
            interface_->TextExtent("A", font).cy;
    int height = titleHeight + textHeight;
    int top = (0x226 - (height + 0x3C)) / 2;   // the block centred in 550 pixels
    rect = RECT{15, top - 35, 385, 450};
    surface->DrawTextIn(title_, &rect, 2, 0x31B3FF, 0x11, 1);
    if (flags_ & 4) {
        edit_ = new CUiEditField(interface_, RECT{215, top + titleHeight + 30, 585, height + top + 30});
        edit_->SetLook(15, 0, font, 0x32, 0xA7D7E4, 0x31B3FF);
        edit_->SetText(editText_);
        AddChild(edit_);
    } else {
        rect = RECT{15, top + titleHeight - 20, 385, 450};
        surface->DrawTextIn(text_, &rect, font, 0xA7D7E4, static_cast<int>(format_ | 0x10), 1);
    }

    int bottom = top + height + 0x3C;
    Shade(this, 3.0f, 200.0f, static_cast<float>(top), 600.0f, static_cast<float>(bottom));
    Frame(this, 3.0f, 195.0f, static_cast<float>(top - 5), 605.0f, static_cast<float>(bottom + 5));

    // The buttons, 48 high below the block: OK (texels 0x51..0x9B) and cancel (0xA0..0xEA).
    float buttonTop = static_cast<float>(bottom + 0x1A);
    float buttonBottom = static_cast<float>(bottom + 0x4A);
    if ((flags_ & 2) && (flags_ & 1)) {
        AddBlankSprite()->SetRectangle(3.0f, 100.0f, 263.0f, buttonTop, 337.0f, buttonBottom, 0x51, 2, 0x9B, 0x32);
        AddBlankSprite()->SetRectangle(3.0f, 100.0f, 463.0f, buttonTop, 537.0f, buttonBottom, 0xA0, 2, 0xEA, 0x32);
        AddRect(this, 263, bottom + 0x1A, 337, bottom + 0x4A)->SetTip(0xC3B4);
        AddRect(this, 463, bottom + 0x1A, 537, bottom + 0x4A)->SetTip(0xC3B5);
    } else if (!(flags_ & 2) && (flags_ & 1)) {
        AddBlankSprite()->SetRectangle(3.0f, 100.0f, 363.0f, buttonTop, 437.0f, buttonBottom, 0x51, 2, 0x9B, 0x32);
        AddRect(this, 363, bottom + 0x1A, 437, bottom + 0x4A)->SetTip(0xC3B4);
    } else {
        // Cancel only: region 0 stays empty, so the button is region 1 as with two buttons.
        AddBlankSprite()->SetRectangle(3.0f, 100.0f, 363.0f, buttonTop, 437.0f, buttonBottom, 0xA0, 2, 0xEA, 0x32);
        AddRegion();
        AddRect(this, 363, bottom + 0x1A, 437, bottom + 0x4A)->SetTip(0xC3B5);
    }
}

// --- Difficulty question ---------------------------------------------------------------

void CDifficultyBox::CreateScreen() {
    LoadTexture("saveload", 3);
    CUiSurface* surface = NewSurface(interface_, 200, 100, 600, 500);
    AddSurface(surface);
    // Measure the title (font 2) and the explanation (font 1, DT_CALCRECT | DT_WORDBREAK).
    RECT rect = {15, 0, 385, 200};
    int titleHeight = surface->DrawTextIn(title_, &rect, 2, 0, 0x411, 1);
    std::string explanation;
    int textHeight = 0;
    if (g_clientWorld->texts_->Open("tip option_difficulty") != 0) {
        g_clientWorld->texts_->ReadToHash(&explanation, 1);
        str::TrimRight(explanation, "\r\n");
        rect = RECT{15, 0, 385, 300};
        textHeight = surface->DrawTextIn(explanation, &rect, 1, 0, 0x410, 1);
    }
    int total = textHeight + titleHeight + 0x6C;
    int top = (0x226 - total) / 2;   // the block centred in 550 pixels
    rect = RECT{15, top - 0x55, 385, 450};
    surface->DrawTextIn(title_, &rect, 2, 0x31B3FF, 0x11, 1);
    rect = RECT{15, titleHeight + top - 7, 385, 450};
    surface->DrawTextIn(explanation, &rect, 1, 0xA7D7E4, 0x10, 1);

    // The two choices, their labels centred on 430 (surface x 230) by their mean width.
    std::string easy;
    std::string hard;
    g_clientWorld->texts_->GetString("difficulty_easy", &easy);
    g_clientWorld->texts_->GetString("difficulty_hard", &hard);
    SIZE easySize = interface_->TextExtent(easy, 2);
    SIZE hardSize = interface_->TextExtent(hard, 2);
    int half = ((easySize.cx + hardSize.cx) / 2 + 0x1E) / 2;
    int row = titleHeight + top + 0x1E;   // layout y of the easy row (the surface starts at 100)
    rect = RECT{0xE6 - half, row - 100, 385, 450};
    surface->DrawTextIn(easy, &rect, 2, 0xA7D7E4, 0, 1);
    rect = RECT{0xE6 - half, row - 0x4C, 385, 450};
    surface->DrawTextIn(hard, &rect, 2, 0xA7D7E4, 0, 1);
    easy_ = 0;

    // The check boxes (texels 0xD1..0xE6 x 0x37..0x4C); the hard one checked (its texture shifted by
    // the sprite's U offset, as the button handler does).
    int boxLeft = 400 - half;
    int boxRight = 0x1A5 - half;
    AddBlankSprite()->SetRectangle(2.5f, 100.0f, static_cast<float>(boxLeft), static_cast<float>(row),
                               static_cast<float>(boxRight), static_cast<float>(row + 0x15), 0xD1, 0x37, 0xE6, 0x4C);
    CUiMesh* hardBox = AddBlankSprite();
    hardBox->SetRectangle(2.5f, 100.0f, static_cast<float>(boxLeft), static_cast<float>(row + 0x18),
                          static_cast<float>(boxRight), static_cast<float>(row + 0x2D), 0xD1, 0x37, 0xE6, 0x4C);
    hardBox->textureOffsetU_ = kCheckedOffset;

    int bottom = top + total;
    Shade(this, 3.0f, 200.0f, static_cast<float>(top), 600.0f, static_cast<float>(bottom));
    Frame(this, 3.0f, 195.0f, static_cast<float>(top - 5), 605.0f, static_cast<float>(bottom + 5));
    float buttonTop = static_cast<float>(bottom + 0x1A);
    float buttonBottom = static_cast<float>(bottom + 0x4A);
    AddBlankSprite()->SetRectangle(3.0f, 100.0f, 263.0f, buttonTop, 337.0f, buttonBottom, 0x51, 2, 0x9B, 0x32);
    AddBlankSprite()->SetRectangle(3.0f, 100.0f, 463.0f, buttonTop, 537.0f, buttonBottom, 0xA0, 2, 0xEA, 0x32);
    AddRect(this, 263, bottom + 0x1A, 337, bottom + 0x4A)->SetTip(0xC3B4);
    AddRect(this, 463, bottom + 0x1A, 537, bottom + 0x4A)->SetTip(0xC3B5);
    AddRect(this, boxLeft, row, boxRight, row + 0x15);
    AddRect(this, boxLeft, row + 0x18, boxRight, row + 0x2D);
}

// --- Titles ----------------------------------------------------------------------------

void CCreditsScreen::CreateScreen() {
    CInterface3D* ui = interface_;
    const std::string& name = name_;
    if (!fromGame_) {
        g_clientWorld->sounds_->StopAll(1);
        g_clientWorld->music_->Stop();
    }
    g_cursor.ShowImage(0);
    app::PlayMovies((name + "fin"), 0);
    nextLineAt_ = 0.0f;

    CMappedResource* archive = &archive_;
    archive->OpenResource((options::g_options->resourcesPath_ + "outro.res"), 0);
    std::uint32_t size = 0;
    void* data = archive->GetFile("credits.scr", &size);
    if (!data) {
        currentLine_ = lines_.end();
    } else {
        std::string script;
        std::memmove(str::SetLength(script, static_cast<int>(size)), data, size);
        str::CutAtNull(script);
        text::FromAssetText(&script);   // the headers' text in UTF-8 (the pages are RTF)
        RemoveCarriageReturns(&script);
        TitlesTokenizer lines{{0, &script, "\n"}};
        lines.Start();
        while (lines.position != -1) {
            std::string line = lines.word;
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
            record.speed = static_cast<float>(str::ToDouble(words.word));
            words.Next();
            record.style = words.WordOrEmpty();
            words.Next();
            record.text = words.WordOrEmpty();
            lines_.push_back(record);
            lines.Next();
        }
        currentLine_ = lines_.begin();
        archive->CloseFile(data);
    }

    // The scrolling text (10..790 x 150..450) and the header (10..790 x 110..140), frozen.
    {
        RECT rect = {10, 0x96, 0x316, 0x1C2};
        textSurface_ = new CUiScrollingSurface(ui, &rect);
        AddSurface(textSurface_);
        textSurface_->Hide(false);
    }
    {
        RECT rect = {10, 0x6E, 0x316, 0x8C};
        headerSurface_ = AddSurface(new CUiSurface(ui, &rect, SurfaceBlend::Keyed));
        headerSurface_->Hide(false);
    }
    if (fromGame_) GameSetServerPaused(0);
    savedDisplay_ = static_cast<int>(g_sceneRenderer->fogColour_);
    g_sceneRenderer->fogColour_ = 0;
    player_.Open((g_discFolder + "Movies\\" + name + ".bik").c_str(),   // "<folder>Movies\<name>.bik"
                                             0);
    finished_ = false;
}
