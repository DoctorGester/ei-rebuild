// The camp's input side (buttons, keys), its message-result and help-topic slots (CampPages,
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
#include "platform/keys.h"

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

constexpr int kRegionAction = 0;
constexpr int kRegionSecondAction = 1;
constexpr int kRegionTurnLeft = 2;
constexpr int kRegionTurnRight = 3;
constexpr int kRegionFirstPage = 4;
constexpr int kRegionLastPage = 11;
constexpr int kEquipmentPage = 0;
constexpr int kLeavePage = 7;
constexpr std::uint32_t kAnswerYes = 0x10001;

} // namespace

void CampPages::LeftDown(int buttons, int x, int y) {
    UiContainer::LeftDown(buttons, x, y);
    int region = RegionAt(x, y);
    switch (region) {
    case kRegionAction:
        UpdateAction();
        if (actionEnabled) DoAction();
        break;
    case kRegionSecondAction:
        UpdateAction();
        if (actionShown) UndoAction();
        break;
    case kRegionTurnLeft:
    case kRegionTurnRight:
        if (page == kEquipmentPage) {
            ui->PlayInterfaceSound(kMoveSound);
            turning = region == kRegionTurnLeft ? 1 : 2;
            CaptureMouse();
        }
        break;
    default:
        if (region >= kRegionFirstPage && region <= kRegionLastPage && page != region - kRegionFirstPage)
            ShowPage(region - kRegionFirstPage);
        break;
    }
}

void CampPages::LeftUp(int buttons, int x, int y) {
    UiContainer::LeftUp(buttons, x, y);
    ReleaseMouse();
    turning = 0;
}

void CampPages::KeyDown(int key, int repeat, int modifiers) {
    if (key == platform::kKeyEscape) {
        ShowPage(kLeavePage);
        handled = true;
        return;
    }
    if ((key == platform::kKeyBack || key == platform::kKeyReturn) && IsMultiplayerClient()) {
        if (key == platform::kKeyBack)
            ui->ChatKeyBackspace();
        else
            ui->ChatKeyEnter();
        handled = true;
        return;
    }
    UiContainer::KeyDown(key, repeat, modifiers);
}

void CampPages::MessageResult(int result) {
    if (static_cast<std::uint32_t>(result) != kAnswerYes) return;
    PartyUnitRecord* heroRecord = HeroRecord();
    if (heroRecord->weapons.empty()) return;
    Item* weapon = heroRecord->weapons[0];
    Item* item = LocalPlayerData()->AddItem(weapon, 0, 1);
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
    list->AddItem(item, 1, 1);
    item->Release();
}

// An item named `name` from the list's item manager (not checked for null) with
// record index `index` and count `count`, added.
void AddNamed(Party* list, const char* name, std::uint32_t index, std::uint32_t count) {
    auto* item = AsLoot(list->ItemFactory()->CreateItem(name));
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
        items->Clear(1, 0);

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
        spells->Clear(1, 0);
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

// The hero arrows' tooltips (texts "tip_20300" / "tip_20301").
constexpr int kTipPreviousHero = 0x4F4C;
constexpr int kTipNextHero = 0x4F4D;

} // namespace

// --- the trade panel -----------------------------------------------------------------------------

ItemHoverInfo TradePanel::HoverInfo(int x, int y) {
    ItemHoverInfo info;
    if (!enabled) return info;
    // The region is tested with unsigned compares, so -1 (no region) falls through
    // every range and ends on a held-perk index far past the list.
    unsigned region = static_cast<unsigned>(RegionAt(x, y));
    if (region >= 4 && region < 10) {
        info.kind = HoverKind::Skill;
        info.index = static_cast<int>(region - 4);
        info.text = SkillNames()[region - 4];
    } else if (region == 10) {
        info.kind = HoverKind::Science;
        info.index = 0;
        info.text = SkillNames()[6];
    } else if (region < 11) {
        return info;   // regions 0-3: the scroll arrows
    } else {
        const TradeRow* row;
        if (region < 0x22) {
            unsigned i = static_cast<unsigned>(learnableTop) + region - 11;
            if (i >= learnable.size()) return info;
            row = &learnable[i];
            info.index = 0;
        } else {
            unsigned i = static_cast<unsigned>(perkTop) + region - 0x22;
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

ItemHoverInfo HeroStatsPanel::HoverInfo(int x, int y) {
    PartyUnitRecord* hero = owner->HeroRecord();
    ItemHoverInfo info;
    info.stats = &hero->stats;
    int row = RegionAt(x, y);
    if (((page == 1 || page == 11) && row >= 2) || (page == 10 && row >= 2 && row < 5)) {
        info.kind = HoverKind::Attribute;
        info.index = row - 2;
        info.text = AttributeNames()[row - 2];
    }
    if (page == 10 && (row == 14 || row == 5)) {
        info.kind = HoverKind::Text;
        info.text = row == 14 ? kPointsLeft : kHeight;
    }
    if (page != 0) return info;
    auto armour = [&info](int part, const char* text) {
        info.kind = HoverKind::Armour;
        info.part = part;
        info.text = text;
    };
    switch (row) {
    case 2: armour(kPartSkull, kSkull); break;
    case 3: armour(kPartArms, kArms); break;
    case 4: armour(kPartTorso, kTorso); break;
    case 5: armour(kPartLegs, kLegs); break;
    case 6:
    case 7:
        if (hero->weaponIndex < static_cast<std::uint32_t>(std::ssize(hero->weapons))) {
            info.kind = HoverKind::Weapon;
            info.weapon = AsWeapon(hero->weapons[hero->weaponIndex]);
            info.text = row == 6 ? kAttack : kDefence;
        }
        break;
    case 8:
        info.kind = HoverKind::Text;
        info.text = kDamage;
        break;
    case 9:
        info.kind = HoverKind::Attribute;
        info.index = 7;
        info.text = AttributeNames()[7];
        break;
    case 10:
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
    SceneMesh(0)->visible = party;
    SceneMesh(1)->visible = party;
    regions[0]->SetTip(party ? kTipPreviousHero : 0);
    regions[1]->SetTip(party ? kTipNextHero : 0);
    heroIndex = newHeroIndex;
    if (newTitle) title = std::string(*newTitle);
    points = static_cast<int>(owner->HeroRecord()->height * 100.0f);
    DrawStats();
}

// --- the camp's hero record ----------------------------------------------------------------------

PartyUnitRecord* CampPages::HeroRecord() {
    return &LocalPlayerData()->members[hero];
}
