// The multiplayer hero screen's per-frame draw and update, its close
// and the trade panel's experience figures it shows.
// See docs/notes/ui.md, section 5.
#include "ui.h"

#include "core_maths.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "map_object_figures.h"
#include "party.h"
#include "random.h"
#include "text_manager.h"
#include "texture.h"
#include "ui_camp.h"
#include "ui_cursor.h"
#include "ui_item_info.h"
#include "ui_screens.h"
#include "screen_layout.h"
#include "unit.h"
#include "world.h"
#include "world_glue.h"
#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "lib/rect.h"
#include "lib/str.h"
#include "platform/platform.h"

#include <cmath>
#include <cstdint>
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

constexpr int kFirstVoiceRegion = 9;
constexpr int kVoiceRegionEnd = 20;
constexpr int kCreateButton = 9;             // scene object of the create button
constexpr float kListDepth = 553.0f;
constexpr float kNewHeroDepth = 400.0f;
constexpr std::uint32_t kWhite = 0xFFFFFF;
constexpr int kTextFormat = 1;

const char* const kVoicesKey = "lmp_voices";
// The voice rows' first y offset of the selection frame.
constexpr float kVoiceFrameTop = 332.5f;
constexpr int kSelectionFrame = 2;           // scene object
constexpr std::uint32_t kListColour = 0xA7D7E4;
constexpr int kListFormat = 0x800;
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
    UiSurface* surface = surfaces[2];
    std::string levelText;
    std::string line;
    surface->Clear();
    levelText = g_clientWorld.texts->GetString(kLevelUpKey);
    Rect title = {0, 130, 200, 150};
    surface->DrawTextIn(levelText, &title, kTextFormat, kWhite, 1, 1);
    levelText = str::Format(kSignedFormat, -tradePanel->PurchaseCost());
    Rect cost = {0, 150, 200, 170};
    surface->DrawTextIn(levelText, &cost, kTextFormat, kWhite, 1, 1);
    levelText = g_clientWorld.texts->GetString(kCurrentExpKey);
    line = str::Format(kLabelFormat, levelText, tradePanel->Experience());
    Rect current = {0, 110, 200, 130};
    surface->DrawTextIn(line, &current, kTextFormat, kWhite, 1, 1);
}

void MpHeroScreen::Draw(SceneRenderer* pass) {
    if (mode == kModeStats) DrawExperience();
    int x = g_cursor.position.x;
    int y = g_cursor.position.y;
    hoveredItem = nullptr;
    int topic = 0;
    hoveredItem = weaponPanel->ItemAt(x, y, &topic);
    if (!hoveredItem && topic == 0) hoveredItem = spellPanel->ItemAt(x, y, &topic);

    if (hoveredItem) {
        if (descPanel) descPanel->Describe(hoveredItem);
    } else if (topic != 0) {
        if (descPanel) descPanel->ShowHelp(topic);
    } else {
        ItemHoverInfo info;
        if (tradePanel->enabled) info = tradePanel->HoverInfo(x, y);
        if (info.kind == HoverKind::None) {
            if (statsPanel->enabled) info = statsPanel->HoverInfo(x, y);
            if (info.kind == HoverKind::None && mode == kModeNewHero) {
                int region = RegionAt(x, y);
                if (region >= kFirstVoiceRegion && region < kVoiceRegionEnd) {
                    info.kind = HoverKind::Text;
                    info.text = kSelectVoiceKey;
                }
            }
        }
        if (descPanel) descPanel->DescribeStats(info);
    }

    UiContainer::Draw(pass);
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
    EI_NAN_AUDIT(points, target);
    bool equal = points == target;
    bool ready = equal && !name.empty();
    float shade = ready ? 1.0f : 0.5f;
    auto* button = SceneMesh(kCreateButton);
    button->tint[0] = shade;
    button->tint[1] = shade;
    button->tint[2] = shade;
}

// --- Close -----------------------------------------------------------------------------

void MpHeroScreen::CloseScreen() {
    g_savedParties.SaveAll();
    std::vector<PartyUnitRecord*> records;
    for (PartyUnitRecord& record : LocalPlayerData()->members) records.push_back(&record);
    g_clientWorld.RemoveUnitsOf(&records);
    previewModel = nullptr;
    descPanel->Describe(nullptr);
}

// --- The hero list and the voices drawn -----------------------------------------------------

void MpHeroScreen::DrawHeroList() {
    UiSurface* surface = surfaces[0];
    surface->Clear();
    UiMesh* frame = SceneMesh(kSelectionFrame);
    frame->visible = false;
    Rect area = {12, 13, 265, 33};
    const auto count = static_cast<std::uint32_t>(g_savedParties.Count());
    std::uint32_t index = static_cast<std::uint32_t>(heroTop);
    for (int y = 262; y < 502; y += 24, ++index) {
        if (index >= count) break;
        if (static_cast<int>(index) == heroIndex) {
            frame->visible = true;
            frame->Place(240.0f, static_cast<float>(y), kFrameDepth);
        }
        // A hero record is a party; its name the first member's text 0.
        surface->DrawTextIn(g_savedParties.Hero(static_cast<int>(index))->GetParty()->members[0].texts[0], &area, 1, kListColour,
                            kListFormat, 1);
        rects::Offset(&area, 0, 24);
    }
}

void MpHeroScreen::DrawVoices() {
    UiSurface* surface = surfaces[2];
    surface->Clear();
    UiMesh* frame = SceneMesh(kSelectionFrame);
    frame->visible = false;
    std::string title;
    Rect area = {10, 5, 190, 20};
    title = g_clientWorld.texts->GetString(kVoicesKey);
    surface->DrawTextIn(title, &area, 0, kWhite, 1, 1);
    area = {20, 25, 175, 40};
    std::uint32_t index = static_cast<std::uint32_t>(voiceTop);
    for (int offset = 0; offset < 165; offset += 15, ++index) {
        const std::vector<MpVoiceEntry>& currentVoices = *voices;
        if (index >= currentVoices.size()) break;
        if (static_cast<int>(index) == voiceIndex) {
            frame->visible = true;
            float y = offset + kVoiceFrameTop;
            frame->Place(97.5f, y, kFrameDepth);
        }
        surface->DrawTextIn(currentVoices[index].name, &area, 0, kListColour, kListFormat, 1);
        rects::Offset(&area, 0, 15);
    }
}

// --- The preview's unit -------------------------------------------------------------------

void MpHeroScreen::MakePreview(bool allArmour) {
    previewModel = nullptr;
    auto* player = LocalPlayerData();
    if (player->members.empty()) {
        player->Clear(1, 0);
        player->AddMember(kHeroName, kHumanHero, game::kEmptyText);
    }
    const std::vector<PartyUnitRecord*> records{player->members.data()};
    GameCreatePartyUnits(&records, allArmour);
    auto* unit = g_clientWorld.FindUnit(player->members.data()->unitId);
    unit->SetSecondPrototype(unit->GetPrototype());
    previewModel = unit;
    if (unit) {
        FigureAnimation* animation = unit->FindSkillAnimation(kPreviewAnimation);
        if (animation) {
            std::uint32_t random = g_clientRandom.Next();
            double frames = platform::Milliseconds() * kFramesPerMillisecond;
            int start = static_cast<int>(frames - static_cast<int>(random % 51));
            unit->SetAnimationEntries(-1, static_cast<std::int8_t>(animation->index), start, 0);
        }
        UnitController* controller = unit->Controller();
        controller->ShowWeapon(kNoWeaponSlot);
        controller->RefreshModel();
        controller->RestartAnimation(static_cast<float>(platform::Milliseconds() * kFramesPerMillisecond - 1.0));
        unit->SetFromStats(&player->members.data()->stats);
        unit->Controller()->RefreshWounds();
    }
}

// --- A hero shown -------------------------------------------------------------------------

void MpHeroScreen::ShowHero(int index) {
    auto* player = LocalPlayerData();
    if (!player->members.empty()) {
        std::vector<PartyUnitRecord*> records{player->members.data()};
        g_clientWorld.RemoveUnitsOf(&records);
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
    for (int i = 0; i < std::ssize(hero->weapons); ++i) unit->AddWeapon(hero->weapons[i]);
    unit->SendEquipment();
    unit->Controller()->ShowWeapon(hero->weaponIndex);
    unit->Controller()->RefreshModel();
    turn = 0.0f;
    DrawHeroList();
}

// --- Mode --------------------------------------------------------------------------------------------

// The multiplayer hero screen's mode (MpHeroScreen::SetMode): the hero list (9), the new
// hero (10) and its statistics page (11) laid out. Rebirth's WideScreen adds the curtains' visibility
// (screen_layout::ShowHeroCurtains) after the original's body, which this does too. See ui_screens.h and
// docs/notes/ui.md, section 5.

namespace {

// The sprites: the selection frame, the four quarters of the frame around the figure, and the
// three shown only on the new hero's pages (the last is the create button).
constexpr int kFirstQuarter = 3;
constexpr int kFirstNewHeroSprite = 7;
constexpr int kSpritesEnd = 10;
// The regions every mode keeps (the buttons); the list rows come after them.
constexpr int kFixedRegions = 9;
constexpr int kRowsBottom = 490;

constexpr float kQuarterDepth = 7.0f;
constexpr float kScale = 100.0f;

void ShowSprites(MpHeroScreen* screen, int first, int end, int visible) {
    for (int i = first; i < end; ++i) screen->SceneMesh(i)->visible = visible;
}

// A quarter of the frame around the figure: texels (u0, 0x1C)-(u1, 0xE4).
void SetQuarter(MpHeroScreen* screen, int quarter, float left, float top, float right, float bottom, int u0, int u1) {
    screen->SceneMesh(kFirstQuarter + quarter)->SetRectangle(kQuarterDepth, kScale, left, top, right, bottom, u0, 0x1C, u1, 0xE4);
}

// The selection frame over a list's first row (the whole texture).
void SetSelectionFrame(MpHeroScreen* screen, float left, float top, float right, float bottom) {
    screen->SceneMesh(kSelectionFrame)->SetRectangle(kFrameDepth, kScale, left, top, right, bottom, 0, 0, 0x100, 0x100);
}

// The rows of a list as regions: `height` apart from `top` down to kRowsBottom.
void AddRows(MpHeroScreen* screen, int left, int right, int top, int height) {
    int y = top;
    do {
        const int next = y + height;
        screen->AddRegion()->SetRect(left, y, right, next);
        y = next;
    } while (y < kRowsBottom);
}

} // namespace

void MpHeroScreen::SetMode(int newMode) {
    UiModel* menu = UiWindow::models[0];    // the window's models (ItemScreen::models is the item cache)
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
    ShowSprites(this, 0, static_cast<int>(std::ssize(sceneObjects)), 0);
    for (int i = 0; i < std::ssize(surfaces); ++i) surfaces[i]->Hide(true);
    while (std::ssize(regions) > kFixedRegions) RemoveRegionAt(kFixedRegions);
    for (int i = 0; i < std::ssize(regions); ++i) regions[i]->SetTip(0);

    UiRegion** newRegions = regions.data();
    if (newMode == kModeList) {
        menu->ShowPart("but03", 1);
        menu->ShowPart("button03_multiplayer", 1);
        heroScroll->SetEnabled(true);
        ShowSprites(this, 0, static_cast<int>(std::ssize(sceneObjects)), 1);
        ShowSprites(this, kFirstNewHeroSprite, kSpritesEnd, 0);
        SetQuarter(this, 0, 410.0f, 100.0f, 555.0f, 300.0f, 0x53, 0xE4);
        SetQuarter(this, 1, 555.0f, 100.0f, 700.0f, 300.0f, 0x1C, 0xAD);
        SetQuarter(this, 2, 410.0f, 300.0f, 555.0f, 500.0f, 0x53, 0xE4);
        SetQuarter(this, 3, 555.0f, 300.0f, 700.0f, 500.0f, 0x1C, 0xAD);
        newRegions[0]->SetRect(563, 526, 637, 574);
        newRegions[1]->SetRect(163, 526, 237, 574);
        newRegions[2]->SetRect(455, 470, 485, 500);
        newRegions[3]->SetRect(625, 470, 655, 500);
        newRegions[0]->SetTip(0x9CA5);
        newRegions[1]->SetTip(0x9CA4);
        for (int i = 5; i < kFixedRegions; ++i) newRegions[i]->SetTip(i + 0x9D03);
        surfaces[0]->Show();
        surfaces[1]->Show();
        nameEdit->SetEnabled(false);
        nameEdit->SetFocused(false);
        SetSelectionFrame(this, 110.0f, 250.0f, 370.0f, 274.0f);
        AddRows(this, 110, 370, 250, 24);
        ShowHero(heroIndex);
    } else if (newMode == kModeNewHero) {
        voiceScroll->SetEnabled(true);
        ShowSprites(this, kFirstQuarter, kSpritesEnd, 1);
        SetQuarter(this, 0, 200.0f, 100.0f, 400.0f, 300.0f, 0x1C, 0xE4);
        SetQuarter(this, 1, 400.0f, 100.0f, 600.0f, 300.0f, 0x1C, 0xE4);
        SetQuarter(this, 2, 200.0f, 300.0f, 400.0f, 500.0f, 0x1C, 0xE4);
        SetQuarter(this, 3, 400.0f, 300.0f, 600.0f, 500.0f, 0x1C, 0xE4);
        iconWindow->SetEnabled(true);
        iconWindow->PlaceBars(300);
        statsPanel->SetEnabled(true);
        figureStrips[0]->SetEnabled(true);
        figureStrips[1]->SetEnabled(true);
        newRegions[0]->SetRect(620, 430, 660, 470);
        newRegions[1]->SetRect(740, 430, 780, 470);
        newRegions[2]->SetRect(300, 470, 330, 500);
        newRegions[3]->SetRect(470, 470, 500, 500);
        newRegions[0]->SetTip(0x9D6D);
        newRegions[1]->SetTip(0x9D6C);
        newRegions[4]->SetTip(0x9D6E);
        nameEdit->SetEnabled(true);
        nameEdit->SetFocused(true);
        surfaces[2]->Show();
        SetSelectionFrame(this, 15.0f, 310.0f, 180.0f, 325.0f);
        AddRows(this, 10, 180, 325, 15);
        DrawVoices();
    } else if (newMode == kModeStats) {
        ShowSprites(this, kFirstNewHeroSprite, kSpritesEnd, 1);
        iconWindow->SetEnabled(true);
        iconWindow->PlaceBars(400);
        weaponPanel->SetEnabled(true);
        spellPanel->SetEnabled(true);
        tradePanel->SetEnabled(true);
        statsPanel->SetEnabled(true);
        newRegions[0]->SetRect(620, 430, 660, 470);
        newRegions[1]->SetRect(740, 430, 780, 470);
        newRegions[0]->SetTip(0x9E35);
        newRegions[1]->SetTip(0x9E34);
        newRegions[4]->SetTip(0x9E36);
        surfaces[2]->Show();
        nameEdit->SetEnabled(false);
        nameEdit->SetFocused(false);
        tradePanel->SelectHero(0);
    }
    descPanel->Describe(nullptr);
    statsPanel->ShowStats(newMode);
    mode = newMode;
    // Rebirth's WideScreen: the curtains shown but on the list.
    screen_layout::ShowHeroCurtains(this, newMode);
}
