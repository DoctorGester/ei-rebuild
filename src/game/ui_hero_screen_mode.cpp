// The multiplayer hero screen's mode (MpHeroScreen::SetMode): the hero list (9), the new
// hero (10) and its statistics page (11) laid out. Rebirth's WideScreen adds the curtains' visibility
// (widescreen::ShowHeroCurtains) after the original's body, which this does too. See ui_screens.h and
// docs/notes/ui.md, section 5.
#include "ui.h"
#include "ui_screens.h"

#include "ui_camp.h"
#include "ui_item_info.h"
#include "ui_widescreen.h"

#include <cstdint>

namespace {

// The sprites: the selection frame, the four quarters of the frame around the figure, and the
// three shown only on the new hero's pages (the last is the create button).
constexpr int kSelectionFrame = 2;
constexpr int kFirstQuarter = 3;
constexpr int kFirstNewHeroSprite = 7;
constexpr int kSpritesEnd = 10;
// The regions every mode keeps (the buttons); the list rows come after them.
constexpr int kFixedRegions = 9;
constexpr int kRowsBottom = 490;

constexpr float kQuarterDepth = 7.0f;
constexpr float kFrameDepth = 3.0f;
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
    widescreen::ShowHeroCurtains(this, newMode);
}
