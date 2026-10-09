// The multiplayer hero screen's per-frame draw (006159E0) and update (00617820), its close
// (00615850), and the trade panel's experience figures it shows (00625530 / 00625560).
// See docs/notes/ui.md, section 5.
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
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_gameplay.h"
#include "originals_units.h"
#include "lib/gameapi.h"
#include "party.h"

#include <cmath>
#include <cstdint>

namespace {

const char* const kLevelUpKey = reinterpret_cast<const char*>(0x0079386C);       // "camp_level_up"
const char* const kSignedFormat = reinterpret_cast<const char*>(0x00793818);     // "%+d"
const char* const kCurrentExpKey = reinterpret_cast<const char*>(0x00793858);    // "camp_current_exp"
const char* const kLabelFormat = reinterpret_cast<const char*>(0x00792F14);      // "%s %d"
const char* const kSelectVoiceKey = reinterpret_cast<const char*>(0x00793A10);   // "lmp_select_voice"

// The points a new hero's three attributes must add up to before it can be created.
float NewHeroPoints() { return *reinterpret_cast<const volatile float*>(0x00741C98); }
// The preview's full turn (2 pi), the modulus of its angle.
float FullTurn() { return *reinterpret_cast<const volatile float*>(0x007C7F10); }
// The multiplayer heroes object (its vector at +4 is MpHeroes()).
void* MpHeroesObject() { return reinterpret_cast<void*>(0x007C82A0); }
// The client glue the record hand-over is called on (not read).
void* ClientGlue() { return reinterpret_cast<void*>(0x007AFEA5); }
// CArray<record*> (the party's records handed to the client world).
void* RecordArrayVtable() { return reinterpret_cast<void*>(0x0073FAA4); }

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
    char* hero = reinterpret_cast<HeroFn>(owner_->vtable_[51])(owner_);
    return static_cast<int>(static_cast<double>(StatsOf(hero)->spentExperience) - Record().spentExperience);
}
EI_REPLACE_METHOD(0x00625560, CTradePanel::PurchaseCost);

// --- 006159E0: Draw (slot 20) ------------------------------------------------------------------

void CMpHeroScreen::DrawExperience() {
    CUiSurface* surface = window.surfaces_.data[2];
    mfc::CString text;
    mfc::CString line;
    SurfaceGetDC(surface);
    SurfaceClearOriginal(surface);
    TextsGetString(GameTexts(), nullptr, kLevelUpKey, &text);
    RECT title = {0, 130, 200, 150};
    SurfaceDrawText(surface, nullptr, text, &title, kTextFormat, kWhite, 1, 1);
    text.Format(kSignedFormat, -TradePanelPurchaseCostOriginal(tradePanel_));
    RECT cost = {0, 150, 200, 170};
    SurfaceDrawText(surface, nullptr, text, &cost, kTextFormat, kWhite, 1, 1);
    TextsGetString(GameTexts(), nullptr, kCurrentExpKey, &text);
    line.Format(kLabelFormat, static_cast<const char*>(text), TradePanelExperienceOriginal(tradePanel_));
    RECT current = {0, 110, 200, 130};
    SurfaceDrawText(surface, nullptr, line, &current, kTextFormat, kWhite, 1, 1);
    SurfaceReleaseDC(surface);
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
        if (descPanel_) InfoPanelDescribeOriginal(descPanel_, nullptr, hoveredItem_);
    } else if (topic != 0) {
        if (descPanel_) InfoPanelShowHelpOriginal(descPanel_, nullptr, topic);
    } else {
        // The original sets only the kind; the other fields stay as the stack had them (ours 0).
        ItemHoverInfo info = {};
        info.kind = -1;
        auto* trade = static_cast<CUiWindow*>(tradePanel_);
        auto* stats = static_cast<CUiWindow*>(statsPanel_);
        if (!trade->enabled_ || (TradePanelHoverInfoOriginal(trade, nullptr, x, y, &info), info.kind < 0)) {
            if (stats->enabled_) StatsPanelHoverInfoOriginal(stats, nullptr, x, y, &info);
            if (info.kind < 0 && mode_ == kModeNewHero) {
                int region = window.RegionAt(x, y);
                if (region >= kFirstVoiceRegion && region < kVoiceRegionEnd) {
                    info.kind = kVoiceHintKind;
                    info.text = kSelectVoiceKey;
                }
            }
        }
        if (descPanel_) InfoPanelDescribeStatsOriginal(descPanel_, nullptr, &info);
    }

    reinterpret_cast<CUiContainer*>(&window)->DrawContainer(pass);
    if (mode_ == kModeList && heroIndex_ >= 0)
        WindowDrawHeroPreviewOriginal(&window, nullptr, pass, previewModel_, turn_, kListDepth, 0);
    else if (mode_ == kModeNewHero)
        WindowDrawHeroPreviewOriginal(&window, nullptr, pass, previewModel_, turn_, kNewHeroDepth, 0);
    return 1;
}
namespace {
BOOL __fastcall HeroScreenDraw(CMpHeroScreen* self, void*, int pass) { return self->Draw(pass); }
} // namespace
EI_REPLACE(0x006159E0, HeroScreenDraw);

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
    if (ScrollbarTakePositionOriginal(heroScroll_, nullptr, &position)) {
        heroTop_ = position;
        CallDrawHeroList();
    }
    if (ScrollbarTakePositionOriginal(voiceScroll_, nullptr, &position)) {
        voiceTop_ = position;
        if (mode_ != kModeNewHero) return;
        CallDrawVoices();
    }
    if (mode_ != kModeNewHero) return;

    mfc::CString name(static_cast<CUiEditField*>(nameEdit_)->text_);
    const UnitStats* stats = StatsOf(Hero());
    double points = static_cast<double>(stats->stat20Base) + stats->stat18Base + stats->stat10Base;
    // FCOMP; TEST AH,0x40: equal, or unordered.
    float target = NewHeroPoints();
    bool equal = x87::IsNaN(static_cast<float>(points)) || x87::IsNaN(target) || points == target;
    bool ready = equal && name.GetLength() != 0;
    float shade = ready ? 1.0f : 0.5f;
    auto* button = reinterpret_cast<CUiMesh*>(window.sceneObjects_.data[kCreateButton]);
    button->tint_[0] = shade;
    button->tint_[1] = shade;
    button->tint_[2] = shade;
}
namespace {
void __fastcall HeroScreenUpdate(CMpHeroScreen* self, void*) { self->Update(); }
} // namespace
EI_REPLACE(0x00617820, HeroScreenUpdate);

// --- 00615850: Close (slot 26) -----------------------------------------------------------------

void CMpHeroScreen::Close() {
    MpHeroesSaveOriginal(MpHeroesObject());
    RecordPtrArray records = {RecordArrayVtable(), nullptr, 0, 0, 0};
    auto* party = reinterpret_cast<PartyRecords*>(LocalPlayerData());
    for (int i = 0; i < party->count; ++i) {
        auto* record = reinterpret_cast<std::uint8_t*>(&party->records[i]);
        int size = records.size;
        RecordArraySetSizeOriginal(&records, nullptr, size + 1, -1);
        records.data[size] = record;
    }
    GameTakeUnitRecordsOriginal(ClientGlue(), nullptr, &records);
    previewModel_ = nullptr;
    InfoPanelDescribeOriginal(descPanel_, nullptr, nullptr);
    if (records.data) gamecrt::operator_delete(records.data);
}
namespace {
void __fastcall HeroScreenClose(CMpHeroScreen* self, void*) { self->Close(); }
} // namespace
EI_REPLACE(0x00615850, HeroScreenClose);

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

ItemObjectCache::Node* ItemObjectCache::Node::Next() {
    Node* node = this;
    if (node->right) {
        node = node->right;
        while (node->left) node = node->left;
        return node;
    }
    Node* up = node->parent;
    while (node == up->right) {
        node = up;
        up = up->parent;
    }
    return node->right != up ? up : node;
}

void __fastcall ReleaseCachedObjects(ItemObjectCache* cache) {
    for (ItemObjectCache::Node* node = cache->header->left; node != cache->header; node = node->Next())
        static_cast<CEngineObject*>(node->object)->Release();
}
EI_REPLACE(0x00634500, ReleaseCachedObjects);
EI_REPLACE(0x00634560, ReleaseCachedObjects);

// --- 00633260: the destructor body -------------------------------------------------------------

namespace {
void DestroyVoices(MpVoiceVector& voices) {
    for (MpVoiceEntry* entry = voices.begin; entry != voices.end; ++entry) entry->Destroy();
    FreeBlock8Original(voices.begin, static_cast<int>(voices.capacity - voices.begin));
}
void DestroyStrings(const char** begin, const char** end) {
    for (const char** at = begin; at != end; ++at) CStringDestroy(at);
}
// 00634500 / 00634560 through their address (their wrappers run).
__declspec(naked) void __fastcall ReleaseModelsAt(ItemObjectCache*) EI_ORIGINAL(0x00634500)
__declspec(naked) void __fastcall ReleaseTexturesAt(ItemObjectCache*) EI_ORIGINAL(0x00634560)
} // namespace

void CMpHeroScreen::Destroy() {
    auto* screen = reinterpret_cast<CItemScreen*>(this);
    DestroyVoices(voiceLists_[1]);
    DestroyVoices(voiceLists_[0]);
    CStringDestroy(&text_);
    DestroyStrings(names_.begin, names_.end);
    FreeBlock4Original(names_.begin, static_cast<int>(names_.capacity - names_.begin));
    for (void** at = defaults_; at != defaultsEnd_; ++at) CStringDestroy(at);
    FreeBlock4ProxyOriginal(&defaultsCapacity_, nullptr, defaults_, static_cast<int>(defaultsCapacity_ - defaults_));
    ReleaseTexturesAt(&screen->textures_);
    DestroyTextureMapOriginal(&screen->textures_);
    ReleaseModelsAt(&screen->models_);
    DestroyModelMapOriginal(&screen->models_);
    DestroyContainerBody(this);
}
namespace {
void __fastcall HeroScreenDestroy(CMpHeroScreen* self) { self->Destroy(); }
} // namespace
EI_REPLACE(0x00633260, HeroScreenDestroy);

// --- 006167C0 / 00616560: the hero list and the voices drawn -----------------------------------------

namespace {
const char* const kVoicesKey = reinterpret_cast<const char*>(0x00793A24);   // "lmp_voices"
// The voice rows' first y offset of the selection frame.
float VoiceFrameTop() { return *reinterpret_cast<const volatile float*>(0x00741C94); }
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
    SurfaceGetDC(surface);
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
    SurfaceReleaseDC(surface);
}
namespace {
void __fastcall HeroScreenDrawHeroList(CMpHeroScreen* self) { self->DrawHeroList(); }
} // namespace
EI_REPLACE(0x006167C0, HeroScreenDrawHeroList);

void CMpHeroScreen::DrawVoices() {
    CUiSurface* surface = window.surfaces_.data[2];
    SurfaceGetDC(surface);
    SurfaceClearOriginal(surface);
    SelectionFrame(window)->visible_ = 0;
    mfc::CString title;
    RECT rect = {10, 5, 190, 20};
    TextsGetString(GameTexts(), nullptr, kVoicesKey, &title);
    SurfaceDrawText(surface, nullptr, title, &rect, 0, kWhite, 1, 1);
    rect = {20, 25, 175, 40};
    std::uint32_t index = static_cast<std::uint32_t>(voiceTop_);
    for (int offset = 0; offset < 165; offset += 15, ++index) {
        MpVoiceVector& voices = Voices();
        int bytes = static_cast<int>(reinterpret_cast<char*>(voices.end) - reinterpret_cast<char*>(voices.begin));
        if (index >= static_cast<std::uint32_t>(bytes >> 3)) break;
        if (static_cast<int>(index) == voiceIndex_) {
            SelectionFrame(window)->visible_ = 1;
            float y = static_cast<float>(static_cast<double>(offset) + VoiceFrameTop());
            SelectionFrame(window)->Place(97.5f, y, kFrameDepth);
        }
        SurfaceDrawText(surface, nullptr, voices.begin[index].name, &rect, 0, kListColour, kListFormat, 1);
        gameapi::OffsetRect(&rect, 0, 15);
    }
    SurfaceReleaseDC(surface);
}
namespace {
void __fastcall HeroScreenDrawVoices(CMpHeroScreen* self) { self->DrawVoices(); }
} // namespace
EI_REPLACE(0x00616560, HeroScreenDrawVoices);

// --- 00616E20: the preview's unit -------------------------------------------------------------

namespace {
const char* const kHeroName = reinterpret_cast<const char*>(0x0078B0F4);         // "Hero"
const char* const kHumanHero = reinterpret_cast<const char*>(0x0078B0FC);        // "Human Hero"
const char* const kEmptyText = reinterpret_cast<const char*>(0x0079B298);        // ""
constexpr int kPreviewAnimation = 0x21;      // the skill animation the preview plays
} // namespace

void CMpHeroScreen::MakePreview(int flag) {
    previewModel_ = nullptr;
    RecordPtrArray records = {RecordArrayVtable(), nullptr, 0, 0, 0};
    auto* player = reinterpret_cast<CParty*>(LocalPlayerData());
    if (player->members_.size == 0) {
        player->Clear(1, 0);
        player->AddMember(kHeroName, kHumanHero, kEmptyText);
    }
    PartyUnitRecord* first = player->members_.data;
    int size = records.size;
    RecordArraySetSizeOriginal(&records, nullptr, size + 1, -1);
    records.data[size] = reinterpret_cast<std::uint8_t*>(first);
    GameTakeUnitRecordsFlagOriginal(ClientGlue(), nullptr, &records, flag);
    auto* unit = static_cast<CUnit*>(GameUnitByIdOriginal(ClientGlue(), nullptr, player->members_.data->unknown_01C));
    unit->SetPrototype23C(unit->GetPrototype());
    previewModel_ = reinterpret_cast<std::uint8_t*>(unit);
    if (unit) {
        FigureAnimation* animation = reinterpret_cast<CObject3D*>(unit)->FindSkillAnimation(kPreviewAnimation);
        if (animation) {
            std::uint32_t random = IsaacRandom::Global().Next();
            double frames = x87::FromUnsigned(gameapi::timeGetTime()) * 0.0009765625f * 15.0f;
            int start = static_cast<int>(frames - static_cast<int>(random % 51));
            unit->SetUnknown254(-1, static_cast<std::int8_t>(animation->index), start, 0);
        }
        CUnitController* controller = unit->Controller();
        controller->ShowWeapon(0xFFFFFFFF);
        controller->RefreshModel();
        double now = x87::FromUnsigned(gameapi::timeGetTime()) * 0.0009765625f * 15.0f;
        controller->RestartAnimation(StoreFloat(static_cast<float>(now - 1.0f)));
        unit->SetFromStats(reinterpret_cast<UnitStats*>(player->members_.data->stats));
        unit->Controller()->RefreshWounds();
    }
    if (records.data) gamecrt::operator_delete(records.data);
}
namespace {
void __fastcall HeroScreenMakePreview(CMpHeroScreen* self, void*, int flag) { self->MakePreview(flag); }
} // namespace
EI_REPLACE(0x00616E20, HeroScreenMakePreview);

// --- 00616910: a hero shown -------------------------------------------------------------------

void CMpHeroScreen::ShowHero(int index) {
    RecordPtrArray records = {RecordArrayVtable(), nullptr, 0, 0, 0};
    auto* player = reinterpret_cast<CParty*>(LocalPlayerData());
    if (player->members_.size > 0) {
        int size = records.size;
        RecordArraySetSizeOriginal(&records, nullptr, size + 1, -1);
        records.data[size] = reinterpret_cast<std::uint8_t*>(player->members_.data);
        GameTakeUnitRecordsOriginal(ClientGlue(), nullptr, &records);
    }
    heroIndex_ = -1;
    if (index >= 0 && static_cast<std::uint32_t>(index) < MpHeroCount()) {
        heroIndex_ = index;
        PartyCopyFrom(player, nullptr, MpHero(index));
        MpHeroesSelectOriginal(MpHeroesObject(), nullptr, MpHero(index));
    }
    HeroScreenMakePreviewOriginal(this, nullptr, 1);
    auto* hero = reinterpret_cast<PartyUnitRecord*>(Hero());
    auto* unit = static_cast<CUnit*>(GameUnitByIdOriginal(ClientGlue(), nullptr, hero->unknown_01C));
    for (int slot = 0; slot < 4; ++slot) UnitRemoveWeaponOriginal(unit, nullptr, slot);
    for (int i = 0; i < static_cast<int>(hero->weaponCount); ++i) UnitAddWeaponOriginal(unit, nullptr, hero->weapons[i]);
    UnitSendEquipmentOriginal(unit);
    unit->Controller()->ShowWeapon(hero->weaponIndex);
    unit->Controller()->RefreshModel();
    turn_ = 0.0f;
    CallDrawHeroList();
    if (records.data) gamecrt::operator_delete(records.data);
}
namespace {
void __fastcall HeroScreenShowHero(CMpHeroScreen* self, void*, int index) { self->ShowHero(index); }
} // namespace
EI_REPLACE(0x00616910, HeroScreenShowHero);
