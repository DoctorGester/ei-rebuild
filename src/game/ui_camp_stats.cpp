// The camp's statistics column (CHeroStatsPanel) and trade panel (CTradePanel): what the
// statistics under the mouse are, for the description panel (the camp's Draw
// and the hero screen's draw ask every frame), and the column's hero. See ui_camp.h and
// docs/notes/ui.md ("Camp, global map, hero screens").
#include "ui_camp.h"

#include "item.h"
#include "party.h"
#include "ui_item_info.h"
#include "unit.h"
#include "world.h"

#include "lib/mfc.h"
#include "lib/str.h"

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

// The hover kinds (ItemHoverInfo::kind).
constexpr int kNone = -1;
constexpr int kAttribute = 0;
constexpr int kSkill = 1;
constexpr int kSchool = 2;
constexpr int kPerk = 3;
constexpr int kArmour = 4;
constexpr int kWeapon = 5;
constexpr int kMagic = 6;
constexpr int kText = 7;

// The body parts' armour slots (ItemHoverInfo::part).
constexpr int kPartSkull = 0;
constexpr int kPartTorso = 1;
constexpr int kPartArms = 2;
constexpr int kPartLegs = 4;

// The hero arrows' tooltips (texts "tip_20300" / "tip_20301").
constexpr int kTipPreviousHero = 0x4F4C;
constexpr int kTipNextHero = 0x4F4D;

UnitStats* HeroStats(CItemScreen* owner) { return &owner->HeroRecord()->stats; }

} // namespace

// --- the trade panel -----------------------------------------------------------------------------

void CTradePanel::HoverInfo(int x, int y, ItemHoverInfo* info) {
    info->kind = kNone;
    if (!enabled_) return;
    // The region is tested with unsigned compares, so -1 (no region) falls through
    // every range and ends on a held-perk index far past the list.
    unsigned region = static_cast<unsigned>(RegionAt(x, y));
    if (region >= 4 && region < 10) {
        info->kind = kSkill;
        info->index = static_cast<int>(region - 4);
        info->text = SkillNames()[region - 4];
    } else if (region == 10) {
        info->kind = kSchool;
        info->index = 0;
        info->text = SkillNames()[6];
    } else if (region < 11) {
        info->kind = kNone;   // regions 0-3: the scroll arrows
        return;
    } else {
        const TradeRow* row;
        if (region < 0x22) {
            unsigned i = static_cast<unsigned>(learnableTop_) + region - 11;
            if (i >= static_cast<unsigned>(static_cast<int>(learnable_.size()))) return;
            info->kind = kPerk;
            info->index = 0;
            row = &learnable_[i];
        } else {
            unsigned i = static_cast<unsigned>(perkTop_) + region - 0x22;
            if (i >= static_cast<unsigned>(static_cast<int>(perks_.size()))) return;
            info->kind = kPerk;
            row = &perks_[i];
            info->index = row->level;
        }
        info->text = row->key;
        str::TrimRight(info->text, kPerkLevelDigits);
        info->text += kPerkFirstLevel;
    }
    info->stats = HeroStats(owner_);
}

// --- the statistics column -----------------------------------------------------------------------

void CHeroStatsPanel::HoverInfo(int x, int y, ItemHoverInfo* info) {
    PartyUnitRecord* hero = owner_->HeroRecord();
    info->kind = kNone;
    info->stats = &hero->stats;
    int row = RegionAt(x, y);
    if (((page_ == 1 || page_ == 11) && row >= 2) || (page_ == 10 && row >= 2 && row < 5)) {
        info->kind = kAttribute;
        info->index = row - 2;
        info->text = AttributeNames()[row - 2];
    }
    if (page_ == 10) {
        if (row == 14) {
            info->kind = kText;
            info->text = kPointsLeft;
        }
        if (page_ == 10 && row == 5) {
            info->kind = kText;
            info->text = kHeight;
        }
    }
    if (page_ != 0 || row < 2) return;
    switch (row) {
    case 2:
        info->kind = kArmour;
        info->part = kPartSkull;
        info->text = kSkull;
        break;
    case 3:
        info->kind = kArmour;
        info->part = kPartArms;
        info->text = kArms;
        break;
    case 4:
        info->kind = kArmour;
        info->part = kPartTorso;
        info->text = kTorso;
        break;
    case 5:
        info->kind = kArmour;
        info->part = kPartLegs;
        info->text = kLegs;
        break;
    case 6:
    case 7:
        if (hero->weaponIndex < static_cast<std::uint32_t>(hero->weapons.size)) {
            info->kind = kWeapon;
            info->weapon = AsWeapon(hero->weapons[hero->weaponIndex]);
            info->text = row == 6 ? kAttack : kDefence;
        }
        break;
    case 8:
        info->kind = kText;
        info->text = kDamage;
        break;
    case 9:
        info->kind = kAttribute;
        info->index = 7;
        info->text = AttributeNames()[7];
        break;
    case 10:
        info->kind = kMagic;
        info->text = kMagicSkills;
        break;
    default:
        break;
    }
}

// --- the column's hero ---------------------------------------------------------------------------

void CHeroStatsPanel::SetHero(int heroIndex, const char* title) {
    bool party = LocalPlayerData()->members_.size > 1;
    SceneMesh(0)->visible_ = party ? 1 : 0;
    SceneMesh(1)->visible_ = party ? 1 : 0;
    regions_.data[0]->SetTip(party ? kTipPreviousHero : 0, 1, 0);
    regions_.data[1]->SetTip(party ? kTipNextHero : 0, 1, 0);
    heroIndex_ = heroIndex;
    if (title) title_ = title;
    points_ = static_cast<int>(owner_->HeroRecord()->height * 100.0f);
    DrawStats();
}

// --- the camp's hero record ----------------------------------------------------------------------

PartyUnitRecord* CCampPages::HeroRecord() {
    return &LocalPlayerData()->members_.data[hero_];
}
