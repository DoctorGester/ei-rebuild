// The multiplayer hero screen's Create. Rebirth's WideScreen adds two hidden curtain sprites over the
// screen's margins (widescreen::AddCurtains), which this does after the original's body; EI_Plugin.asi
// patches it too (EiPluginFixes, reproduced: the figures' sex test). See ui_screens.h and docs/notes/ui.md,
// section 5. Scenarios ui_hero_screen, ui_hero_stats and ui_hero_delete open the screen.
#include "party.h"
#include "ui.h"
#include "ui_screens.h"

#include "core_transform.h"
#include "item_db_text.h"
#include "ui_camp.h"
#include "ui_item_info.h"
#include "ui_widescreen.h"
#include "unit_create.h"
#include "unit_db.h"
#include "world.h"

#include "ei/rebirth.h"
#include <cstdint>
#include <cstring>
#include <string>
#include "lib/str.h"
#include "lib/text_case.h"
#include "lib/vecmath.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstddef>
#include <new>
#include <format>

namespace {

constexpr float kMenuTilt = /*007C7F14*/ 1.5707964f;   // a static initialiser: pi * 0.5

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

// By name (_mbscmp). Names that compare equal are the same text.
void SortNames(std::vector<std::string>& names) { std::sort(names.begin(), names.end(), str::MbsLess()); }

// A figure the new hero can take: its voice (when the creature's record has the flag) and its
// prototype's name.
void AddFigure(std::vector<MpVoiceEntry>& voices, std::vector<std::string>& names, const UnitPrototypeRecord* prototype, const std::string& label,
               bool voiced) {
    if (voiced) voices.push_back(MpVoiceEntry{prototype->name, label});
    names.push_back(prototype->name);
}

// EI_Plugin's test: the 16 bits at +4 of the model's name ("fe"), as the plugin reads them, past a short
// name's end included.
bool ModelNameIsFemale(const std::string& model) {
    std::uint16_t word;
    std::memcpy(&word, model.c_str() + 4, sizeof word);
    return word == 0x6566;
}

} // namespace

void CMpHeroScreen::CreateScreen() {
    g_gameFlow.UseResources(true);
    LoadTexture("campinfo", TextureKind::Interface);
    CUiModel* model = AddModel();
    model->Load("unmoco2", "MainMenu00", "MainMenu00labels");
    model->SetScreenPosition(400.0f, 350.0f, 8.0f);
    {
        // Turned about x by pi / 2 times 1.2.
        const Vector3 axis = {1.0f, 0.0f, 0.0f};
        Quaternion turn;
        model->object_->localRotation_ = *turn.SetAxisAngle(axis, kMenuTilt * 1.2f);
    }
    model->ShowAllParts(0);
    model->ShowPart("but03", 1);
    model->ShowPart("button03_multiplayer", 1);

    // The nine regions every mode keeps (the mode function places the first four and gives the tips).
    auto region = [this](int left, int top, int right, int bottom) {
        AddRegion()->SetRect(left, top, right, bottom);
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

    CreateBase();
    LoadTexture("saveload", TextureKind::Interface);

    auto sprite = [this](float depth, float left, float top, float right, float bottom, int u0, int v0, int u1, int v1) {
        CUiMesh* added = AddBlankSprite();
        added->SetRectangle(depth, 100.0f, left, top, right, bottom, u0, v0, u1, v1);
        return added;
    };
    auto plain = [](CUiMesh* mesh, std::uint32_t colour) {
        mesh->texture_ = nullptr;
        mesh->flags_ |= FigureMeshFlag::Flat;
        mesh->flatColour_ = colour;
    };
    auto texture = [this](int index) { return objects_[index]; };
    // 0, 1: the two buttons under the list; 2: the selection frame.
    sprite(3.0f, 163.0f, 526.0f, 237.0f, 574.0f, 0x51, 2, 0x9B, 0x32)->texture_ = texture(kSaveLoadTexture);
    sprite(3.0f, 563.0f, 526.0f, 637.0f, 574.0f, 0xA0, 2, 0xEA, 0x32)->texture_ = texture(kSaveLoadTexture);
    {
        CUiMesh* frame = sprite(3.0f, 110.0f, 250.0f, 370.0f, 274.0f, 0, 0, 0x100, 0x100);
        plain(frame, kSelectionColour);
        frame->visible_ = false;
    }
    // 3-6: the quarters of the frame around the figure.
    LoadTexture("campm1", TextureKind::Interface);
    LoadTexture("campm2", TextureKind::Interface);
    LoadTexture("campm3", TextureKind::Interface);
    LoadTexture("campm4", TextureKind::Interface);
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
        CUiMesh* added = AddBlankSprite();
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
    descPanel_ = new CItemInfoPanel(interface_, this);
    AddChild(descPanel_);
    descPanel_->Build(600, 100);

    // The hero's statistics.
    statsPanel_ = new CHeroStatsPanel(interface_, this);
    AddChild(statsPanel_);
    statsPanel_->AddObject(texture(kPanelTexture));
    statsPanel_->Build();

    // The equipment (without the body) and the quick items.
    weaponPanel_ = new CHeroEquipPanel(interface_, this);
    AddChild(weaponPanel_);
    weaponPanel_->AddObject(texture(kItemTexture));
    weaponPanel_->Build(false);

    spellPanel_ = new CHeroQuickStrip(interface_, this);
    AddChild(spellPanel_);
    spellPanel_->AddObject(texture(kItemTexture));
    spellPanel_->Build(false);

    // The skills for experience.
    tradePanel_ = new CTradePanel(interface_, this);
    AddChild(tradePanel_);
    tradePanel_->AddObject(texture(kPanelTexture));
    tradePanel_->Build();

    heroScroll_ = new CUiScrollbar(interface_, RECT{110, 250, 384, 490}, 3.0f, 0, 1.0f);
    AddChild(heroScroll_);

    // The figures a new hero can take, men (model "unhuma") and women ("unhufe"): every creature
    // record whose unit prototype has one of the two models, with a numbered voice when the
    // record has its flag.
    CTextManager* texts = g_clientWorld.texts_;
    std::string male;
    std::string female;
    std::string figureModel;
    std::string label;
    texts->GetString("lmp_male_voice", &male);
    texts->GetString("lmp_female_voice", &female);
    std::vector<std::string>& men = defaults_;
    std::vector<std::string>& women = names_;
    std::vector<MpVoiceEntry>& maleVoices = voiceLists_[0];
    std::vector<MpVoiceEntry>& femaleVoices = voiceLists_[1];
    const NpcPrototypeTable& creatures = g_unitDatabase.npcs;
    const int count = static_cast<int>(creatures.records.size());
    for (int i = 0; i < count; ++i) {
        // The vector's checked access, repeated like the original.
        auto creature = [&creatures, i]() -> const UnitNpcRecord* {
            return static_cast<std::uint32_t>(i) < creatures.records.size() ? &creatures.records[i] : nullptr;
        };
        const UnitPrototypeRecord* prototype;
        {
            std::string key(creature()->name);
            prototype = unitdb::PrototypeNamed(key);
        }
        if (!prototype) continue;
        const UnitModelRecord* figure;
        {
            // operator[] inlined (a pair of the name and null through).
            std::string key(prototype->modelName);
            figure = unitdb::ModelNamed(key);
        }
        figureModel = figure->figureName;
        auto isModel = [&figureModel](const char* name) {
            return text::CompareIgnoringCase(figureModel,
                                   name) == 0;
        };
        bool isMale = isModel("unhuma");
        bool isFemale = !isMale && isModel("unhufe");
        if (ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginFixes)) {
            // EI_Plugin's fixes: the two name comparisons become one test of
            // the two characters at +4 of the model's name (the word 0x6566, "fe"); a model that
            // has them is a woman's, every other one a man's.
            isFemale = ModelNameIsFemale(figureModel);
            isMale = !isFemale;
        }
        if (isMale) {
            label = std::format("{} {}", male, static_cast<int>(maleVoices.size()) + 1);
            AddFigure(maleVoices, men, prototype, label, creature()->voiced != 0);
        }
        if (isFemale) {
            label = std::format("{} {}", female, static_cast<int>(femaleVoices.size()) + 1);
            AddFigure(femaleVoices, women, prototype, label, creature()->voiced != 0);
        }
    }
    // Both lists by name, the women's from the end.
    SortNames(men);
    SortNames(women);
    std::reverse(women.begin(), women.end());

    // The two figure strips: the men on top, the women below.
    auto figureStrip = [this](int index, std::vector<std::string>& names, bool top) {
        figureStrips_[index] = new CFigureStrip(interface_, this);
        AddChild(figureStrips_[index]);
        figureStrips_[index]->SetupFigures(&names, top, false);
    };
    figureStrip(0, men, true);
    figureStrip(1, women, false);

    // The edit windows: a listed hero's new name, and the new hero's name.
    renameEdit_ = new CUiEditField(interface_, RECT{558, 110, 695, 126});
    AddChild(renameEdit_);
    renameEdit_->SetLook(3, 0, 1, kNameLength, kRenameColour, kCaretColour);
    renameEdit_->SetEnabled(false);
    renameEdit_->SetFocused(false);
    nameEdit_ = new CUiEditField(interface_, RECT{0, 127, 200, 143});
    AddChild(nameEdit_);
    nameEdit_->SetLook(3, 1, 1, kNameLength, kWhite, kCaretColour);
    nameEdit_->SetEnabled(false);
    nameEdit_->SetFocused(false);
    voiceScroll_ = new CUiScrollbar(interface_, RECT{10, 325, 190, 490}, 3.0f, 0, 1.0f);
    AddChild(voiceScroll_);
    voiceScroll_->SetEnabled(false);
    voiceSound_ = -1;
    voiceTop_ = 0;
    voiceIndex_ = 0;
    voices_ = &voiceLists_[0];
    turn_ = 0.0f;
    rotating_ = 0;

    AddSurface(new CUiSurface(interface_, 100, 240, 390, 500, SurfaceBlend::Keyed, true));
    AddSurface(new CUiSurface(interface_, 100, 100, 390, 220, SurfaceBlend::Keyed, true));
    AddSurface(new CUiSurface(interface_, 0, 300, 200, 500, SurfaceBlend::Keyed, true));
    AddSurface(new CUiSurface(interface_, 425, 110, 555, 126, SurfaceBlend::Keyed, true));
    surfaces_[kSurfaceRename]->Hide(false);

    // The list's four commands.
    std::string text;
    CUiSurface* commands = surfaces_[kSurfaceCommands];
    commands->Clear();
    const char* const commandKeys[] = {
        "lmp_pers_new",
        "lmp_pers_rename",
        "lmp_pers_delete",
        "lmp_pers_redress",
    };
    int top = 16;
    for (const char* key : commandKeys) {
        texts->GetString(key, &text);
        RECT rect{0, top, 290, top + 20};
        commands->DrawTextIn(text, &rect, 1, kWhite, kTextFormat, 1);
        top += 24;
    }

    // The saved heroes, from the "mp" folder of the game's data.
    SavedParties* heroes = &g_savedParties;
    heroes->folder_ = g_dataFolder + "mp\\";
    heroes->Reload();
    RefreshHeroScroll();
    DrawHeroList();
    heroIndex_ = heroes->entries_.size() == 0 ? -1 : 0;
    SetMode(kModeList);
    // Rebirth's WideScreen: the curtains, hidden (sprites 19, 20).
    widescreen::AddCurtains(this, 0);
}
