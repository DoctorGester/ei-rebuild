// The multiplayer hero screen's per-frame draw and update, its close
// and the trade panel's experience figures it shows.
// See docs/notes/ui.md, section 5.
#include "game/ui/ui.h"
#include "game/ui/ui_scrollbar.h"
#include "game/ui/ui_mesh.h"

#include "engine/core/core_maths.h"
#include "game/app/singletons.h"
#include "game/objects/object3d.h"
#include "game/players/party.h"
#include "engine/core/random.h"
#include "game/players/saved_parties.h"
#include "engine/data/text_manager.h"
#include "engine/scene/scene_renderer.h"
#include "game/ui/ui_camp.h"
#include "game/ui/ui_cursor.h"
#include "game/ui/ui_item_info.h"
#include "game/ui/ui_screens.h"
#include "game/ui/screen_layout.h"
#include "game/units/unit.h"
#include "game/world/world.h"
#include "lib/crt.h"
#include "lib/rect.h"
#include "lib/str.h"
#include "platform/platform.h"

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>

namespace {

const char* const kLevelUpKey = "camp_level_up";
const char* const kSignedFormat = "%+d";
const char* const kCurrentExpKey = "camp_current_exp";
const char* const kLabelFormat = "%s %d";
const char* const kSelectVoiceKey = "lmp_select_voice";

// The preview's full turn (2 pi), the modulus of its angle.
constexpr float kFullTurn = /*007C7F10*/ 6.2831855f;   // a static initialiser: pi + pi
// Animation frames per millisecond of timeGetTime (15 / 1024; a double: the milliseconds since the system started).
constexpr double kFramesPerMillisecond = 15.0 / 1024.0;

constexpr float kListDepth = 553.0f;
constexpr float kNewHeroDepth = 400.0f;
constexpr std::uint32_t kWhite = 0xFFFFFF;
constexpr int kFont = 1;

const char* const kVoicesKey = "lmp_voices";
// The voice rows' first y offset of the selection frame.
constexpr float kVoiceFrameTop = 332.5f;
constexpr render::text::Format kListFormat = render::text::Format::NoPrefix;
constexpr float kFrameDepth = 3.0f;

const char* const kHeroName = "Hero";
const char* const kHumanHero = "Human Hero";
constexpr SkillAnimation kPreviewAnimation = SkillAnimation::Special14;      // what the preview plays

} // namespace

// --- The trade panel's experience figures ------------------------------------

int TradePanel::Experience() const {
    const TradeStatsRecord& record = Record();
    return static_cast<int>(record.pendingExperience + record.experience - record.spentExperience);
}

int TradePanel::PurchaseCost() {
    PartyUnitRecord* hero = owner->HeroRecord();
    return static_cast<int>(hero->stats.spentExperience - Record().spentExperience);
}

// --- Draw ------------------------------------------------------------------------------

void MpHeroScreen::DrawExperience() {
    std::vector<UiLabel>& out = labels[2];
    std::string levelText;
    std::string line;
    out.clear();
    levelText = g_clientWorld.texts->GetString(kLevelUpKey);
    out.push_back({levelText, Rect{0, 130, 200, 150}, {kFont, kWhite, render::text::Format::Center}});
    levelText = str::Format(kSignedFormat, -tradePanel->PurchaseCost());
    out.push_back({levelText, Rect{0, 150, 200, 170}, {kFont, kWhite, render::text::Format::Center}});
    levelText = g_clientWorld.texts->GetString(kCurrentExpKey);
    line = str::Format(kLabelFormat, levelText, tradePanel->Experience());
    out.push_back({line, Rect{0, 110, 200, 130}, {kFont, kWhite, render::text::Format::Center}});
}

void MpHeroScreen::DrawSprites(SceneRenderer* pass) {
    if (mode == kModeStats) DrawExperience();
    const Point cursor = g_cursor.position;
    ItemUnder under = weaponPanel->ItemAt(cursor);
    if (!under.item && under.topic == 0) under = spellPanel->ItemAt(cursor);
    hoveredItem = under.item;
    const int topic = under.topic;

    if (hoveredItem) {
        if (descPanel) descPanel->Describe(hoveredItem);
    } else if (topic != 0) {
        if (descPanel) descPanel->ShowHelp(topic);
    } else {
        ItemHoverInfo info;
        if (tradePanel->enabled) info = tradePanel->HoverInfo(cursor);
        if (info.kind == HoverKind::None) {
            if (statsPanel->enabled) info = statsPanel->HoverInfo(cursor);
            if (info.kind == HoverKind::None && mode == kModeNewHero) {
                if (const int row = ListRow(RegionUnder(cursor)); row >= 0 && row < kVoiceRows) {
                    info.kind = HoverKind::Text;
                    info.text = kSelectVoiceKey;
                }
            }
        }
        if (descPanel) descPanel->DescribeStats(info);
    }

    UiContainer::DrawSprites(pass);
    if (mode == kModeList && heroIndex >= 0)
        DrawHeroPreview(pass, previewModel, turn, kListDepth, false);
    else if (mode == kModeNewHero)
        DrawHeroPreview(pass, previewModel, turn, kNewHeroDepth, false);
}

// --- Update ----------------------------------------------------------------------------

void MpHeroScreen::Update() {
    UiContainer::Update();
    if (rotating == 1 || rotating == 2) {
        float step = FrameTime() * 2.0f;
        turn = std::fmod(rotating == 1 ? step + turn : turn - step, kFullTurn);
    }
    if (heroScroll->TakeMoved()) {
        heroTop = heroScroll->Position();
        DrawHeroList();
    }
    if (voiceScroll->TakeMoved()) {
        voiceTop = voiceScroll->Position();
        if (mode != kModeNewHero) return;
        DrawVoices();
    }
    if (mode != kModeNewHero) return;

    std::string name = nameEdit->text;
    const UnitStats* stats = &HeroRecord()->stats;
    float points = stats->intelligenceBase + stats->dexterityBase + stats->strengthBase;
    float target = kAttributePoints;
    bool equal = points == target;
    bool ready = equal && !name.empty();
    float shade = ready ? 1.0f : 0.5f;
    createIcon->tint[0] = shade;
    createIcon->tint[1] = shade;
    createIcon->tint[2] = shade;
}

// --- Close -----------------------------------------------------------------------------

void MpHeroScreen::CloseScreen() {
    g_savedParties.SaveAll();
    std::vector<PartyUnitRecord*> records;
    for (PartyUnitRecord& record : LocalPlayerData()->members) records.push_back(&record);
    g_clientWorld.RemoveUnitsOf(records);
    previewModel = nullptr;
    descPanel->Describe(nullptr);
}

// --- The hero list and the voices drawn -----------------------------------------------------

void MpHeroScreen::DrawHeroList() {
    labels[0].clear();
    selectionFrame->visible = false;
    Rect area = {12, 13, 265, 33};
    const auto count = static_cast<std::uint32_t>(g_savedParties.Count());
    std::uint32_t index = static_cast<std::uint32_t>(heroTop);
    for (int y = 262; y < 502; y += 24, ++index) {
        if (index >= count) break;
        if (static_cast<int>(index) == heroIndex) {
            selectionFrame->visible = true;
            selectionFrame->Place(240.0f, static_cast<float>(y), kFrameDepth);
        }
        // A hero record is a party; its name the first member's scriptName.
        labels[0].push_back({g_savedParties.Hero(static_cast<int>(index))->GetParty()->members[0].scriptName, area, {1, kUiTextColour, kListFormat}});
        area.Offset(0, 24);
    }
}

void MpHeroScreen::DrawVoices() {
    std::vector<UiLabel>& out = labels[2];
    out.clear();
    selectionFrame->visible = false;
    std::string title;
    title = g_clientWorld.texts->GetString(kVoicesKey);
    out.push_back({title, Rect{10, 5, 190, 20}, {0, kWhite, render::text::Format::Center}});
    Rect area = {20, 25, 175, 40};
    std::uint32_t index = static_cast<std::uint32_t>(voiceTop);
    for (int offset = 0; offset < 165; offset += 15, ++index) {
        const std::vector<MpVoiceEntry>& currentVoices = *voices;
        if (index >= currentVoices.size()) break;
        if (static_cast<int>(index) == voiceIndex) {
            selectionFrame->visible = true;
            float y = offset + kVoiceFrameTop;
            selectionFrame->Place(97.5f, y, kFrameDepth);
        }
        out.push_back({currentVoices[index].name, area, {0, kUiTextColour, kListFormat}});
        area.Offset(0, 15);
    }
}

// --- The preview's unit -------------------------------------------------------------------

void MpHeroScreen::MakePreview(bool allArmour) {
    previewModel = nullptr;
    auto* player = LocalPlayerData();
    if (player->members.empty()) {
        player->Clear(true, false);
        player->AddMember(kHeroName, kHumanHero, "");
    }
    const std::vector<PartyUnitRecord*> records{player->members.data()};
    g_clientWorld.CreatePartyUnits(records, allArmour);
    auto* unit = g_clientWorld.FindUnit(player->members.data()->unitId);
    unit->SetSecondPrototype(unit->GetPrototype());
    previewModel = unit;
    if (unit) {
        FigureAnimation* animation = unit->FindSkillAnimation(kPreviewAnimation);
        if (animation) {
            std::uint32_t random = g_clientRandom.Next();
            double frames = platform::FrameClock() * kFramesPerMillisecond;
            int start = static_cast<int>(frames - static_cast<int>(random % 51));
            unit->SetAnimationEntries(-1, static_cast<std::int8_t>(animation->index), start, 0);
        }
        UnitController* controller = unit->controller;
        controller->ShowWeapon(kNoWeaponSlot);
        controller->RefreshModel();
        controller->RestartAnimation(static_cast<float>(platform::FrameClock() * kFramesPerMillisecond - 1.0));
        unit->SetFromStats(&player->members.data()->stats);
        unit->controller->RefreshWounds();
    }
}

// --- A hero shown -------------------------------------------------------------------------

void MpHeroScreen::ShowHero(int index) {
    auto* player = LocalPlayerData();
    if (!player->members.empty()) {
        std::vector<PartyUnitRecord*> records{player->members.data()};
        g_clientWorld.RemoveUnitsOf(records);
    }
    heroIndex = -1;
    if (index >= 0 && index < g_savedParties.Count()) {
        heroIndex = index;
        player->Assign(g_savedParties.Hero(index)->GetParty());
        g_savedParties.Select(g_savedParties.Hero(index));
    }
    MakePreview(true);
    auto* hero = HeroRecord();
    auto* unit = g_clientWorld.FindUnit(hero->unitId);
    for (int slot = 0; slot < 4; ++slot) unit->RemoveWeapon(slot);
    for (Item* item : hero->weapons) unit->AddWeapon(item);
    unit->SendEquipment();
    unit->controller->ShowWeapon(hero->weaponIndex);
    unit->controller->RefreshModel();
    turn = 0.0f;
    DrawHeroList();
}

// --- Mode --------------------------------------------------------------------------------------------

// The multiplayer hero screen's mode (MpHeroScreen::SetMode): the hero list (9), the new
// hero (10) and its statistics page (11) laid out. Rebirth's WideScreen adds the curtains' visibility
// (screen_layout::ShowHeroCurtains) after the original's body, which this does too. See ui_screens.h and
// docs/notes/ui.md, section 5.

namespace {

constexpr int kRowsBottom = 490;

constexpr float kQuarterDepth = 7.0f;
constexpr float kScale = 100.0f;

void ShowNewHeroIcons(MpHeroScreen* screen, bool visible) {
    for (UiMesh* icon : {screen->resetIcon, screen->cancelIcon, screen->createIcon}) icon->visible = visible;
}

// A quarter of the frame around the figure: texels (u0, 0x1C)-(u1, 0xE4).
void SetQuarter(MpHeroScreen* screen, int quarter, float left, float top, float right, float bottom, int u0, int u1) {
    screen->quarters[quarter]->SetRectangle(kQuarterDepth, kScale, left, top, right, bottom, u0, 0x1C, u1, 0xE4);
}

// The selection frame over a list's first row (the whole texture).
void SetSelectionFrame(MpHeroScreen* screen, float left, float top, float right, float bottom) {
    screen->selectionFrame->SetRectangle(kFrameDepth, kScale, left, top, right, bottom, 0, 0, kUiTextureSize,
                                         kUiTextureSize);
}

// The rows of a list as regions (the screen's listRows): `height` apart from `top` down to kRowsBottom.
void AddRows(MpHeroScreen* screen, int left, int right, int top, int height) {
    int y = top;
    do {
        const int next = y + height;
        UiRegion* row = screen->AddRegion();
        row->SetRect(left, y, right, next);
        screen->listRows.push_back(row);
        y = next;
    } while (y < kRowsBottom);
}

} // namespace

void MpHeroScreen::SetMode(int newMode) {
    UiModel* menu = UiWindow::models[0].get();    // the window's models (ItemScreen::models is the item cache)
    menu->ShowAllParts(0);
    weaponPanel->SetEnabled(false);
    spellPanel->SetEnabled(false);
    tradePanel->SetEnabled(false);
    statsPanel->SetEnabled(false);
    figureStrips[0]->SetEnabled(false);
    figureStrips[1]->SetEnabled(false);
    heroScroll->SetEnabled(false);
    voiceScroll->SetEnabled(false);
    iconWindow->SetEnabled(false);
    for (UiMesh* sprite : sceneObjects) sprite->visible = false;
    for (const auto& surface : surfaces) surface->Hide();
    for (UiRegion* row : listRows) RemoveRegion(row);
    listRows.clear();
    for (const auto& region : regions) region->SetTip(0);

    if (newMode == kModeList) {
        menu->ShowPart("but03", 1);
        menu->ShowPart("button03_multiplayer", 1);
        heroScroll->SetEnabled(true);
        for (UiMesh* sprite : sceneObjects) sprite->visible = true;
        ShowNewHeroIcons(this, false);
        SetQuarter(this, 0, 410.0f, 100.0f, 555.0f, 300.0f, 0x53, 0xE4);
        SetQuarter(this, 1, 555.0f, 100.0f, 700.0f, 300.0f, 0x1C, 0xAD);
        SetQuarter(this, 2, 410.0f, 300.0f, 555.0f, 500.0f, 0x53, 0xE4);
        SetQuarter(this, 3, 555.0f, 300.0f, 700.0f, 500.0f, 0x1C, 0xAD);
        backButton->SetRect(563, 526, 637, 574);
        forwardButton->SetRect(163, 526, 237, 574);
        turnButtons[0]->SetRect(455, 470, 485, 500);
        turnButtons[1]->SetRect(625, 470, 655, 500);
        backButton->SetTip(0x9CA5);
        forwardButton->SetTip(0x9CA4);
        newHeroButton->SetTip(0x9D08);
        renameButton->SetTip(0x9D09);
        deleteButton->SetTip(0x9D0A);
        inventoryButton->SetTip(0x9D0B);
        surfaces[0]->Show();
        surfaces[1]->Show();
        nameEdit->SetEnabled(false);
        nameEdit->SetFocused(false);
        SetSelectionFrame(this, 110.0f, 250.0f, 370.0f, 274.0f);
        AddRows(this, 110, 370, 250, 24);
        ShowHero(heroIndex);
    } else if (newMode == kModeNewHero) {
        voiceScroll->SetEnabled(true);
        for (UiMesh* quarter : quarters) quarter->visible = true;
        ShowNewHeroIcons(this, true);
        SetQuarter(this, 0, 200.0f, 100.0f, 400.0f, 300.0f, 0x1C, 0xE4);
        SetQuarter(this, 1, 400.0f, 100.0f, 600.0f, 300.0f, 0x1C, 0xE4);
        SetQuarter(this, 2, 200.0f, 300.0f, 400.0f, 500.0f, 0x1C, 0xE4);
        SetQuarter(this, 3, 400.0f, 300.0f, 600.0f, 500.0f, 0x1C, 0xE4);
        iconWindow->SetEnabled(true);
        iconWindow->PlaceBars(300);
        statsPanel->SetEnabled(true);
        figureStrips[0]->SetEnabled(true);
        figureStrips[1]->SetEnabled(true);
        backButton->SetRect(620, 430, 660, 470);
        forwardButton->SetRect(740, 430, 780, 470);
        turnButtons[0]->SetRect(300, 470, 330, 500);
        turnButtons[1]->SetRect(470, 470, 500, 500);
        backButton->SetTip(0x9D6D);
        forwardButton->SetTip(0x9D6C);
        resetButton->SetTip(0x9D6E);
        nameEdit->SetEnabled(true);
        nameEdit->SetFocused(true);
        surfaces[2]->Show();
        SetSelectionFrame(this, 15.0f, 310.0f, 180.0f, 325.0f);
        AddRows(this, 10, 180, 325, 15);
        DrawVoices();
    } else if (newMode == kModeStats) {
        ShowNewHeroIcons(this, true);
        iconWindow->SetEnabled(true);
        iconWindow->PlaceBars(400);
        weaponPanel->SetEnabled(true);
        spellPanel->SetEnabled(true);
        tradePanel->SetEnabled(true);
        statsPanel->SetEnabled(true);
        backButton->SetRect(620, 430, 660, 470);
        forwardButton->SetRect(740, 430, 780, 470);
        backButton->SetTip(0x9E35);
        forwardButton->SetTip(0x9E34);
        resetButton->SetTip(0x9E36);
        surfaces[2]->Show();
        nameEdit->SetEnabled(false);
        nameEdit->SetFocused(false);
        tradePanel->SelectHero(0);
    }
    descPanel->Describe(nullptr);
    statsPanel->ShowStats(newMode);
    mode = newMode;
    // Rebirth's WideScreen: the curtains shown but on the list.
    screen_layout::ShowHeroCurtains(curtains, newMode);
}
