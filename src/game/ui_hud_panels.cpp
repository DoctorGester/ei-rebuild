// The battle HUD panels' state displays: the unit panel's bars and numbers,
// the movement panel's flag button and the top panel's pages and layout.
// See ui.h ("Battle screen and its HUD panels") and docs/notes/items_spells.md ("Party item
// lists").
#include "ui.h"

#include "item.h"
#include "text_manager.h"
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

#include <cstring>
#include <format>

namespace {

constexpr const char* kSoundClick = "buttons\\battle\\click.wav";
constexpr std::uint32_t kTextColour = 0xA7D7E4;
constexpr char kNewLine[] = {'\n', '\0'};
constexpr std::uint32_t kRed = 0x0000FF;                             // COLORREF

// Text into the panel's first surface (Rebirth-patched).
void DrawPanelText(UiWindow* window, std::string_view text, RECT rect, std::uint32_t colour, UINT format, int shadow) {
    window->surfaces[0]->DrawTextIn(text, &rect, 0, colour, format, shadow);
}

// A horizontal bar sprite filled to `ratio` (0..1): its right edge moves to ratio - 0.5 and its
// texture's right edge to (ratio * `width` + 64) / 256 + 1/512 texels, between the rows `top`
// and `bottom` of the HUD texture.
void FillBar(UiMesh* bar, float ratio, float width, float top, float bottom) {
    bar->visible = true;
    float u = (ratio * width + 64.0f) * (1.0f / 256.0f) + (1.0f / 512.0f);
    float edge = ratio - 0.5f;
    bar->vertices[0].x()[2] = edge;
    bar->vertices[0].x()[3] = edge;
    bar->texCoords[2].u = u;
    bar->texCoords[2].v = top;
    bar->texCoords[3].u = u;
    bar->texCoords[3].v = bottom;
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

void HudUnitPanel::ShowHealth(CUnit* unit) {
    SceneMesh(11)->visible = true;
    float ratio;
    if (unit->packedVitals.maxHitPoints != 0 && unit->packedVitals.hitPoints > 0 && BarRatio(unit->packedVitals.hitPoints, unit->packedVitals.maxHitPoints, &ratio))
        FillBar(SceneMesh(10), ratio, 100.0f, 0.396484375f, 0.376953125f);
    std::string health = std::format("{}", unit->packedVitals.hitPoints > 0 ? unit->packedVitals.hitPoints : 0);
    DrawPanelText(this, health, RECT{130, 185, 160, 200}, kTextColour, 0, 1);

    for (int i = 0; i < std::ssize(owner->party); ++i) {
        if (owner->party[i] != unit) continue;
        SceneMesh(13)->visible = true;
        if (unit->packedVitals.maxMana != 0 && unit->packedVitals.mana != 0 && BarRatio(unit->packedVitals.mana, unit->packedVitals.maxMana, &ratio))
            FillBar(SceneMesh(12), ratio, 100.0f, 0.369140625f, 0.349609375f);
        std::string mana = std::format("{}", static_cast<int>(unit->packedVitals.mana));
        DrawPanelText(this, mana, RECT{130, 200, 160, 215}, kTextColour, 0, 1);
        break;
    }
}

void HudUnitPanel::ShowParts(CUnit* unit) {
    // Label positions of the six parts.
    static constexpr int kLabelX[6] = {50, 50, 95, 5, 95, 5};
    static constexpr int kLabelY[6] = {35, 105, 70, 70, 140, 140};
    static const char* const kPartNames[4] = {"skull", "torso", "leg", "arm"};

    for (int i = 0; i < 6; ++i) {
        const UnitParamsSlot& part = unit->params.slots[i];
        if (part.kind == 0) continue;
        SceneMesh(15 + 2 * i)->visible = true;
        UiMesh* bar = SceneMesh(14 + 2 * i);
        float ratio = 0.0f;
        if (part.kind == 3 && part.maximum != 0 && part.current > 0 &&
            BarRatio(part.current, part.maximum, &ratio))
            FillBar(bar, ratio, 50.0f, 0.505859375f, 0.486328125f);
        else
            bar->visible = false;
        // The bar's colour row: green above 2/3, yellow above 1/3, else red.
        bar->textureOffsetV = ratio > 2.0f / 3.0f ? 0.0f : ratio > 1.0f / 3.0f ? -0.02734375f : -0.0546875f;

        std::string name = unit->GetPrototypeModel()->slots[i].levelName;
        for (int k = 0; k < 4; ++k) {
            if (str::Compare(name,
                                kPartNames[k]) == 0) {
                name = partLabels[k];
                break;
            }
        }
        DrawPanelText(this, name, RECT{kLabelX[i], kLabelY[i], kLabelX[i] + 60, kLabelY[i] + 15},
                      part.kind == 1 ? kRed : kTextColour, 1, 1);
    }
}

// --- Movement panel ------------------------------------------------------------------------------

void HudMovePanel::ToggleFlag() {
    ui->PlayInterfaceSound(kSoundClick);
    // The selected party units' common flag: -1 none selected, -2 mixed.
    int common = -1;
    for (int i = 0; i < std::ssize(owner->party); ++i) {
        CUnit* unit = owner->party[i];
        if (!(unit->flags & MapObjectFlag::Selected)) continue;
        if (common == -1)
            common = unit->fightAtWill;
        else if (common != unit->fightAtWill)
            common = -2;
    }
    int value = !owner->party.empty() && common == 1 ? 0 : 1;
    for (int i = 0; i < std::ssize(owner->party); ++i) {
        CUnit* unit = owner->party[i];
        if (unit->flags & MapObjectFlag::Selected) g_clientWorld.LocalView()->SetUnitFightAtWill(unit, value);
    }
    flag = value;
    // The button's frame (the original also has 0.48046875 for any other value; `value` is 0 or 1).
    SceneMesh(5)->textureOffsetU = value == 0 ? 0.3359375f : 0.0f;
}

// --- Unit panel: the effects on the unit -----------------------------------------------

void HudUnitPanel::ShowEffects(CUnit* unit) {
    constexpr int kPlaces = 6;
    constexpr int kMoreMark = 9;      // scene object: there are more effects than places
    constexpr int kFirstIcon = 26;    // scene objects 26..31, one per listed effect
    constexpr int kIconSize = 64;
    constexpr int kLineHeight = 15;
    // Where each kept picture sits in the panel's 256 x 256 icon texture (its object 1).
    static constexpr int kPlaceX[kPlaces] = {0, 0, 64, 64, 64, 64};
    static constexpr int kPlaceY[kPlaces] = {128, 192, 0, 64, 128, 192};

    TextManager* clientWorldTexts = g_clientWorld.texts;
    UiSurface* surface = surfaces[0];
    std::string code, text, number;
    int icons[kPlaces], places[kPlaces];
    bool taken[kPlaces] = {};
    for (int i = 0; i < kPlaces; ++i) icons[i] = places[i] = -1;
    RECT line{40, 35, 155, 50};

    if (std::ssize(unit->effects) > kPlaces) SceneMesh(kMoreMark)->visible = true;
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
            bool found = clientWorldTexts->TextManager::Open((std::string("spell ") + code)) != 0;
            if (found) text = clientWorldTexts->ReadLine(true);
        }
        surface->DrawTextIn(text, &line, 0, kTextColour, 0, 1);
        gameapi::OffsetRect(&line, 0, kLineHeight);
        number = std::format("{}", effects[i].ticksLeft / 15);
        surface->DrawTextIn(number, &line, 0, kTextColour, 0, 1);
        gameapi::OffsetRect(&line, 0, kLineHeight);
    }

    // The pictures already in the texture keep their places (the last one that matches).
    for (int i = 0; i < kPlaces; ++i) {
        for (int place = 0; place < kPlaces; ++place) {
            if (icons[i] >= 0 && icons[i] == effectIcons[place]) {
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
            picture = std::format("spell{:04}", icons[i]);
            CopyImage(objects[1], picture, kIconSize, kIconSize, kIconSize, kIconSize, x, y);
            effectIcons[free] = icons[i];
        }
        UiMesh* icon = SceneMesh(kFirstIcon + i);
        icon->visible = true;
        icon->textureOffsetU = x * (1.0f / 256.0f);
        icon->textureOffsetV = (y + kIconSize) * (1.0f / 256.0f);
    }
}

// --- Unit panel: the body part buttons -------------------------------------------------

void HudUnitPanel::ShowBodyPart() {
    constexpr int kFirstPartButton = 3;
    constexpr int kPartButtons = 4;
    for (int i = 0; i < kPartButtons; ++i) {
        float tint = bodyPart == i ? 1.0f : 0.5f;
        UiMesh* button = SceneMesh(kFirstPartButton + i);
        button->tint[0] = tint;
        button->tint[1] = tint;
        button->tint[2] = tint;
    }
}

// --- Top panel -----------------------------------------------------------------------------------

void HudTopPanel::ShowPage() {
    for (int i = 0; i < 2; ++i) {
        float tint = page == i ? 1.0f : 0.5f;
        UiMesh* button = SceneMesh(3 + i);
        button->tint[0] = tint;
        button->tint[1] = tint;
        button->tint[2] = tint;
    }
    UiScrollText* log = owner->messageLog;
    if (page == 0) {
        log->SetText(pageTexts[0]);
        log->SetPosition(log->maxPosition);
        log->moved = true;
    } else {
        log->SetText(pageTexts[1]);
        log->SetPosition(0);
    }
}

void HudTopPanel::ShowItems() {
    TextManager* texts = g_clientWorld.texts;
    // A local list of the world's copies.
    std::vector<Item*> merged{};
    CPlayer* player = g_clientWorld.LocalView()->player;
    std::string line;
    std::string format;
    for (auto node = player->items.begin(); node != player->items.end();) {
        Item* item = *node;
        ++node;
        Item* same = nullptr;
        for (auto entry = merged.begin(); entry != merged.end() && !same; ++entry)
            if ((*entry)->IsSameAs(item)) same = *entry;
        if (same) {
            same->count += item->count;
            continue;
        }
        Item* copy = g_clientWorld.MakeCopyOf(item);
        copy->count = item->count;
        copy->AddRef();
        merged.push_back(copy);
    }

    auto& text = pageTexts[1];
    text.clear();
    auto* party = LocalPlayerData();
    if (static_cast<int>(party->money) > 0) {
        format = texts->TextManager::GetString("format_money");
        line = str::Format(format, static_cast<int>(party->money));
        text += line;
        text += kNewLine;
    }
    for (auto node = merged.begin(); node != merged.end();) {
        Item* item = *node;
        ++node;
        line = texts->FormatItemName(item);
        text += line;
        text += kNewLine;
        item->Release();
    }
    merged.clear();
    if (page == 1) {
        UiScrollText* log = owner->messageLog;
        log->SetText(text);
        log->SetPosition(0);
    }
    merged.clear();
}

void HudTopPanel::Layout() {
    // Place is Rebirth-patched.
    float slideOffset = static_cast<float>(offset);
    SceneMesh(0)->Place(400.0f, 57.5f - slideOffset, 20.0f);
    SceneMesh(1)->Place(400.0f, 10.0f - slideOffset, 20.0f);
    SceneMesh(3)->Place(200.0f, 10.0f - slideOffset, 10.0f);
    SceneMesh(4)->Place(230.0f, 10.0f - slideOffset, 10.0f);
    SceneMesh(5)->Place(400.0f, 47.5f - slideOffset, 18.0f);
    RECT area{190, 25 - offset, 610, 89 - offset};
    owner->messageLog->SetRect(&area);
    for (int i = 1; i < std::ssize(regions); ++i) {
        regions[i]->offsetX = 0;
        regions[i]->offsetY = -offset;
    }
}
