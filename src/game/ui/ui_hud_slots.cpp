// The battle HUD's slot panels (ui_hud.h): the weapon bar, the quick items, the spell column and the action column.
// Their mouse handlers and per-frame updates, the functions that add a slot's sprite, the weapon bar's highlight and
// the unit each shows, and picking or using what a slot holds. Rebirth patches all of them for its WideScreen layout,
// reproduced for every window (screen_layout.h): the weapon bar and the action column at the screen's left edge, the
// quick items and the spell column at its right (each window's origin, UiWindow::originX). See docs/notes/ui.md, sections 6 ("Battle screen") and 7.
#include "game/ui/ui_hud.h"
#include "game/app/singletons.h"
#include "game/items/item.h"
#include "game/items/item_texts.h"
#include "game/players/player_client.h"
#include "game/players/player.h"
#include "game/ui/screen_layout.h"
#include "game/spells/spell.h"
#include "engine/data/text_manager.h"
#include "game/ui/ui.h"
#include "game/ui/ui_battle.h"
#include "game/ui/ui_float.h"
#include "game/ui/ui_hud_pieces.h"
#include "game/ui/ui_mesh.h"
#include "game/units/unit.h"
#include "lib/containers.h"
#include "lib/str.h"
#include "lib/vecmath.h"
#include "render/render_state.h"

#include <concepts>
#include <cstdint>
#include <format>
#include <iterator>
#include <list>
#include <string>
#include <vector>

namespace {
constexpr const char* kSoundWeapon = "buttons\\battle\\weapon.wav";

// The quick items' and the columns' draw: the window's, with specular on.
void DrawWithSpecular(UiWindow* window, SceneRenderer* pass) {
    render::g_renderState.specular = true;
    window->DrawBase(pass);
    render::g_renderState.specular = false;
}

// A texel as a texture offset: 1 / 256.
constexpr float kTexelStep = 0.00390625f;
// The layout y the columns' bottom button sits at (490).
constexpr float kColumnBottom = 490.0f;

constexpr std::uint32_t kWhite = 0xFFFFFFFF;
constexpr std::uint32_t kWeaponColourOn = 0xFF282828;    // the weapon bar's highlighted slot
constexpr std::uint32_t kColourOff = 0xFF000000;
constexpr int kStep = 40;                          // the weapon slots' width, the columns' button height
constexpr int kIconSize = 0x40;                    // the items', spells' and actions' pictures
constexpr int kWeaponWidth = 0x2A, kWeaponHeight = 0x80;
constexpr int kMaxQuickItems = 8;

// The tooltips' labels: the texts "infoitem_<id>" in order.
template <int N>
std::vector<std::string> Labels(const int (&ids)[N]) {
    TextManager* texts = g_clientWorld.texts.get();
    std::vector<std::string> labels;
    std::string text;
    for (int id : ids) {
        text = std::format("infoitem_{}", id);
        text = texts->GetString(text);
        labels.push_back(text);
    }
    return labels;
}

// Where the n-th 64 x 64 picture goes in a panel's texture: four down a column, the columns from
// `firstColumn`.
struct IconPlace {
    int x = 0;
    int y = 0;
};
IconPlace IconAt(int n, int firstColumn) { return {firstColumn + (n / 4) * kIconSize, (n % 4) * kIconSize}; }

void SetOffsets(UiMesh* sprite, int x, int bottom) {
    sprite->textureOffsetU = x * kTexelStep;
    sprite->textureOffsetV = bottom * kTexelStep;
}

void MakePlain(UiMesh* sprite) {
    sprite->flatColour = kWhite;
    sprite->flags |= FigureMeshFlag::Flat;
}

constexpr const char* kSoundNoMagic = "buttons\\battle\\nomagic.wav";

// The shared tail of the quick item and the spell use: `spell` (an item's spell) used at once by `unit`'s
// side. An attack spell goes to the nearest unit `unit` sees (none: the "nomagic" sound and the pending
// action dropped; false), any other to `unit` itself; then UseOnUnit for a spell aimed at a unit, else
// UseAtPoint at the target's position, with `quick`.
bool UseAtOnce(BattleScreen* screen, Unit* unit, Spell* spell, bool quick) {
    Unit* target = unit;
    if (spell->Use().offensive) {
        target = g_clientWorld.NearestVisibleUnit(unit);
        if (!target) {
            g_interface.PlayInterfaceSound(kSoundNoMagic);
            screen->SetPendingAction(PendingAction::None, -1, -1);
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

constexpr std::uint32_t kButtonColourOn = 0xFF404040;
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
    UiMesh* sprite = column->sceneObjects[index];
    sprite->SetScale(on ? kButtonScaleOn : kButtonScaleOff);
    sprite->flatSpecular = on ? kButtonColourOn : kColourOff;
    *selected = index;
}

// The columns' clearing: every button off, selected passing through
// each index, then -1.
void ClearButtons(UiWindow* column, int count, int* selected) {
    for (int i = 0; i < count; ++i) {
        UiMesh* sprite = column->sceneObjects[i];
        sprite->SetScale(kButtonScaleOff);
        sprite->flatSpecular = kColourOff;
        *selected = i;
    }
    *selected = -1;
}
} // namespace

// --- Weapon bar ------------------------------------------------------------------------------------------------------

HudWeaponBar::HudWeaponBar(BattleScreen* newOwner) : owner(newOwner) {
    // WideScreen: the rectangle and the first region from the window's origin; the regions still run up to
    // x 240, so the bar has more of them (no tips beyond the fourth).
    originX = -screen_layout::EdgeShift();
    rect = {80 + originX, 520, 240 + originX, 600};
    for (int x = 80 + originX; x < 240; x += kStep) AddRegion()->SetRect(x, 520, x + kStep, 600);
    ClearSceneObjects();
    for (int i = 0; i < 4; ++i) regions[i]->SetTip(0);
    const int ids[] = {15, 20, 10, 5};
    labels = Labels(ids);
}

void HudWeaponBar::OnEvent(UiEvent& event) {
    DispatchMouse(event, [&] { PressRegion(event, count); }, [&] { LeftUp(event); }, [&] { HitRegion(event, count); });
}

void HudWeaponBar::LeftUp(UiEvent& event) {
    if (const int slot = RegionIndex(regions, PressedRegion(event, count)); slot >= 0) Choose(slot);
}

void HudWeaponBar::Choose(int slot) {
    const int previous = selected;
    if (slot >= count || slot == previous) return;
    if (previous >= 0 && previous < count) Highlight(previous, false);
    Highlight(slot, true);
    selected = slot;
    g_interface.PlayInterfaceSound(kSoundWeapon);
    // No check for a missing local view here (like the original).
    if (unit) g_clientWorld.LocalView()->SetWeapon(unit, slot);
}

void HudWeaponBar::Update() {
    Unit* currentUnit = unit;
    if (!currentUnit || selected == currentUnit->pendingWeapon) return;
    Highlight(selected, false);
    selected = unit->pendingWeapon;
    Highlight(selected, true);
}

void HudWeaponBar::DrawSprites(SceneRenderer* pass) {
    render::RenderState& state = render::g_renderState;
    const std::uint8_t alphaRef = state.alphaReference;
    const bool alphaTest = state.alphaTest;
    state.alphaReference = 1;
    state.alphaTest = true;
    state.specular = true;
    DrawBase(pass);
    state.alphaReference = alphaRef;
    state.alphaTest = alphaTest;
    state.specular = false;
}

void HudWeaponBar::Highlight(int index, bool on) {
    if (index < 0 || index >= count) return;
    UiMesh* sprite = sceneObjects[index];
    const int left = (index * 5 + 10) * 8 + originX;
    const int right = (index * 5 + 15) * 8 + originX;
    if (on) {
        float x = index * 40.0f + 100.0f;
        sprite->Place(x + originX, 540.0f, 12.0f);
        sprite->SetScale(0.65f);
        sprite->flatSpecular = kWeaponColourOn;
        regions[index]->SetRect(left, 495, right, 600);
        return;
    }
    // The more weapons, the larger each: 1.2 per slot in use, 24 deep at scale 1.
    float scale = 1.0f;
    for (int i = count; i > 0; --i) scale *= 1.2f;
    float depth = scale * 24.0f;
    float x = index * 40.0f + 100.0f;
    sprite->Place(x + originX, 560.0f, depth);
    sprite->SetScale(scale);
    sprite->flatSpecular = kColourOff;
    regions[index]->SetRect(left, 520, right, 600);
}

void HudWeaponBar::AddWeapon(std::string_view model, std::string_view image) {
    // Slots 0 and 1 share the texture's first column, 2 and 3 the second; odd slots the lower
    // half. (Left unset for a count over 3, which SetUnit never reaches.)
    const int x = count >= 2 ? kWeaponWidth : 0;
    const int y = (count & 1) ? kWeaponHeight : 0;
    CopyImage(objects[0], image, 0x80, 0x80, kWeaponWidth, kWeaponHeight, x, y);
    UiMesh* sprite = AddSprite(model);
    if (sprite) {
        // Turned about z, then y, then z again: pi * -0.05, pi * -1/3 and pi * 0.75.
        const Quaternion first = Quaternion::AxisAngle(Vector3{0.0f, 0.0f, 1.0f}, kPi * (-0.05f));
        const Quaternion second = Quaternion::AxisAngle(Vector3{0.0f, 1.0f, 0.0f}, kPi * (-0.33333334f));
        const Quaternion third = Quaternion::AxisAngle(Vector3{0.0f, 0.0f, 1.0f}, kPi * 0.75f);
        sprite->localRotation = (first * second) * third;
    } else {
        sprite = AddBlankSprite();
        // 80 and 120 from the window's origin.
        float offset = static_cast<float>(count * kStep);
        sprite->SetRectangle(12.0f, 40.0f, offset + (80.0f + originX), 520.0f,
                             offset + (120.0f + originX), 600.0f, 0, 0, kWeaponWidth, kWeaponHeight);
    }
    SetOffsets(sprite, x, y + kWeaponHeight);
    MakePlain(sprite);
    int index = count;
    count = index + 1;
    Highlight(index, false);
}

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
        AddWeapon(weapon->PictureName(), weapon->FigureName());
        tip = GetItemTexts(g_clientWorld.texts.get(), weapon).name;
        tip += "\n";

        // The damage type: the first of the seven weights that is not zero (a NaN counts as zero).
        const std::array<float, kDamageTypeCount> types = weapon->DamageTypes();
        const float zero = 0.0f;
        unsigned tipType = 0;
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

// --- Quick items -----------------------------------------------------------------------------------------------------

HudQuickItems::HudQuickItems(BattleScreen* newOwner) : owner(newOwner) {
    originX = screen_layout::EdgeShift();
    rect = {520 + originX, 500, 720 + originX, 600};
    // The bottom row (the unit's items), then the top row (the player's), each from the right.
    for (int x = 670; x > 470; x -= 50) AddRegion()->SetRect(x + originX, 550, x + 50 + originX, 600);
    for (int x = 670; x > 470; x -= 50) AddRegion()->SetRect(x + originX, 500, x + 50 + originX, 550);
    ClearSceneObjects();
    for (int i = 0; i < 8; ++i) {
        regions[i]->SetTip(0);
        regionItem[i] = -1;
    }
    entries.clear();
    const int ids[] = {7, 8, 9, 10, 11, 12, 13, 21, 22, 26, 27, 28};
    labels = Labels(ids);
}

void HudQuickItems::DrawSprites(SceneRenderer* pass) { DrawWithSpecular(this, pass); }

void HudQuickItems::OnEvent(UiEvent& event) {
    DispatchMouse(event, [&] { LeftDown(event); }, [&] { LeftUp(event); }, [&] { MouseMove(event); });
}

void HudQuickItems::LeftDown(UiEvent& event) {
    if (RegionItem(RegionIndex(regions, RegionUnder(event.at))) < 0) return;
    event.handled = true;
    CaptureMouse();
}

void HudQuickItems::MouseMove(UiEvent& event) {
    if (RegionItem(RegionIndex(regions, RegionUnder(event.at))) >= 0) event.handled = true;
}

void HudQuickItems::Update() {
    if (!unit) return;
    for (unsigned i = 0; i < 4; ++i) {
        if (!unit->GetQuickItem(i)) continue;
        if (!regions[i]->HasActiveTip()) continue;
        regions[i]->SetTipText(ItemTip(i));
    }
}

void HudQuickItems::LeftUp(UiEvent& event) {
    if (!ReleaseMouse()) return;
    const int slot = RegionIndex(regions, RegionUnder(event.at));
    const int entry = RegionItem(slot);
    if (entry < 0) return;
    event.handled = true;
    if (slot < 4 && entries[entry].own) {
        if (g_interface.doubleClick)
            UseNow(slot);
        else
            Pick(slot);
        return;
    }
    g_interface.PlayInterfaceSound(kSoundClick);
    if (selected != slot) {
        // One of the player's items (top row): used on an object, with the item's key in the screen's
        // itemUseKey and its id in pendingItem.
        owner->itemUseKey = entries[entry].item->GetUse();
        owner->SetPendingAction(PendingAction::KeyOnObject, slot, entries[entry].id);
        return;
    }
    // The highlighted one again: back to plain selection.
    owner->SetPendingAction(PendingAction::None, -1, -1);
}

UiMesh* HudQuickItems::AddItem(std::string_view model, std::string_view image) {
    if (shownCount >= kMaxQuickItems) return nullptr;
    const IconPlace at = IconAt(shownCount, 0x80);
    CopyImage(objects[0], image, kIconSize, kIconSize, kIconSize, kIconSize, at.x, at.y);
    UiMesh* sprite = AddSprite(model);
    if (sprite) {
        sprite->localRotation = Quaternion::AxisAngle(Vector3{1.0f, 0.0f, 0.0f}, kPi);
    } else {
        sprite = AddBlankSprite();
        sprite->SetRectangle(20.0f, 20.0f, 680.0f + originX, 560.0f, 720.0f + originX, 600.0f, 0, 0, kIconSize,
                             kIconSize);
    }
    SetOffsets(sprite, at.x, at.y + kIconSize);
    MakePlain(sprite);
    ++shownCount;
    return sprite;
}

void HudQuickItems::Pick(int slot) {
    if (!unit) return;
    Item* item = unit->GetQuickItem(slot);
    if (!item) {
        owner->SetPendingAction(PendingAction::None, -1, -1);
        return;
    }
    g_interface.PlayInterfaceSound(kSoundClick);
    if (selected == slot) {
        owner->SetPendingAction(PendingAction::None, -1, -1);
        return;
    }
    QuickItemCategory category = item->GetCategory();
    if (category != QuickItemCategory::Wand && category != QuickItemCategory::Potion) return;
    PendingAction mode = PendingAction::None;
    if (Spell* spell = item->spell)
        mode = (spell->targetFlags & SpellTargetFlag::AtUnits) ? PendingAction::QuickOnUnit : PendingAction::QuickAtPoint;
    owner->SetPendingAction(mode, slot, -1);
}

void HudQuickItems::UseNow(int slot) {
    if (!unit) return;
    Item* item = unit->GetQuickItem(slot);
    if (!item) return;
    owner->SetPendingAction(PendingAction::QuickOnUnit, slot, -1);
    QuickItemCategory category = item->GetCategory();
    if ((category == QuickItemCategory::Wand || category == QuickItemCategory::Potion) && item->spell)
        if (!UseAtOnce(owner, unit, item->spell, true)) return;
    g_interface.PlayInterfaceSound(kSoundClick);
    owner->SetPendingAction(PendingAction::None, -1, -1);
}

void HudQuickItems::Highlight(int slot, bool on) {
    const int entry = regionItem[slot];
    if (entry < 0 || entry >= shownCount) return;
    UiMesh* mesh = entries[entry].sprite;
    if (slot < kBottomRow)
        mesh->SetScale(on ? kQuickScaleOn : kQuickScaleOff);
    else
        mesh->SetScale(on ? kOwnScaleOn : kOwnScaleOff);
    if (on) {
        mesh->flatSpecular = kButtonColourOn;
        selected = slot;
    } else {
        mesh->flatSpecular = kColourOff;
    }
}

void HudQuickItems::ClearHighlights() {
    for (int i = 0; i < kQuickSlots; ++i) Highlight(i, false);
    selected = -1;
}

std::string HudQuickItems::ItemTip(unsigned index) {
    std::string text;
    Item* item = unit->GetQuickItem(index);
    if (!item) return text;
    text = GetItemTexts(g_clientWorld.texts.get(), item).name;
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

void HudQuickItems::SetUnit(Unit* newUnit) {
    std::string tip;
    unit = newUnit;
    ClearSceneObjects();
    shownCount = 0;
    selected = -1;
    for (int i = 0; i < 8; ++i) {
        regions[i]->SetTip(0);
        regionItem[i] = -1;
    }
    entries.clear();    // (zeroed new entries)
    if (!unit) return;

    // The entry of the sprite just added.
    auto finish = [&](Item* item, bool own, UiMesh* sprite) {
        entries.resize(shownCount);
        Entry& entry = entries[shownCount - 1];
        entry.own = own;
        entry.id = item->id;
        entry.item = item;
        entry.sprite = sprite;
    };
    // The sprite just added: placed in its row (x from the panels' right column, 695; Rebirth moves it), its
    // slot pointing at its entry, scaled, opaque.
    auto place = [&](int slot, UiMesh* sprite) {
        const float right = screen_layout::g_panelRight;
        if (slot < 4)
            sprite->Place(right - (slot * 50), 575.0f, 20.0f);
        else
            sprite->Place(right - (slot * 50 - 200), 525.0f, 20.0f);
        regionItem[slot] = shownCount - 1;
        sprite->SetScale(slot < 4 ? 0.55f : 0.85f);
        sprite->flatSpecular = 0xFF000000;
    };

    // The unit's four quick items in the bottom row.
    for (unsigned slot = 0; slot < 4; ++slot) {
        Item* item = unit->GetQuickItem(slot);
        if (!item) continue;
        UiMesh* sprite = AddItem(item->PictureName(), item->FigureName());
        finish(item, true, sprite);
        regions[slot]->SetTipString(ItemTip(slot), static_cast<int>(slot) + 0x2A);
        place(static_cast<int>(slot), sprite);
    }

    // The local player's own unit also shows up to four of the player's ItemKind::Quest items above, from
    // the last of the player's items backwards (node: +4 previous, +8 item).
    Player* player = g_clientWorld.LocalView()->player;
    if (!player->IsLeader(unit)) return;
    player = g_clientWorld.LocalView()->player;
    auto node = player->items.rbegin();
    if (node == player->items.rend()) return;
    int slot = 4;
    while (node != player->items.rend()) {
        if (slot - 4 >= 4) break;
        Item* item = *node;
        ++node;
        if (!item || item->Kind() != ItemKind::Quest)
            continue;
        UiMesh* sprite = AddItem(item->PictureName(), item->FigureName());
        finish(item, false, sprite);
        // The same string serves as the second and third texts.
        tip = GetItemTexts(g_clientWorld.texts.get(), item).name;
        regions[slot]->SetTipString(tip);
        place(slot, sprite);
        ++slot;
    }
}

// --- Spell column ----------------------------------------------------------------------------------------------------

HudSpellColumn::HudSpellColumn(BattleScreen* newOwner) : owner(newOwner) {
    originX = screen_layout::EdgeShift();
    rect = {760 + originX, 190, 800 + originX, 510};
    ClearSceneObjects();
    ClearRegions();
    const int ids[] = {7, 18, 8, 9, 10, 11, 12, 13, 21, 22, 26, 27, 28};
    labels = Labels(ids);
}

void HudSpellColumn::DrawSprites(SceneRenderer* pass) { DrawWithSpecular(this, pass); }

void HudSpellColumn::OnEvent(UiEvent& event) {
    DispatchMouse(event, [&] { PressRegion(event, count); }, [&] { LeftUp(event); }, [&] { HitRegion(event, count); });
}

void HudSpellColumn::LeftUp(UiEvent& event) {
    const int index = RegionIndex(regions, PressedRegion(event, count));
    if (index < 0) return;
    if (g_interface.doubleClick)
        ClickAlternate(index);
    else
        Click(index);
}

void HudSpellColumn::AddSpell(std::string_view model, std::string_view image) {
    // What is there moves up a step; the new button takes the bottom place.
    for (int i = 0; i < std::ssize(regions); ++i) {
        int top = (i - count) * kStep;
        regions[i]->SetRect(760 + originX, top + 470, 800 + originX, top + 510);
    }
    AddRegion()->SetRect(760 + originX, 470, 800 + originX, 510);
    for (int i = 0; i < std::ssize(sceneObjects); ++i) {
        float y = kColumnBottom - ((count - i) * kStep);
        sceneObjects[i]->Place(780.0f + originX, y, 40.0f);
    }
    const IconPlace at = IconAt(count, 0x80);
    CopyImage(objects[0], image, kIconSize, kIconSize, kIconSize, kIconSize, at.x, at.y);
    UiMesh* sprite = AddSprite(model);
    SetOffsets(sprite, at.x, at.y + kIconSize);
    sprite->localRotation = Quaternion::AxisAngle(Vector3{0.0f, 0.0f, 1.0f}, kPi);
    sprite->Place(780.0f + originX, 490.0f, 40.0f);
    MakePlain(sprite);
    int index = count;
    count = index + 1;
    sprite->SetScale(1.0f);
    sprite->flatSpecular = kColourOff;
    selected = index;
}

void HudSpellColumn::Click(int index) {
    if (index < 0 || index >= count) return;
    int previous = selected;
    if (previous == index) {
        g_interface.PlayInterfaceSound(kSoundClick);
        owner->SetPendingAction(PendingAction::None, -1, -1);
        return;
    }
    if (previous >= 0 && previous < count) Highlight(previous, false);
    Highlight(index, true);
    g_interface.PlayInterfaceSound(kSoundClick);
    Item* item = unit->GetSpell(index);
    if (!item) return;
    Spell* spell = item->spell;
    if (!spell) return;
    owner->SetPendingAction((spell->targetFlags & SpellTargetFlag::AtUnits) ? PendingAction::SpellOnUnit : PendingAction::SpellAtPoint, index, -1);
}

void HudSpellColumn::ClickAlternate(int index) {
    if (!unit || index < 0 || index >= count) return;
    owner->SetPendingAction(PendingAction::SpellOnUnit, index, -1);
    Item* item = unit->GetSpell(index);
    if (item && item->spell)
        if (!UseAtOnce(owner, unit, item->spell, false)) return;
    g_interface.PlayInterfaceSound(kSoundClick);
    owner->SetPendingAction(PendingAction::None, -1, -1);
}

void HudSpellColumn::Highlight(int index, bool on) { HighlightButton(this, &selected, index, on); }

void HudSpellColumn::ClearHighlights() { ClearButtons(this, count, &selected); }

void HudSpellColumn::SetUnit(Unit* newUnit) {
    std::string tip;
    std::string description;
    unit = newUnit;
    ClearSceneObjects();
    ClearRegions();
    count = 0;
    selected = -1;
    if (!unit) return;

    for (unsigned i = 0; i < 8; ++i) {
        Item* spell = unit->GetSpell(i);
        if (!spell) continue;
        AddSpell(spell->PictureName(), spell->FigureName());
        tip = GetItemTexts(g_clientWorld.texts.get(), spell).name;

        // The spell's numbers below its name: one line per label of the panel's table.
        Spell* data = spell->spell;    // the spell item's spell
        if (data) {
            const std::vector<std::string>& currentLabels = labels;
            std::string line;
            tip += " \n";   // " \n"
            line = std::format("{} {}  ", currentLabels[0], Truncate(data->manaCost));
            tip += line;
            line = "Unknown";
            description = std::format("school_{}", data->GetPrototype()->castType);
            line = g_clientWorld.texts->TextManager::GetString(description);
            line = currentLabels[1] + " " + line + " \n";
            tip += line;
            if (data->GetPrototype()->allowedModifiers[3] == 0)
                line = std::format("{} {}  ", currentLabels[2], currentLabels[12]);
            else
                line = std::format("{} {}  ", currentLabels[2], Truncate(data->damage));
            tip += line;
            const float one = 1.0f;
            if (data->speed < one)
                line = std::format("{} {} \n", currentLabels[3], currentLabels[8]);
            else
                line = std::format("{} {} \n", currentLabels[3], Truncate(data->speed));
            tip += line;
            line = std::format("{} {}{}  ", currentLabels[4], Truncate(data->range), currentLabels[10]);
            tip += line;
            if (lib::Has(data->targetFlags, SpellTargetFlag::AtUnits)) {
                line = std::format("{} {} \n", currentLabels[5], currentLabels[9]);
            } else {
                // The area: radius squared times pi (a float).
                float area = data->radius * data->radius * 3.1415927f;
                line = str::Format("%s %.1f%s \n", currentLabels[5], area, currentLabels[10]);   // "%s %.1f%s \n"
            }
            tip += line;
            std::uint32_t ticks = data->duration;
            if (ticks < 2) {
                line = std::format("{} {}  ", currentLabels[6], currentLabels[8]);
            } else {
                // Ticks to seconds (1/15).
                float seconds = ticks * 0.06666667f;
                line = str::Format("%s %.1f%s  ", currentLabels[6], seconds, currentLabels[11]);
            }
            tip += line;
            line = std::format("{} {} \n", currentLabels[7], static_cast<int>(data->targetCount));
            tip += line;
        }
        // The slot's button is the one AddSpell just added (the original indexed the regions by slot, reading past
        // them after an empty slot); the tip's key is still by slot.
        regions.back()->SetTipString(tip, static_cast<int>(i) + 0x1E);
    }
}

// --- Action column ---------------------------------------------------------------------------------------------------

HudActionColumn::HudActionColumn(BattleScreen* newOwner) : owner(newOwner) {
    originX = -screen_layout::EdgeShift();
    rect = {originX, 250, 40 + originX, 510};
    for (int y = 470; y > 230; y -= kStep) AddRegion()->SetRect(originX, y, 40 + originX, y + kStep);
    ClearSceneObjects();
    actions.clear();
    for (int i = 0; i < 6; ++i) regions[i]->SetTip(0);
    std::string text = g_clientWorld.texts->GetString("infoitem_16");
    labels.push_back(text);
}

void HudActionColumn::DrawSprites(SceneRenderer* pass) { DrawWithSpecular(this, pass); }

void HudActionColumn::OnEvent(UiEvent& event) {
    DispatchMouse(event, [&] { PressRegion(event, count); }, [&] { LeftUp(event); }, [&] { HitRegion(event, count); });
}

void HudActionColumn::LeftUp(UiEvent& event) {
    if (const int index = RegionIndex(regions, PressedRegion(event, count)); index >= 0) Select(index);
}

void HudActionColumn::AddAction(std::string_view model, std::string_view image, int action) {
    const IconPlace at = IconAt(count, 0);
    CopyImage(objects[0], image, kIconSize, kIconSize, kIconSize, kIconSize, at.x, at.y);
    UiMesh* sprite = AddSprite(model);
    SetOffsets(sprite, at.x, at.y + kIconSize);
    sprite->localRotation = Quaternion::AxisAngle(Vector3{0.0f, 0.0f, 1.0f}, kPi);
    float y = kColumnBottom - (count * kStep);
    sprite->Place(20.0f + originX, y, 40.0f);
    MakePlain(sprite);
    int place = lib::Count(actions);
    actions.resize(place + 1);
    actions[place] = static_cast<std::uint32_t>(action);
    int index = count;
    count = index + 1;
    sprite->SetScale(1.0f);
    sprite->flatSpecular = kColourOff;
    selected = index;
}

void HudActionColumn::Select(int index) {
    if (index < 0 || index >= count) return;
    int previous = selected;
    if (previous == index) {
        g_interface.PlayInterfaceSound(kSoundClick);
        owner->SetPendingAction(PendingAction::None, -1, -1);
        return;
    }
    if (previous >= 0 && previous < count) Highlight(previous, false);
    Highlight(index, true);
    g_interface.PlayInterfaceSound(kSoundClick);
    int action = static_cast<int>(actions[index]);
    owner->SetPendingAction(action == 0 ? PendingAction::Order : PendingAction::Follow, index, action);
}

void HudActionColumn::Highlight(int index, bool on) { HighlightButton(this, &selected, index, on); }

void HudActionColumn::ClearHighlights() { ClearButtons(this, count, &selected); }

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
