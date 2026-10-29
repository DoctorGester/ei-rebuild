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

#include <cstdint>
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

ItemHoverInfo CTradePanel::HoverInfo(int x, int y) {
    ItemHoverInfo info;
    if (!enabled_) return info;
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
            unsigned i = static_cast<unsigned>(learnableTop_) + region - 11;
            if (i >= learnable_.size()) return info;
            row = &learnable_[i];
            info.index = 0;
        } else {
            unsigned i = static_cast<unsigned>(perkTop_) + region - 0x22;
            if (i >= perks_.size()) return info;
            row = &perks_[i];
            info.index = row->level;
        }
        info.kind = HoverKind::Perk;
        info.text = row->key;
        str::TrimRight(info.text, kPerkLevelDigits);
        info.text += kPerkFirstLevel;
    }
    info.stats = &owner_->HeroRecord()->stats;
    return info;
}

// --- the statistics column -----------------------------------------------------------------------

ItemHoverInfo CHeroStatsPanel::HoverInfo(int x, int y) {
    PartyUnitRecord* hero = owner_->HeroRecord();
    ItemHoverInfo info;
    info.stats = &hero->stats;
    int row = RegionAt(x, y);
    if (((page_ == 1 || page_ == 11) && row >= 2) || (page_ == 10 && row >= 2 && row < 5)) {
        info.kind = HoverKind::Attribute;
        info.index = row - 2;
        info.text = AttributeNames()[row - 2];
    }
    if (page_ == 10 && (row == 14 || row == 5)) {
        info.kind = HoverKind::Text;
        info.text = row == 14 ? kPointsLeft : kHeight;
    }
    if (page_ != 0) return info;
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

void CHeroStatsPanel::SetHero(int heroIndex, std::optional<std::string_view> title) {
    bool party = std::ssize(LocalPlayerData()->members_) > 1;
    SceneMesh(0)->visible_ = party;
    SceneMesh(1)->visible_ = party;
    regions_[0]->SetTip(party ? kTipPreviousHero : 0);
    regions_[1]->SetTip(party ? kTipNextHero : 0);
    heroIndex_ = heroIndex;
    if (title) title_ = std::string(*title);
    points_ = static_cast<int>(owner_->HeroRecord()->height * 100.0f);
    DrawStats();
}

// --- the camp's hero record ----------------------------------------------------------------------

PartyUnitRecord* CCampPages::HeroRecord() {
    return &LocalPlayerData()->members_[hero_];
}
