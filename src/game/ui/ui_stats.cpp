// The statistics panel's -/+ buttons on the new hero's page and its hero
// arrows. See docs/notes/ui.md, section 5.
#include "game/ui/ui_camp.h"
#include "game/app/singletons.h"
#include "game/items/item.h"
#include "game/items/item_db.h"
#include "engine/data/text_manager.h"
#include "game/ui/ui.h"
#include "game/ui/ui_item_info.h"
#include "game/units/unit.h"
#include "game/world/world.h"
#include "lib/float_select.h"
#include "lib/str.h"

#include <cstdint>
#include <string>

namespace {
const char* const kPerkSound = "buttons\\camp\\perk.wav";
const char* const kHeroSound = "buttons\\camp\\hero.wav";

constexpr int kNewHeroPage = 10;
constexpr float kMinAttribute = 15.0f;
constexpr float kMaxAttribute = 35.0f;
constexpr float kMaxTotal = 75.0f;
constexpr float kStep = 1.0f;
constexpr int kMaxHeight = 99;
constexpr float kHeightScale = 0.01f;
constexpr int kRepeatTicks = 250;            // a step every frame from 250 ms
constexpr int kFirstHoldTicks = -500;        // the first step waits 750 ms

// An attribute pair (its base and current value, kept equal on this page).
void SetAttribute(float& base, float& current, float value) {
    base = value;
    current = value;
}

const char* const kPerkPrefix = "perk ";
const char* const kValueFormat = "%d";
const char* const kRangeFormat = "%d/%d";
const char* const kPartPrefix = "hl_";
const char* const kPartIndent = "     ";
const char* const kArmourFormat = "%.1f";

constexpr int kDamageTypes = 7;
constexpr int kShownTypes = 6;               // the seventh is summed but not compared
constexpr int kLayers = 3;

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

bool HeroStatsPanel::StepPoint(int button) {
    PartyUnitRecord* hero = owner->HeroRecord();
    UnitStats* stats = &hero->stats;
    int total = static_cast<int>(stats->strengthBase) + static_cast<int>(stats->intelligenceBase) +
                static_cast<int>(stats->dexterityBase);
    bool roomLeft = total < kMaxTotal;
    switch (button) {
    case 0:
        if (stats->strengthBase <= kMinAttribute) return false;
        SetAttribute(stats->strengthBase, stats->strength, stats->strengthBase - kStep);
        return true;
    case 1:
        if (stats->dexterityBase <= kMinAttribute) return false;
        SetAttribute(stats->dexterityBase, stats->dexterity, stats->dexterityBase - kStep);
        return true;
    case 2:
        if (stats->intelligenceBase <= kMinAttribute) return false;
        SetAttribute(stats->intelligenceBase, stats->intelligence, stats->intelligenceBase - kStep);
        return true;
    case 3:
        if (points < 1) return false;
        --points;
        hero->height = points * kHeightScale;
        return true;
    case 4:
        if (stats->strengthBase >= kMaxAttribute || !roomLeft) return false;
        SetAttribute(stats->strengthBase, stats->strength, stats->strengthBase + kStep);
        return true;
    case 5:
        if (stats->dexterityBase >= kMaxAttribute || !roomLeft) return false;
        SetAttribute(stats->dexterityBase, stats->dexterity, stats->dexterityBase + kStep);
        return true;
    case 6:
        if (stats->intelligenceBase >= kMaxAttribute || !roomLeft) return false;
        SetAttribute(stats->intelligenceBase, stats->intelligence, stats->intelligenceBase + kStep);
        return true;
    case 7:
        if (points > kMaxHeight) return false;
        ++points;
        hero->height = points * kHeightScale;
        return true;
    default:
        return false;
    }
}

// --- Update --------------------------------------------------------------------------------------

void HeroStatsPanel::Update() {
    if (page != kNewHeroPage || heldButton < 0) return;
    holdTicks += UiContainer::FrameTicks();
    if (holdTicks < kRepeatTicks) return;
    bool stepped = StepPoint(heldButton);
    owner->RefreshStats();
    if (!stepped) {
        g_interface.mouseCapture = nullptr;
        heldButton = -1;
        holdTicks = 0;
    }
}

// --- The left button (input click) -----------------------------------------------------------------

void HeroStatsPanel::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: case UiEventKind::LeftDouble: LeftDown(event); break;
    case UiEventKind::LeftUp: LeftUp(event); break;
    default: UiItemPanel::OnEvent(event); break;
    }
}

void HeroStatsPanel::LeftDown(UiEvent& event) {
    const UiRegion* region = RegionUnder(event.at);
    if (page == kNewHeroPage) {
        const int row = RegionIndex(rows, region);
        const int button = row >= kFirstButtonRow && row < kFirstButtonRow + kPointButtons ? row - kFirstButtonRow : -1;
        heldButton = -1;
        if (StepPoint(button)) heldButton = button;
        if (button >= 0) {
            g_interface.PlayInterfaceSound(kPerkSound);
            owner->RefreshStats();
        }
        if (heldButton >= 0) {
            holdTicks = kFirstHoldTicks;
            CaptureMouse();
        }
        return;
    }
    if (region == previousButton) {
        g_interface.PlayInterfaceSound(kHeroSound);
        if (camp) camp->PreviousHero();
    } else if (region == nextButton) {
        g_interface.PlayInterfaceSound(kHeroSound);
        if (camp) camp->NextHero();
    }
}

void HeroStatsPanel::LeftUp(UiEvent&) {
    g_interface.mouseCapture = nullptr;
    heldButton = -1;
    holdTicks = 0;
}

// --- Rows --------------------------------------------------------------------------------------------

// The statistics panel's row painters: a labelled value and a body part's armour. See docs/notes/ui.md,
// section 5.

// --- A labelled value ----------------------------------------------------------------------------

void HeroStatsPanel::DrawRow(const Rect& area, std::string_view label, int value, int maximum) {
    TextManager* clientWorldTexts = g_clientWorld.texts.get();
    std::string key = kPerkPrefix;
    key += label;
    std::string name;
    if (clientWorldTexts->TextManager::Open(key))
        name = clientWorldTexts->ReadLine(true);
    else
        name = label;
    std::string number = maximum >= 0 ? str::Format(kRangeFormat, value, maximum) : str::Format(kValueFormat, value);
    labels.push_back({name, area, {0, kUiTextColour, render::text::Format::Left}});
    labels.push_back({number, area, {0, kUiTextColour, render::text::Format::Right}});
}

// --- A body part's armour ------------------------------------------------------------------------

float HeroStatsPanel::PartArmour(int part) {
    const UnitStatSlot& slot = owner->HeroRecord()->stats.slots[part];
    float sums[kDamageTypes];
    for (int damageType = 0; damageType < kDamageTypes; ++damageType) {
        float sum = 0.0f;
        for (int layer = 0; layer < kLayers; ++layer) sum += slot.layers[layer].absorb[damageType];
        sums[damageType] = sum;
    }
    // The largest of the first six (a branch-free select that keeps the old value on a NaN).
    float largest = 0.0f;
    for (int shownType = 0; shownType < kShownTypes; ++shownType)
        largest = MaxKeepB(sums[shownType], largest);
    return largest;
}

void HeroStatsPanel::DrawPart(const Rect& area, std::string_view name, int part) {
    std::string key = kPartPrefix;
    key += name;
    std::string label = g_clientWorld.texts->TextManager::GetString(key);
    label = kPartIndent + label;
    labels.push_back({label, area, {0, kUiTextColour, render::text::Format::Left}});
    labels.push_back({str::Format(kArmourFormat, PartArmour(part)), area, {0, kUiTextColour, render::text::Format::Right}});
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
