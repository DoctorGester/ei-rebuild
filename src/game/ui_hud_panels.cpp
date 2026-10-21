// The battle HUD panels' state displays: the unit panel's bars and numbers (006477E0, 00647B70),
// the movement panel's flag button (00649810) and the top panel's pages and layout (0064A320,
// 00649E20). See ui.h ("Battle screen and its HUD panels") and docs/notes/items_spells.md ("Party item
// lists").
#include "ui.h"

#include "item.h"
#include "item_db_text.h"
#include "map_object.h"
#include "party.h"
#include "player_client.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_engine.h"
#include "originals_ui.h"
#include "originals_world.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstdint>

namespace {

constexpr const char* kSoundClick = "buttons\\battle\\click.wav";   // 00794460
constexpr std::uint32_t kTextColour = 0xA7D7E4;
constexpr char kNewLine[] = {'\n', '\0'};                           // 00784340
constexpr std::uint32_t kRed = 0x0000FF;                             // COLORREF

// The panel's scene object `index` (the HUD's are interface sprites).
CUiMesh* Sprite(CUiWindow* window, int index) { return window->SceneMesh(index); }

// Text into the panel's first surface (005FDE20, Rebirth-patched: through its address).
void DrawPanelText(CUiWindow* window, const char* text, RECT rect, std::uint32_t colour, int a, int b) {
    SurfaceDrawText(window->surfaces_.data[0], nullptr, text, &rect, 0, colour, a, b);
}

// A horizontal bar sprite filled to `ratio` (0..1): its right edge moves to ratio - 0.5 and its
// texture's right edge to (ratio * `width` + 64) / 256 + 1/512 texels, between the rows `top`
// and `bottom` of the HUD texture.
void FillBar(CUiMesh* bar, float ratio, float width, float top, float bottom) {
    bar->visible_ = 1;
    float u = (ratio * width + 64.0f) * (1.0f / 256.0f) + (1.0f / 512.0f);
    float edge = ratio - 0.5f;
    bar->Quads()[0].x[2] = edge;
    bar->Quads()[0].x[3] = edge;
    bar->Texels()[2].u = u;
    bar->Texels()[2].v = top;
    bar->Texels()[3].u = u;
    bar->Texels()[3].v = bottom;
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
CUiScrollText* MessageLog(CBattleScreen* screen) { return static_cast<CUiScrollText*>(screen->children_.data[9]); }

// The local view, whose SetUnitFlag438 (0066AF30) sets a unit's +0x438 flag and sends it to the server.
CPlayerClient* Orders() { return reinterpret_cast<CPlayerClient*>(TheClientWorld()->LocalView()); }

} // namespace

void CUiScrollbar::CallSetRect(const RECT* rect) {
    using Fn = void(__thiscall*)(CUiScrollbar*, const RECT*);
    ei::VirtualSlot<Fn>(vtable_, 26)(this, rect);
}

// --- Unit panel ----------------------------------------------------------------------------------

void CHudUnitPanel::ShowHealth(CUnit* unit) {
    Sprite(this, 11)->visible_ = 1;
    float ratio;
    if (unit->packedVitals.maxHitPoints != 0 && unit->packedVitals.hitPoints > 0 && BarRatio(unit->packedVitals.hitPoints, unit->packedVitals.maxHitPoints, &ratio))
        FillBar(Sprite(this, 10), ratio, 100.0f, 0.396484375f, 0.376953125f);
    mfc::CString health;
    health.Format("%d", unit->packedVitals.hitPoints > 0 ? unit->packedVitals.hitPoints : 0);
    DrawPanelText(this, health, RECT{130, 185, 160, 200}, kTextColour, 0, 1);

    for (int i = 0; i < owner_->party_.size; ++i) {
        if (owner_->party_[i] != unit) continue;
        Sprite(this, 13)->visible_ = 1;
        if (unit->packedVitals.maxMana != 0 && unit->packedVitals.mana != 0 && BarRatio(unit->packedVitals.mana, unit->packedVitals.maxMana, &ratio))
            FillBar(Sprite(this, 12), ratio, 100.0f, 0.369140625f, 0.349609375f);
        mfc::CString mana;
        mana.Format("%d", static_cast<int>(unit->packedVitals.mana));
        DrawPanelText(this, mana, RECT{130, 200, 160, 215}, kTextColour, 0, 1);
        break;
    }
}
EI_REWRITTEN(0x006477E0, CHudUnitPanel::ShowHealth);

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
        bar->textureOffsetV_ = ratio > 2.0f / 3.0f ? 0.0f : ratio > 1.0f / 3.0f ? -0.02734375f : -0.0546875f;

        mfc::CString name;
        name = reinterpret_cast<UnitModelRecord*>(unit->GetPrototypeModel())->slots[i].levelName;
        for (int k = 0; k < 4; ++k) {
            if (gamecrt::mbscmp(static_cast<const char*>(name),
                                kPartNames[k]) == 0) {
                name = partLabels_[k];
                break;
            }
        }
        DrawPanelText(this, name, RECT{kLabelX[i], kLabelY[i], kLabelX[i] + 60, kLabelY[i] + 15},
                      part.state == 1 ? kRed : kTextColour, 1, 1);
    }
}
EI_REWRITTEN(0x00647B70, CHudUnitPanel::ShowParts);

// --- Movement panel ------------------------------------------------------------------------------

void CHudMovePanel::ToggleFlag() {
    interface_->PlaySound(kSoundClick, 0);
    // The selected party units' common flag: -1 none selected, -2 mixed.
    int common = -1;
    for (int i = 0; i < owner_->party_.size; ++i) {
        CUnit* unit = owner_->party_[i];
        if (!(unit->flags_ & 0x10)) continue;
        if (common == -1)
            common = unit->fightAtWill;
        else if (common != unit->fightAtWill)
            common = -2;
    }
    int value = owner_->party_.size > 0 && common == 1 ? 0 : 1;
    for (int i = 0; i < owner_->party_.size; ++i) {
        CUnit* unit = owner_->party_[i];
        if (unit->flags_ & 0x10) Orders()->SetUnitFlag438(unit, value);
    }
    flag_ = value;
    // The button's frame (the original also has 0.48046875 for any other value; `value` is 0 or 1).
    Sprite(this, 5)->textureOffsetU_ = value == 0 ? 0.3359375f : 0.0f;
}
EI_REWRITTEN(0x00649810, CHudMovePanel::ToggleFlag);

// --- Unit panel: the effects on the unit (00646B90) -----------------------------------------------

void CHudUnitPanel::ShowEffects(CUnit* unit) {
    constexpr int kPlaces = 6;
    constexpr int kMoreMark = 9;      // scene object: there are more effects than places
    constexpr int kFirstIcon = 26;    // scene objects 26..31, one per listed effect
    constexpr int kIconSize = 64;
    constexpr int kLineHeight = 15;
    // Where each kept picture sits in the panel's 256 x 256 icon texture (its object 1).
    static constexpr int kPlaceX[kPlaces] = {0, 0, 64, 64, 64, 64};
    static constexpr int kPlaceY[kPlaces] = {128, 192, 0, 64, 128, 192};

    CTextManager* texts = TheTextManager();
    CUiSurface* surface = surfaces_.data[0];
    mfc::CString code, text, number;
    int icons[kPlaces], places[kPlaces];
    bool taken[kPlaces] = {};
    for (int i = 0; i < kPlaces; ++i) icons[i] = places[i] = -1;
    RECT line{40, 35, 155, 50};

    if (unit->effectCount > kPlaces) Sprite(this, kMoreMark)->visible_ = 1;
    // The unit's effects are the map object's records (map_object.h).
    const MapEffect* effects = reinterpret_cast<const MapEffect*>(unit->effects);
    for (int i = 0; i < unit->effectCount && i < kPlaces; ++i) {
        code.Empty();
        text.Empty();
        const auto* spell =
            reinterpret_cast<const itemdb::SpellRecord*>(itemdb::Spells().RecordAt(effects[i].type, itemdb::kSpellSize));
        if (spell) {
            icons[i] = static_cast<int>(spell->icon);
            code = static_cast<const char*>(spell->code);
            // The texts' section "spell <code>": its first line is the spell's name.
            bool found = TextsOpenSection(texts, nullptr, mfc::CString("spell ") + code) != 0;
            if (found) texts->ReadLine(&text, 1);
        }
        SurfaceDrawText(surface, nullptr, text, &line, 0, kTextColour, 0, 1);
        gameapi::OffsetRect(&line, 0, kLineHeight);
        number.Format("%d", effects[i].ticksLeft / 15);
        SurfaceDrawText(surface, nullptr, number, &line, 0, kTextColour, 0, 1);
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
    mfc::CString picture;
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
            picture.Format("spell%04d", icons[i]);
            CopyImage(objects_.data[1], picture, kIconSize, kIconSize, kIconSize, kIconSize, x, y, 0, 0);
            effectIcons_[free] = icons[i];
        }
        CUiMesh* icon = Sprite(this, kFirstIcon + i);
        icon->visible_ = 1;
        icon->textureOffsetU_ = static_cast<float>(x) * (1.0f / 256.0f);
        icon->textureOffsetV_ = static_cast<float>(y + kIconSize) * (1.0f / 256.0f);
    }
}
EI_REWRITTEN(0x00646B90, CHudUnitPanel::ShowEffects);

// --- Unit panel: the body part buttons (006481D0) -------------------------------------------------

void CHudUnitPanel::ShowBodyPart() {
    constexpr int kFirstPartButton = 3;
    constexpr int kPartButtons = 4;
    for (int i = 0; i < kPartButtons; ++i) {
        float tint = bodyPart_ == i ? 1.0f : 0.5f;
        CUiMesh* button = Sprite(this, kFirstPartButton + i);
        button->tint_[0] = tint;
        button->tint_[1] = tint;
        button->tint_[2] = tint;
    }
}
EI_REWRITTEN(0x006481D0, CHudUnitPanel::ShowBodyPart);

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
        log->SetText(pageTexts_[0]);
        log->CallSetPosition(log->maxPosition_);
        log->moved_ = true;
    } else {
        log->SetText(pageTexts_[1]);
        log->CallSetPosition(0);
    }
}
EI_REWRITTEN(0x0064A320, CHudTopPanel::ShowPage);

void CHudTopPanel::ShowItems() {
    CTextManager* texts = TheTextManager();
    // A local CPtrList (MFC's vtable, blocks of 10) of the world's copies.
    mfc::CTypedPtrList<CItem> merged{ei::GamePointer<void>(0x0073BE80), nullptr, nullptr, 0, nullptr, nullptr, 10};
    CPlayer* player = reinterpret_cast<LocalViewData*>(TheClientWorld()->LocalView())->player;
    mfc::CString line;
    mfc::CString format;
    for (auto* node = player->items_.head; node;) {
        CItem* item = node->data;
        node = node->next;
        CItem* same = nullptr;
        for (auto* entry = merged.head; entry && !same; entry = entry->next)
            if (entry->data->IsSameAs(item)) same = entry->data;
        if (same) {
            same->count_ += item->count_;
            continue;
        }
        CItem* copy = WorldCopyItemOriginal(TheClientWorld(), nullptr, item);
        copy->count_ = item->count_;
        ++copy->refCount_;
        merged.AddTail(copy);
    }

    mfc::CString& text = pageTexts_[1];
    text.Empty();
    auto* party = LocalPlayerData();
    if (static_cast<int>(party->money_) > 0) {
        TextsGetString(texts, nullptr, "format_money", &format);
        line.Format(format, static_cast<int>(party->money_));
        text += line;
        text += kNewLine;
    }
    for (auto* node = merged.head; node;) {
        CItem* item = node->data;
        node = node->next;
        texts->FormatItemName(item, &line);
        text += line;
        text += kNewLine;
        item->Release();
    }
    merged.RemoveAll();
    if (page_ == 1) {
        CUiScrollText* log = MessageLog(owner_);
        log->SetText(text);
        log->CallSetPosition(0);
    }
    merged.RemoveAll();
}
EI_REPLACE_METHOD(0x0064A7A0, CHudTopPanel::ShowItems);

void CHudTopPanel::Layout() {
    // Place (005E6FA0) is Rebirth-patched: through its address.
    float offset = static_cast<float>(offset_);
    SpritePlaceOriginal(sceneObjects_.data[0], nullptr, 400.0f, 57.5f - offset, 20.0f);
    SpritePlaceOriginal(sceneObjects_.data[1], nullptr, 400.0f, 10.0f - offset, 20.0f);
    SpritePlaceOriginal(sceneObjects_.data[3], nullptr, 200.0f, 10.0f - offset, 10.0f);
    SpritePlaceOriginal(sceneObjects_.data[4], nullptr, 230.0f, 10.0f - offset, 10.0f);
    SpritePlaceOriginal(sceneObjects_.data[5], nullptr, 400.0f, 47.5f - offset, 18.0f);
    RECT rect{190, 25 - offset_, 610, 89 - offset_};
    MessageLog(owner_)->CallSetRect(&rect);
    for (int i = 1; i < regions_.size; ++i) {
        regions_.data[i]->offsetX_ = 0;
        regions_.data[i]->offsetY_ = -offset_;
    }
}
EI_REWRITTEN(0x00649E20, CHudTopPanel::Layout);
