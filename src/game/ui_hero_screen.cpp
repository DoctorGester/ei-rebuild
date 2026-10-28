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
float NewHeroPoints() { return 75.0f; }
// The preview's full turn (2 pi), the modulus of its angle.
constexpr float kFullTurn = /*007C7F10*/ 6.2831855f;   // a static initialiser: pi + pi
// The multiplayer heroes (their vector of SavedHero).

constexpr int kModeList = 9;
constexpr int kModeNewHero = 10;
constexpr int kModeStats = 11;
constexpr int kFirstVoiceRegion = 9;
constexpr int kVoiceRegionEnd = 20;
constexpr int kVoiceHintKind = 7;            // a plain text for the description panel
constexpr int kCreateButton = 9;             // scene object of the create button
constexpr float kListDepth = 553.0f;
constexpr float kNewHeroDepth = 400.0f;
constexpr std::uint32_t kWhite = 0xFFFFFF;
constexpr int kTextFormat = 1;

UnitStats* StatsOf(PartyUnitRecord* hero) { return &hero->stats; }

const char* const kVoicesKey = "lmp_voices";
// The voice rows' first y offset of the selection frame.
float VoiceFrameTop() { return 332.5f; }
constexpr int kSelectionFrame = 2;           // scene object
constexpr std::uint32_t kListColour = 0xA7D7E4;
constexpr int kListFormat = 0x800;
constexpr float kFrameDepth = 3.0f;

CUiMesh* SelectionFrame(CUiWindow& w) {
    return w.SceneMesh(kSelectionFrame);
}
// A multiplayer hero record (a party) and its name: its first member's text 0.
CParty* MpHero(std::uint32_t index) { return SavedHero(static_cast<int>(index))->Party(); }
std::uint32_t MpHeroCount() {
    return static_cast<std::uint32_t>(SavedHeroCount());
}

const char* const kHeroName = "Hero";
const char* const kHumanHero = "Human Hero";
constexpr int kPreviewAnimation = 0x21;      // the skill animation the preview plays

} // namespace

// --- The trade panel's experience figures ------------------------------------

int CTradePanel::Experience() const {
    const TradeRecord& record = Record();
    return static_cast<int>(static_cast<double>(record.pendingExperience) + record.experience - record.spentExperience);
}

int CTradePanel::PurchaseCost() {
    PartyUnitRecord* hero = owner_->HeroRecord();
    return static_cast<int>(static_cast<double>(StatsOf(hero)->spentExperience) - Record().spentExperience);
}

// --- Draw ------------------------------------------------------------------------------

void CMpHeroScreen::DrawExperience() {
    CUiSurface* surface = surfaces_[2];
    std::string text;
    std::string line;
    UiSurfaceGetDC(surface);
    surface->Clear();
    g_clientWorld->texts_->CTextManager::GetString(kLevelUpKey, &text);
    RECT title = {0, 130, 200, 150};
    surface->DrawTextIn(text, &title, kTextFormat, kWhite, 1, 1);
    text = str::Format(kSignedFormat, -tradePanel_->PurchaseCost());
    RECT cost = {0, 150, 200, 170};
    surface->DrawTextIn(text, &cost, kTextFormat, kWhite, 1, 1);
    g_clientWorld->texts_->CTextManager::GetString(kCurrentExpKey, &text);
    line = str::Format(kLabelFormat, text.c_str(), tradePanel_->Experience());
    RECT current = {0, 110, 200, 130};
    surface->DrawTextIn(line, &current, kTextFormat, kWhite, 1, 1);
    UiSurfaceReleaseDC(surface);
}

BOOL CMpHeroScreen::Draw(CSceneRenderer* pass) {
    if (mode_ == kModeStats) DrawExperience();
    int x = g_cursor->position_.x;
    int y = g_cursor->position_.y;
    hoveredItem_ = nullptr;
    int topic = 0;
    hoveredItem_ = weaponPanel_->ItemAt(x, y, &topic);
    if (!hoveredItem_ && topic == 0) hoveredItem_ = spellPanel_->ItemAt(x, y, &topic);

    if (hoveredItem_) {
        if (descPanel_) descPanel_->Describe(hoveredItem_);
    } else if (topic != 0) {
        if (descPanel_) descPanel_->ShowHelp(topic);
    } else {
        // Only the kind is set; the other fields stay as the stack had them (ours 0).
        ItemHoverInfo info = {};
        info.kind = -1;
        if (!tradePanel_->enabled_ || (tradePanel_->HoverInfo(x, y, &info), info.kind < 0)) {
            if (statsPanel_->enabled_) statsPanel_->HoverInfo(x, y, &info);
            if (info.kind < 0 && mode_ == kModeNewHero) {
                int region = RegionAt(x, y);
                if (region >= kFirstVoiceRegion && region < kVoiceRegionEnd) {
                    info.kind = kVoiceHintKind;
                    info.text = kSelectVoiceKey;
                }
            }
        }
        if (descPanel_) descPanel_->DescribeStats(&info);
    }

    CUiContainer::Draw(pass);
    if (mode_ == kModeList && heroIndex_ >= 0)
        DrawHeroPreview(pass, previewModel_, turn_, kListDepth, 0);
    else if (mode_ == kModeNewHero)
        DrawHeroPreview(pass, previewModel_, turn_, kNewHeroDepth, 0);
    return 1;
}

// --- Update ----------------------------------------------------------------------------

void CMpHeroScreen::Update() {
    UpdateBase();
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
    const UnitStats* stats = StatsOf(HeroRecord());
    double points = static_cast<double>(stats->intelligenceBase) + stats->dexterityBase + stats->strengthBase;
    float target = NewHeroPoints();
    EI_NAN_AUDIT(points, target);
    bool equal = points == target;
    bool ready = equal && static_cast<int>(name.size()) != 0;
    float shade = ready ? 1.0f : 0.5f;
    auto* button = SceneMesh(kCreateButton);
    button->tint_[0] = shade;
    button->tint_[1] = shade;
    button->tint_[2] = shade;
}

// --- Close -----------------------------------------------------------------------------

void CMpHeroScreen::Close() {
    g_savedParties->SaveAll();
    std::vector<PartyUnitRecord*> records = {};
    auto* party = LocalPlayerData();
    for (int i = 0; i < std::ssize(party->members_); ++i) {
        PartyUnitRecord* record = &party->members_[i];
        int size = std::ssize(records);
        records.resize(size + 1);
        records[size] = record;
    }
    GameRemoveUnitsOf(&records);
    previewModel_ = nullptr;
    descPanel_->Describe(nullptr);
}

// --- The caches' objects released ----------------------------------------------------------

template <typename Object>
void ReleaseCachedObjects(ItemObjectCache<Object>* cache) {
    for (auto& entry : cache->map) entry.second.object->Release();
}
template void ReleaseCachedObjects<CUiMesh>(ItemModelCache*);
template void ReleaseCachedObjects<CTexture>(ItemTextureCache*);

// Built inline by the main menu's Multiplayer click: the item screen, both caches
// the name vectors, the text and the voice lists empty.
CMpHeroScreen::CMpHeroScreen(CInterface3D* ui) : CItemScreen(ui) {}

CMpHeroScreen::~CMpHeroScreen() {}

// --- The hero list and the voices drawn -----------------------------------------------------

void CMpHeroScreen::DrawHeroList() {
    CUiSurface* surface = surfaces_[0];
    UiSurfaceGetDC(surface);
    surface->Clear();
    SelectionFrame(*this)->visible_ = 0;
    RECT rect = {12, 13, 265, 33};
    std::uint32_t index = static_cast<std::uint32_t>(heroTop_);
    for (int y = 262; y < 502; y += 24, ++index) {
        if (index >= MpHeroCount()) break;
        if (static_cast<int>(index) == heroIndex_) {
            SelectionFrame(*this)->visible_ = 1;
            SelectionFrame(*this)->Place(240.0f, static_cast<float>(y), kFrameDepth);
        }
        const char* name = MpHero(index)->members_[0].texts[0].c_str();
        surface->DrawTextIn(name, &rect, 1, kListColour, kListFormat, 1);
        gameapi::OffsetRect(&rect, 0, 24);
    }
    UiSurfaceReleaseDC(surface);
}

void CMpHeroScreen::DrawVoices() {
    CUiSurface* surface = surfaces_[2];
    UiSurfaceGetDC(surface);
    surface->Clear();
    SelectionFrame(*this)->visible_ = 0;
    std::string title;
    RECT rect = {10, 5, 190, 20};
    g_clientWorld->texts_->CTextManager::GetString(kVoicesKey, &title);
    surface->DrawTextIn(title, &rect, 0, kWhite, 1, 1);
    rect = {20, 25, 175, 40};
    std::uint32_t index = static_cast<std::uint32_t>(voiceTop_);
    for (int offset = 0; offset < 165; offset += 15, ++index) {
        std::vector<MpVoiceEntry>& voices = *voices_;
        if (index >= static_cast<std::uint32_t>(voices.end() - voices.begin())) break;
        if (static_cast<int>(index) == voiceIndex_) {
            SelectionFrame(*this)->visible_ = 1;
            float y = static_cast<float>(static_cast<double>(offset) + VoiceFrameTop());
            SelectionFrame(*this)->Place(97.5f, y, kFrameDepth);
        }
        surface->DrawTextIn(voices.begin()[index].name, &rect, 0, kListColour, kListFormat, 1);
        gameapi::OffsetRect(&rect, 0, 15);
    }
    UiSurfaceReleaseDC(surface);
}

// --- The preview's unit -------------------------------------------------------------------

void CMpHeroScreen::MakePreview(int flag) {
    previewModel_ = nullptr;
    std::vector<PartyUnitRecord*> records = {};
    auto* player = LocalPlayerData();
    if (std::ssize(player->members_) == 0) {
        player->Clear(1, 0);
        player->AddMember(kHeroName, kHumanHero, game::kEmptyText);
    }
    PartyUnitRecord* first = player->members_.data();
    int size = std::ssize(records);
    records.resize(size + 1);
    records[size] = first;
    GameCreatePartyUnits(&records, flag);
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
        controller->ShowWeapon(0xFFFFFFFF);
        controller->RefreshModel();
        double now = x87::FromUnsigned(gameapi::timeGetTime()) * 0.0009765625f * 15.0f;
        controller->RestartAnimation(static_cast<float>(now - 1.0f));
        unit->SetFromStats(&player->members_.data()->stats);
        unit->Controller()->RefreshWounds();
    }
}

// --- A hero shown -------------------------------------------------------------------------

void CMpHeroScreen::ShowHero(int index) {
    std::vector<PartyUnitRecord*> records = {};
    auto* player = LocalPlayerData();
    if (std::ssize(player->members_) > 0) {
        int size = std::ssize(records);
        records.resize(size + 1);
        records[size] = player->members_.data();
        GameRemoveUnitsOf(&records);
    }
    heroIndex_ = -1;
    if (index >= 0 && static_cast<std::uint32_t>(index) < MpHeroCount()) {
        heroIndex_ = index;
        player->Assign(MpHero(index));
        g_savedParties->Select(SavedHero(static_cast<int>(index)));
    }
    MakePreview(1);
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
