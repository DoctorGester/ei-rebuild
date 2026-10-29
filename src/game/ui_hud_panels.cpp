// The battle HUD panels' state displays: the unit panel's bars and numbers,
// the movement panel's flag button and the top panel's pages and layout.
// See ui.h ("Battle screen and its HUD panels") and docs/notes/items_spells.md ("Party item
// lists").
#include "ui.h"

#include "item.h"
#include "item_db_text.h"
#include "map_object.h"
#include "party.h"
#include "player_client.h"

#include "lib/gameapi.h"
#include <cstdint>
#include <string>
#include "unit.h"
#include "unit_db.h"
#include "world.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <list>
#include <cstring>

namespace {

constexpr const char* kSoundClick = "buttons\\battle\\click.wav";
constexpr std::uint32_t kTextColour = 0xA7D7E4;
constexpr char kNewLine[] = {'\n', '\0'};
constexpr std::uint32_t kRed = 0x0000FF;                             // COLORREF

// Text into the panel's first surface (Rebirth-patched).
void DrawPanelText(CUiWindow* window, std::string_view text, RECT rect, std::uint32_t colour, UINT format, int shadow) {
    window->surfaces_[0]->DrawTextIn(text, &rect, 0, colour, format, shadow);
}

// A horizontal bar sprite filled to `ratio` (0..1): its right edge moves to ratio - 0.5 and its
// texture's right edge to (ratio * `width` + 64) / 256 + 1/512 texels, between the rows `top`
// and `bottom` of the HUD texture.
void FillBar(CUiMesh* bar, float ratio, float width, float top, float bottom) {
    bar->visible_ = true;
    float u = (ratio * width + 64.0f) * (1.0f / 256.0f) + (1.0f / 512.0f);
    float edge = ratio - 0.5f;
    bar->vertices_[0].x()[2] = edge;
    bar->vertices_[0].x()[3] = edge;
    bar->texCoords_[2].u = u;
    bar->texCoords_[2].v = top;
    bar->texCoords_[3].u = u;
    bar->texCoords_[3].v = bottom;
}

// `value` / `maximum` clamped to 1, or 0 when the bar is not filled (`ratio` is left
// alone then, like the original).
bool BarRatio(int value, int maximum, float* ratio) {
    *ratio = static_cast<float>(value) / static_cast<float>(maximum);
    if (*ratio > 1.0f) {
        *ratio = 1.0f;
        return true;
    }
    return *ratio > 0.0f;
}

} // namespace

// --- Unit panel ----------------------------------------------------------------------------------

void CHudUnitPanel::ShowHealth(CUnit* unit) {
    SceneMesh(11)->visible_ = true;
    float ratio;
    if (unit->packedVitals.maxHitPoints != 0 && unit->packedVitals.hitPoints > 0 && BarRatio(unit->packedVitals.hitPoints, unit->packedVitals.maxHitPoints, &ratio))
        FillBar(SceneMesh(10), ratio, 100.0f, 0.396484375f, 0.376953125f);
    std::string health;
    health = str::Format("%d", unit->packedVitals.hitPoints > 0 ? unit->packedVitals.hitPoints : 0);
    DrawPanelText(this, health, RECT{130, 185, 160, 200}, kTextColour, 0, 1);

    for (int i = 0; i < std::ssize(owner_->party_); ++i) {
        if (owner_->party_[i] != unit) continue;
        SceneMesh(13)->visible_ = true;
        if (unit->packedVitals.maxMana != 0 && unit->packedVitals.mana != 0 && BarRatio(unit->packedVitals.mana, unit->packedVitals.maxMana, &ratio))
            FillBar(SceneMesh(12), ratio, 100.0f, 0.369140625f, 0.349609375f);
        std::string mana;
        mana = str::Format("%d", static_cast<int>(unit->packedVitals.mana));
        DrawPanelText(this, mana, RECT{130, 200, 160, 215}, kTextColour, 0, 1);
        break;
    }
}

void CHudUnitPanel::ShowParts(CUnit* unit) {
    // Label positions of the six parts.
    static constexpr int kLabelX[6] = {50, 50, 95, 5, 95, 5};
    static constexpr int kLabelY[6] = {35, 105, 70, 70, 140, 140};
    static const char* const kPartNames[4] = {"skull", "torso", "leg", "arm"};

    for (int i = 0; i < 6; ++i) {
        const UnitParamsSlot& part = unit->params.slots[i];
        if (part.kind == 0) continue;
        SceneMesh(15 + 2 * i)->visible_ = true;
        CUiMesh* bar = SceneMesh(14 + 2 * i);
        float ratio = 0.0f;
        if (part.kind == 3 && part.maximum != 0 && part.current > 0 &&
            BarRatio(part.current, part.maximum, &ratio))
            FillBar(bar, ratio, 50.0f, 0.505859375f, 0.486328125f);
        else
            bar->visible_ = false;
        // The bar's colour row: green above 2/3, yellow above 1/3, else red.
        bar->textureOffsetV_ = ratio > 2.0f / 3.0f ? 0.0f : ratio > 1.0f / 3.0f ? -0.02734375f : -0.0546875f;

        std::string name;
        name = unit->GetPrototypeModel()->slots[i].levelName;
        for (int k = 0; k < 4; ++k) {
            if (str::Compare(name,
                                kPartNames[k]) == 0) {
                name = partLabels_[k];
                break;
            }
        }
        DrawPanelText(this, name, RECT{kLabelX[i], kLabelY[i], kLabelX[i] + 60, kLabelY[i] + 15},
                      part.kind == 1 ? kRed : kTextColour, 1, 1);
    }
}

// --- Movement panel ------------------------------------------------------------------------------

void CHudMovePanel::ToggleFlag() {
    interface_->PlaySound(kSoundClick);
    // The selected party units' common flag: -1 none selected, -2 mixed.
    int common = -1;
    for (int i = 0; i < std::ssize(owner_->party_); ++i) {
        CUnit* unit = owner_->party_[i];
        if (!(unit->flags_ & MapObjectFlag::Selected)) continue;
        if (common == -1)
            common = unit->fightAtWill;
        else if (common != unit->fightAtWill)
            common = -2;
    }
    int value = !owner_->party_.empty() && common == 1 ? 0 : 1;
    for (int i = 0; i < std::ssize(owner_->party_); ++i) {
        CUnit* unit = owner_->party_[i];
        if (unit->flags_ & MapObjectFlag::Selected) g_clientWorld->LocalView()->SetUnitFightAtWill(unit, value);
    }
    flag_ = value;
    // The button's frame (the original also has 0.48046875 for any other value; `value` is 0 or 1).
    SceneMesh(5)->textureOffsetU_ = value == 0 ? 0.3359375f : 0.0f;
}

// --- Unit panel: the effects on the unit -----------------------------------------------

void CHudUnitPanel::ShowEffects(CUnit* unit) {
    constexpr int kPlaces = 6;
    constexpr int kMoreMark = 9;      // scene object: there are more effects than places
    constexpr int kFirstIcon = 26;    // scene objects 26..31, one per listed effect
    constexpr int kIconSize = 64;
    constexpr int kLineHeight = 15;
    // Where each kept picture sits in the panel's 256 x 256 icon texture (its object 1).
    static constexpr int kPlaceX[kPlaces] = {0, 0, 64, 64, 64, 64};
    static constexpr int kPlaceY[kPlaces] = {128, 192, 0, 64, 128, 192};

    CTextManager* texts = g_clientWorld->texts_;
    CUiSurface* surface = surfaces_[0];
    std::string code, text, number;
    int icons[kPlaces], places[kPlaces];
    bool taken[kPlaces] = {};
    for (int i = 0; i < kPlaces; ++i) icons[i] = places[i] = -1;
    RECT line{40, 35, 155, 50};

    if (std::ssize(unit->effects) > kPlaces) SceneMesh(kMoreMark)->visible_ = true;
    // The unit's effects are the map object's records.
    const MapEffect* effects = unit->effects.data();
    for (int i = 0; i < std::ssize(unit->effects) && i < kPlaces; ++i) {
        code.clear();
        text.clear();
        const itemdb::SpellRecord* spell = itemdb::SpellAt(effects[i].type);
        if (spell) {
            icons[i] = static_cast<int>(spell->icon);
            code = spell->code.c_str();
            // The texts' section "spell <code>": its first line is the spell's name.
            bool found = texts->CTextManager::Open((std::string("spell ") + code)) != 0;
            if (found) texts->ReadLine(&text, 1);
        }
        surface->DrawTextIn(text, &line, 0, kTextColour, 0, 1);
        gameapi::OffsetRect(&line, 0, kLineHeight);
        number = str::Format("%d", effects[i].ticksLeft / 15);
        surface->DrawTextIn(number, &line, 0, kTextColour, 0, 1);
        gameapi::OffsetRect(&line, 0, kLineHeight);
    }

    // The pictures already in the texture keep their places (the last one that matches).
    for (int i = 0; i < kPlaces; ++i) {
        for (int place = 0; place < kPlaces; ++place) {
            if (icons[i] >= 0 && icons[i] == effectIcons_[place]) {
                taken[place] = true;
                places[i] = place;
            }
        }
    }
    // The others go into the free places in order; the list ends when there is none left.
    std::string picture;
    int free = 0;
    for (int i = 0; i < kPlaces; ++i) {
        if (icons[i] < 0) continue;
        bool fresh = false;
        if (places[i] < 0) {
            while (free < kPlaces && taken[free]) ++free;
            if (free == kPlaces) break;
            taken[free] = true;
            places[i] = free;
            fresh = true;
        }
        int x = kPlaceX[places[i]];
        int y = kPlaceY[places[i]];
        if (fresh) {
            picture = str::Format("spell%04d", icons[i]);
            CopyImage(objects_[1], picture, kIconSize, kIconSize, kIconSize, kIconSize, x, y);
            effectIcons_[free] = icons[i];
        }
        CUiMesh* icon = SceneMesh(kFirstIcon + i);
        icon->visible_ = true;
        icon->textureOffsetU_ = static_cast<float>(x) * (1.0f / 256.0f);
        icon->textureOffsetV_ = static_cast<float>(y + kIconSize) * (1.0f / 256.0f);
    }
}

// --- Unit panel: the body part buttons -------------------------------------------------

void CHudUnitPanel::ShowBodyPart() {
    constexpr int kFirstPartButton = 3;
    constexpr int kPartButtons = 4;
    for (int i = 0; i < kPartButtons; ++i) {
        float tint = bodyPart_ == i ? 1.0f : 0.5f;
        CUiMesh* button = SceneMesh(kFirstPartButton + i);
        button->tint_[0] = tint;
        button->tint_[1] = tint;
        button->tint_[2] = tint;
    }
}

// --- Top panel -----------------------------------------------------------------------------------

void CHudTopPanel::ShowPage() {
    for (int i = 0; i < 2; ++i) {
        float tint = page_ == i ? 1.0f : 0.5f;
        CUiMesh* button = SceneMesh(3 + i);
        button->tint_[0] = tint;
        button->tint_[1] = tint;
        button->tint_[2] = tint;
    }
    CUiScrollText* log = owner_->messageLog_;
    if (page_ == 0) {
        log->SetText(pageTexts_[0]);
        log->SetPosition(log->maxPosition_);
        log->moved_ = true;
    } else {
        log->SetText(pageTexts_[1]);
        log->SetPosition(0);
    }
}

void CHudTopPanel::ShowItems() {
    CTextManager* texts = g_clientWorld->texts_;
    // A local list of the world's copies.
    std::list<CItem*> merged{};
    CPlayer* player = g_clientWorld->LocalView()->player_;
    std::string line;
    std::string format;
    for (auto node = player->items_.begin(); node != player->items_.end();) {
        CItem* item = *node;
        ++node;
        CItem* same = nullptr;
        for (auto entry = merged.begin(); entry != merged.end() && !same; ++entry)
            if ((*entry)->IsSameAs(item)) same = *entry;
        if (same) {
            same->count_ += item->count_;
            continue;
        }
        CItem* copy = g_clientWorld->MakeCopyOf(item);
        copy->count_ = item->count_;
        copy->AddRef();
        merged.push_back(copy);
    }

    auto& text = pageTexts_[1];
    text.clear();
    auto* party = LocalPlayerData();
    if (static_cast<int>(party->money_) > 0) {
        texts->CTextManager::GetString("format_money", &format);
        line = str::Format(format, static_cast<int>(party->money_));
        text += line;
        text += kNewLine;
    }
    for (auto node = merged.begin(); node != merged.end();) {
        CItem* item = *node;
        ++node;
        texts->FormatItemName(item, &line);
        text += line;
        text += kNewLine;
        item->Release();
    }
    merged.clear();
    if (page_ == 1) {
        CUiScrollText* log = owner_->messageLog_;
        log->SetText(text);
        log->SetPosition(0);
    }
    merged.clear();
}

void CHudTopPanel::Layout() {
    // Place is Rebirth-patched.
    float offset = static_cast<float>(offset_);
    SceneMesh(0)->Place(400.0f, 57.5f - offset, 20.0f);
    SceneMesh(1)->Place(400.0f, 10.0f - offset, 20.0f);
    SceneMesh(3)->Place(200.0f, 10.0f - offset, 10.0f);
    SceneMesh(4)->Place(230.0f, 10.0f - offset, 10.0f);
    SceneMesh(5)->Place(400.0f, 47.5f - offset, 18.0f);
    RECT rect{190, 25 - offset_, 610, 89 - offset_};
    owner_->messageLog_->SetRect(&rect);
    for (int i = 1; i < std::ssize(regions_); ++i) {
        regions_[i]->offsetX_ = 0;
        regions_[i]->offsetY_ = -offset_;
    }
}
