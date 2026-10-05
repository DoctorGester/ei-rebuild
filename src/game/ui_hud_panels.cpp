// The battle HUD panels' state displays: the unit panel's bars and numbers (006477E0, 00647B70),
// the movement panel's flag button (00649810) and the top panel's pages and layout (0064A320,
// 00649E20). See ui.h ("Battle screen and its HUD panels") and docs/notes/items_spells.md
// ("Area ticket A-H").
#include "ui.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_ui.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"

#include <cstdint>

namespace {

constexpr const char* kSoundClick = "buttons\\battle\\click.wav";   // 00794460
constexpr std::uint32_t kTextColour = 0xA7D7E4;
constexpr std::uint32_t kRed = 0x0000FF;                             // COLORREF

// The panel's scene object `index` (the HUD's are interface sprites).
CUiMesh* Sprite(CUiWindow* window, int index) { return reinterpret_cast<CUiMesh*>(window->sceneObjects_.data_[index]); }

// Text into the panel's first surface (005FDE20, Rebirth-patched: through its address).
void DrawPanelText(CUiWindow* window, const char* text, RECT rect, std::uint32_t colour, int a, int b) {
    SurfaceDrawText(window->surfaces_.data_[0], nullptr, text, &rect, 0, colour, a, b);
}

// A horizontal bar sprite filled to `ratio` (0..1): its right edge moves to ratio - 0.5 and its
// texture's right edge to (ratio * `width` + 64) / 256 + 1/512 texels, between the rows `top`
// and `bottom` of the HUD texture.
void FillBar(CUiMesh* bar, float ratio, float width, float top, float bottom) {
    bar->visible_ = 1;
    float u = (ratio * width + 64.0f) * (1.0f / 256.0f) + (1.0f / 512.0f);
    float edge = ratio - 0.5f;
    bar->quads_[0].x[2] = edge;
    bar->quads_[0].x[3] = edge;
    bar->uvs_[4] = u;
    bar->uvs_[5] = top;
    bar->uvs_[6] = u;
    bar->uvs_[7] = bottom;
}

// `value` / `maximum` clamped to 1, or 0 when the bar is not filled (the original also leaves
// it alone then).
bool BarRatio(int value, int maximum, float* ratio) {
    *ratio = static_cast<float>(value) / static_cast<float>(maximum);
    if (*ratio > 1.0f) {
        *ratio = 1.0f;
        return true;
    }
    return *ratio > 0.0f;
}

// The battle screen's message log (its tenth child window).
CUiScrollText* MessageLog(CBattleScreen* screen) { return static_cast<CUiScrollText*>(screen->children_.data_[9]); }

// The local view's order setting a unit's +0x438 flag (0066AF30, thiscall; sends it to the server).
struct LocalViewOrders {
    BOOL SetFlag438(CUnit* unit, int value);
};
__declspec(naked) BOOL LocalViewOrders::SetFlag438(CUnit*, int) EI_ORIGINAL(0x0066AF30)

LocalViewOrders* Orders() { return reinterpret_cast<LocalViewOrders*>(WorldClient()->LocalView()); }

} // namespace

void CUiScrollbar::CallSetRect(const RECT* rect) {
    using Fn = void(__thiscall*)(CUiScrollbar*, const RECT*);
    reinterpret_cast<Fn>(vtable_[26])(this, rect);
}

// --- Unit panel ----------------------------------------------------------------------------------

void CHudUnitPanel::ShowHealth(CUnit* unit) {
    Sprite(this, 11)->visible_ = 1;
    float ratio;
    if (unit->maxHitPoints != 0 && unit->hitPoints > 0 && BarRatio(unit->hitPoints, unit->maxHitPoints, &ratio))
        FillBar(Sprite(this, 10), ratio, 100.0f, 0.396484375f, 0.376953125f);
    mfc::CString health;
    health.Format("%d", unit->hitPoints > 0 ? unit->hitPoints : 0);
    DrawPanelText(this, health, RECT{130, 185, 160, 200}, kTextColour, 0, 1);

    for (int i = 0; i < owner_->partyCount_; ++i) {
        if (owner_->party_[i] != unit) continue;
        Sprite(this, 13)->visible_ = 1;
        if (unit->maxMana != 0 && unit->mana != 0 && BarRatio(unit->mana, unit->maxMana, &ratio))
            FillBar(Sprite(this, 12), ratio, 100.0f, 0.369140625f, 0.349609375f);
        mfc::CString mana;
        mana.Format("%d", static_cast<int>(unit->mana));
        DrawPanelText(this, mana, RECT{130, 200, 160, 215}, kTextColour, 0, 1);
        break;
    }
}
EI_REPLACE_METHOD(0x006477E0, CHudUnitPanel::ShowHealth);

void CHudUnitPanel::ShowParts(CUnit* unit) {
    // Label positions of the six parts (00742434 / 0074244C).
    static constexpr int kLabelX[6] = {50, 50, 95, 5, 95, 5};
    static constexpr int kLabelY[6] = {35, 105, 70, 70, 140, 140};
    static const char* const kPartNames[4] = {"skull", "torso", "leg", "arm"};

    for (int i = 0; i < 6; ++i) {
        const UnitBodyPart& part = unit->bodyParts[i];
        if (part.state == 0) continue;
        Sprite(this, 15 + 2 * i)->visible_ = 1;
        CUiMesh* bar = Sprite(this, 14 + 2 * i);
        float ratio = 0.0f;
        if (part.state == 3 && part.maxDurability != 0 && part.durability > 0 &&
            BarRatio(part.durability, part.maxDurability, &ratio))
            FillBar(bar, ratio, 50.0f, 0.505859375f, 0.486328125f);
        else
            bar->visible_ = 0;
        // The bar's colour row: green above 2/3, yellow above 1/3, else red.
        bar->vOffset_ = ratio > 2.0f / 3.0f ? 0.0f : ratio > 1.0f / 3.0f ? -0.02734375f : -0.0546875f;

        mfc::CString name;
        name = reinterpret_cast<UnitModelRecord*>(unit->GetPrototypeModel())->slots[i].levelName;
        for (int k = 0; k < 4; ++k) {
            if (gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(name)),
                                reinterpret_cast<const unsigned char*>(kPartNames[k])) == 0) {
                name = partLabels_[k];
                break;
            }
        }
        DrawPanelText(this, name, RECT{kLabelX[i], kLabelY[i], kLabelX[i] + 60, kLabelY[i] + 15},
                      part.state == 1 ? kRed : kTextColour, 1, 1);
    }
}
EI_REPLACE_METHOD(0x00647B70, CHudUnitPanel::ShowParts);

// --- Movement panel ------------------------------------------------------------------------------

void CHudMovePanel::ToggleFlag() {
    interface_->PlaySound(kSoundClick, 0);
    // The selected party units' common flag: -1 none selected, -2 mixed.
    int common = -1;
    for (int i = 0; i < owner_->partyCount_; ++i) {
        CUnit* unit = owner_->party_[i];
        if (!(unit->flags_ & 0x10)) continue;
        if (common == -1)
            common = unit->unknown_438;
        else if (common != unit->unknown_438)
            common = -2;
    }
    int value = owner_->partyCount_ > 0 && common == 1 ? 0 : 1;
    for (int i = 0; i < owner_->partyCount_; ++i) {
        CUnit* unit = owner_->party_[i];
        if (unit->flags_ & 0x10) Orders()->SetFlag438(unit, value);
    }
    flag_ = value;
    // The button's frame (the original also has 0.48046875 for any other value; `value` is 0 or 1).
    Sprite(this, 5)->uOffset_ = value == 0 ? 0.3359375f : 0.0f;
}
EI_REPLACE_METHOD(0x00649810, CHudMovePanel::ToggleFlag);

// --- Top panel -----------------------------------------------------------------------------------

void CHudTopPanel::ShowPage() {
    for (int i = 0; i < 2; ++i) {
        float tint = page_ == i ? 1.0f : 0.5f;
        CUiMesh* button = Sprite(this, 3 + i);
        button->tint_[0] = tint;
        button->tint_[1] = tint;
        button->tint_[2] = tint;
    }
    CUiScrollText* log = MessageLog(owner_);
    if (page_ == 0) {
        LogSetTextOriginal(log, nullptr, pageTexts_[0]);
        log->CallSetPosition(log->maxPosition_);
        log->moved_ = true;
    } else {
        LogSetTextOriginal(log, nullptr, pageTexts_[1]);
        log->CallSetPosition(0);
    }
}
EI_REPLACE_METHOD(0x0064A320, CHudTopPanel::ShowPage);

void CHudTopPanel::Layout() {
    // Place (005E6FA0) is Rebirth-patched: through its address.
    float offset = static_cast<float>(offset_);
    SpritePlaceOriginal(sceneObjects_.data_[0], nullptr, 400.0f, 57.5f - offset, 20.0f);
    SpritePlaceOriginal(sceneObjects_.data_[1], nullptr, 400.0f, 10.0f - offset, 20.0f);
    SpritePlaceOriginal(sceneObjects_.data_[3], nullptr, 200.0f, 10.0f - offset, 10.0f);
    SpritePlaceOriginal(sceneObjects_.data_[4], nullptr, 230.0f, 10.0f - offset, 10.0f);
    SpritePlaceOriginal(sceneObjects_.data_[5], nullptr, 400.0f, 47.5f - offset, 18.0f);
    RECT rect{190, 25 - offset_, 610, 89 - offset_};
    MessageLog(owner_)->CallSetRect(&rect);
    for (int i = 1; i < regions_.size_; ++i) {
        regions_.data_[i]->offsetX_ = 0;
        regions_.data_[i]->offsetY_ = -offset_;
    }
}
EI_REPLACE_METHOD(0x00649E20, CHudTopPanel::Layout);
