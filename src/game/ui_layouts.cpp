// Layout builders of the windows opened from the Esc menu (their slot 25, "Create"): the regions,
// sprites, surfaces, scroll bar and 3D model each window is made of. See docs/notes/ui.md.
//
// The builders call the sprite placement functions (005E70E0, 005E7D60) and CUiRegion::SetRect
// through their original addresses: Rebirth patches them for its widescreen layout.
#include "ui.h"

#include "console.h"
#include "item_db_text.h"
#include "maths.h"
#include "ui_screens.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>
#include <new>

// --- Forwarders ---------------------------------------------------------------------------------

__declspec(naked) void CUiRegion::SetTip(int, int, int) EI_ORIGINAL(0x005FC730)
__declspec(naked) void CUiRegion::CallSetRect(int, int, int, int) EI_ORIGINAL(0x005FC9E0)
__declspec(naked) CUiSurface* CUiSurface::Construct(CInterface3D*, int, int, int, int, int, int) EI_ORIGINAL(0x005FCC00)
__declspec(naked) void CUiModel::Load(const char*, const char*, const char*) EI_ORIGINAL(0x005FC1F0)
__declspec(naked) void CUiModel::ShowAllParts(int) EI_ORIGINAL(0x005FC4F0)
__declspec(naked) void CUiModel::ShowPart(const char*, int) EI_ORIGINAL(0x005FC4C0)
__declspec(naked) int CUiWindow::LoadTexture(const char*, int, int) EI_ORIGINAL(0x005FEF20)
__declspec(naked) CEngineObject* CUiWindow::CreateTexture(int, int, int) EI_ORIGINAL(0x005FF0B0)
__declspec(naked) CUiMesh* CUiWindow::AddSprite(int) EI_ORIGINAL(0x005FF3C0)
__declspec(naked) void CUiMesh::SetRectangle(float, float, float, float, float, float, int, int, int, int)
    EI_ORIGINAL(0x005E70E0)
__declspec(naked) void CUiMesh::SetFrame(float, float, float, float, float, float, float, int, int, int, int)
    EI_ORIGINAL(0x005E7D60)
__declspec(naked) void CUiContainer::AddChild(CUiWindow*) EI_ORIGINAL(0x00600D30)
__declspec(naked) CUiScrollbar* CUiScrollbar::Construct(CInterface3D*, RECT, float, int, float) EI_ORIGINAL(0x0065AEA0)
__declspec(naked) void CSaveLoadWindow::FillEntries() EI_ORIGINAL(0x00655D40)
__declspec(naked) void CUiRegion::SetTipString(const char*, int, int) EI_ORIGINAL(0x005FC6B0)
__declspec(naked) void COptionsWindow::BuildSettings() EI_ORIGINAL(0x00651940)
__declspec(naked) void COptionsWindow::LoadSettings() EI_ORIGINAL(0x00652200)
__declspec(naked) void COptionsWindow::BuildBindings() EI_ORIGINAL(0x00651600)

namespace {
// The text manager (global at 007AF788; item_db_text.h) as the builders use it.
struct UiTexts {
    // 004DEFC0 (not rewritten). Finds the text `key`; non-zero when found (the cursor is then
    // at its start).
    int Find(const char* key);
};
__declspec(naked) int UiTexts::Find(const char*) EI_ORIGINAL(0x004DEFC0)
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
    model->Load(GameText(0x00793FA8), GameText(0x00793FB0), GameText(0x00793FBC));   // "unmoco1", "escMenu00", "escMenu00labels"
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
    CreateTexture(0x100, 0x100, 0x1E);

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
    FillEntries();
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
    ShowPicture();
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
    auto* groupNames = reinterpret_cast<console::CStringArray*>(groupNames_);
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
        groupNames->SetAtGrow(groupNames->GetSize(), text);
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
    unknown_414 = 0;
    BuildSettings();
    LoadSettings();
    BuildBindings();
    displayModeChanged_ = false;
    ShowGroup(0);
    if (!sliding_) interface_->ShowHelp(0);
}
EI_REPLACE_METHOD(0x006503C0, COptionsWindow::Build);

// --- Esc menu (0062AB20) ------------------------------------------------------------------------

void CEscMenu::Build() {
    window.rect_ = RECT{0, 0, 800, 600};
    CUiModel* model = window.AddModel();
    model->Load(GameText(0x00793FA8), GameText(0x00793FB0), GameText(0x00793FBC));
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
    reinterpret_cast<UiModelAnim*>(window.models_.data_[0])->Play(1);
    window.interface_->PlaySound(GameText(0x00793F90), 0);   // "buttons\menu\shift.wav"
}
EI_REPLACE_METHOD(0x0062AB20, CEscMenu::Build);

// --- Esc menu clicks (0062AE90) -------------------------------------------------------------------

namespace {
// Not rewritten: the container window constructor, the STLport vector constructors of the two
// windows' members, the message box constructor and the interface's window activation. Thiscall
// methods are declared __fastcall with the unused EDX.
__declspec(naked) void __fastcall ConstructContainer(void* /*memory*/, void*, CInterface3D*) EI_ORIGINAL(0x00600B20)
__declspec(naked) void __fastcall ConstructSaveList(void* /*vector*/, void*, void* /*allocator*/) EI_ORIGINAL(0x0062B550)
__declspec(naked) void __fastcall ConstructBindingList(void*, void*, void*) EI_ORIGINAL(0x0062B570)
__declspec(naked) void __fastcall ConstructSettingList(void*, void*, void*) EI_ORIGINAL(0x0062B5E0)
__declspec(naked) CUiWindow* __fastcall ConstructMessageBox(void* /*memory*/, void*, CInterface3D*, const char*,
                                                            const char* /*text key*/, int, int, int)
    EI_ORIGINAL(0x00634650)
__declspec(naked) void __fastcall ActivateWindow(CInterface3D*, void*, void* /*window*/, int, int) EI_ORIGINAL(0x005F7350)

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
    ConstructContainer(w, nullptr, ui);
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
    ConstructContainer(w, nullptr, ui);
    char allocator[2];
    ConstructBindingList(reinterpret_cast<std::uint8_t*>(w) + 0x36C, nullptr, &allocator[0]);
    ConstructSettingList(&w->settings_, nullptr, &allocator[1]);
    w->sliding_ = false;
    new (w->groupNames_) console::CStringArray();
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
    if (open) ActivateWindow(ui, nullptr, opened, 1, 1);
    UiContainerMouseDown(window.InputSide(), nullptr, buttons, x, y);
}

namespace {
void __fastcall EscMenuLButtonDown(void* input, void*, int buttons, int x, int y) {
    reinterpret_cast<CEscMenu*>(static_cast<std::uint8_t*>(input) - 0x94)->OnLButtonDown(buttons, x, y);
}
} // namespace
EI_REPLACE(0x0062AE90, EscMenuLButtonDown);
