// Draw virtuals of the camp's and the hero screen's panels: the quick strip, the repair grid, the
// constructors' spell maker and item maker. See docs/notes/ui.md, section 5. The trade grid's is in
// ui_trade_panel.cpp.
#include "game/ui/ui.h"
#include "game/ui/ui_mesh.h"
#include "engine/scene/scene_renderer.h"
#include "game/ui/ui_camp.h"
#include "game/ui/ui_item_info.h"
#include "game/items/item.h"
#include "game/items/item_db.h"
#include "game/spells/spell_db.h"
#include "game/players/party.h"
#include "game/units/unit.h"

#include <cstdint>
#include <string>
#include "render/render.h"
#include "game/app/singletons.h"
#include "lib/containers.h"
#include "lib/float_select.h"
#include "lib/maths.h"
#include "lib/str.h"
#include "game/ui/screen_layout.h"

#include <cmath>
#include <format>

namespace {


const char* const kCostFormat = "%s %d (%d)";

int Channel(int value) { return value > 0xFF ? 0xFF : value < 0 ? 0 : value; }

std::uint32_t Colour(int alpha, int red, int blue) {
    return static_cast<std::uint32_t>(((Channel(alpha) << 8 | Channel(red)) << 8 | Channel(red)) << 8 | Channel(blue));
}

// The highlight's pulse: the page's time advanced by the frame time, p = (cos(5 t) + 1) / 2; then the
// colour's three values, truncated: the
// alpha 130 (1 - p) + 240 p, `low` 80 (1 - p) and `high` 160 p + 80 (1 - p).
struct Pulse {
    int alpha = 0;
    int low = 0;
    int high = 0;
};
Pulse Advance(ConstructorPage* page) {
    page->pulse += UiContainer::FrameTime();
    float p = (lib::Cos(page->pulse * 5.0f) + 1.0f) * 0.5f;
    float a = 1.0f - p;
    float low = a * 80.0f;
    return Pulse{static_cast<int>(a * 130.0f + p * 240.0f), static_cast<int>(low), static_cast<int>(p * 160.0f + low)};
}

void DrawSprite(UiMesh* sprite, SceneRenderer* pass, float x, float y) {
    sprite->Place(x, y, 3.0f);
    sprite->Draw(pass);
}

// A slot's item drawn between depth clears: the screen to 0, the slot (layout `slot`) to 1, the
// model, then the screen to 1 again.
void DrawInSlot(ConstructorPage* page, SceneRenderer* pass, const ItemCell& cell, Rect slot) {
    slot = screen_layout::ToScreen(slot);
    ClearUiDepth(0.0f);
    ClearUiDepth(1.0f, &slot, 1);
    page->owner->DrawItem(pass, cell);
    ClearUiDepth(1.0f);
}

// A part's prototype by its record type (weapons, armour, quick items), or null.
const itemdb::ItemRecordHead* PartRecord(const ItemLoot* part) {
    switch (part->recordType) {
    case LootRecordType::Weapon: return itemdb::WeaponAt(part->index);
    case LootRecordType::Armor: return itemdb::ArmorAt(part->index);
    case LootRecordType::Quick: return itemdb::QuickItemAt(part->index);
    default: return nullptr;
    }
}

} // namespace

// --- The quick strip ------------------------------------------------------------------

void HeroQuickStrip::DrawSprites(SceneRenderer* pass) {
    PartyUnitRecord* hero = owner->HeroRecord();
    int count = lib::Count(hero->spells);
    if (selected >= 0 && selected < count) {
        highlightFrame->visible = true;
        highlightFrame->Place(static_cast<float>(selected * 100 + 50), static_cast<float>(y + 50), 3.0f);
    } else {
        highlightFrame->visible = false;
    }
    for (int i = 0; i < std::ssize(hero->spells); ++i) {
        ItemCell cell{hero->spells[i], static_cast<float>(i * 100 + 50),
                      static_cast<float>(y + 50), 12.0f, 100.0f, mode};
        owner->DrawItem(pass, cell);
    }
    DrawBase(pass);
}

// --- The repair grid ------------------------------------------------------------------

void RepairGrid::DrawSprites(SceneRenderer* pass) {
    Rect grid = screen_layout::ToScreen(Rect{200, 100, 600, 500});
    Rect screen{0, 0, g_interface.width, g_interface.height};
    ClearUiDepth(0.0f, &screen, 1);
    ClearUiDepth(1.0f, &grid, 1);

    labels.clear();
    const std::vector<Item*>& list = *items;
    for (int i = 0; i < std::ssize(list); ++i) {
        int x = (i % 4) * 100;
        int y = (i / 4) * 100;
        // 1 << (3 - row) as SHL does it (the count masked to five bits).
        auto scale = static_cast<int>(1u << ((3 - (i >> 2)) & 31));
        Item* item = list[i];
        ItemCell cell{item, x + 250.0f, y + 150.0f,
                      scale * 6.0f, 100.0f, mode};
        owner->DrawItem(pass, cell);
        int pieces = static_cast<int>(item->count);    // the stack's count
        if (pieces > 1) {
            labels.push_back({std::format("{}", pieces), Rect{x + 0x0F, y + 0x0F, x + 0x55, y + 0x23}, {1, kUiTextColour, render::text::Format::Right}});
        }
        labels.push_back({std::format("{}", camp->Price(item, PriceKind::Repair)), Rect{x + 0x0F, y + 0x41, x + 0x55, y + 0x55}, {1, kUiTextColour, render::text::Format::Center}});
    }
    DrawBase(pass);
    ClearUiDepth(1.0f, &screen, 1);
}

// --- The constructors' second pages ----------------------------------------

void SpellMakerPage::DrawSprites(SceneRenderer* pass) {
    const std::vector<Item*>& newSlots = slots->items;
    int needed = 0;
    float manaSum = 0.0f;
    float lastMana = 0.0f;
    SpellSchool school = SpellSchool::Fire;
    bool hasContainer = false;
    bool hasSpell = false;
    int row = 0;
    int column = 0;
    for (Item* item : newSlots) {
        ItemCell cell{item, 0.0f, 0.0f, 6.0f, 100.0f, mode};
        if (IsSpellContainer(item)) {
            cell.x = 250.0f;
            cell.y = 300.0f;
            owner->DrawItem(pass, cell);
            hasContainer = true;
        } else if (IsSpell(item)) {
            const itemdb::SpellRecord* spell = itemdb::SpellAt(item->AsLoot()->index);
            needed += spell->skill;
            manaSum += spell->mana;
            lastMana = spell->mana;
            school = spell->school;
            cell.x = 350.0f;
            cell.y = 300.0f;
            owner->DrawItem(pass, cell);
            hasSpell = true;
        } else if (IsModifier(item)) {
            const itemdb::SpellModifierRecord* modifier = itemdb::SpellModifierAt(item->AsLoot()->index);
            manaSum += modifier->mana;
            needed += modifier->skill;
            cell.x = column + 450.0f;
            cell.y = (row * 100) + 150.0f;
            owner->DrawItem(pass, cell);
            if (++row > 3) {
                row = 0;
                column += 100;
            }
        }
    }
    labels.clear();
    const Rect first{10, 0, 200, 20};
    const Rect second{10, 20, 200, 40};
    if (hasSpell) {
        PartySkill best = camp->BestPartySkill(school);
        labels.push_back({str::Format(kCostFormat, texts[0], best.skill, needed), first, {1, kUiTextColour}});
        // The larger of the two (the last spell's when the sum is smaller or unordered).
        float shown = MaxKeepB(manaSum, lastMana);
        labels.push_back({str::Format(kCostFormat, texts[1], best.mana, static_cast<int>(shown)), second, {1, kUiTextColour}});
    } else {
        labels.push_back({texts[0], first, {1, kUiTextColour}});
        labels.push_back({texts[1], second, {1, kUiTextColour}});
    }
    DrawBase(pass);

    highlight->visible = true;
    Pulse beat = Advance(this);
    if (!hasContainer) {
        if (hasSpell) {
            highlight->flatColour = Colour(beat.alpha, beat.low, beat.high);
            for (int x = 450; x < 650; x += 100)
                for (int y = 150; y < 550; y += 100) DrawSprite(highlight, pass, static_cast<float>(x), static_cast<float>(y));
        } else {
            highlight->flatColour = Colour(beat.alpha, beat.high, beat.low);
            DrawSprite(highlight, pass, 250.0f, 300.0f);
            DrawSprite(highlight, pass, 350.0f, 300.0f);
        }
    }
    highlight->visible = false;
}

void ConstructorPage::DrawSprites(SceneRenderer* pass) {
    DrawBase(pass);
    const std::vector<Item*>& newSlots = slots->items;
    bool hasBlueprint = false;   // or a spell container
    bool hasPart = false;
    const itemdb::ItemRecordHead* part = nullptr;
    int materials = 0;
    int row = 0;
    int column = 0;
    for (Item* item : newSlots) {
        ItemCell cell{item, 0.0f, 0.0f, 6.0f, 100.0f, mode};
        if (IsBuildable(item)) {
            cell.x = 250.0f;
            cell.y = 300.0f;
            DrawInSlot(this, pass, cell, Rect{215, 265, 285, 335});
            hasBlueprint = true;
        } else if (IsPart(item)) {
            cell.x = 350.0f;
            cell.y = 250.0f;
            DrawInSlot(this, pass, cell, Rect{315, 215, 385, 285});
            hasPart = true;
            // Another record type keeps the part found before.
            auto* loot = item->AsLoot();
            if (loot->recordType <= LootRecordType::Quick) part = PartRecord(loot);
        } else if (IsSpellContainer(item)) {
            cell.x = 350.0f;
            cell.y = 350.0f;
            owner->DrawItem(pass, cell);
            hasBlueprint = true;
        } else if (IsMaterial(item)) {
            ++materials;
            cell.x = column + 450.0f;
            cell.y = (row * 100) + 150.0f;
            owner->DrawItem(pass, cell);
            if (++row > 3) {
                row = 0;
                column += 100;
            }
        }
    }
    highlight->visible = true;
    Pulse beat = Advance(this);
    highlight->flatColour = Colour(beat.alpha, beat.high, beat.low);
    if (!hasBlueprint) {
        if (!hasPart) {
            DrawSprite(highlight, pass, 250.0f, 300.0f);
            DrawSprite(highlight, pass, 350.0f, 250.0f);
        } else if (part) {
            // The grid cells the part has room for beyond the materials in it.
            int room = static_cast<int>(part->materialUnits);
            int cellIndex = 0;
            bool none = true;
            for (int x = 450; x < 650; x += 100) {
                for (int y = 150; y < 550; y += 100) {
                    ++cellIndex;
                    if (cellIndex > materials && cellIndex <= room) {
                        DrawSprite(highlight, pass, static_cast<float>(x), static_cast<float>(y));
                        none = false;
                    }
                }
            }
            if (none) {
                // Full: the container slot, with the colour's values the other way round.
                highlight->flatColour = Colour(beat.alpha, beat.low, beat.high);
                DrawSprite(highlight, pass, 350.0f, 350.0f);
            }
        }
    }
    highlight->visible = false;
}
