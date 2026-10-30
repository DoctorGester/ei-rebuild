// The HUD buttons that start or drop a pending action on the battle screen: a spell button
// an action button, a quick item slot, and the battle screen's pending action they share. See ui.h
// and docs/notes/ui.md ("Battle screen").
#include "ui.h"

#include "game_singletons.h"
#include "item.h"
#include "spell.h"
#include "text_manager.h"
#include "ui_float.h"
#include "unit.h"
#include "world.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>

namespace {
constexpr const char* kSoundClick = "buttons\\battle\\click.wav";
constexpr const char* kSoundNoMagic = "buttons\\battle\\nomagic.wav";

// The shared tail of the quick item and the spell use: `spell` (an item's spell) used at once by `unit`'s
// side. An attack spell goes to the nearest unit `unit` sees (none: the "nomagic" sound and the pending
// action dropped; false), any other to `unit` itself; then UseOnUnit for a spell aimed at a unit, else
// UseAtPoint at the target's position, with `quick`.
bool UseAtOnce(BattleScreen* screen, Interface3D* ui, Unit* unit, Spell* spell, bool quick) {
    Unit* target = unit;
    if (spell->IsOffensive()) {
        target = g_clientWorld.NearestVisibleUnit(unit);
        if (!target) {
            ui->PlayInterfaceSound(kSoundNoMagic);
            screen->SetPendingAction(kPendingNone, -1, -1);
            return false;
        }
    } else if (!target) {
        return true;
    }
    if (spell->targetFlags & SpellTargetFlag::AtUnits)
        screen->UseOnUnit(target, quick, 0);
    else
        screen->UseAtPoint(target->localPosition, unit, quick);
    return true;
}
} // namespace

// --- The pending action ------------------------------------------------------------------------

void BattleScreen::SetPendingAction(PendingAction mode, int slot, int item) {
    pendingSlot = slot;
    pendingAction = mode;
    pendingItem = item;
    quickItems->ClearHighlights();
    spellColumn->ClearHighlights();
    actionColumn->ClearHighlights();
    switch (mode) {
    case kPendingSpellOnUnit:
    case kPendingSpellAtPoint:
        spellColumn->Highlight(slot, true);
        break;
    case kPendingQuickOnUnit:
    case kPendingQuickAtPoint:
    case kPendingItemOnObject:
    case kPendingKeyOnObject:
        quickItems->Highlight(slot, true);
        break;
    case kPendingFollow:
    case kPendingOrder:
        actionColumn->Highlight(slot, true);
        break;
    default:
        break;
    }
}

// --- A spell button ------------------------------------------------------------------

void HudSpellColumn::Click(int index) {
    if (index < 0 || index >= count) return;
    int previous = selected;
    if (previous == index) {
        ui->PlayInterfaceSound(kSoundClick);
        owner->SetPendingAction(kPendingNone, -1, -1);
        return;
    }
    if (previous >= 0 && previous < count) Highlight(previous, false);
    Highlight(index, true);
    ui->PlayInterfaceSound(kSoundClick);
    Item* item = unit->GetSpell(index);
    if (!item) return;
    Spell* spell = item->spell;
    if (!spell) return;
    owner->SetPendingAction((spell->targetFlags & SpellTargetFlag::AtUnits) ? kPendingSpellOnUnit : kPendingSpellAtPoint, index, -1);
}

// --- An action button ----------------------------------------------------------------

void HudActionColumn::Select(int index) {
    if (index < 0 || index >= count) return;
    int previous = selected;
    if (previous == index) {
        ui->PlayInterfaceSound(kSoundClick);
        owner->SetPendingAction(kPendingNone, -1, -1);
        return;
    }
    if (previous >= 0 && previous < count) Highlight(previous, false);
    Highlight(index, true);
    ui->PlayInterfaceSound(kSoundClick);
    int action = static_cast<int>(actions[index]);
    owner->SetPendingAction(action == 0 ? kPendingOrder : kPendingFollow, index, action);
}

// --- A quick item slot ---------------------------------------------------------------

void HudQuickItems::Pick(int slot) {
    if (!unit) return;
    Item* item = unit->GetQuickItem(slot);
    if (!item) {
        owner->SetPendingAction(kPendingNone, -1, -1);
        return;
    }
    ui->PlayInterfaceSound(kSoundClick);
    if (selected == slot) {
        owner->SetPendingAction(kPendingNone, -1, -1);
        return;
    }
    QuickItemCategory category = item->GetCategory();
    if (category != QuickItemCategory::Wand && category != QuickItemCategory::Potion) return;
    PendingAction mode = kPendingNone;
    if (Spell* spell = item->spell)
        mode = (spell->targetFlags & SpellTargetFlag::AtUnits) ? kPendingQuickOnUnit : kPendingQuickAtPoint;
    owner->SetPendingAction(mode, slot, -1);
}

// --- A quick item or spell used at once ------------------------------------

void HudQuickItems::UseNow(int slot) {
    if (!unit) return;
    Item* item = unit->GetQuickItem(slot);
    if (!item) return;
    owner->SetPendingAction(kPendingQuickOnUnit, slot, -1);
    QuickItemCategory category = item->GetCategory();
    if ((category == QuickItemCategory::Wand || category == QuickItemCategory::Potion) && item->spell)
        if (!UseAtOnce(owner, ui, unit, item->spell, true)) return;
    ui->PlayInterfaceSound(kSoundClick);
    owner->SetPendingAction(kPendingNone, -1, -1);
}

void HudSpellColumn::ClickAlternate(int index) {
    if (!unit || index < 0 || index >= count) return;
    owner->SetPendingAction(kPendingSpellOnUnit, index, -1);
    Item* item = unit->GetSpell(index);
    if (item && item->spell)
        if (!UseAtOnce(owner, ui, unit, item->spell, false)) return;
    ui->PlayInterfaceSound(kSoundClick);
    owner->SetPendingAction(kPendingNone, -1, -1);
}

// --- Ui hud highlight --------------------------------------------------------------------------------

// The HUD buttons' highlights: the quick item panel's slots, the spell and action
// columns' buttons and their clearing, and the
// sprite scale they set. The pending action (ui_hud_pick.cpp, SetPendingAction) and
// the panels' SetUnit drive them. See docs/notes/ui.md ("Battle screen").

namespace {

constexpr std::uint32_t kColourOn = 0xFF404040;
constexpr std::uint32_t kColourOff = 0xFF000000;
constexpr float kButtonScaleOn = 1.15f;
constexpr float kButtonScaleOff = 1.0f;
constexpr float kQuickScaleOn = 0.65f;    // the unit's quick items (bottom row)
constexpr float kQuickScaleOff = 0.55f;
constexpr float kOwnScaleOn = 1.0f;       // the player's items (top row)
constexpr float kOwnScaleOff = 0.85f;
constexpr int kBottomRow = 4;             // slots 0-3
constexpr int kQuickSlots = 8;

// The columns' button highlight (both columns share the code).
void HighlightButton(UiWindow* column, int* selected, int index, bool on) {
    UiMesh* sprite = column->SceneMesh(index);
    sprite->SetScale(on ? kButtonScaleOn : kButtonScaleOff);
    sprite->flatSpecular = on ? kColourOn : kColourOff;
    *selected = index;
}

// The columns' clearing: every button off, selected passing through
// each index, then -1.
void ClearButtons(UiWindow* column, int count, int* selected) {
    for (int i = 0; i < count; ++i) {
        UiMesh* sprite = column->SceneMesh(i);
        sprite->SetScale(kButtonScaleOff);
        sprite->flatSpecular = kColourOff;
        *selected = i;
    }
    *selected = -1;
}

} // namespace

// --- A sprite's scale ----------------------------------------------------------------

void UiMesh::SetScale(float scale) {
    std::uint32_t bits = std::bit_cast<std::uint32_t>(scale);
    ScaleX() = std::bit_cast<float>(bits);
    ScaleY() = std::bit_cast<float>(bits);
    ScaleZ() = std::bit_cast<float>(bits);
}

// --- A quick item slot ---------------------------------------------------------------

void HudQuickItems::Highlight(int slot, bool on) {
    int sprite = regionItem[slot];
    if (sprite < 0 || sprite >= shownCount) return;
    UiMesh* mesh = SceneMesh(sprite);
    if (slot < kBottomRow)
        mesh->SetScale(on ? kQuickScaleOn : kQuickScaleOff);
    else
        mesh->SetScale(on ? kOwnScaleOn : kOwnScaleOff);
    if (on) {
        mesh->flatSpecular = kColourOn;
        selected = slot;
    } else {
        mesh->flatSpecular = kColourOff;
    }
}

void HudQuickItems::ClearHighlights() {
    for (int i = 0; i < kQuickSlots; ++i) Highlight(i, false);
    selected = -1;
}

// --- The columns' buttons -----------------------------------------------------------------

void HudSpellColumn::Highlight(int index, bool on) { HighlightButton(this, &selected, index, on); }

void HudActionColumn::Highlight(int index, bool on) { HighlightButton(this, &selected, index, on); }

void HudSpellColumn::ClearHighlights() { ClearButtons(this, count, &selected); }

void HudActionColumn::ClearHighlights() { ClearButtons(this, count, &selected); }

// --- Ui hud tips -------------------------------------------------------------------------------------

// The battle screen panels' tooltips: the weapon bar's unit, a quick item's tip
// and the action column's unit. See ui.h and docs/notes/ui.md ("Battle screen").

// --- The weapon bar's unit -----------------------------------------------------------

void HudWeaponBar::SetUnit(Unit* newUnit) {
    std::string tip;
    std::string description;
    std::string line;
    unit = newUnit;
    ClearSceneObjects();
    count = 0;
    selected = -1;
    for (int i = 0; i < 4; ++i) regions[i]->SetTip(0);
    if (!unit) return;

    for (unsigned slot = 0; slot < 4; ++slot) {
        ItemWeapon* weapon = unit->GetWeapon(slot);
        if (!weapon) continue;
        {
            std::string image = weapon->FigureName();
            std::string model = weapon->PictureName();
            AddWeapon(model, image);
        }
        tip = g_clientWorld.texts->GetItemTexts(weapon).name;
        tip += "\n";

        // The damage type: the first of the seven weights that is not zero (a NaN counts as zero).
        float types[7];
        weapon->GetDamageTypes(types);
        const float zero = 0.0f;
        unsigned tipType = 0;
        EI_NAN_AUDIT(types[0], types[1], types[2], types[3], types[4], types[5], types[6]);
        while (tipType < 7 && types[tipType] == zero) ++tipType;
        description = "Unknown";
        if (tipType < 7) {
            line = std::format("damage_{}", tipType);
            description = g_clientWorld.texts->GetString(line);
        }
        line = std::format("{} {}-{} {}\n", labels[0], Truncate(weapon->damage),
                    Truncate(weapon->damageSpread + weapon->damage), description);
        tip += line;
        float value = weapon->reach;
        int category = static_cast<int>(weapon->GetPrototype()->category);
        line = std::format("{} {}  {} {}\n", labels[1], category, labels[2], Truncate(value));
        tip += line;

        // A weapon with a spell: the spell's name ("spell <name>").
        if (Spell* spell = weapon->spell) {
            description = "Unknown";
            if (g_clientWorld.texts->TextManager::Open(std::string("spell ") + spell->GetPrototype()->code))
                description = g_clientWorld.texts->ReadLine(true);
            line = std::format("{} {}\n", labels[3], description);
            tip += line;
        }
        regions[slot]->SetTipString(tip, static_cast<int>(slot) + 0x26);
    }
    selected = static_cast<int>(unit->pendingWeapon);
    if (selected >= count) selected = -1;
    Highlight(selected, true);
}

// --- A quick item's tooltip ----------------------------------------------------------

std::string HudQuickItems::ItemTip(unsigned index) {
    std::string text;
    Item* item = unit->GetQuickItem(index);
    if (!item) return text;
    text = g_clientWorld.texts->GetItemTexts(item).name;
    std::string line;
    Spell* data = item->spell;
    if (!data) return text;
    const float one = 1.0f;
    const float pi = 3.1415927f;
    const float perTick = 0.06666667f;   // 1/15

    if (item->GetCategory() == QuickItemCategory::Wand) {
        // A wand: level and charges, mana, duration, range, area, time, the last number.
        text += " \n";
        float charges = item->chargeMax;
        float level = data->manaCost;
        int now = Truncate(item->charge);
        int maximum = Truncate(charges);
        line = std::format("{} {}/{} ({})\n", labels[0], Truncate(level), maximum, now);
        text += line;
        if (data->GetPrototype()->allowedModifiers[3] == 0)
            line = std::format("{} {}  ", labels[1], labels[11]);
        else
            line = std::format("{} {}  ", labels[1], Truncate(data->damage));
        text += line;
        float duration = data->speed;
        EI_NAN_AUDIT(duration);
        if (duration < one)
            line = std::format("{} {} \n", labels[2], labels[7]);
        else
            line = std::format("{} {} \n", labels[2], Truncate(duration));
        text += line;
        line = std::format("{} {}{}  ", labels[3], Truncate(data->range), labels[9]);
        text += line;
        if (data->targetFlags & SpellTargetFlag::AtUnits) {
            line = std::format("{} {} \n", labels[4], labels[8]);
        } else {
            float radius = data->radius;
            float area = radius * radius * pi;
            line = str::Format("%s %.1f%s \n", labels[4], area, labels[9]);
        }
        text += line;
        std::uint32_t ticks = data->duration;
        if (ticks < 2) {
            line = std::format("{} {}  ", labels[5], labels[7]);
        } else {
            float seconds = ticks * perTick;
            line = str::Format("%s %.1f%s  ", labels[5], seconds, labels[10]);
        }
        text += line;
        line = std::format("{} {} \n", labels[6], static_cast<int>(data->targetCount));
        text += line;
    }
    if (item->GetCategory() == QuickItemCategory::Potion) {
        // A potion: mana and time.
        text += " \n";
        if (data->GetPrototype()->allowedModifiers[3] == 0)
            line = std::format("{} {}\n", labels[1], labels[11]);
        else
            line = std::format("{} {}\n", labels[1], Truncate(data->damage));
        text += line;
        std::uint32_t ticks = data->duration;
        if (ticks < 2) {
            line = std::format("{} {}\n", labels[5], labels[7]);
        } else {
            float seconds = ticks * perTick;
            line = str::Format("%s %.1f%s\n", labels[5], seconds, labels[10]);
        }
        text += line;
    }
    return text;
}

// --- The action column's unit --------------------------------------------------------

void HudActionColumn::SetUnit(Unit* newUnit, bool extra) {
    unit = newUnit;
    ClearSceneObjects();
    actions.clear();
    count = 0;
    selected = -1;
    for (int i = 0; i < 6; ++i) regions[i]->SetTip(0);
    if (extra) {
        AddAction("initqi1item", "skill0011", -1);
        regions[count - 1]->SetTip(0x290E, 0x1C);
    }
    Unit* shown = unit;
    if (!shown) return;

    // The science button (the original loops once over a switch of three buttons).
    std::string title;
    std::string tip;
    std::string hint;
    AddAction("initqi1item", "skill0009", 0);
    title = "Science";
    if (g_clientWorld.texts->TextManager::Open(std::string("perk ") + title)) tip = g_clientWorld.texts->ReadLine(true);
    title = std::format("{} {}\n", labels[0], shown->params.attributes[0]);
    if (g_clientWorld.texts->TextManager::Open("tip 10511") != 0) hint = g_clientWorld.texts->ReadToHash();
    tip = tip + "\n" + title + hint;
    regions[count - 1]->SetTipString(tip, 0x1D);
}
