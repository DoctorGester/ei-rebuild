// The multiplayer hero screen's per-frame draw and update, its close
// and the trade panel's experience figures it shows.
// See docs/notes/ui.md, section 5.
#include "ui.h"
#include "game_strings.h"
#include "world_glue.h"
#include "ui_screens.h"
#include "ui_camp.h"
#include "ui_item_info.h"
#include "texture.h"

#include "core_maths.h"
#include "item_db_text.h"
#include "map_object_figures.h"
#include "random.h"
#include "unit.h"
#include "world.h"

#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/crt.h"
#include <cstdint>
#include <string>
#include "lib/gameapi.h"
#include "party.h"
#include "game_singletons.h"
#include "ui_cursor.h"
#include "lib/str.h"

#include <cmath>
#include <memory>

namespace {

const char* const kLevelUpKey = "camp_level_up";
const char* const kSignedFormat = "%+d";
const char* const kCurrentExpKey = "camp_current_exp";
const char* const kLabelFormat = "%s %d";
const char* const kSelectVoiceKey = "lmp_select_voice";

// The points a new hero's three attributes must add up to before it can be created.
constexpr float kNewHeroPoints = 75.0f;
// The preview's full turn (2 pi), the modulus of its angle.
constexpr float kFullTurn = /*007C7F10*/ 6.2831855f;   // a static initialiser: pi + pi

constexpr int kFirstVoiceRegion = 9;
constexpr int kVoiceRegionEnd = 20;
constexpr int kCreateButton = 9;             // scene object of the create button
constexpr float kListDepth = 553.0f;
constexpr float kNewHeroDepth = 400.0f;
constexpr std::uint32_t kWhite = 0xFFFFFF;
constexpr int kTextFormat = 1;

const char* const kVoicesKey = "lmp_voices";
// The voice rows' first y offset of the selection frame.
constexpr float kVoiceFrameTop = 332.5f;
constexpr int kSelectionFrame = 2;           // scene object
constexpr std::uint32_t kListColour = 0xA7D7E4;
constexpr int kListFormat = 0x800;
constexpr float kFrameDepth = 3.0f;

const char* const kHeroName = "Hero";
const char* const kHumanHero = "Human Hero";
constexpr int kPreviewAnimation = 0x21;      // the skill animation the preview plays

} // namespace

// --- The trade panel's experience figures ------------------------------------

int CTradePanel::Experience() const {
    const TradeStatsRecord& record = Record();
    return static_cast<int>(static_cast<double>(record.pendingExperience) + record.experience - record.spentExperience);
}

int CTradePanel::PurchaseCost() {
    PartyUnitRecord* hero = owner_->HeroRecord();
    return static_cast<int>(static_cast<double>(hero->stats.spentExperience) - Record().spentExperience);
}

// --- Draw ------------------------------------------------------------------------------

void CMpHeroScreen::DrawExperience() {
    CUiSurface* surface = surfaces_[2];
    std::string text;
    std::string line;
    surface->Clear();
    g_clientWorld.texts_->GetString(kLevelUpKey, &text);
    RECT title = {0, 130, 200, 150};
    surface->DrawTextIn(text, &title, kTextFormat, kWhite, 1, 1);
    text = str::Format(kSignedFormat, -tradePanel_->PurchaseCost());
    RECT cost = {0, 150, 200, 170};
    surface->DrawTextIn(text, &cost, kTextFormat, kWhite, 1, 1);
    g_clientWorld.texts_->GetString(kCurrentExpKey, &text);
    line = str::Format(kLabelFormat, text, tradePanel_->Experience());
    RECT current = {0, 110, 200, 130};
    surface->DrawTextIn(line, &current, kTextFormat, kWhite, 1, 1);
}

void CMpHeroScreen::Draw(CSceneRenderer* pass) {
    if (mode_ == kModeStats) DrawExperience();
    int x = g_cursor.position_.x;
    int y = g_cursor.position_.y;
    hoveredItem_ = nullptr;
    int topic = 0;
    hoveredItem_ = weaponPanel_->ItemAt(x, y, &topic);
    if (!hoveredItem_ && topic == 0) hoveredItem_ = spellPanel_->ItemAt(x, y, &topic);

    if (hoveredItem_) {
        if (descPanel_) descPanel_->Describe(hoveredItem_);
    } else if (topic != 0) {
        if (descPanel_) descPanel_->ShowHelp(topic);
    } else {
        ItemHoverInfo info;
        if (tradePanel_->enabled_) info = tradePanel_->HoverInfo(x, y);
        if (info.kind == HoverKind::None) {
            if (statsPanel_->enabled_) info = statsPanel_->HoverInfo(x, y);
            if (info.kind == HoverKind::None && mode_ == kModeNewHero) {
                int region = RegionAt(x, y);
                if (region >= kFirstVoiceRegion && region < kVoiceRegionEnd) {
                    info.kind = HoverKind::Text;
                    info.text = kSelectVoiceKey;
                }
            }
        }
        if (descPanel_) descPanel_->DescribeStats(info);
    }

    CUiContainer::Draw(pass);
    if (mode_ == kModeList && heroIndex_ >= 0)
        DrawHeroPreview(pass, previewModel_, turn_, kListDepth, false);
    else if (mode_ == kModeNewHero)
        DrawHeroPreview(pass, previewModel_, turn_, kNewHeroDepth, false);
}

// --- Update ----------------------------------------------------------------------------

void CMpHeroScreen::Update() {
    CUiContainer::Update();
    if (rotating_ == 1 || rotating_ == 2) {
        double step = FrameTime();
        step += step;
        double angle = rotating_ == 1 ? step + turn_ : turn_ - step;
        turn_ = static_cast<float>(std::fmod(angle, static_cast<double>(kFullTurn)));
    }
    int position;
    if (heroScroll_->TakePosition(&position)) {
        heroTop_ = position;
        DrawHeroList();
    }
    if (voiceScroll_->TakePosition(&position)) {
        voiceTop_ = position;
        if (mode_ != kModeNewHero) return;
        DrawVoices();
    }
    if (mode_ != kModeNewHero) return;

    std::string name = nameEdit_->text_;
    const UnitStats* stats = &HeroRecord()->stats;
    double points = static_cast<double>(stats->intelligenceBase) + stats->dexterityBase + stats->strengthBase;
    float target = kNewHeroPoints;
    EI_NAN_AUDIT(points, target);
    bool equal = points == target;
    bool ready = equal && !name.empty();
    float shade = ready ? 1.0f : 0.5f;
    auto* button = SceneMesh(kCreateButton);
    button->tint_[0] = shade;
    button->tint_[1] = shade;
    button->tint_[2] = shade;
}

// --- Close -----------------------------------------------------------------------------

void CMpHeroScreen::CloseScreen() {
    g_savedParties.SaveAll();
    std::vector<PartyUnitRecord*> records;
    for (PartyUnitRecord& record : LocalPlayerData()->members_) records.push_back(&record);
    GameRemoveUnitsOf(&records);
    previewModel_ = nullptr;
    descPanel_->Describe(nullptr);
}

// --- The hero list and the voices drawn -----------------------------------------------------

void CMpHeroScreen::DrawHeroList() {
    CUiSurface* surface = surfaces_[0];
    surface->Clear();
    CUiMesh* frame = SceneMesh(kSelectionFrame);
    frame->visible_ = false;
    RECT rect = {12, 13, 265, 33};
    const auto count = static_cast<std::uint32_t>(SavedHeroCount());
    std::uint32_t index = static_cast<std::uint32_t>(heroTop_);
    for (int y = 262; y < 502; y += 24, ++index) {
        if (index >= count) break;
        if (static_cast<int>(index) == heroIndex_) {
            frame->visible_ = true;
            frame->Place(240.0f, static_cast<float>(y), kFrameDepth);
        }
        // A hero record is a party; its name the first member's text 0.
        surface->DrawTextIn(SavedHero(static_cast<int>(index))->Party()->members_[0].texts[0], &rect, 1, kListColour,
                            kListFormat, 1);
        gameapi::OffsetRect(&rect, 0, 24);
    }
}

void CMpHeroScreen::DrawVoices() {
    CUiSurface* surface = surfaces_[2];
    surface->Clear();
    CUiMesh* frame = SceneMesh(kSelectionFrame);
    frame->visible_ = false;
    std::string title;
    RECT rect = {10, 5, 190, 20};
    g_clientWorld.texts_->GetString(kVoicesKey, &title);
    surface->DrawTextIn(title, &rect, 0, kWhite, 1, 1);
    rect = {20, 25, 175, 40};
    std::uint32_t index = static_cast<std::uint32_t>(voiceTop_);
    for (int offset = 0; offset < 165; offset += 15, ++index) {
        const std::vector<MpVoiceEntry>& voices = *voices_;
        if (index >= voices.size()) break;
        if (static_cast<int>(index) == voiceIndex_) {
            frame->visible_ = true;
            float y = static_cast<float>(static_cast<double>(offset) + kVoiceFrameTop);
            frame->Place(97.5f, y, kFrameDepth);
        }
        surface->DrawTextIn(voices[index].name, &rect, 0, kListColour, kListFormat, 1);
        gameapi::OffsetRect(&rect, 0, 15);
    }
}

// --- The preview's unit -------------------------------------------------------------------

void CMpHeroScreen::MakePreview(bool allArmour) {
    previewModel_ = nullptr;
    auto* player = LocalPlayerData();
    if (player->members_.empty()) {
        player->Clear(1, 0);
        player->AddMember(kHeroName, kHumanHero, game::kEmptyText);
    }
    const std::vector<PartyUnitRecord*> records{player->members_.data()};
    GameCreatePartyUnits(&records, allArmour);
    auto* unit = GameFindClientUnit(player->members_.data()->unitId);
    unit->SetSecondPrototype(unit->GetPrototype());
    previewModel_ = unit;
    if (unit) {
        FigureAnimation* animation = unit->FindSkillAnimation(kPreviewAnimation);
        if (animation) {
            std::uint32_t random = g_random.Next();
            double frames = x87::FromUnsigned(gameapi::timeGetTime()) * 0.0009765625f * 15.0f;
            int start = static_cast<int>(frames - static_cast<int>(random % 51));
            unit->SetAnimationEntries(-1, static_cast<std::int8_t>(animation->index), start, 0);
        }
        CUnitController* controller = unit->Controller();
        controller->ShowWeapon(kNoWeaponSlot);
        controller->RefreshModel();
        double now = x87::FromUnsigned(gameapi::timeGetTime()) * 0.0009765625f * 15.0f;
        controller->RestartAnimation(static_cast<float>(now - 1.0f));
        unit->SetFromStats(&player->members_.data()->stats);
        unit->Controller()->RefreshWounds();
    }
}

// --- A hero shown -------------------------------------------------------------------------

void CMpHeroScreen::ShowHero(int index) {
    auto* player = LocalPlayerData();
    if (!player->members_.empty()) {
        std::vector<PartyUnitRecord*> records{player->members_.data()};
        GameRemoveUnitsOf(&records);
    }
    heroIndex_ = -1;
    if (index >= 0 && index < SavedHeroCount()) {
        heroIndex_ = index;
        player->Assign(SavedHero(index)->Party());
        g_savedParties.Select(SavedHero(index));
    }
    MakePreview(true);
    auto* hero = HeroRecord();
    auto* unit = GameFindClientUnit(hero->unitId);
    for (int slot = 0; slot < 4; ++slot) unit->RemoveWeapon(slot);
    for (int i = 0; i < std::ssize(hero->weapons); ++i) unit->AddWeapon(hero->weapons[i]);
    unit->SendEquipment();
    unit->Controller()->ShowWeapon(hero->weaponIndex);
    unit->Controller()->RefreshModel();
    turn_ = 0.0f;
    DrawHeroList();
}
