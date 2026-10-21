// The multiplayer hero screen's per-frame draw (006159E0) and update (00617820), its close
// (00615850), and the trade panel's experience figures it shows (00625530 / 00625560).
// See docs/notes/ui.md, section 5.
#include "ui.h"
#include "game_strings.h"
#include "world_glue.h"
#include "ui_screens.h"
#include "ui_camp.h"
#include "ui_item_info.h"

#include "core_maths.h"
#include "item_db_text.h"
#include "map_object_figures.h"
#include "random.h"
#include "unit.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_gameplay.h"
#include "originals_units.h"
#include "lib/gameapi.h"
#include "lib/stlp.h"
#include "party.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cmath>
#include <cstdint>

namespace {

const char* const kLevelUpKey = "camp_level_up";
const char* const kSignedFormat = "%+d";
const char* const kCurrentExpKey = "camp_current_exp";
const char* const kLabelFormat = "%s %d";
const char* const kSelectVoiceKey = "lmp_select_voice";

// The points a new hero's three attributes must add up to before it can be created.
float NewHeroPoints() { return 75.0f /*00741C98*/; }
// The preview's full turn (2 pi), the modulus of its angle.
float FullTurn() { return ei::GameData<const volatile float>(0x007C7F10); }
// The multiplayer heroes object (its vector at +4 is MpHeroes()).
void* MpHeroesObject() { return ei::GamePointer<void>(0x007C82A0); }
// CArray<record*> (the party's records handed to the client world).
void* RecordArrayVtable() { return ei::GamePointer<void>(0x0073FAA4); }

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

UnitStats* StatsOf(char* hero) { return reinterpret_cast<UnitStats*>(reinterpret_cast<PartyUnitRecord*>(hero)->stats); }

} // namespace

// --- 00625530 / 00625560: the trade panel's experience figures ------------------------------------

int CTradePanel::Experience() const {
    const TradeRecord& record = Record();
    return static_cast<int>(static_cast<double>(record.pendingExperience) + record.experience - record.spentExperience);
}
EI_REPLACE_METHOD(0x00625530, CTradePanel::Experience);

int CTradePanel::PurchaseCost() {
    using HeroFn = char*(__thiscall*)(CUiWindow*);
    char* hero = ei::VirtualSlot<HeroFn>(owner_->vtable_, 51)(owner_);
    return static_cast<int>(static_cast<double>(StatsOf(hero)->spentExperience) - Record().spentExperience);
}
EI_REPLACE_METHOD(0x00625560, CTradePanel::PurchaseCost);

// --- 006159E0: Draw (slot 20) ------------------------------------------------------------------

void CMpHeroScreen::DrawExperience() {
    CUiSurface* surface = window.surfaces_.data[2];
    mfc::CString text;
    mfc::CString line;
    UiSurfaceGetDC(surface);
    SurfaceClearOriginal(surface);
    TextsGetString(TheTextManager(), nullptr, kLevelUpKey, &text);
    RECT title = {0, 130, 200, 150};
    SurfaceDrawText(surface, nullptr, text, &title, kTextFormat, kWhite, 1, 1);
    text.Format(kSignedFormat, -tradePanel_->PurchaseCost());
    RECT cost = {0, 150, 200, 170};
    SurfaceDrawText(surface, nullptr, text, &cost, kTextFormat, kWhite, 1, 1);
    TextsGetString(TheTextManager(), nullptr, kCurrentExpKey, &text);
    line.Format(kLabelFormat, static_cast<const char*>(text), tradePanel_->Experience());
    RECT current = {0, 110, 200, 130};
    SurfaceDrawText(surface, nullptr, line, &current, kTextFormat, kWhite, 1, 1);
    UiSurfaceReleaseDC(surface);
}

BOOL CMpHeroScreen::Draw(int pass) {
    if (mode_ == kModeStats) DrawExperience();
    int x = MouseScreenX();
    int y = MouseScreenY();
    hoveredItem_ = nullptr;
    int topic = 0;
    hoveredItem_ = static_cast<CUiWindow*>(weaponPanel_)->VirtualItemAt(x, y, &topic);
    if (!hoveredItem_ && topic == 0) hoveredItem_ = static_cast<CUiWindow*>(spellPanel_)->VirtualItemAt(x, y, &topic);

    if (hoveredItem_) {
        if (descPanel_) static_cast<CItemInfoPanel*>(descPanel_)->Describe(reinterpret_cast<CItem*>(hoveredItem_));
    } else if (topic != 0) {
        if (descPanel_) static_cast<CItemInfoPanel*>(descPanel_)->ShowHelp(topic);
    } else {
        // The original sets only the kind; the other fields stay as the stack had them (ours 0).
        ItemHoverInfo info = {};
        info.kind = -1;
        if (!tradePanel_->enabled_ || (tradePanel_->HoverInfo(x, y, &info), info.kind < 0)) {
            if (statsPanel_->enabled_) statsPanel_->HoverInfo(x, y, &info);
            if (info.kind < 0 && mode_ == kModeNewHero) {
                int region = window.RegionAt(x, y);
                if (region >= kFirstVoiceRegion && region < kVoiceRegionEnd) {
                    info.kind = kVoiceHintKind;
                    info.text = kSelectVoiceKey;
                }
            }
        }
        if (descPanel_) descPanel_->DescribeStats(&info);
    }

    reinterpret_cast<CUiContainer*>(&window)->DrawContainer(pass);
    if (mode_ == kModeList && heroIndex_ >= 0)
        window.DrawHeroPreview(reinterpret_cast<CSceneRenderer*>(pass), previewModel_, turn_, kListDepth, 0);
    else if (mode_ == kModeNewHero)
        window.DrawHeroPreview(reinterpret_cast<CSceneRenderer*>(pass), previewModel_, turn_, kNewHeroDepth, 0);
    return 1;
}
EI_REPLACE_METHOD(0x006159E0, CMpHeroScreen::Draw);

// --- 00617820: Update (slot 24) ----------------------------------------------------------------

void CMpHeroScreen::Update() {
    UiUpdateChildrenOriginal(&window);
    if (rotating_ == 1 || rotating_ == 2) {
        double step = WindowFrameTimeOriginal(&window);
        step += step;
        double angle = rotating_ == 1 ? step + turn_ : turn_ - step;
        turn_ = static_cast<float>(std::fmod(angle, static_cast<double>(FullTurn())));
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

    mfc::CString name(static_cast<CUiEditField*>(nameEdit_)->text_);
    const UnitStats* stats = StatsOf(Hero());
    double points = static_cast<double>(stats->stat20Base) + stats->stat18Base + stats->stat10Base;
    // FCOMP; TEST AH,0x40: equal.
    float target = NewHeroPoints();
    EI_NAN_AUDIT(points, target);
    bool equal = points == target;
    bool ready = equal && name.GetLength() != 0;
    float shade = ready ? 1.0f : 0.5f;
    auto* button = reinterpret_cast<CUiMesh*>(window.sceneObjects_.data[kCreateButton]);
    button->tint_[0] = shade;
    button->tint_[1] = shade;
    button->tint_[2] = shade;
}
EI_REPLACE_METHOD(0x00617820, CMpHeroScreen::Update);

// --- 00615850: Close (slot 26) -----------------------------------------------------------------

void CMpHeroScreen::Close() {
    static_cast<SavedParties*>(MpHeroesObject())->SaveAll();
    RecordPtrArray records = {RecordArrayVtable(), nullptr, 0, 0, 0};
    auto* party = reinterpret_cast<PartyRecords*>(LocalPlayerData());
    for (int i = 0; i < party->count; ++i) {
        auto* record = reinterpret_cast<std::uint8_t*>(&party->records[i]);
        int size = records.size;
        records.SetSize(size + 1, -1);
        records.data[size] = record;
    }
    GameRemoveUnitsOf(&records);
    previewModel_ = nullptr;
    static_cast<CItemInfoPanel*>(descPanel_)->Describe(nullptr);
    if (records.data) gamecrt::operator_delete(records.data);
}
EI_REPLACE_METHOD(0x00615850, CMpHeroScreen::Close);

// --- 00615800: a voice entry's destructor -------------------------------------------------------

void MpVoiceEntry::Destroy() {
    CStringDestroy(&name);
    CStringDestroy(&file);
}
namespace {
void __fastcall VoiceEntryDestroy(MpVoiceEntry* self) { self->Destroy(); }
} // namespace
EI_REPLACE(0x00615800, VoiceEntryDestroy);

// --- 00634500 / 00634560: the caches' objects released ----------------------------------------------

void __fastcall ReleaseCachedObjects(ItemObjectCache* cache) {
    for (ItemObjectCache::Node* node = cache->map.begin(); node != cache->map.end(); node = ItemObjectCache::Map::Next(node))
        static_cast<CEngineObject*>(node->second.object)->Release();
}
EI_REPLACE(0x00634500, ReleaseCachedObjects);
EI_REPLACE(0x00634560, ReleaseCachedObjects);

// --- 00633260: the destructor body -------------------------------------------------------------

namespace {
void DestroyVoices(MpVoiceVector& voices) {
    for (MpVoiceEntry* entry = voices.begin(); entry != voices.end(); ++entry) entry->Destroy();
    stlp::deallocate(voices.begin(), static_cast<std::size_t>(voices.EndOfStorage() - voices.begin()) * sizeof(MpVoiceEntry));   // 006344C0
}
void DestroyStrings(const char** begin, const char** end) {
    for (const char** at = begin; at != end; ++at) CStringDestroy(at);
}
} // namespace

void CMpHeroScreen::Destroy() {
    auto* screen = reinterpret_cast<CItemScreen*>(this);
    DestroyVoices(voiceLists_[1]);
    DestroyVoices(voiceLists_[0]);
    CStringDestroy(&text_);
    DestroyStrings(names_.begin(), names_.end());
    stlp::deallocate(names_.begin(), static_cast<std::size_t>(names_.EndOfStorage() - names_.begin()) * sizeof(void*));   // 00520F60
    for (void** at = defaults_; at != defaultsEnd_; ++at) CStringDestroy(at);
    stlp::deallocate(defaults_, static_cast<std::size_t>(defaultsCapacity_ - defaults_) * sizeof(void*));   // 00520F20
    ReleaseCachedObjects(&screen->textures_);
    screen->textures_.map.Destroy();   // 00634200
    ReleaseCachedObjects(&screen->models_);
    screen->models_.map.Destroy();     // 00634160
    DestroyContainerBody(this);
}
namespace {
void __fastcall HeroScreenDestroy(CMpHeroScreen* self) { self->Destroy(); }
} // namespace
EI_REPLACE(0x00633260, HeroScreenDestroy);

// --- 006167C0 / 00616560: the hero list and the voices drawn -----------------------------------------

namespace {
const char* const kVoicesKey = "lmp_voices";
// The voice rows' first y offset of the selection frame.
float VoiceFrameTop() { return 332.5f /*00741C94*/; }
constexpr int kSelectionFrame = 2;           // scene object
constexpr std::uint32_t kListColour = 0xA7D7E4;
constexpr int kListFormat = 0x800;
constexpr float kFrameDepth = 3.0f;

CUiMesh* SelectionFrame(CUiWindow& window) {
    return reinterpret_cast<CUiMesh*>(window.sceneObjects_.data[kSelectionFrame]);
}
// A multiplayer hero record (a party, 0x74 bytes) and its name: its first member's text 0.
CParty* MpHero(std::uint32_t index) { return reinterpret_cast<CParty*>(MpHeroes().begin + index * kMpHeroSize); }
std::uint32_t MpHeroCount() {
    return static_cast<std::uint32_t>(static_cast<int>(MpHeroes().end - MpHeroes().begin) / kMpHeroSize);
}
} // namespace

void CMpHeroScreen::DrawHeroList() {
    CUiSurface* surface = window.surfaces_.data[0];
    UiSurfaceGetDC(surface);
    SurfaceClearOriginal(surface);
    SelectionFrame(window)->visible_ = 0;
    RECT rect = {12, 13, 265, 33};
    std::uint32_t index = static_cast<std::uint32_t>(heroTop_);
    for (int y = 262; y < 502; y += 24, ++index) {
        if (index >= MpHeroCount()) break;
        if (static_cast<int>(index) == heroIndex_) {
            SelectionFrame(window)->visible_ = 1;
            SelectionFrame(window)->Place(240.0f, static_cast<float>(y), kFrameDepth);
        }
        const char* name = *reinterpret_cast<const char* const*>(MpHero(index)->members_.data);
        SurfaceDrawText(surface, nullptr, name, &rect, 1, kListColour, kListFormat, 1);
        gameapi::OffsetRect(&rect, 0, 24);
    }
    UiSurfaceReleaseDC(surface);
}
namespace {
void __fastcall HeroScreenDrawHeroList(CMpHeroScreen* self) { self->DrawHeroList(); }
} // namespace
EI_REPLACE(0x006167C0, HeroScreenDrawHeroList);

void CMpHeroScreen::DrawVoices() {
    CUiSurface* surface = window.surfaces_.data[2];
    UiSurfaceGetDC(surface);
    SurfaceClearOriginal(surface);
    SelectionFrame(window)->visible_ = 0;
    mfc::CString title;
    RECT rect = {10, 5, 190, 20};
    TextsGetString(TheTextManager(), nullptr, kVoicesKey, &title);
    SurfaceDrawText(surface, nullptr, title, &rect, 0, kWhite, 1, 1);
    rect = {20, 25, 175, 40};
    std::uint32_t index = static_cast<std::uint32_t>(voiceTop_);
    for (int offset = 0; offset < 165; offset += 15, ++index) {
        MpVoiceVector& voices = Voices();
        int bytes = static_cast<int>(reinterpret_cast<char*>(voices.end()) - reinterpret_cast<char*>(voices.begin()));
        if (index >= static_cast<std::uint32_t>(bytes >> 3)) break;
        if (static_cast<int>(index) == voiceIndex_) {
            SelectionFrame(window)->visible_ = 1;
            float y = static_cast<float>(static_cast<double>(offset) + VoiceFrameTop());
            SelectionFrame(window)->Place(97.5f, y, kFrameDepth);
        }
        SurfaceDrawText(surface, nullptr, voices.begin()[index].name, &rect, 0, kListColour, kListFormat, 1);
        gameapi::OffsetRect(&rect, 0, 15);
    }
    UiSurfaceReleaseDC(surface);
}
namespace {
void __fastcall HeroScreenDrawVoices(CMpHeroScreen* self) { self->DrawVoices(); }
} // namespace
EI_REPLACE(0x00616560, HeroScreenDrawVoices);

// --- 00616E20: the preview's unit -------------------------------------------------------------

namespace {
const char* const kHeroName = "Hero";
const char* const kHumanHero = "Human Hero";
constexpr int kPreviewAnimation = 0x21;      // the skill animation the preview plays
} // namespace

void CMpHeroScreen::MakePreview(int flag) {
    previewModel_ = nullptr;
    RecordPtrArray records = {RecordArrayVtable(), nullptr, 0, 0, 0};
    auto* player = reinterpret_cast<CParty*>(LocalPlayerData());
    if (player->members_.size == 0) {
        player->Clear(1, 0);
        player->AddMember(kHeroName, kHumanHero, game::EmptyText());
    }
    PartyUnitRecord* first = player->members_.data;
    int size = records.size;
    records.SetSize(size + 1, -1);
    records.data[size] = reinterpret_cast<std::uint8_t*>(first);
    GameCreatePartyUnits(&records, flag);
    auto* unit = static_cast<CUnit*>(GameFindClientUnit(player->members_.data->unitId));
    unit->SetPrototype23C(unit->GetPrototype());
    previewModel_ = unit;
    if (unit) {
        FigureAnimation* animation = reinterpret_cast<CObject3D*>(unit)->FindSkillAnimation(kPreviewAnimation);
        if (animation) {
            std::uint32_t random = IsaacRandom::Global().Next();
            double frames = x87::FromUnsigned(gameapi::timeGetTime()) * 0.0009765625f * 15.0f;
            int start = static_cast<int>(frames - static_cast<int>(random % 51));
            unit->SetAnimationEntries(-1, static_cast<std::int8_t>(animation->index), start, 0);
        }
        CUnitController* controller = unit->Controller();
        controller->ShowWeapon(0xFFFFFFFF);
        controller->RefreshModel();
        double now = x87::FromUnsigned(gameapi::timeGetTime()) * 0.0009765625f * 15.0f;
        controller->RestartAnimation(static_cast<float>(now - 1.0f));
        unit->SetFromStats(reinterpret_cast<UnitStats*>(player->members_.data->stats));
        unit->Controller()->RefreshWounds();
    }
    if (records.data) gamecrt::operator_delete(records.data);
}
EI_REPLACE_METHOD(0x00616E20, CMpHeroScreen::MakePreview);

// --- 00616910: a hero shown -------------------------------------------------------------------

void CMpHeroScreen::ShowHero(int index) {
    RecordPtrArray records = {RecordArrayVtable(), nullptr, 0, 0, 0};
    auto* player = reinterpret_cast<CParty*>(LocalPlayerData());
    if (player->members_.size > 0) {
        int size = records.size;
        records.SetSize(size + 1, -1);
        records.data[size] = reinterpret_cast<std::uint8_t*>(player->members_.data);
        GameRemoveUnitsOf(&records);
    }
    heroIndex_ = -1;
    if (index >= 0 && static_cast<std::uint32_t>(index) < MpHeroCount()) {
        heroIndex_ = index;
        static_cast<CParty*>(player)->Assign(static_cast<CParty*>(MpHero(index)));
        static_cast<MpHeroRoster*>(MpHeroesObject())->Select(reinterpret_cast<const MpHeroRecord*>(MpHero(index)));   // the record starts with its party
    }
    MakePreview(1);
    auto* hero = reinterpret_cast<PartyUnitRecord*>(Hero());
    auto* unit = static_cast<CUnit*>(GameFindClientUnit(hero->unitId));
    for (int slot = 0; slot < 4; ++slot) unit->RemoveWeapon(slot);
    for (int i = 0; i < static_cast<int>(hero->weaponCount); ++i) static_cast<CUnit*>(unit)->AddWeapon(hero->weapons[i]);
    unit->SendEquipment();
    unit->Controller()->ShowWeapon(hero->weaponIndex);
    unit->Controller()->RefreshModel();
    turn_ = 0.0f;
    DrawHeroList();
    if (records.data) gamecrt::operator_delete(records.data);
}
EI_REPLACE_METHOD(0x00616910, CMpHeroScreen::ShowHero);
