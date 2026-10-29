// The multiplayer hero screen's mode (CMpHeroScreen::SetMode): the hero list (9), the new
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

void ShowSprites(CMpHeroScreen* screen, int first, int end, int visible) {
    for (int i = first; i < end; ++i) screen->SceneMesh(i)->visible_ = visible;
}

// A quarter of the frame around the figure: texels (u0, 0x1C)-(u1, 0xE4).
void SetQuarter(CMpHeroScreen* screen, int quarter, float left, float top, float right, float bottom, int u0, int u1) {
    screen->SceneMesh(kFirstQuarter + quarter)->SetRectangle(kQuarterDepth, kScale, left, top, right, bottom, u0, 0x1C, u1, 0xE4);
}

// The selection frame over a list's first row (the whole texture).
void SetSelectionFrame(CMpHeroScreen* screen, float left, float top, float right, float bottom) {
    screen->SceneMesh(kSelectionFrame)->SetRectangle(kFrameDepth, kScale, left, top, right, bottom, 0, 0, 0x100, 0x100);
}

// The rows of a list as regions: `height` apart from `top` down to kRowsBottom.
void AddRows(CMpHeroScreen* screen, int left, int right, int top, int height) {
    int y = top;
    do {
        const int next = y + height;
        screen->AddRegion()->SetRect(left, y, right, next);
        y = next;
    } while (y < kRowsBottom);
}

} // namespace

void CMpHeroScreen::SetMode(int mode) {
    CUiModel* menu = CUiWindow::models_[0];   // the window's models (CItemScreen::models_ is the item cache)
    menu->ShowAllParts(0);
    weaponPanel_->SetEnabled(false);
    spellPanel_->SetEnabled(false);
    tradePanel_->SetEnabled(false);
    statsPanel_->SetEnabled(false);
    figureStrips_[0]->SetEnabled(false);
    figureStrips_[1]->SetEnabled(false);
    heroScroll_->SetEnabled(false);
    voiceScroll_->SetEnabled(false);
    iconWindow_->SetEnabled(false);
    ShowSprites(this, 0, static_cast<int>(std::ssize(sceneObjects_)), 0);
    for (int i = 0; i < std::ssize(surfaces_); ++i) surfaces_[i]->Hide(true);
    while (std::ssize(regions_) > kFixedRegions) RemoveRegionAt(kFixedRegions);
    for (int i = 0; i < std::ssize(regions_); ++i) regions_[i]->SetTip(0);

    CUiRegion** regions = regions_.data();
    if (mode == kModeList) {
        menu->ShowPart("but03", 1);
        menu->ShowPart("button03_multiplayer", 1);
        heroScroll_->SetEnabled(true);
        ShowSprites(this, 0, static_cast<int>(std::ssize(sceneObjects_)), 1);
        ShowSprites(this, kFirstNewHeroSprite, kSpritesEnd, 0);
        SetQuarter(this, 0, 410.0f, 100.0f, 555.0f, 300.0f, 0x53, 0xE4);
        SetQuarter(this, 1, 555.0f, 100.0f, 700.0f, 300.0f, 0x1C, 0xAD);
        SetQuarter(this, 2, 410.0f, 300.0f, 555.0f, 500.0f, 0x53, 0xE4);
        SetQuarter(this, 3, 555.0f, 300.0f, 700.0f, 500.0f, 0x1C, 0xAD);
        regions[0]->SetRect(563, 526, 637, 574);
        regions[1]->SetRect(163, 526, 237, 574);
        regions[2]->SetRect(455, 470, 485, 500);
        regions[3]->SetRect(625, 470, 655, 500);
        regions[0]->SetTip(0x9CA5);
        regions[1]->SetTip(0x9CA4);
        for (int i = 5; i < kFixedRegions; ++i) regions[i]->SetTip(i + 0x9D03);
        surfaces_[0]->Show();
        surfaces_[1]->Show();
        nameEdit_->SetEnabled(false);
        nameEdit_->SetFocused(false);
        SetSelectionFrame(this, 110.0f, 250.0f, 370.0f, 274.0f);
        AddRows(this, 110, 370, 250, 24);
        ShowHero(heroIndex_);
    } else if (mode == kModeNewHero) {
        voiceScroll_->SetEnabled(true);
        ShowSprites(this, kFirstQuarter, kSpritesEnd, 1);
        SetQuarter(this, 0, 200.0f, 100.0f, 400.0f, 300.0f, 0x1C, 0xE4);
        SetQuarter(this, 1, 400.0f, 100.0f, 600.0f, 300.0f, 0x1C, 0xE4);
        SetQuarter(this, 2, 200.0f, 300.0f, 400.0f, 500.0f, 0x1C, 0xE4);
        SetQuarter(this, 3, 400.0f, 300.0f, 600.0f, 500.0f, 0x1C, 0xE4);
        iconWindow_->SetEnabled(true);
        iconWindow_->PlaceBars(300);
        statsPanel_->SetEnabled(true);
        figureStrips_[0]->SetEnabled(true);
        figureStrips_[1]->SetEnabled(true);
        regions[0]->SetRect(620, 430, 660, 470);
        regions[1]->SetRect(740, 430, 780, 470);
        regions[2]->SetRect(300, 470, 330, 500);
        regions[3]->SetRect(470, 470, 500, 500);
        regions[0]->SetTip(0x9D6D);
        regions[1]->SetTip(0x9D6C);
        regions[4]->SetTip(0x9D6E);
        nameEdit_->SetEnabled(true);
        nameEdit_->SetFocused(true);
        surfaces_[2]->Show();
        SetSelectionFrame(this, 15.0f, 310.0f, 180.0f, 325.0f);
        AddRows(this, 10, 180, 325, 15);
        DrawVoices();
    } else if (mode == kModeStats) {
        ShowSprites(this, kFirstNewHeroSprite, kSpritesEnd, 1);
        iconWindow_->SetEnabled(true);
        iconWindow_->PlaceBars(400);
        weaponPanel_->SetEnabled(true);
        spellPanel_->SetEnabled(true);
        tradePanel_->SetEnabled(true);
        statsPanel_->SetEnabled(true);
        regions[0]->SetRect(620, 430, 660, 470);
        regions[1]->SetRect(740, 430, 780, 470);
        regions[0]->SetTip(0x9E35);
        regions[1]->SetTip(0x9E34);
        regions[4]->SetTip(0x9E36);
        surfaces_[2]->Show();
        nameEdit_->SetEnabled(false);
        nameEdit_->SetFocused(false);
        tradePanel_->SelectHero(0);
    }
    descPanel_->Describe(nullptr);
    statsPanel_->ShowStats(mode);
    mode_ = mode;
    // Rebirth's WideScreen: the curtains shown but on the list.
    widescreen::ShowHeroCurtains(this, mode);
}
