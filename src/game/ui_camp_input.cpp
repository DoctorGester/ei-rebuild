// The camp's input (buttons, keys), its message-result and help-topic slots (CampPages,
// ui_camp.h). See docs/notes/ui.md, section 5.
#include "party.h"

#include "console.h"
#include "item.h"
#include "item_db.h"
#include "item_manager.h"
#include "resource_manager.h"
#include "spell.h"
#include "ui.h"
#include "ui_camp.h"
#include "ui_item_info.h"
#include "ui_screens.h"
#include "unit.h"
#include "world.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "platform/platform.h"

#include <cstdint>
#include <string>

namespace {

constexpr const char* kMoveSound = "buttons\\camp\\move.wav";

// The help topics of pages 0-6 (the game's strings).
const char* PageHelpTopic(int page) {
    switch (page) {
    case 0: return "camp_weapons";
    case 1: return "camp_skills";
    case 2: return "camp_spell_trade";
    case 3: return "camp_spell_constr";
    case 4: return "camp_item_trade";
    case 5: return "camp_item_constr";
    case 6: return "camp_repair";
    default: return nullptr;
    }
}

constexpr int kEquipmentPage = 0;
constexpr int kLeavePage = 7;
constexpr std::uint32_t kAnswerYes = BoxResult(MessageQuestion::First, MessageAnswer::Ok);

} // namespace

void CampPages::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: LeftDown(event); break;
    case UiEventKind::LeftUp: LeftUp(event); break;
    case UiEventKind::KeyDown: KeyDown(event); break;
    default: ItemScreen::OnEvent(event); break;
    }
}

void CampPages::LeftDown(UiEvent& event) {
    UiContainer::OnEvent(event);
    UiRegion* region = RegionUnder(event.at);
    const int pageButton = RegionIndex(pageButtons, region);
    if (region == actionButton) {
        UpdateAction();
        if (actionEnabled) DoAction();
    } else if (region == secondActionButton) {
        UpdateAction();
        if (actionShown) UndoAction();
    } else if (region == turnLeftArea || region == turnRightArea) {
        if (page == kEquipmentPage) {
            ui->PlayInterfaceSound(kMoveSound);
            turning = region == turnLeftArea ? 1 : 2;
            CaptureMouse();
        }
    } else if (pageButton >= 0 && page != pageButton) {
        ShowPage(pageButton);
    }
}

void CampPages::LeftUp(UiEvent& event) {
    UiContainer::OnEvent(event);
    ReleaseMouse();
    turning = 0;
}

void CampPages::KeyDown(UiEvent& event) {
    if (event.key == platform::Key::Escape) {
        ShowPage(kLeavePage);
        event.handled = true;
        return;
    }
    if ((event.key == platform::Key::Backspace || event.key == platform::Key::Return) && IsMultiplayerClient()) {
        if (event.key == platform::Key::Backspace)
            ui->ChatKeyBackspace();
        else
            ui->ChatKeyEnter();
        event.handled = true;
        return;
    }
    UiContainer::OnEvent(event);
}

void CampPages::MessageResult(std::uint32_t result) {
    if (result != kAnswerYes) return;
    PartyUnitRecord* heroRecord = HeroRecord();
    if (heroRecord->weapons.empty()) return;
    Item* weapon = heroRecord->weapons[0];
    Item* item = LocalPlayerData()->AddItem(weapon, false, true);
    heroRecord->RemoveWeapon(weapon);   // the weapon list's Remove
    heroRecord->weaponIndex = kNoWeaponSlot;
    itemStrip->ScrollToItem(item);
    RefreshWeapons();
}

const char* CampPages::HelpTopic() { return PageHelpTopic(page); }

// --- Ui camp debug -----------------------------------------------------------------------------------

// The camp's debug fill of the constructors' lists. See ui_camp.h and
// docs/notes/ui.md, section 5.

namespace {

constexpr std::uint32_t kInstructionCount = 10;
constexpr std::uint32_t kMaterialCount = 20;

// The list's copy of `item`: counted for the call, added (keeping its count, merged), released.
void AddCounted(Party* list, Item* item) {
    item->AddRef();
    list->AddItem(item, true, true);
    item->Release();
}

// An item named `name` from the list's item manager (not checked for null) with
// record index `index` and count `count`, added.
void AddNamed(Party* list, const char* name, std::uint32_t index, std::uint32_t count) {
    auto* item = list->ItemFactory()->CreateItem(name)->AsLoot();
    item->index = index;
    item->count = count;
    AddCounted(list, item);
}

// One item of class `kind` for `record` in every material whose soundName names the record's material class,
// made by the list's item constructor with `manager`. A material index out of the table reads through null.
void AddInMaterials(Party* list, ResourceManager* manager, ItemConstructor* maker, itemdb::ItemRecordHead* record,
                    ItemKind kind, int materials) {
    for (int j = 0; j < materials; ++j) {
        if (text::CompareIgnoringCase(itemdb::MaterialAt(static_cast<std::uint32_t>(j))->soundName, record->materialClass) != 0)
            continue;
        AddCounted(list, maker->BuildItem(manager, kind, record, itemdb::MaterialAt(static_cast<std::uint32_t>(j))));
    }
}

// The record's name contains `word`.
bool NameHas(const itemdb::ItemRecordHead* record, const char* word) {
    return str::Find(record->name, word) >= 0;
}

} // namespace

void CampPages::DebugFill() {
    std::vector<std::string> names;
    std::vector<std::string> materials = g_clientWorld.itemManager->ListMaterialNames();
    const int materialCount = static_cast<int>(materials.size());

    if (Party* items = itemParts[0]) {
        items->Clear(true, false);

        names = g_clientWorld.itemManager->ListWeaponNames();
        for (int i = 0; i < static_cast<int>(names.size()); ++i) {
            AddNamed(items, "Instruction Weapon", static_cast<std::uint32_t>(i), kInstructionCount);
            ResourceManager* manager = items->ItemFactory();
            ItemConstructor* maker = items->GetItemConstructor();
            itemdb::ItemRecordHead* weapon = itemdb::WeaponAt(static_cast<std::uint32_t>(i));
            AddInMaterials(items, manager, maker, weapon, ItemKind::Weapon, materialCount);
        }

        names = g_clientWorld.itemManager->ListArmorNames();
        for (int i = 0; i < static_cast<int>(names.size()); ++i) {
            ResourceManager* manager = items->ItemFactory();
            itemdb::ItemRecordHead* armor = itemdb::ArmorAt(static_cast<std::uint32_t>(i));
            if (NameHas(armor, "prototype")) continue;
            AddNamed(items, "Instruction Armor", static_cast<std::uint32_t>(i), kInstructionCount);
            ItemConstructor* maker = items->GetItemConstructor();
            AddInMaterials(items, manager, maker, armor, ItemKind::Armor, materialCount);
        }

        names = g_clientWorld.itemManager->ListQuickItemNames();
        for (int i = 0; i < static_cast<int>(names.size()); ++i) {
            itemdb::ItemRecordHead* quick = itemdb::QuickItemAt(static_cast<std::uint32_t>(i));
            if (NameHas(quick, "wand"))
                AddNamed(items, "Instruction Quick Item", static_cast<std::uint32_t>(i), kInstructionCount);
        }

        // "Material.<name>", twenty each, the record index left as made.
        names = g_clientWorld.itemManager->ListMaterialNames();
        for (const std::string& material : names) {
            auto* item = items->ItemFactory()->CreateItem("Material." + material);
            item->count = kMaterialCount;
            AddCounted(items, item);
        }
    }

    if (Party* spells = spellParts[0]) {
        spells->Clear(true, false);
        names = g_clientWorld.spellManager->ListModifierNames();
        for (int i = 0; i < static_cast<int>(names.size()); ++i)
            AddNamed(spells, "Rune", static_cast<std::uint32_t>(i), kInstructionCount);
        names = g_clientWorld.spellManager->ListSpellNames();
        for (int i = 0; i < static_cast<int>(names.size()); ++i)
            AddNamed(spells, "Prototype", static_cast<std::uint32_t>(i), kInstructionCount);
    }
}

// --- Ui camp stats -----------------------------------------------------------------------------------

// The camp's statistics column (HeroStatsPanel) and trade panel (TradePanel): what the
// statistics under the mouse are, for the description panel (the camp's Draw
// and the hero screen's draw ask every frame), and the column's hero. See ui_camp.h and
// docs/notes/ui.md ("Camp, global map, hero screens").

namespace {

constexpr const char* kPerkLevelDigits = "123";
constexpr const char* kPerkFirstLevel = "0";
constexpr const char* kPointsLeft = "lmp_points_left";
constexpr const char* kHeight = "lmp_pers_tall";
constexpr const char* kSkull = "hl_skull";
constexpr const char* kArms = "hl_arms";
constexpr const char* kTorso = "hl_torso";
constexpr const char* kLegs = "hl_legs";
constexpr const char* kAttack = "attack";
constexpr const char* kDefence = "defense";
constexpr const char* kDamage = "camp_damage";
constexpr const char* kMagicSkills = "magic_skills";

// The body parts' armour slots (ItemHoverInfo::part).
constexpr int kPartSkull = 0;
constexpr int kPartTorso = 1;
constexpr int kPartArms = 2;
constexpr int kPartLegs = 4;

// The statistics column's rows on page 0 (HeroStatsPanel::rows).
constexpr int kSkullRow = 0, kArmsRow = 1, kTorsoRow = 2, kLegsRow = 3, kAttackRow = 4, kDefenceRow = 5, kDamageRow = 6,
              kEncumbranceRow = 7, kMagicRow = 8;

// The hero arrows' tooltips (texts "tip_20300" / "tip_20301").
constexpr int kTipPreviousHero = 0x4F4C;
constexpr int kTipNextHero = 0x4F4D;

} // namespace

// --- the trade panel -----------------------------------------------------------------------------

ItemHoverInfo TradePanel::HoverInfo(Point at) {
    ItemHoverInfo info;
    if (!enabled) return info;
    // No region falls through to the held perks (HeldPerkRow), past the list.
    const UiRegion* region = RegionUnder(at);
    const int school = RegionIndex(schoolRows, region);
    const int learnableRow = RegionIndex(learnableRows, region);
    if (school >= 0) {
        info.kind = HoverKind::Skill;
        info.index = school;
        info.text = SkillNames()[school];
    } else if (region == scienceRow) {
        info.kind = HoverKind::Science;
        info.index = 0;
        info.text = SkillNames()[6];
    } else if (RegionIndex(perkScroll, region) >= 0 || RegionIndex(learnableScroll, region) >= 0) {
        return info;
    } else {
        const TradeRow* row;
        if (learnableRow >= 0) {
            unsigned i = static_cast<unsigned>(learnableTop) + static_cast<unsigned>(learnableRow);
            if (i >= learnable.size()) return info;
            row = &learnable[i];
            info.index = 0;
        } else {
            unsigned i = static_cast<unsigned>(perkTop) + static_cast<unsigned>(HeldPerkRow(region));
            if (i >= perks.size()) return info;
            row = &perks[i];
            info.index = row->level;
        }
        info.kind = HoverKind::Perk;
        info.text = row->key;
        str::TrimRight(info.text, kPerkLevelDigits);
        info.text += kPerkFirstLevel;
    }
    info.stats = &owner->HeroRecord()->stats;
    return info;
}

// --- the statistics column -----------------------------------------------------------------------

ItemHoverInfo HeroStatsPanel::HoverInfo(Point at) {
    PartyUnitRecord* hero = owner->HeroRecord();
    ItemHoverInfo info;
    info.stats = &hero->stats;
    const int row = RegionIndex(rows, RegionUnder(at));
    if (((page == 1 || page == 11) && row >= 0) || (page == 10 && row >= 0 && row < kHeightRow)) {
        info.kind = HoverKind::Attribute;
        info.index = row;
        info.text = AttributeNames()[row];
    }
    if (page == 10 && (row == kPointsLeftRow || row == kHeightRow)) {
        info.kind = HoverKind::Text;
        info.text = row == kPointsLeftRow ? kPointsLeft : kHeight;
    }
    if (page != 0) return info;
    auto armour = [&info](int part, const char* text) {
        info.kind = HoverKind::Armour;
        info.part = part;
        info.text = text;
    };
    switch (row) {
    case kSkullRow: armour(kPartSkull, kSkull); break;
    case kArmsRow: armour(kPartArms, kArms); break;
    case kTorsoRow: armour(kPartTorso, kTorso); break;
    case kLegsRow: armour(kPartLegs, kLegs); break;
    case kAttackRow:
    case kDefenceRow:
        if (hero->weaponIndex < static_cast<std::uint32_t>(std::ssize(hero->weapons))) {
            info.kind = HoverKind::Weapon;
            info.weapon = hero->weapons[hero->weaponIndex]->AsWeapon();
            info.text = row == kAttackRow ? kAttack : kDefence;
        }
        break;
    case kDamageRow:
        info.kind = HoverKind::Text;
        info.text = kDamage;
        break;
    case kEncumbranceRow:
        info.kind = HoverKind::Attribute;
        info.index = 7;
        info.text = AttributeNames()[7];
        break;
    case kMagicRow:
        info.kind = HoverKind::Magic;
        info.text = kMagicSkills;
        break;
    default:
        break;
    }
    return info;
}

// --- the column's hero ---------------------------------------------------------------------------

void HeroStatsPanel::SetHero(int newHeroIndex, std::optional<std::string_view> newTitle) {
    bool party = std::ssize(LocalPlayerData()->members) > 1;
    nextArrow->visible = party;
    previousArrow->visible = party;
    previousButton->SetTip(party ? kTipPreviousHero : 0);
    nextButton->SetTip(party ? kTipNextHero : 0);
    heroIndex = newHeroIndex;
    if (newTitle) title = std::string(*newTitle);
    points = static_cast<int>(owner->HeroRecord()->height * 100.0f);
    DrawStats();
}

// --- the camp's hero record ----------------------------------------------------------------------

PartyUnitRecord* CampPages::HeroRecord() {
    return &LocalPlayerData()->members[hero];
}
