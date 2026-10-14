// Layout builders of the windows opened from the Esc menu (their slot 25, "Create"): the regions,
// sprites, surfaces, scroll bar and 3D model each window is made of. See docs/notes/ui.md.
//
// The builders call the sprite placement functions (005E70E0, 005E7D60) and CUiRegion::SetRect
// through their original addresses: Rebirth patches them for its widescreen layout.
#include "ui.h"
#include "game_strings.h"

#include "console.h"
#include "item_db_text.h"
#include "maths.h"
#include "resource.h"
#include "ui_screens.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_render.h"
#include "originals_world.h"

#include <cstdint>
#include <cstring>
#include <new>

// --- Forwarders ---------------------------------------------------------------------------------

__declspec(naked) void CUiRegion::CallSetRect(int, int, int, int) EI_ORIGINAL(0x005FC9E0)
__declspec(naked) void CUiRegion::CallAddPoint(int, int) EI_ORIGINAL(0x005FCA70)
__declspec(naked) void CUiMesh::SetTriangle(float, float, float, float, float, float, float, float, float, float, float,
                                            float, float, float) EI_ORIGINAL(0x005E99D0)
__declspec(naked) CUiSurface* CUiSurface::Construct(CInterface3D*, int, int, int, int, int, int) EI_ORIGINAL(0x005FCC00)
__declspec(naked) void CUiModel::CallLoad(const char*, const char*, const char*) EI_ORIGINAL(0x005FC1F0)
void CUiMesh::SetRectangle(float a0, float a1, float a2, float a3, float a4, float a5, int a6, int a7, int a8, int a9) { UiMeshRectangleOriginal((void*)this, nullptr, a0, a1, a2, a3, a4, a5, a6, a7, a8, a9); }
__declspec(naked) void CUiMesh::SetFrame(float, float, float, float, float, float, float, int, int, int, int)
    EI_ORIGINAL(0x005E7D60)

namespace {
// The text manager (global at 007AF788; item_db_text.h) as the builders use it.
struct UiTexts {
    // 004DEFC0 (not rewritten). Finds the text `key`; non-zero when found (the cursor is then
    // at its start).
    int Find(const char* key);
};
int UiTexts::Find(const char* key) { return ::TextsOpenSection(this, nullptr, key); }
CTextManager* Texts() { return *reinterpret_cast<CTextManager**>(0x007AF788); }
} // namespace

namespace {

// The game's own strings are passed (callees may keep the pointer).
const char* GameText(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

// Fields of an interface sprite set directly by the builders.
void SetSpriteTexture(CUiMesh* sprite, void* texture) {
    std::memcpy(reinterpret_cast<std::uint8_t*>(sprite) + 0x280, &texture, 4);
}
void SetSpriteColour(CUiMesh* sprite, std::uint32_t argb) {
    std::memcpy(reinterpret_cast<std::uint8_t*>(sprite) + 0xD4, &argb, 4);
}
// Flag 0x100 of the object flags (+0x08): drawn with the colour, untextured.
void SetSpritePlain(CUiMesh* sprite) {
    std::uint32_t flags;
    std::memcpy(&flags, reinterpret_cast<std::uint8_t*>(sprite) + 0x08, 4);
    flags |= 0x100;
    std::memcpy(reinterpret_cast<std::uint8_t*>(sprite) + 0x08, &flags, 4);
}

// A region with the rectangle (left, top, right, bottom).
CUiRegion* AddRect(CUiWindow* window, int left, int top, int right, int bottom) {
    CUiRegion* region = window->AddRegion();
    region->CallSetRect(left, top, right, bottom);
    return region;
}

// A surface of the window over the layout rectangle (left, top, right, bottom).
CUiSurface* NewSurface(CInterface3D* ui, int left, int top, int right, int bottom) {
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    if (!memory) return nullptr;
    return static_cast<CUiSurface*>(memory)->Construct(ui, left, top, right, bottom, 5, 1);
}

CUiScrollbar* NewScrollbar(CInterface3D* ui, int left, int top, int right, int bottom) {
    void* memory = gamecrt::operator_new(0x18C);
    if (!memory) return nullptr;
    RECT rect = {left, top, right, bottom};
    return static_cast<CUiScrollbar*>(memory)->Construct(ui, rect, 3.0f, 0, 1.0f);
}

// The frame colours of the windows (texels of the shared frame texture).
void Frame(CUiWindow* window, float depth, float left, float top, float right, float bottom) {
    window->AddSprite(0)->SetFrame(depth, 100.0f, left, top, right, bottom, 5.0f, 2, 0xFE, 0xB4, 0xF9);
}

// A plain dark rectangle (colour 0xA0000000).
void Shade(CUiWindow* window, float depth, float left, float top, float right, float bottom) {
    CUiMesh* sprite = window->AddSprite(0);
    sprite->SetRectangle(depth, 100.0f, left, top, right, bottom, 0, 0, 0x100, 0x100);
    SetSpriteTexture(sprite, nullptr);
    SetSpritePlain(sprite);
    SetSpriteColour(sprite, 0xA0000000);
}

// The window's 3D model: the stone menu (`escMenu00`) turned about x.
CUiModel* AddMenuModel(CUiWindow* window, float angle) {
    CUiModel* model = window->AddModel();
    model->CallLoad(GameText(0x00793FA8), GameText(0x00793FB0), GameText(0x00793FBC));   // "unmoco1", "escMenu00", "escMenu00labels"
    Vector3 axis = {1.0f, 0.0f, 0.0f};
    Quaternion turn;
    turn.SetAxisAngle(axis, angle);
    std::memcpy(reinterpret_cast<std::uint8_t*>(model->object_) + 0x34, &turn, sizeof(turn));
    return model;
}

} // namespace

// --- Save/load window (00653DD0) -------------------------------------------------------------

void CSaveLoadWindow::Build() {
    rect_ = RECT{0, 0, 800, 600};
    LoadTexture(GameText(0x00793380), 3, 0);   // "saveload"
    // The selected save's picture (5:6:5; game-owned memory with "cpuwindows", ui_saveload.cpp).
    GivePictureMemory(CreateTexture(0x100, 0x100, 0x1E));

    // The three buttons' faces: OK (left), cancel (right), delete (middle).
    AddSprite(0)->SetRectangle(3.0f, 100.0f, 163.0f, 526.0f, 237.0f, 574.0f, 0x51, 2, 0x9B, 0x32);
    AddSprite(0)->SetRectangle(3.0f, 100.0f, 563.0f, 526.0f, 637.0f, 574.0f, 0xA0, 2, 0xEA, 0x32);
    AddSprite(0)->SetRectangle(3.0f, 100.0f, 363.0f, 526.0f, 437.0f, 574.0f, 2, 2, 0x4C, 0x32);
    // Regions 0-2: OK (its tooltip says save or load), cancel, delete.
    AddRect(this, 163, 526, 237, 574)->SetTip(isSave_ ? 0xEAC4 : 0xEAC5, 1, 0);
    AddRect(this, 563, 526, 637, 574)->SetTip(0xEAC7, 1, 0);
    AddRect(this, 363, 526, 437, 574)->SetTip(0xEAC6, 1, 0);

    // The picture of the selected save (texture of the window's second texture object).
    CUiMesh* picture = AddSprite(0);
    picture->SetRectangle(3.0f, 100.0f, 410.0f, 100.0f, 700.0f, 320.0f, 0, 0x100, 0x100, 0x40);
    SetSpriteTexture(picture, nullptr);
    CUiMesh* highlight = AddSprite(0);
    highlight->SetRectangle(3.0f, 100.0f, 108.0f, 108.0f, 370.0f, 132.0f, 0, 0, 0x100, 0x100);
    SetSpriteTexture(highlight, nullptr);
    SetSpriteColour(highlight, 0xC8A06800);
    SetSpritePlain(highlight);
    Frame(this, 2.0f, 95.0f, 95.0f, 395.0f, 505.0f);     // the list
    Frame(this, 2.0f, 405.0f, 95.0f, 705.0f, 325.0f);    // the picture
    Frame(this, 2.0f, 405.0f, 335.0f, 705.0f, 505.0f);   // the description
    Shade(this, 5.0f, 100.0f, 100.0f, 390.0f, 500.0f);
    Shade(this, 5.0f, 410.0f, 100.0f, 700.0f, 320.0f);
    Shade(this, 5.0f, 410.0f, 340.0f, 700.0f, 500.0f);

    // Regions 3-18: the list rows, 24 pixels each.
    for (int top = 108; top < 492; top += 24) AddRect(this, 108, top, 370, top + 24);

    AddSurface(NewSurface(interface_, 100, 100, 700, 500));
    scrollbar_ = NewScrollbar(interface_, 108, 108, 384, 492);
    AddChild(scrollbar_);

    CUiModel* model = AddMenuModel(this, *reinterpret_cast<const float*>(0x007C81C8));
    model->SetScreenPosition(400.0f, 700.0f, 4.8f);
    model->ShowAllParts(0);
    if (isSave_) {
        model->ShowPart(GameText(0x0079495C), 1);   // "save"
        model->ShowPart(GameText(0x00794950), 1);   // "savelabel"
    } else {
        model->ShowPart(GameText(0x00794948), 1);   // "load"
        model->ShowPart(GameText(0x0079493C), 1);   // "loadlabel"
    }

    dialog_ = nullptr;
    if (sliding_) slide_ = 0.0f;
    CallFillEntries();
    if (isSave_) {
        // Saving: the first save is selected and its title offered as the name.
        selected_ = 0;
        if (EntryCount() != 0) {
            nameTyped_ = false;
            *reinterpret_cast<mfc::CString*>(&name_) = *reinterpret_cast<mfc::CString*>(&entriesBegin_->title_);
            cursor_ = reinterpret_cast<const int*>(name_)[-2];   // CString length
        }
    } else {
        selected_ = EntryCount() != 0 ? 0 : -1;
    }
    RefreshList();
    CallShowPicture();
    if (!sliding_) interface_->ShowHelp(0);
}
EI_REPLACE_METHOD(0x00653DD0, CSaveLoadWindow::Build);

// --- Options window (006503C0) ------------------------------------------------------------------

namespace {
// The option groups (names at 007947A8): video, sound, sens, game, select, actions, camera,
// items, spells, cshots, windows.
constexpr int kOptionGroups = 11;
const char* OptionGroupName(int index) { return reinterpret_cast<const char* const*>(0x007947A8)[index]; }
} // namespace

void COptionsWindow::Build() {
    rect_ = RECT{0, 0, 800, 600};
    LoadTexture(GameText(0x00793380), 3, 0);   // "saveload"
    CreateTexture(0x100, 0x100, 0x1E);

    // OK and cancel (regions 0 and 1).
    AddSprite(0)->SetRectangle(3.0f, 100.0f, 213.0f, 526.0f, 287.0f, 574.0f, 0x51, 2, 0x9B, 0x32);
    AddSprite(0)->SetRectangle(3.0f, 100.0f, 513.0f, 526.0f, 587.0f, 574.0f, 0xA0, 2, 0xEA, 0x32);
    AddRect(this, 213, 526, 287, 574)->SetTip(0x111D4, 1, 0);
    AddRect(this, 513, 526, 587, 574)->SetTip(0x111D5, 1, 0);
    CUiMesh* highlight = AddSprite(0);
    highlight->SetRectangle(3.0f, 100.0f, 250.0f, 108.0f, 692.0f, 132.0f, 0, 0, 0x100, 0x100);
    SetSpriteTexture(highlight, nullptr);
    SetSpriteColour(highlight, 0xC8A06800);
    SetSpritePlain(highlight);

    // Regions 2-12: the option groups, each with its button sprite, tooltip and title.
    mfc::CString text;
    for (int k = 0, top = 156; k < kOptionGroups; ++k, top += 24) {
        CUiRegion* region = AddRect(this, 100, top, 250, top + 24);
        AddSprite(0)->SetRectangle(3.0f, 100.0f, 110.0f, static_cast<float>(top + 2), 240.0f,
                                   static_cast<float>(top + 21), 0x7A, 0x6C, 0xFC, 0x7F);
        text.Format(GameText(0x00794860), OptionGroupName(k));   // "tip option_group_%s"
        if (reinterpret_cast<UiTexts*>(Texts())->Find(text) != 0) Texts()->ReadToHash(&text, 1);
        region->SetTipString(text, 1, 0);
        {
            mfc::CString prefix(GameText(0x00794850));   // "option_group_"
            mfc::CString key = prefix + OptionGroupName(k);
            Texts()->GetString(key, &text);
        }
        groupNames_.SetAtGrow(groupNames_.GetSize(), text);
    }

    Frame(this, 2.0f, 95.0f, 95.0f, 705.0f, 505.0f);
    Shade(this, 5.0f, 100.0f, 100.0f, 700.0f, 500.0f);
    // Regions 13-26: the rows of the shown group.
    for (int top = 156; top < 492; top += 24) AddRect(this, 250, top, 520, top + 24);
    AddSurface(NewSurface(interface_, 100, 100, 700, 500));

    CUiModel* model = AddMenuModel(this, *reinterpret_cast<const float*>(0x007C81A0));
    model->SetScreenPosition(400.0f, 700.0f, 4.8f);
    model->ShowAllParts(0);
    model->ShowPart(GameText(0x00794848), 1);   // "options"
    model->ShowPart(GameText(0x00794838), 1);   // "optionslabel"

    if (sliding_) slide_ = 0.0f;
    waitingForKey_ = false;
    pressedKey_ = nullptr;
    CallBuildSettings();
    CallLoadSettings();
    CallBuildBindings();
    displayModeChanged_ = false;
    CallShowGroup(0);
    if (!sliding_) interface_->ShowHelp(0);
}
EI_REPLACE_METHOD(0x006503C0, COptionsWindow::Build);

// --- Esc menu (0062AB20) ------------------------------------------------------------------------

void CEscMenu::Build() {
    window.rect_ = RECT{0, 0, 800, 600};
    CUiModel* model = window.AddModel();
    model->CallLoad(GameText(0x00793FA8), GameText(0x00793FB0), GameText(0x00793FBC));
    model->SetScreenPosition(400.0f, 700.0f, 4.8f);
    Vector3 axis = {1.0f, 0.0f, 0.0f};
    Quaternion turn;
    turn.SetAxisAngle(axis, *reinterpret_cast<const float*>(0x007C7FB0));
    std::memcpy(reinterpret_cast<std::uint8_t*>(model->object_) + 0x34, &turn, sizeof(turn));

    // Regions 0-4 (save, load, resume, quit, options) from the menus.reg rectangles; the
    // original goes through 0062AC90 (SetRect with a RECT by value).
    for (std::uintptr_t source = 0x007C80A8; source <= 0x007C80E8; source += sizeof(RECT)) {
        CUiRegion* region = window.AddRegion();
        RECT rect;
        gameapi::CopyRect(&rect, reinterpret_cast<const RECT*>(source));
        region->CallSetRect(rect.left, rect.top, rect.right, rect.bottom);
    }
    window.models_.data[0]->Play(1);
    window.interface_->PlaySound(GameText(0x00793F90), 0);   // "buttons\menu\shift.wav"
}
EI_REPLACE_METHOD(0x0062AB20, CEscMenu::Build);

// --- Esc menu clicks (0062AE90) -------------------------------------------------------------------

namespace {
// Not rewritten: the container window constructor, the STLport vector constructors of the two
// windows' members, the message box constructor and the interface's window activation. Thiscall
// methods are declared __fastcall with the unused EDX.
__declspec(naked) void __fastcall ConstructSaveList(void* /*vector*/, void*, void* /*allocator*/) EI_ORIGINAL(0x0062B550)
__declspec(naked) void __fastcall ConstructBindingList(void*, void*, void*) EI_ORIGINAL(0x0062B570)
__declspec(naked) void __fastcall ConstructSettingList(void*, void*, void*) EI_ORIGINAL(0x0062B5E0)
CUiWindow* __fastcall ConstructMessageBox(void* a0, void*, CInterface3D* a2, const char* a3, const char* a4, int a5, int a6, int a7) { return (CUiWindow*)MessageBoxConstructOriginal(a0, nullptr, a2, a3, a4, a5, a6, a7); }

constexpr std::uintptr_t kSaveLoadVtable = 0x00741388;
constexpr std::uintptr_t kSaveLoadInputVtable = 0x00741354;
constexpr std::uintptr_t kOptionsVtable = 0x00741FAC;
constexpr std::uintptr_t kOptionsInputVtable = 0x00741F78;

void SetVtables(void* window, std::uintptr_t main, std::uintptr_t input) {
    std::memcpy(window, &main, 4);
    std::memcpy(static_cast<std::uint8_t*>(window) + 0x94, &input, 4);
}

// What the original's inlined CSaveLoadWindow constructor does. Null when allocation failed.
CSaveLoadWindow* NewSaveLoadWindow(CInterface3D* ui) {
    void* memory = gamecrt::operator_new(sizeof(CSaveLoadWindow));
    if (!memory) return nullptr;
    auto* w = static_cast<CSaveLoadWindow*>(memory);
    ConstructContainerAt(w, nullptr, ui);
    char allocator;
    ConstructSaveList(&w->entriesBegin_, nullptr, &allocator);
    w->isSave_ = false;
    w->sliding_ = false;
    new (static_cast<void*>(&w->name_)) mfc::CString();
    SetVtables(w, kSaveLoadVtable, kSaveLoadInputVtable);
    return w;
}

// The inlined COptionsWindow constructor.
COptionsWindow* NewOptionsWindow(CInterface3D* ui) {
    void* memory = gamecrt::operator_new(sizeof(COptionsWindow));
    if (!memory) return nullptr;
    auto* w = static_cast<COptionsWindow*>(memory);
    ConstructContainerAt(w, nullptr, ui);
    char allocator[2];
    ConstructBindingList(&w->bindings_, nullptr, &allocator[0]);
    ConstructSettingList(&w->settings_, nullptr, &allocator[1]);
    w->sliding_ = false;
    w->groupNames_.Construct();
    SetVtables(w, kOptionsVtable, kOptionsInputVtable);
    return w;
}
} // namespace

void CEscMenu::OnLButtonDown(int buttons, int x, int y) {
    CInterface3D* ui = window.interface_;
    int region = window.RegionAt(x, y);
    if (region >= 0) {
        ui->PlaySound(GameText(0x00793FF0), 0);   // "buttons\menu\ok.wav"
        window.inputHandled_ = true;
    }
    void* opened = nullptr;
    bool open = true;
    switch (region) {
    case 0:   // save
    case 1: { // load
        if (IsMultiplayerClient()) {
            open = false;
            break;
        }
        CSaveLoadWindow* w = NewSaveLoadWindow(ui);
        // (Like the original, these two stores do not check the allocation.)
        w->isSave_ = region == 0;
        w->sliding_ = true;
        opened = w;
        break;
    }
    case 2:   // resume
        ui->SetPendingValue(0);
        open = false;
        break;
    case 3: { // quit: the message box
        void* memory = gamecrt::operator_new(0x180);
        opened = memory ? ConstructMessageBox(memory, nullptr, ui, GameText(0x0079239C), GameText(0x00793FE4),
                                              0x10000, 3, 1)   // "quit_msg"
                        : nullptr;
        break;
    }
    case 4: { // options
        COptionsWindow* w = NewOptionsWindow(ui);
        w->sliding_ = true;
        opened = w;
        break;
    }
    default:
        open = false;
        break;
    }
    if (open) InterfacePushWindowOriginal(ui, nullptr, opened, 1, 1);
    UiContainerMouseDown(window.InputSide(), nullptr, buttons, x, y);
}

namespace {
void __fastcall EscMenuLButtonDown(void* input, void*, int buttons, int x, int y) {
    reinterpret_cast<CEscMenu*>(static_cast<std::uint8_t*>(input) - 0x94)->OnLButtonDown(buttons, x, y);
}
} // namespace
EI_REPLACE(0x0062AE90, EscMenuLButtonDown);

// --- Main menu clicks (00632D70) ------------------------------------------------------------------

namespace {
// Not rewritten. The game menu object (007AFEA4 / 007AFEA5, thiscall): 00578DC0 the game's folder,
// 00578E20 the text table (004E0020 a key's text, RET 8), 00578D10 the server flag (RET 4),
// 00578920 (RET 4) / 00578940 before the titles. 005F75B0 (on 007C7D50, RET 4) shows a status text.
// 005DB3F0 plays a movie (ECX path, EDX 0, five flags), 00455F20 quits (exit code in ECX).
struct MenuGlue {
    const mfc::CString* Directory();   // 00578DC0
    void* TextTable();                 // 00578E20
    void SetServer(int on);            // 00578D10
    void BeforeTitles(int value);      // 00578920
    void BeforeTitles2();              // 00578940
};
const mfc::CString* MenuGlue::Directory() { return GameDirectoryOriginal(this); }
void* MenuGlue::TextTable() { return GameFlowOriginal(); }
void MenuGlue::SetServer(int f) { SetServerFlagOriginal(f); }
void MenuGlue::BeforeTitles(int a0) { MenuSetOriginal(a0); }
void MenuGlue::BeforeTitles2() { MenuCallOriginal(); }
MenuGlue* Glue(std::uintptr_t address) { return reinterpret_cast<MenuGlue*>(address); }

// The titles window's members: CMappedResource 00671280, a vector 00634300 (allocator, RET 4),
// 005DB340 (the music player at +0x1D8). The hero screen's: the item screen base's caches 00633F50
// / 00633FF0, two lists 0053F4F0 and two vectors 006342A0 (allocator, RET 4).
__declspec(naked) void* __fastcall MenuMappedResource(void*) EI_ORIGINAL(0x00671280)
__declspec(naked) void __fastcall MenuTitlesVector(void*, void*, const void*) EI_ORIGINAL(0x00634300)
__declspec(naked) void __fastcall MenuModelCache(void*) EI_ORIGINAL(0x00633F50)
__declspec(naked) void __fastcall MenuTextureCache(void*) EI_ORIGINAL(0x00633FF0)
__declspec(naked) void __fastcall MenuHeroVector(void*, void*, const void*) EI_ORIGINAL(0x006342A0)

template <typename T>
void Put(void* window, int offset, T value) {
    std::memcpy(static_cast<std::uint8_t*>(window) + offset, &value, sizeof(T));
}
// The empty CString's data pointer (00796E48), stored as a CString member is.
std::uint32_t EmptyStringData() { return *reinterpret_cast<const std::uint32_t*>(0x00796E48); }
} // namespace

void CMainMenu::OnLButtonDown(int buttons, int x, int y) {
    CInterface3D* ui = interface_;
    const char* okSound = GameText(0x00793FF0);   // "buttons\menu\ok.wav"
    switch (RegionAt(x, y)) {
    case 0: {
        // A new game: the difficulty question (a message box with its own vtables).
        ui->PlaySound(okSound, 0);
        void* memory = gamecrt::operator_new(0x184);
        CUiWindow* box = nullptr;
        if (memory) {
            box = ConstructMessageBox(memory, nullptr, ui, GameText(0x00794258), game::EmptyText(), 0x20000, 3,
                                      1);   // "option_difficulty", ""
            SetVtables(box, 0x0074225C, 0x00742228);
        }
        InterfacePushWindowOriginal(ui, nullptr, box, 1, 1);
        break;
    }
    case 1: {
        // Load.
        ui->PlaySound(okSound, 0);
        CSaveLoadWindow* w = NewSaveLoadWindow(ui);
        // (Like the original, these two stores do not check the allocation.)
        w->isSave_ = false;
        w->sliding_ = false;
        InterfacePushWindowOriginal(ui, nullptr, w, 1, 1);
        break;
    }
    case 2: {
        // Exit: the exit movie, then quit.
        {
            mfc::CString path = *Glue(0x007AFEA4)->Directory() + GameText(0x0079422C);   // "Movies\Exit.bik"
            PlayMovieFile(path, 0, 0, 0, 1, 1, 1);
        }
        QuitGame(0);
        break;
    }
    case 3: {
        // Options.
        ui->PlaySound(okSound, 0);
        COptionsWindow* w = NewOptionsWindow(ui);
        w->sliding_ = false;   // (unchecked, as the original)
        InterfacePushWindowOriginal(ui, nullptr, w, 1, 1);
        break;
    }
    case 4: {
        // The titles (opened over a cleared menu, not as a dialog).
        Glue(0x007AFEA5)->BeforeTitles(0);
        Glue(0x007AFEA5)->BeforeTitles2();
        ui->PlaySound(okSound, 0);
        ClearSurfaces();
        void* w = gamecrt::operator_new(0x1FC);
        if (w) {
            ConstructContainerAt(w, nullptr, ui);
            MenuMappedResource(static_cast<std::uint8_t*>(w) + 0x170);
            char allocator = 0;
            MenuTitlesVector(static_cast<std::uint8_t*>(w) + 0x1C8, nullptr, &allocator);
            new (static_cast<std::uint8_t*>(w) + 0x1D4) mfc::CString(GameText(0x00794250));   // "Titles"
            Screen8Member1D8(static_cast<std::uint8_t*>(w) + 0x1D8);
            Put<std::uint8_t>(w, 0x1F4, 1);
            SetVtables(w, 0x00740C3C, 0x00740C08);
        }
        InterfacePushWindowOriginal(ui, nullptr, w, 0, 1);
        break;
    }
    case 5: {
        // Multiplayer: the hero screen (a server session starts).
        mfc::CString text;
        TextsGetString(Glue(0x007AFEA4)->TextTable(), nullptr, GameText(0x00793A54), &text);   // "reserved_04"
        ShowStatusText(reinterpret_cast<void*>(0x007C7D50), nullptr, text);
        Glue(0x007AFEA4)->SetServer(1);
        ui->PlaySound(okSound, 0);
        void* w = gamecrt::operator_new(0x220);
        if (w) {
            auto* bytes = static_cast<std::uint8_t*>(w);
            ConstructContainerAt(w, nullptr, ui);
            Put<std::uint32_t>(w, 0x168, 0);
            MenuModelCache(bytes + 0x170);
            MenuTextureCache(bytes + 0x180);
            SetVtables(w, 0x00740A2C, 0x007409F8);   // the item screens' base
            char allocators[4] = {};
            FirstListConstruct(bytes + 0x1C8, nullptr, &allocators[1]);
            FirstListConstruct(bytes + 0x1D4, nullptr, &allocators[0]);
            Put<std::uint32_t>(w, 0x1F4, EmptyStringData());
            MenuHeroVector(bytes + 0x1F8, nullptr, &allocators[2]);
            MenuHeroVector(bytes + 0x204, nullptr, &allocators[3]);
            SetVtables(w, 0x0074214C, 0x00742118);
        }
        InterfacePushWindowOriginal(ui, nullptr, w, 1, 1);
        break;
    }
    default:
        break;
    }
    UiContainerMouseDown(InputSide(), nullptr, buttons, x, y);
}

namespace {
void __fastcall MainMenuLButtonDown(void* input, void*, int buttons, int x, int y) {
    reinterpret_cast<CMainMenu*>(static_cast<std::uint8_t*>(input) - 0x94)->OnLButtonDown(buttons, x, y);
}
} // namespace
EI_REPLACE(0x00632D70, MainMenuLButtonDown);

// --- Message box (00634790) -----------------------------------------------------------------------

namespace {
// Not rewritten: the surface's DC (005FDC00 / 005FDC60), its text (005FDE20, Rebirth-patched;
// returns the text's height), the interface's text extent (005F8340, Rebirth-patched; takes a
// CString by value, see ui_text.cpp) and the edit field: constructor 0065C010 (RET 0x14: the
// interface and a RECT by value), its fonts and colours 0065C1A0 (RET 0x18), its text 0065C290.
struct BoxSurfaceAt {
    void* Begin() { return SurfaceGetDC(this); }                                                                     // 005FDC00
    int Text(const char* text, RECT* rect, int font, COLORREF colour, int format, int flag);   // 005FDE20
    void End() { SurfaceReleaseDC(this); }                                                                       // 005FDC60
};
int BoxSurfaceAt::Text(const char* text, RECT* rect, int format, COLORREF colour, int a, int b) { return SurfaceDrawText(this, nullptr, text, rect, format, colour, a, b); }

struct BoxMetricsAt {
    SIZE TextExtentRaw(char* text, int font);
    SIZE TextExtent(mfc::CString text, int font) {
        char* data;
        std::memcpy(&data, &text, sizeof(data));
        mfc::CString empty;
        std::memcpy(&text, &empty, sizeof(data));   // the callee releases the data
        return TextExtentRaw(data, font);
    }
};
__declspec(naked) SIZE BoxMetricsAt::TextExtentRaw(char*, int) EI_ORIGINAL(0x005F8340)

CUiEditField* EditConstruct(void* memory, void*, CInterface3D* ui, RECT r) {
    return static_cast<CUiEditField*>(EditFieldConstructOriginal(memory, nullptr, ui, r.left, r.top, r.right, r.bottom));
}
void EditSetLook(CUiWindow* edit, void*, int a, int b, int font, int c, COLORREF text, COLORREF caret) {
    EditFieldSetLookOriginal(edit, nullptr, a, b, font, c, text, caret);
}
void EditSetText(CUiWindow* edit, void*, const char* text) { EditFieldSetTextOriginal(edit, nullptr, text); }

BoxSurfaceAt* BoxSurface(CUiSurface* surface) { return reinterpret_cast<BoxSurfaceAt*>(surface); }
} // namespace

void CMessageBox::Build() {
    LoadTexture(GameText(0x00793380), 3, 0);   // "saveload"
    CUiSurface* surface = NewSurface(interface_, 200, 50, 600, 500);
    AddSurface(surface);
    BoxSurface(surface)->Begin();
    int font = result_ != 0xFFFF0000u ? 2 : 1;
    // Measure the title (font 2) and the text (DT_CALCRECT | DT_WORDBREAK | DT_CENTER, 370 wide).
    RECT rect = {15, 0, 385, 200};
    int titleHeight = BoxSurface(surface)->Text(title_, &rect, 2, 0, 0x411, 1);
    rect = RECT{15, 0, 385, 400};
    int textHeight = BoxSurface(surface)->Text(text_, &rect, font, 0, 0x411, 1);
    // An edit field takes one line of the font (the extent of "A").
    if (flags_ & 4)
        textHeight =
            reinterpret_cast<BoxMetricsAt*>(interface_)->TextExtent(mfc::CString(GameText(0x00787BA4)), font).cy;
    int height = titleHeight + textHeight;
    int top = (0x226 - (height + 0x3C)) / 2;   // the block centred in 550 pixels
    rect = RECT{15, top - 35, 385, 450};
    BoxSurface(surface)->Text(title_, &rect, 2, 0x31B3FF, 0x11, 1);
    if (flags_ & 4) {
        void* memory = gamecrt::operator_new(0x17C);
        edit_ = memory ? EditConstruct(memory, nullptr, interface_,
                                       RECT{215, top + titleHeight + 30, 585, height + top + 30})
                       : nullptr;
        // (Like the original, the edit field is set up without checking the allocation.)
        EditSetLook(edit_, nullptr, 15, 0, font, 0x32, 0xA7D7E4, 0x31B3FF);
        EditSetText(edit_, nullptr, editText_);
        AddChild(edit_);
    } else {
        rect = RECT{15, top + titleHeight - 20, 385, 450};
        BoxSurface(surface)->Text(text_, &rect, font, 0xA7D7E4, static_cast<int>(format_ | 0x10), 1);
    }
    BoxSurface(surface)->End();

    int bottom = top + height + 0x3C;
    Shade(this, 3.0f, 200.0f, static_cast<float>(top), 600.0f, static_cast<float>(bottom));
    Frame(this, 3.0f, 195.0f, static_cast<float>(top - 5), 605.0f, static_cast<float>(bottom + 5));

    // The buttons, 48 high below the block: OK (texels 0x51..0x9B) and cancel (0xA0..0xEA).
    float buttonTop = static_cast<float>(bottom + 0x1A);
    float buttonBottom = static_cast<float>(bottom + 0x4A);
    if ((flags_ & 2) && (flags_ & 1)) {
        AddSprite(0)->SetRectangle(3.0f, 100.0f, 263.0f, buttonTop, 337.0f, buttonBottom, 0x51, 2, 0x9B, 0x32);
        AddSprite(0)->SetRectangle(3.0f, 100.0f, 463.0f, buttonTop, 537.0f, buttonBottom, 0xA0, 2, 0xEA, 0x32);
        AddRect(this, 263, bottom + 0x1A, 337, bottom + 0x4A)->SetTip(0xC3B4, 1, 0);
        AddRect(this, 463, bottom + 0x1A, 537, bottom + 0x4A)->SetTip(0xC3B5, 1, 0);
    } else if (!(flags_ & 2) && (flags_ & 1)) {
        AddSprite(0)->SetRectangle(3.0f, 100.0f, 363.0f, buttonTop, 437.0f, buttonBottom, 0x51, 2, 0x9B, 0x32);
        AddRect(this, 363, bottom + 0x1A, 437, bottom + 0x4A)->SetTip(0xC3B4, 1, 0);
    } else {
        // Cancel only: region 0 stays empty, so the button is region 1 as with two buttons.
        AddSprite(0)->SetRectangle(3.0f, 100.0f, 363.0f, buttonTop, 437.0f, buttonBottom, 0xA0, 2, 0xEA, 0x32);
        AddRegion();
        AddRect(this, 363, bottom + 0x1A, 437, bottom + 0x4A)->SetTip(0xC3B5, 1, 0);
    }
}
EI_REPLACE_METHOD(0x00634790, CMessageBox::Build);

// --- Difficulty question (006337B0) ---------------------------------------------------------------

void CDifficultyBox::Build() {
    LoadTexture(GameText(0x00793380), 3, 0);   // "saveload"
    CUiSurface* surface = NewSurface(interface_, 200, 100, 600, 500);
    AddSurface(surface);
    BoxSurface(surface)->Begin();
    // Measure the title (font 2) and the explanation (font 1, DT_CALCRECT | DT_WORDBREAK).
    RECT rect = {15, 0, 385, 200};
    int titleHeight = BoxSurface(surface)->Text(title_, &rect, 2, 0, 0x411, 1);
    mfc::CString explanation;
    int textHeight = 0;
    if (reinterpret_cast<UiTexts*>(Texts())->Find(GameText(0x0079429C)) != 0) {   // "tip option_difficulty"
        Texts()->ReadToHash(&explanation, 1);
        explanation.TrimRight(GameText(0x0078DD00));   // CR LF
        rect = RECT{15, 0, 385, 300};
        textHeight = BoxSurface(surface)->Text(explanation, &rect, 1, 0, 0x410, 1);
    }
    int total = textHeight + titleHeight + 0x6C;
    int top = (0x226 - total) / 2;   // the block centred in 550 pixels
    rect = RECT{15, top - 0x55, 385, 450};
    BoxSurface(surface)->Text(title_, &rect, 2, 0x31B3FF, 0x11, 1);
    rect = RECT{15, titleHeight + top - 7, 385, 450};
    BoxSurface(surface)->Text(explanation, &rect, 1, 0xA7D7E4, 0x10, 1);

    // The two choices, their labels centred on 430 (surface x 230) by their mean width.
    mfc::CString easy;
    mfc::CString hard;
    Texts()->GetString(GameText(0x0079428C), &easy);   // "difficulty_easy"
    Texts()->GetString(GameText(0x0079427C), &hard);   // "difficulty_hard"
    auto* metrics = reinterpret_cast<BoxMetricsAt*>(interface_);
    SIZE easySize = metrics->TextExtent(easy, 2);
    SIZE hardSize = metrics->TextExtent(hard, 2);
    int half = ((easySize.cx + hardSize.cx) / 2 + 0x1E) / 2;
    int row = titleHeight + top + 0x1E;   // layout y of the easy row (the surface starts at 100)
    rect = RECT{0xE6 - half, row - 100, 385, 450};
    BoxSurface(surface)->Text(easy, &rect, 2, 0xA7D7E4, 0, 1);
    rect = RECT{0xE6 - half, row - 0x4C, 385, 450};
    BoxSurface(surface)->Text(hard, &rect, 2, 0xA7D7E4, 0, 1);
    BoxSurface(surface)->End();
    easy_ = 0;

    // The check boxes (texels 0xD1..0xE6 x 0x37..0x4C); the hard one checked (its texture shifted
    // by +0xCC, as the button handler does).
    int boxLeft = 400 - half;
    int boxRight = 0x1A5 - half;
    AddSprite(0)->SetRectangle(2.5f, 100.0f, static_cast<float>(boxLeft), static_cast<float>(row),
                               static_cast<float>(boxRight), static_cast<float>(row + 0x15), 0xD1, 0x37, 0xE6, 0x4C);
    CUiMesh* hardBox = AddSprite(0);
    hardBox->SetRectangle(2.5f, 100.0f, static_cast<float>(boxLeft), static_cast<float>(row + 0x18),
                          static_cast<float>(boxRight), static_cast<float>(row + 0x2D), 0xD1, 0x37, 0xE6, 0x4C);
    hardBox->uOffset_ = kCheckedOffset;

    int bottom = top + total;
    Shade(this, 3.0f, 200.0f, static_cast<float>(top), 600.0f, static_cast<float>(bottom));
    Frame(this, 3.0f, 195.0f, static_cast<float>(top - 5), 605.0f, static_cast<float>(bottom + 5));
    float buttonTop = static_cast<float>(bottom + 0x1A);
    float buttonBottom = static_cast<float>(bottom + 0x4A);
    AddSprite(0)->SetRectangle(3.0f, 100.0f, 263.0f, buttonTop, 337.0f, buttonBottom, 0x51, 2, 0x9B, 0x32);
    AddSprite(0)->SetRectangle(3.0f, 100.0f, 463.0f, buttonTop, 537.0f, buttonBottom, 0xA0, 2, 0xEA, 0x32);
    AddRect(this, 263, bottom + 0x1A, 337, bottom + 0x4A)->SetTip(0xC3B4, 1, 0);
    AddRect(this, 463, bottom + 0x1A, 537, bottom + 0x4A)->SetTip(0xC3B5, 1, 0);
    AddRect(this, boxLeft, row, boxRight, row + 0x15);
    AddRect(this, boxLeft, row + 0x18, boxRight, row + 0x2D);
}
EI_REPLACE_METHOD(0x006337B0, CDifficultyBox::Build);

// --- Titles (0062B7D0) ----------------------------------------------------------------------------

namespace {
// Not rewritten. 006B71C0 (the loading screen 007AF784, RET 4) steps it; 006B4F70 stops the music
// (007AF760); 005E5270 (the renderer 007C7BD8, RET 4) shows or hides the cursor; 00458270 plays the
// movies listed under a section (ECX; DL clears the screen first; then clear-after and two values
// on the stack); 004461E0 (on a CString) drops its CRs (Remove 0x0D); 0065ED30 (RET 8: interface,
// RECT*) constructs the scrolling text surface (0x58 bytes), 005FCDD0 (RET 0xC: interface, RECT*,
// format) a plain one; 00500960 returns the display object; 005DBA50 (the player, RET 8) starts a
// movie. The tokenizer 00445D20 (start) / 00445D30 (next word; null at the end) splits a CString at
// a separator; 0062C090 (RET 0xC, ECX unused) inserts a copy of a TitleLine before a list node.
__declspec(naked) void __fastcall TitlesStopMusic(void*) EI_ORIGINAL(0x006B4F70)
__declspec(naked) void __fastcall TitlesRemoveCr(mfc::CString*) EI_ORIGINAL(0x004461E0)
__declspec(naked) CUiSurface* __fastcall TitlesScrollSurface(void*, void*, CInterface3D*, RECT*) EI_ORIGINAL(0x0065ED30)
__declspec(naked) CUiSurface* __fastcall TitlesPlainSurface(void*, void*, CInterface3D*, RECT*, int)
    EI_ORIGINAL(0x005FCDD0)
std::uint8_t* __cdecl TitlesDisplay() { return (std::uint8_t*)DisplayObject(); }
__declspec(naked) void __fastcall TitlesPlay(void*, void*, const char*, int) EI_ORIGINAL(0x005DBA50)

struct TitlesTokenizer {
    int position;                    // -1 at the end
    const mfc::CString* source;
    const char* separator;
    mfc::CString word;
    // The current word, or null past the end (the original's `position != -1 ? word : 0`).
    const char* WordOrNull() const { return position != -1 ? static_cast<const char*>(word) : nullptr; }
};
__declspec(naked) void __fastcall TitlesTokenStart(TitlesTokenizer*) EI_ORIGINAL(0x00445D20)
__declspec(naked) const char* __fastcall TitlesTokenNext(TitlesTokenizer*) EI_ORIGINAL(0x00445D30)
__declspec(naked) void** __stdcall TitlesInsertLine(void**, void*, const TitleLine*) EI_ORIGINAL(0x0062C090)

mfc::CString* LineString(const char** field) { return reinterpret_cast<mfc::CString*>(field); }
} // namespace

void CCreditsScreen::Build() {
    CInterface3D* ui = window.interface_;
    const mfc::CString& name = *reinterpret_cast<const mfc::CString*>(&name_);
    if (!fromGame_) {
        LoadingStepOriginal(*reinterpret_cast<void**>(0x007AF784), nullptr, 1);
        TitlesStopMusic(*reinterpret_cast<void**>(0x007AF760));
    }
    CursorShowOriginal(reinterpret_cast<void*>(0x007C7BD8), nullptr, 0);
    PlayMoviesOriginal(mfc::operator+(name, GameText(0x00794040)), 1, 0, 0, 0);   // "<name>fin"
    nextLineAt_ = 0.0f;
    // (As the original: the field's address goes in as uiParam, so it is never written; then font
    // smoothing is switched off.)
    gameapi::SystemParametersInfoA(0x4A, static_cast<UINT>(reinterpret_cast<std::uintptr_t>(&fontSmoothing_)), nullptr,
                                   0);   // SPI_GETFONTSMOOTHING
    gameapi::SystemParametersInfoA(0x4B, 0, nullptr, 0);   // SPI_SETFONTSMOOTHING

    // The script: one record per line, words separated by spaces.
    auto* archive = reinterpret_cast<CMappedResource*>(archive_);
    archive->OpenResource(mfc::operator+(*reinterpret_cast<const mfc::CString*>(0x0079BC94), GameText(0x00794034)),
                          0);   // "outro.res"
    std::uint32_t size = 0;
    void* data = archive->GetFile(GameText(0x00794028), &size);   // "credits.scr"
    if (!data) {
        currentLine_ = lines_.End();
    } else {
        mfc::CString script;
        GameMemmove(script.GetBufferSetLength(static_cast<int>(size)), data, size);
        script.ReleaseBuffer(-1);
        TitlesRemoveCr(&script);
        TitlesTokenizer lines{0, &script, GameText(0x00784340)};   // "\n"
        TitlesTokenStart(&lines);
        while (lines.position != -1 && static_cast<const char*>(lines.word) != nullptr) {
            mfc::CString line(static_cast<const char*>(lines.word));
            TitlesTokenizer words{0, &line, GameText(0x0078DA40)};   // " "
            TitlesTokenStart(&words);
            TitleLine record = {};
            record.text = reinterpret_cast<const char*>(EmptyStringData());
            record.style = record.text;
            {
                mfc::CString first(words.WordOrNull());
                record.scroll = gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(first)),
                                                 reinterpret_cast<const unsigned char*>(GameText(0x00794020))) ==
                                0;   // "#scroll"
            }
            TitlesTokenNext(&words);
            record.speed = static_cast<float>(GameAtof(words.word));
            TitlesTokenNext(&words);
            *LineString(&record.style) = words.WordOrNull();
            TitlesTokenNext(&words);
            *LineString(&record.text) = words.WordOrNull();
            void* inserted;
            TitlesInsertLine(&inserted, lines_.End(), &record);   // push_back
            LineString(&record.style)->~CString();
            LineString(&record.text)->~CString();
            TitlesTokenNext(&lines);
        }
        currentLine_ = lines_.First();
        archive->CloseFile(data);
    }

    // The scrolling text (10..790 x 150..450) and the header (10..790 x 110..140), frozen.
    {
        void* memory = gamecrt::operator_new(0x58);
        RECT rect = {10, 0x96, 0x316, 0x1C2};
        textSurface_ = static_cast<CUiScrollingSurface*>(
            window.AddSurface(memory ? TitlesScrollSurface(memory, nullptr, ui, &rect) : nullptr));
        textSurface_->Freeze(0, 0);
    }
    {
        void* memory = gamecrt::operator_new(0x38);
        RECT rect = {10, 0x6E, 0x316, 0x8C};
        headerSurface_ = window.AddSurface(memory ? TitlesPlainSurface(memory, nullptr, ui, &rect, 5) : nullptr);
        headerSurface_->Freeze(0, 0);
    }
    if (fromGame_) Glue(0x007AFEA4)->SetServer(0);
    std::uint32_t kept;
    std::memcpy(&kept, TitlesDisplay() + 0x14, 4);
    savedDisplay_ = static_cast<int>(kept);
    Put<std::uint32_t>(TitlesDisplay(), 0x14, 0);
    TitlesPlay(&player_, nullptr,
               mfc::operator+(mfc::operator+(mfc::operator+(*Glue(0x007AFEA4)->Directory(), GameText(0x00784A4C)), name),
                              GameText(0x00794018)),   // "<folder>Movies\<name>.bik"
               0);
    finished_ = false;
}
EI_REPLACE_METHOD(0x0062B7D0, CCreditsScreen::Build);
