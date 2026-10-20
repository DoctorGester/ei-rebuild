// The multiplayer hero screen's Create (00613EA0, CMpHeroScreen slot 25). Rebirth's WideScreen
// replaces the function with the original followed by two hidden curtain sprites over the screen's
// margins (widescreen::AddCurtains), which this reproduces; EI_Plugin.asi patches it too
// (EiPluginFixes, reproduced: the figures' sex test), so it is hooked in a Rebirth launch too; nothing in the
// rewritten code calls it but the screen's vtable. See ui_screens.h and docs/notes/ui.md, section 5. Scenarios ui_hero_screen,
// ui_hero_stats and ui_hero_delete open the screen (the main menu's Multiplayer button).
#include "party.h"
#include "stl_sorts.h"
#include "ui.h"
#include "ui_screens.h"
#include "game_strings.h"

#include "core_transform.h"
#include "item_db_text.h"
#include "ui_camp.h"
#include "ui_item_info.h"
#include "ui_widescreen.h"
#include "unit_create.h"
#include "unit_db.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "lib/vecmath.h"
#include "originals_gameplay.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_units.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

namespace {

void** Vtable(std::uintptr_t address) { return reinterpret_cast<void**>(address); }
float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

VtablePair ItemPanelVtables() { return {Vtable(0x00741B58), Vtable(0x00741B24)}; }     // CUiItemPanel
VtablePair InfoPanelVtables() { return {Vtable(0x007404DC), Vtable(0x007404A8)}; }     // CItemInfoPanel
VtablePair StatsPanelVtables() { return {Vtable(0x00741ABC), Vtable(0x00741A88)}; }    // CHeroStatsPanel
VtablePair EquipPanelVtables() { return {Vtable(0x007418E0), Vtable(0x007418AC)}; }    // CHeroEquipPanel
VtablePair QuickStripVtables() { return {Vtable(0x00741844), Vtable(0x00741810)}; }    // CHeroQuickStrip
VtablePair TradePanelVtables() { return {Vtable(0x007417A8), Vtable(0x00741774)}; }    // CTradePanel
VtablePair FigureStripVtables() { return {Vtable(0x00741BF4), Vtable(0x00741C60)}; }

// The objects' sizes where the class declares less (or more) than the original allocates.
constexpr std::size_t kTradePanelSize = 0x1B8;
constexpr std::size_t kFigureStripSize = 0x198;   // the figure strip stops before the item list

constexpr int kTextureKind = 3;
constexpr int kSurfaceFormat = 5;
constexpr int kSlotBuild = 18;          // the statistics and trade panels' Build
constexpr int kModeHeroList = 9;
constexpr std::uint32_t kPlainSprite = 0x100;
constexpr std::uint32_t kSelectionColour = 0xC8A06800;
constexpr std::uint32_t kBackColour = 0xA0000000;
constexpr std::uint32_t kRenameColour = 0xA7D7E4;
constexpr std::uint32_t kWhite = 0xFFFFFF;
constexpr std::uint32_t kCaretColour = 0x31B3FF;
constexpr int kNameLength = 10;
constexpr int kTextFormat = 1;

// objects_: 0 "campinfo", 1 and 2 the item screen's (CreateBase), 3 "saveload", 4..7 the four
// quarters of the frame around the figure.
constexpr int kPanelTexture = 0, kItemTexture = 2, kSaveLoadTexture = 3, kFirstQuarterTexture = 4;
// surfaces_: 0 the hero list, 1 its four commands, 2 the voices, 3 the name being changed.
constexpr int kSurfaceCommands = 1, kSurfaceRename = 3;

// The menu model's turn about x: 007C7F14 (an angle set at start) times 1.2 (0073CDF4).
float MenuTurn() { return GameFloat(0x007C7F14) * GameFloat(0x0073CDF4); }

// The saved heroes' reload (00662FB0, plain RET): Rebirth-patched, so through its address (as the
// screen's mode, 00615D30, originals_ui.h).
__declspec(naked) void __fastcall ReloadHeroesAt(MpHeroRoster*) EI_ORIGINAL(0x00662FB0)
MpHeroRoster* Heroes() { return reinterpret_cast<MpHeroRoster*>(0x007C82A0); }

// The screen's vectors as the library's: the figures' prototype names, and the voices (a
// MpVoiceEntry with its CStrings as objects).
struct Voice {
    mfc::CString file;
    mfc::CString name;
};
static_assert(sizeof(Voice) == sizeof(MpVoiceEntry));
using NameList = stlp::vector<mfc::CString>;
using VoiceList = stlp::vector<Voice>;
static_assert(sizeof(NameList) == sizeof(MpStringVector) && sizeof(VoiceList) == sizeof(MpVoiceVector));

CUiContainer* AsContainer(CMpHeroScreen* screen) { return reinterpret_cast<CUiContainer*>(screen); }

void SetVtables(CUiWindow* window, VtablePair vtables) {
    window->vtable_ = vtables.main;
    window->inputVtable_ = vtables.input;
}

// A panel of `size` bytes built on the window base, owned by `owner`, with the vtable pair; null
// when the allocation fails.
CUiItemPanel* NewPanel(std::size_t size, CInterface3D* ui, CUiWindow* owner, VtablePair vtables) {
    void* memory = gamecrt::operator_new(size);
    if (!memory) return nullptr;
    auto* panel = static_cast<CUiItemPanel*>(memory);
    panel->Construct(ui);
    panel->owner_ = owner;
    SetVtables(panel, vtables);
    return panel;
}

CUiSurface* NewSurface(CInterface3D* ui, int left, int top, int right, int bottom) {
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    return memory ? static_cast<CUiSurface*>(memory)->Construct(ui, left, top, right, bottom, kSurfaceFormat, 1) : nullptr;
}

CUiScrollbar* NewScrollbar(CInterface3D* ui, RECT rect) {
    void* memory = gamecrt::operator_new(sizeof(CUiScrollbar));
    return memory ? static_cast<CUiScrollbar*>(memory)->Construct(ui, rect, 3.0f, 0, 1.0f) : nullptr;
}

CUiEditField* NewEditField(CInterface3D* ui, RECT rect) {
    void* memory = gamecrt::operator_new(sizeof(CUiEditField));
    return memory ? static_cast<CUiEditField*>(memory)->Construct(ui, rect) : nullptr;
}

// Slot 18 of a panel: its Build.
void BuildPanel(CUiWindow* panel) {
    using BuildFn = void(__thiscall*)(CUiWindow*);
    reinterpret_cast<BuildFn>(panel->vtable_[kSlotBuild])(panel);
}

// std::sort(begin, end) as the original inlines it (its first level of the introsort loop does
// the same steps as the out-of-line loop).
void SortNames(NameList& names) {
    mfc::CString* first = names.begin();
    mfc::CString* last = names.end();
    if (first == last) return;
    int depth = 0;
    for (int n = static_cast<int>(last - first); n != 1; n >>= 1) ++depth;
    stl_sorts::NameIntroSort(first, last, nullptr, depth * 2);
    stl_sorts::NameFinalSort(first, last);
}

// A figure the new hero can take: its voice (when the creature's record has the flag) and its
// prototype's name.
void AddFigure(VoiceList& voices, NameList& names, const UnitPrototypeRecord* prototype, const mfc::CString& label,
               bool voiced) {
    if (voiced) voices.push_back(Voice{prototype->name, label});
    names.push_back(prototype->name);
}

// EI_Plugin's test (cmp word [ecx+4], 0x6566 on the name's characters): the 16 bits at +4, as the
// plugin reads them, past a short name's end included.
bool ModelNameIsFemale(const mfc::CString& model) {
    std::uint16_t word;
    std::memcpy(&word, static_cast<const char*>(model) + 4, sizeof word);
    return word == 0x6566;
}

} // namespace

void CMpHeroScreen::Create() {
    TheGameFlow()->UseResources(true);
    window.LoadTexture(game::Text(0x0079365C), kTextureKind, nullptr);   // "campinfo"
    CUiModel* model = window.AddModel();
    model->Load(game::Text(0x0078A5C0), game::Text(0x007939F0), game::Text(0x007939FC));   // "unmoco2", "MainMenu00", "MainMenu00labels"
    model->SetScreenPosition(400.0f, 350.0f, 8.0f);
    {
        const Vector3 axis = {1.0f, 0.0f, 0.0f};
        Quaternion turn;
        model->object_->localRotation_ = *turn.SetAxisAngle(axis, MenuTurn());
    }
    model->ShowAllParts(0);
    model->ShowPart(game::Text(0x007939E8), 1);   // "but03"
    model->ShowPart(game::Text(0x007939D0), 1);   // "button03_multiplayer"

    // The nine regions every mode keeps (00615D30 places the first four and gives the tips).
    auto region = [this](int left, int top, int right, int bottom) {
        window.AddRegion()->CallSetRect(left, top, right, bottom);
    };
    region(600, 400, 680, 500);
    region(720, 400, 800, 500);
    region(300, 470, 330, 500);
    region(470, 470, 500, 500);
    region(680, 430, 720, 470);
    region(180, 112, 310, 136);
    region(180, 136, 310, 160);
    region(180, 160, 310, 184);
    region(180, 184, 310, 208);

    reinterpret_cast<CItemScreen*>(this)->CreateBase();
    window.LoadTexture(game::Text(0x00793380), kTextureKind, nullptr);   // "saveload"

    auto sprite = [this](float depth, float left, float top, float right, float bottom, int u0, int v0, int u1, int v1) {
        CUiMesh* added = window.AddSprite(nullptr);
        added->SetRectangle(depth, 100.0f, left, top, right, bottom, u0, v0, u1, v1);
        return added;
    };
    auto plain = [](CUiMesh* mesh, std::uint32_t colour) {
        mesh->texture_ = nullptr;
        mesh->flags_ |= kPlainSprite;
        mesh->flatColour_ = colour;
    };
    auto texture = [this](int index) { return window.objects_.data[index]; };
    // 0, 1: the two buttons under the list; 2: the selection frame.
    sprite(3.0f, 163.0f, 526.0f, 237.0f, 574.0f, 0x51, 2, 0x9B, 0x32)->texture_ = texture(kSaveLoadTexture);
    sprite(3.0f, 563.0f, 526.0f, 637.0f, 574.0f, 0xA0, 2, 0xEA, 0x32)->texture_ = texture(kSaveLoadTexture);
    {
        CUiMesh* frame = sprite(3.0f, 110.0f, 250.0f, 370.0f, 274.0f, 0, 0, 0x100, 0x100);
        plain(frame, kSelectionColour);
        frame->visible_ = 0;
    }
    // 3-6: the quarters of the frame around the figure.
    window.LoadTexture(game::Text(0x007939C8), kTextureKind, nullptr);   // "campm1"
    window.LoadTexture(game::Text(0x007939C0), kTextureKind, nullptr);   // "campm2"
    window.LoadTexture(game::Text(0x007939B8), kTextureKind, nullptr);   // "campm3"
    window.LoadTexture(game::Text(0x007939B0), kTextureKind, nullptr);   // "campm4"
    sprite(7.0f, 200.0f, 100.0f, 400.0f, 300.0f, 0x1C, 0x1C, 0xE4, 0xE4)->texture_ = texture(kFirstQuarterTexture);
    sprite(7.0f, 400.0f, 100.0f, 600.0f, 300.0f, 0x1C, 0x1C, 0xE4, 0xE4)->texture_ = texture(kFirstQuarterTexture + 1);
    sprite(7.0f, 200.0f, 300.0f, 400.0f, 500.0f, 0x1C, 0x1C, 0xE4, 0xE4)->texture_ = texture(kFirstQuarterTexture + 2);
    sprite(7.0f, 400.0f, 300.0f, 600.0f, 500.0f, 0x1C, 0x1C, 0xE4, 0xE4)->texture_ = texture(kFirstQuarterTexture + 3);
    // 7-9: the new hero's buttons.
    sprite(3.0f, 680.0f, 430.0f, 720.0f, 470.0f, 0x54, 0x98, 0x7C, 0xC0);
    sprite(3.0f, 620.0f, 430.0f, 660.0f, 470.0f, 0x02, 0x98, 0x2A, 0xC0);
    sprite(3.0f, 740.0f, 430.0f, 780.0f, 470.0f, 0x2B, 0x98, 0x53, 0xC0);
    // 10-12: the frames; 13, 14: the backgrounds of the commands and of the list.
    auto frame = [this, &texture](float left, float top, float right, float bottom) {
        CUiMesh* added = window.AddSprite(nullptr);
        added->SetFrame(2.0f, 100.0f, left, top, right, bottom, 5.0f, 2, 0xFE, 0xB4, 0xF9);
        added->texture_ = texture(kSaveLoadTexture);
    };
    frame(95.0f, 95.0f, 395.0f, 225.0f);
    frame(95.0f, 235.0f, 395.0f, 505.0f);
    frame(405.0f, 95.0f, 705.0f, 505.0f);
    plain(sprite(5.0f, 100.0f, 100.0f, 390.0f, 220.0f, 0, 0, 0x100, 0x100), kBackColour);
    plain(sprite(5.0f, 100.0f, 240.0f, 390.0f, 500.0f, 0, 0, 0x100, 0x100), kBackColour);
    // 15-18: the four commands' buttons.
    for (int top = 115; top < 211; top += 24) {
        CUiMesh* button = sprite(3.0f, 180.0f, static_cast<float>(top), 310.0f, static_cast<float>(top + 19), 0x7A, 0x6C,
                                 0xFC, 0x7F);
        button->texture_ = texture(kSaveLoadTexture);
    }

    // The description panel on the right (its item list and text arrays constructed in place).
    auto* info = static_cast<CItemInfoPanel*>(NewPanel(sizeof(CItemInfoPanel), window.interface_, &window, ItemPanelVtables()));
    if (info) {
        info->ItemParty()->Construct();
        info->itemTexts_.Construct();
        info->skillTexts_.Construct();
        SetVtables(info, InfoPanelVtables());
    }
    descPanel_ = info;
    AsContainer(this)->AddChild(info);
    descPanel_->Build(600, 100);

    // The hero's statistics.
    auto* stats = reinterpret_cast<CHeroStatsPanel*>(NewPanel(sizeof(CHeroStatsPanel), window.interface_, &window, ItemPanelVtables()));
    if (stats) {
        new (&stats->title_) mfc::CString;
        stats->texts_.Construct();
        SetVtables(stats, StatsPanelVtables());
    }
    statsPanel_ = stats;
    AsContainer(this)->AddChild(stats);
    stats->AddObject(texture(kPanelTexture));
    BuildPanel(stats);

    // The equipment (without the body) and the quick items.
    auto* equipment = static_cast<CHeroEquipPanel*>(NewPanel(sizeof(CHeroEquipPanel), window.interface_, &window, EquipPanelVtables()));
    weaponPanel_ = equipment;
    AsContainer(this)->AddChild(equipment);
    equipment->AddObject(texture(kItemTexture));
    equipment->Build(false);

    auto* quick = static_cast<CHeroQuickStrip*>(NewPanel(sizeof(CHeroQuickStrip), window.interface_, &window, QuickStripVtables()));
    spellPanel_ = quick;
    AsContainer(this)->AddChild(quick);
    quick->AddObject(texture(kItemTexture));
    quick->Build(false);

    // The skills for experience.
    auto* trade = reinterpret_cast<CTradePanel*>(NewPanel(kTradePanelSize, window.interface_, &window, ItemPanelVtables()));
    if (trade) {
        trade->learnable_ = {};
        trade->perks_ = {};
        trade->schools_ = {};
        trade->science_ = {};
        trade->records_ = {};
        trade->texts_.Construct();
        SetVtables(trade, TradePanelVtables());
    }
    tradePanel_ = trade;
    AsContainer(this)->AddChild(trade);
    trade->AddObject(texture(kPanelTexture));
    BuildPanel(trade);

    {
        CUiScrollbar* bar = NewScrollbar(window.interface_, RECT{110, 250, 384, 490});
        heroScroll_ = bar;
        AsContainer(this)->AddChild(bar);
    }

    // The figures a new hero can take, men (model "unhuma") and women ("unhufe"): every creature
    // record whose unit prototype has one of the two models, with a numbered voice when the
    // record has its flag.
    CTextManager* texts = GameTexts();
    mfc::CString male;
    mfc::CString female;
    mfc::CString figureModel;
    mfc::CString label;
    texts->GetString(game::Text(0x007939A0), &male);     // "lmp_male_voice"
    texts->GetString(game::Text(0x0079398C), &female);   // "lmp_female_voice"
    NameList& men = *reinterpret_cast<NameList*>(&defaults_);
    NameList& women = *reinterpret_cast<NameList*>(&names_);
    VoiceList& maleVoices = *reinterpret_cast<VoiceList*>(&voiceLists_[0]);
    VoiceList& femaleVoices = *reinterpret_cast<VoiceList*>(&voiceLists_[1]);
    const NpcPrototypeTable& creatures = unitdb::NpcTable();
    const int count = static_cast<int>(reinterpret_cast<const UnitNpcRecord*>(creatures.end) -
                                       reinterpret_cast<const UnitNpcRecord*>(creatures.begin));
    for (int i = 0; i < count; ++i) {
        // The vector's checked access, as the original repeats it.
        auto creature = [&creatures, i]() {
            auto* first = reinterpret_cast<const UnitNpcRecord*>(creatures.begin);
            auto* last = reinterpret_cast<const UnitNpcRecord*>(creatures.end);
            return static_cast<std::uint32_t>(i) < static_cast<std::uint32_t>(last - first) ? first + i : nullptr;
        };
        const UnitPrototypeRecord* prototype;
        {
            stlp::string key(static_cast<const char*>(creature()->name));
            prototype = unitdb::PrototypeNamed(key);
        }
        if (!prototype) continue;
        const FigurePrototypeRecord* figure;
        {
            // The original inlines operator[] (a pair of the name and null through 0053D340).
            stlp::string key(static_cast<const char*>(prototype->modelName));
            figure = reinterpret_cast<const FigurePrototypeRecord*>(unitdb::ModelNamed(key));
        }
        figureModel = *reinterpret_cast<const mfc::CString*>(&figure->model);
        auto isModel = [&figureModel](const char* name) {
            return libcrt::Mbsicmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(figureModel)),
                                   reinterpret_cast<const unsigned char*>(name)) == 0;
        };
        bool isMale = isModel(game::Text(0x007908C8));                // "unhuma"
        bool isFemale = !isMale && isModel(game::Text(0x00792EA4));   // "unhufe"
        if (ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginFixes)) {
            // EI_Plugin's fixes (00614BB5, 00614D1B): the two name comparisons become one test of
            // the two characters at +4 of the model's name (the word 0x6566, "fe"); a model that
            // has them is a woman's, every other one a man's.
            isFemale = ModelNameIsFemale(figureModel);
            isMale = !isFemale;
        }
        if (isMale) {
            label.Format(game::Text(0x00792F14), static_cast<const char*>(male), static_cast<int>(maleVoices.size()) + 1);   // "%s %d"
            AddFigure(maleVoices, men, prototype, label, creature()->voiced != 0);
        }
        if (isFemale) {
            label.Format(game::Text(0x00792F14), static_cast<const char*>(female), static_cast<int>(femaleVoices.size()) + 1);
            AddFigure(femaleVoices, women, prototype, label, creature()->voiced != 0);
        }
    }
    // Both lists by name, the women's from the end.
    SortNames(men);
    SortNames(women);
    for (mfc::CString *low = women.begin(), *high = women.end(); low < high; ++low) {
        --high;
        mfc::CString kept(*low);
        *low = *high;
        *high = kept;
    }

    // The two figure strips: the men on top, the women below.
    auto figureStrip = [this](int index, NameList& names, char top) {
        void* memory = gamecrt::operator_new(kFigureStripSize);
        CUiItemStrip* strip = nullptr;
        if (memory) {
            strip = static_cast<CUiItemStrip*>(memory);
            strip->window.Construct(window.interface_);
            strip->owner_ = &window;
            SetVtables(&strip->window, FigureStripVtables());
        }
        figureStrips_[index] = strip;
        AsContainer(this)->AddChild(strip ? &strip->window : nullptr);
        figureStrips_[index]->SetupFigures(reinterpret_cast<MpStringVector*>(&names), top, 0);
    };
    figureStrip(0, men, 1);
    figureStrip(1, women, 0);

    // The edit windows: a listed hero's new name, and the new hero's name.
    {
        CUiEditField* edit = NewEditField(window.interface_, RECT{558, 110, 695, 126});
        renameEdit_ = edit;
        AsContainer(this)->AddChild(edit);
        edit->SetLook(3, 0, 1, kNameLength, kRenameColour, kCaretColour);
        edit->SetEnabled(0, 1);
        edit->SetFocused(false);
    }
    {
        CUiEditField* edit = NewEditField(window.interface_, RECT{0, 127, 200, 143});
        nameEdit_ = edit;
        AsContainer(this)->AddChild(edit);
        edit->SetLook(3, 1, 1, kNameLength, kWhite, kCaretColour);
        edit->SetEnabled(0, 1);
        edit->SetFocused(false);
    }
    {
        CUiScrollbar* bar = NewScrollbar(window.interface_, RECT{10, 325, 190, 490});
        voiceScroll_ = bar;
        AsContainer(this)->AddChild(bar);
        bar->SetEnabled(0, 1);
    }
    voiceSound_ = -1;
    voiceTop_ = 0;
    voiceIndex_ = 0;
    voices_ = reinterpret_cast<char**>(&voiceLists_[0]);
    turn_ = 0.0f;
    rotating_ = 0;

    window.AddSurface(NewSurface(window.interface_, 100, 240, 390, 500));
    window.AddSurface(NewSurface(window.interface_, 100, 100, 390, 220));
    window.AddSurface(NewSurface(window.interface_, 0, 300, 200, 500));
    window.AddSurface(NewSurface(window.interface_, 425, 110, 555, 126));
    window.surfaces_.data[kSurfaceRename]->Freeze(0, 0);

    // The list's four commands.
    mfc::CString text;
    CUiSurface* commands = window.surfaces_.data[kSurfaceCommands];
    UiSurfaceGetDC(commands);
    SurfaceClearOriginal(commands);
    const char* const commandKeys[] = {
        game::Text(0x0079397C),   // "lmp_pers_new"
        game::Text(0x0079396C),   // "lmp_pers_rename"
        game::Text(0x0079395C),   // "lmp_pers_delete"
        game::Text(0x00793948),   // "lmp_pers_redress"
    };
    int top = 16;
    for (const char* key : commandKeys) {
        texts->GetString(key, &text);
        RECT rect{0, top, 290, top + 20};
        SurfaceDrawText(commands, nullptr, text, &rect, 1, kWhite, kTextFormat, 1);
        top += 24;
    }
    UiSurfaceReleaseDC(commands);

    // The saved heroes, from the "mp" folder of the game's data.
    MpHeroRoster* heroes = Heroes();
    heroes->folder = GameDataPath() + game::Text(0x00793944);   // "mp\"
    ReloadHeroesAt(heroes);
    RefreshHeroScroll();
    DrawHeroList();
    heroIndex_ = heroes->records.size() == 0 ? -1 : 0;
    HeroScreenSetModeOriginal(this, nullptr, kModeHeroList);
    // Rebirth's WideScreen (addon's handler after the original): the curtains, hidden (sprites 19, 20).
    widescreen::AddCurtains(&window, 0);
}
EI_REPLACE_METHOD(0x00613EA0, CMpHeroScreen::Create);
// In a Rebirth launch EI_Plugin.asi also patches the function (EiPluginFixes at +D15 / +E7B, the
// figures' sex test above).
