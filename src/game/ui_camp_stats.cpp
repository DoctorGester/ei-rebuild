// The camp's statistics column (CHeroStatsPanel) and trade panel (CTradePanel): what the
// statistics under the mouse are, for the description panel (005F1590; the camp's Draw 0060F050
// and the hero screen's draw ask every frame), and the column's hero. See ui_camp.h and
// docs/notes/ui.md ("Camp, global map, hero screens").
#include "ui_camp.h"

#include "item.h"
#include "party.h"
#include "ui_item_info.h"
#include "unit.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "originals_ui.h"

namespace {

constexpr const char* kPerkLevelDigits = "123";     // 00793E60
constexpr const char* kPerkFirstLevel = "0";        // 007856B8
constexpr const char* kPointsLeft = "lmp_points_left";   // 00793D40
constexpr const char* kHeight = "lmp_pers_tall";         // 00793D30
constexpr const char* kSkull = "hl_skull";               // 00793E38
constexpr const char* kArms = "hl_arms";                 // 00793E30
constexpr const char* kTorso = "hl_torso";               // 00793E24
constexpr const char* kLegs = "hl_legs";                 // 00793E1C
constexpr const char* kAttack = "attack";                // 00793E14
constexpr const char* kDefence = "defense";              // 00792F88
constexpr const char* kDamage = "camp_damage";           // 00793E08
constexpr const char* kMagicSkills = "magic_skills";     // 00793D20

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

UnitStats* HeroStats(CUiWindow* owner) { return reinterpret_cast<UnitStats*>(ShownHero(owner)->stats); }

} // namespace

// --- 00625690: the trade panel ----------------------------------------------------------------

void CTradePanel::HoverInfo(int x, int y, ItemHoverInfo* info) {
    info->kind = kNone;
    if (!enabled_) return;
    // The original tests the region with unsigned compares, so -1 (no region) falls through
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
            if (i >= static_cast<unsigned>(learnable_.Count())) return;
            info->kind = kPerk;
            info->index = 0;
            row = &learnable_.begin[i];
        } else {
            unsigned i = static_cast<unsigned>(perkTop_) + region - 0x22;
            if (i >= static_cast<unsigned>(perks_.Count())) return;
            info->kind = kPerk;
            row = &perks_.begin[i];
            info->index = row->level;
        }
        info->text = row->key;
        info->text.TrimRight(kPerkLevelDigits);
        info->text += kPerkFirstLevel;
    }
    info->stats = HeroStats(owner_);
}
EI_REPLACE_METHOD(0x00625690, CTradePanel::HoverInfo);

// --- 00624CB0: the statistics column -----------------------------------------------------------

void CHeroStatsPanel::HoverInfo(int x, int y, ItemHoverInfo* info) {
    PartyUnitRecord* hero = ShownHero(owner_);
    info->kind = kNone;
    info->stats = reinterpret_cast<UnitStats*>(hero->stats);
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
        if (hero->weaponIndex < hero->weaponCount) {
            info->kind = kWeapon;
            info->weapon = hero->weapons[hero->weaponIndex];
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
EI_REPLACE_METHOD(0x00624CB0, CHeroStatsPanel::HoverInfo);

// --- 006232D0: the column's hero -----------------------------------------------------------------

void CHeroStatsPanel::SetHero(int heroIndex, const char* title) {
    bool party = reinterpret_cast<CParty*>(LocalPlayerData())->members_.size > 1;
    reinterpret_cast<CUiMesh*>(sceneObjects_.data[0])->visible_ = party ? 1 : 0;
    reinterpret_cast<CUiMesh*>(sceneObjects_.data[1])->visible_ = party ? 1 : 0;
    regions_.data[0]->SetTip(party ? kTipPreviousHero : 0, 1, 0);
    regions_.data[1]->SetTip(party ? kTipNextHero : 0, 1, 0);
    heroIndex_ = heroIndex;
    if (title) title_ = title;
    points_ = static_cast<int>(ShownHero(owner_)->height * 100.0f);
    static_cast<CHeroStatsPanel*>(this)->DrawStats();
}
EI_REPLACE_METHOD(0x006232D0, CHeroStatsPanel::SetHero);

// --- 0060FA80: the camp's hero record (slot 64) ----------------------------------------------------

PartyUnitRecord* CCampPages::HeroRecord() {
    return &reinterpret_cast<PartyRecords*>(LocalPlayerData())->records[hero_];
}
EI_REPLACE_METHOD(0x0060FA80, CCampPages::HeroRecord);
