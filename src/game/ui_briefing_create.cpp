// The briefing / base screen's Create (slot 25, 006024B0) and per-frame Update (slot 24,
// 00604130): CBriefingScreen, ui_briefing.h. See docs/notes/ui.md, "Briefing / base screen".
#include "core_streams.h"
#include "game_strings.h"
#include "world_glue.h"
#include "combat.h"
#include "player_messages.h"
#include "ui.h"
#include "ui_briefing.h"

#include "camera.h"
#include "core_maths.h"
#include "core_transform.h"
#include "object_tables.h"
#include "sound.h"
#include "ui_camera_keys.h"
#include "unit.h"
#include "unit_ai.h"
#include "unit_client_models.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/crt.h"
#include "text_split.h"
#include "lib/stlp.h"
#include "originals_ai.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_units.h"
#include "originals_world.h"
#include "game_data.h"
#include "game_singletons.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

float& MapEdgeFade() { return ei::GameData<float>(0x0079BD34); }
bool LoadingGame() { return ei::GameData<const int>(0x007AFE1C) != 0; }
void** ScreenInputVtable() { return ei::GamePointer<void*>(0x00741154); }
void* RecordArrayVtable() { return ei::GamePointer<void>(0x0073FAA4); }

const char* SaveLoadTexture() { return "saveload"; }
const char* DefaultAllod() { return "gipat"; }
const char* TopicPrefix() { return "b."; }
const char* ConstructorTopic() { return "constr"; }
const char* NoZone() { return "none"; }
const char* ZoneVariablePrefix() { return "z."; }

// 00608D20: a network client's chat window (0x170 bytes; RET 4, returns this).
__declspec(naked) CUiWindow* __fastcall ChatWindowConstruct(void* /*memory*/, void*, CInterface3D*) EI_ORIGINAL(0x00608D20)

constexpr int kFromCamp = 5;              // WorldPlayer::lastScreen
constexpr float kEdgeFade = 4.0f;
constexpr int kTextureKind = 3;
constexpr std::uint32_t kPlainFlag = 0x100;   // CUiMesh::flags_: drawn in flatColour_
constexpr std::uint32_t kDarkBackdrop = 0x80000000;
constexpr std::uint32_t kTextBackdrop = 0xA0000000;
constexpr int kSurfaceFormat = 5;
constexpr int kTopicBarChild = 0;
constexpr int kTextChild = 1;
constexpr int kTopicsSurface = 0;
constexpr int kTopicBackdrop = 0;
constexpr int kTopicFrame = 1;

// The states (CBriefingScreen::state_).
constexpr int kWalking = 0;
constexpr int kChoosing = 1;
constexpr int kStarting = 2;
constexpr int kPhrases = 3;
constexpr int kRewards = 4;

constexpr float kMoverTimeout = 30.0f;      // 0073BAF0
constexpr double kCloseEnough = 0.01;       // 0073CE20, squared distance
constexpr int kEventOpenTopics = 1;
constexpr int kEventNoTopics = 2;
constexpr int kTopicNameParts = 3;
constexpr int kConstructorPrefix = 6;       // "constr"
constexpr std::uint32_t kOneBits = 0x3F800000;   // 1.0f

int Compare(const char* a, const char* b) {
    return gamecrt::mbsicmp(a, b);
}

MapRecord* FindLocalMap() {
    BriefingPlayerName name;
    return TheMapDatabase()->FindMap(name);
}

WorldPlayer* LocalViewPlayer() {
    return reinterpret_cast<LocalViewData*>(TheClientWorld()->LocalView())->player;
}

CUnit* ServerUnit(int id) { return WorldFindUnit(TheServerWorld(), nullptr, id); }
CUnitServer* ControllerOf(CUnit* unit) { return reinterpret_cast<CUnitServer*>(unit->controller); }

// A copy of `v`, normalised unless its length is zero (FCOM, TEST AH,0x40).
Vector3 NormalisedUnlessZero(const Vector3& v) {
    Vector3 out{v.x, v.y, v.z};
    float length = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::XYZ, float>(out)));
    EI_NAN_AUDIT(length);
    if (length == 0.0f) return out;
    float inverse = 1.0f / length;
    out.x = static_cast<float>(out.x * inverse);
    out.y = static_cast<float>(out.y * inverse);
    out.z = static_cast<float>(out.z * inverse);
    return out;
}

// 00609A40 inlined: the node unlinked and back to the pool.
void EraseMover(BriefingMover* node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    stlp::deallocate(node, sizeof(BriefingMover));
}

// A mover that took too long is put at its place, facing its direction, at once.
void PlaceMover(BriefingMover* node) {
    CUnit* unit = ServerUnit(node->person->id_);
    if (!unit) return;
    CUnitServer* controller = ControllerOf(unit);
    Vector3 target{node->target.x, node->target.y, node->target.z};
    SectorMapMove(TheSectorMap(), nullptr, unit, &target);
    Vector3 facing = NormalisedUnlessZero(node->direction);
    reinterpret_cast<CTransformObject*>(unit)->SetHeading(&facing.x);
    reinterpret_cast<CTransformObject*>(node->person)->SetHeading(&facing.x);
    controller->ResyncWithUnit();
}

// A walking mover near its target starts turning (its server unit turns over a second).
void StepWalking(BriefingMover* node) {
    const CUnit* person = reinterpret_cast<const CUnit*>(node->person);
    float dy = node->target.y - person->position.y;
    float dx = node->target.x - person->position.x;
    if (LengthSquared2DUnrounded(dx, dy) >= kCloseEnough) return;
    node->state = BriefingMover::kTurning;
    CUnit* unit = ServerUnit(node->person->id_);
    if (!unit) return;
    Vector3 facing = NormalisedUnlessZero(node->direction);
    ControllerOf(unit)->Brain()->TurnTowards(&facing, 1.0f);
}

// A turning mover whose facing (the client unit's -y axis, flat) is near its direction is done.
void StepTurning(BriefingMover* node) {
    const CUnit* person = reinterpret_cast<const CUnit*>(node->person);
    Vector3 axis;
    person->orientation.AxisY(&axis);
    float facingX = static_cast<float>(-axis.x);
    float facingY = -axis.y;
    Vector3 direction = NormalisedUnlessZero(node->direction);
    float dy = direction.y - facingY;
    float dx = direction.x - facingX;
    if (LengthSquared2DUnrounded(dx, dy) >= kCloseEnough) return;
    node->state = BriefingMover::kDone;
}

void SetMeshesVisible(CUiWindow* window, int visible) {
    for (int i = 0; i < window->sceneObjects_.size; ++i)
        reinterpret_cast<CUiMesh*>(window->sceneObjects_.data[i])->visible_ = visible;
}

// The speaker's lips stop (CUnitClientSpecific's blink: eyes open).
void StopTalking(CUnit* speaker) {
    auto* model = reinterpret_cast<CUnitClientSpecific*>(speaker->controller);
    model->blinks = 0;
    model->BlinkOpen(true);
}

// Whether the voice `handle` has ended (neither playing nor waiting).
bool VoiceEnded(int handle) {
    CSoundManager* sounds = TheSoundManager();
    return !sounds->FindPlaying(handle) && !sounds->FindDeferred(static_cast<std::uint32_t>(handle));
}

} // namespace

// --- 006024B0: slot 25 --------------------------------------------------------------------------

void CBriefingScreen::Create() {
    chatActive_ = 0;
    autosave_ = false;
    if (LoadingGame())
        GameSaveStream()->Resize(0);
    else if (LocalViewPlayer()->lastScreen != kFromCamp)
        autosave_ = true;
    MapRecord* map = FindLocalMap();
    LoadPersonTexts();
    LoadTexture(SaveLoadTexture(), kTextureKind, nullptr);

    // The backdrops: a dark one behind the topics with a frame, a lighter one behind the text
    // with a frame.
    CUiMesh* sprite = AddSprite(nullptr);
    sprite->SetRectangle(20.0f, 100.0f, 200.0f, 430.0f, 600.0f, 570.0f, 0, 0, 0x100, 0x100);
    sprite->texture_ = nullptr;
    sprite->flags_ |= kPlainFlag;
    sprite->flatColour_ = kDarkBackdrop;
    AddSprite(nullptr)->SetFrame(10.0f, 100.0f, 195.0f, 425.0f, 605.0f, 575.0f, 5.0f, 2, 0xFE, 0xB4, 0xF9);
    sprite = AddSprite(nullptr);
    sprite->SetRectangle(20.0f, 100.0f, 155.0f, 416.0f, 645.0f, 580.0f, 0, 0, 0x100, 0x100);
    sprite->texture_ = nullptr;
    sprite->flatColour_ = kTextBackdrop;
    sprite->flags_ |= kPlainFlag;
    AddSprite(nullptr)->SetFrame(20.0f, 100.0f, 150.0f, 411.0f, 650.0f, 585.0f, 5.0f, 2, 0xFE, 0xB4, 0xF9);

    // Surface 0 the topic rows, 1 a person's label.
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    AddSurface(memory ? static_cast<CUiSurface*>(memory)->Construct(interface_, 200, 430, 600, 570, kSurfaceFormat, 1)
                      : nullptr);
    memory = gamecrt::operator_new(sizeof(CUiSurface));
    AddSurface(memory ? static_cast<CUiSurface*>(memory)->Construct(interface_, 0, 0, 250, 30, kSurfaceFormat, 1)
                      : nullptr);

    memory = gamecrt::operator_new(sizeof(CUiScrollbar));
    topicBar_ = memory ? static_cast<CUiScrollbar*>(memory)->Construct(interface_, RECT{584, 440, 594, 560}, 3.0f, 0, 1.0f)
                       : nullptr;
    AddChild(topicBar_);
    memory = gamecrt::operator_new(0x1C8);
    text_ = memory ? static_cast<CUiScrollText*>(memory)->Construct(interface_, game::EmptyText(), RECT{165, 426, 635, 570}, 10.0f)
                   : nullptr;
    AddChild(text_);

    SetMeshesVisible(this, 0);
    for (int i = 0; i < surfaces_.size; ++i) surfaces_.data[i]->Freeze(0, 0);
    for (int i = 0; i < children_.size; ++i) children_.data[i]->SetEnabled(0, 1);
    state_ = kWalking;
    exitPending_ = true;
    exit_ = 0;
    leader_ = nullptr;
    savedEdgeFade_ = MapEdgeFade();
    MapEdgeFade() = kEdgeFade;

    if (MapRecord* record = FindLocalMap()) {
        TheMusicSystem()->PlayBriefing(record->allod);
        noWalk_ = record->cage;
    } else {
        TheMusicSystem()->PlayBriefing(DefaultAllod());
        noWalk_ = false;
    }
    GameStartAmbience();
    GameSetWeather(0, 0.0f);

    // The screen's own input side (its third dword is left as allocated) and the view's.
    auto* input = static_cast<UiInputSide*>(gamecrt::operator_new(0xC));
    if (input) {
        input->vtable_ = ScreenInputVtable();
        input->owner_ = interface_;
    }
    AddInput(input);
    memory = gamecrt::operator_new(0x2C);
    AddInput(memory ? reinterpret_cast<UiInputSide*>(static_cast<CCameraInput*>(memory)->Construct(interface_)) : nullptr);

    // The camera: no longer free, kept within the zone's circle (the first lookup's record,
    // not checked for null, as the original), the alternate limits, no target.
    CCamera* camera = TheCamera();
    camera->flags_ &= ~CCamera::kFree;
    if (map->restricted) {
        Vector3 centre{map->restrictX, map->restrictY, 0.0f};
        camera->SetBounds(&centre, map->restrictRadius);
    }
    camera->flags_ |= CCamera::kAlternate;
    camera->SetTargetId(-1);
    briefingStarted_ = false;
    voice_ = -1;
    speaking_ = nullptr;

    if (IsMultiplayerClient()) {
        memory = gamecrt::operator_new(0x170);
        chat_ = memory ? ChatWindowConstruct(memory, nullptr, interface_) : nullptr;
        AddChild(chat_);
        VirtualShowChat(1);
    } else {
        chat_ = nullptr;
    }

    // The people of the base: the local player's objects whose ids are text keys. The walk ends
    // at an empty entry, as the original's does.
    const auto& objects = LocalViewPlayer()->objects.map;
    for (auto* node = objects.First(); node && node->value; node = objects.After(node)) {
        int id = node->value->id_;
        if (IsTextKey(id)) people_.push_back(id);
    }
    markPhase_ = 0.0f;
}

EI_REPLACE_METHOD(0x006024B0, CBriefingScreen::Create);

// --- 00604130: slot 24 --------------------------------------------------------------------------

void CBriefingScreen::Update() {
    markPhase_ = static_cast<float>(WindowFrameTimeOriginal(this) + markPhase_);
    while (markPhase_ >= 1.0f) markPhase_ = static_cast<float>(markPhase_ - 1.0f);
    UiUpdateChildrenOriginal(this);
    SetMeshesVisible(this, 0);
    for (int i = 0; i < surfaces_.size; ++i) surfaces_.data[i]->Freeze(0, 0);
    children_.data[kTopicBarChild]->SetEnabled(0, 1);
    children_.data[kTextChild]->SetEnabled(0, 1);

    for (BriefingMover* node = movers_->next; node != movers_; node = node->next) {
        node->time = static_cast<float>(WindowFrameTimeOriginal(this) + node->time);
        if (node->time > kMoverTimeout) {
            PlaceMover(node);
            node->state = BriefingMover::kDone;
        } else if (node->state == BriefingMover::kWalking) {
            StepWalking(node);
        } else if (node->state == BriefingMover::kTurning) {
            StepTurning(node);
        }
    }
    for (BriefingMover* node = movers_->next; node != movers_;) {
        BriefingMover* next = node->next;
        if (node->state == BriefingMover::kDone) EraseMover(node);
        node = next;
    }

    switch (state_) {
    case kChoosing: {
        surfaces_.data[kTopicsSurface]->Freeze(1, 0);
        reinterpret_cast<CUiMesh*>(sceneObjects_.data[kTopicBackdrop])->visible_ = 1;
        reinterpret_cast<CUiMesh*>(sceneObjects_.data[kTopicFrame])->visible_ = 1;
        topicBar_->SetEnabled(1, 1);
        int position;
        if (topicBar_->TakePosition(&position)) {
            topicTop_ = position;
            DrawTopics();
        }
        MarkPeople();
        LeaveIfExitOpen();
        break;
    }
    case kWalking:
        MarkPeople();
        LeaveIfExitOpen();
        break;
    case kStarting:
        state_ = kPhrases;
        for (BriefingMover* node = movers_->next; node != movers_; node = node->next) {
            if (node->state > 0 && node->waited) {
                state_ = kStarting;
                break;
            }
        }
        if (state_ == kPhrases) StartPhrases();
        break;
    case kPhrases:
        SetMeshesVisible(this, 1);
        text_->SetEnabled(1, 1);
        if (voice_ != -1) {
            if (VoiceEnded(voice_)) NextPhrase(0);
        } else {
            phraseTime_ = static_cast<float>(phraseTime_ - WindowFrameTimeOriginal(this));
            EI_NAN_AUDIT(phraseTime_);
            if (phraseTime_ < 0.0f) NextPhrase(0);
        }
        break;
    case kRewards:
        SetMeshesVisible(this, 1);
        text_->SetEnabled(1, 1);
        if (voice_ != -1) {
            if (speaking_ && VoiceEnded(voice_)) StopTalking(speaking_);
        } else {
            phraseTime_ = static_cast<float>(phraseTime_ - WindowFrameTimeOriginal(this));
            EI_NAN_AUDIT(phraseTime_);
            if (speaking_ && phraseTime_ < 0.0f) StopTalking(speaking_);
        }
        break;
    default:
        break;
    }
}

void CBriefingScreen::MarkPeople() {
    BriefingVariables variables(TopicPrefix());
    console::CStringArray parts;
    // The people with a topic open (the original sorts and dedups the list; only membership is
    // used).
    stlp::vector<int> open;
    for (NamedFloatTable::Node* node = variables.First(); node; node = variables.Next(node)) {
        std::uint32_t bits;
        std::memcpy(&bits, &node->value.mapped, 4);
        if (bits != kOneBits) continue;
        text::Split(node->value.key, &parts, '.');
        if (parts.GetSize() != kTopicNameParts) continue;
        if (Compare(parts[2].Left(kConstructorPrefix), ConstructorTopic()) == 0) continue;
        int id = TextKeyFromName(parts[1]);
        if (std::find(people_.begin(), people_.end(), id) == people_.end()) continue;
        open.push_back(id);
    }
    for (int* id = people_.begin(); id != people_.end(); ++id) {
        if (!WorldFindUnit(TheClientWorld(), nullptr, *id)) continue;
        bool hasTopics = std::find(open.begin(), open.end(), *id) != open.end();
        auto* player = static_cast<WorldPlayer*>(TheServerWorld()->GetPlayer(0));
        if (player && player->server)
            static_cast<CPlayerServer*>(player->server)->SendPair19(hasTopics ? kEventOpenTopics : kEventNoTopics, *id);
    }
    open.clear();
    open.FreeStorage();
}

void CBriefingScreen::LeaveIfExitOpen() {
    struct PartyRecords {
        std::uint8_t unused_00[0x30];
        void* records;               // +0x30 the unit records (0x784 bytes each)
        int count;                   // +0x34
    };
    const auto* party = reinterpret_cast<const PartyRecords*>(LocalPlayerData());
    if (!exitPending_ || party->count <= 0) return;
    mfc::CString zone(GameExitTarget(exit_));
    if (Compare(zone, NoZone()) == 0) return;
    mfc::CString variable(ZoneVariablePrefix());
    variable += zone;
    float visited = PlayerZeroVariable(variable);
    EI_NAN_AUDIT(visited);
    if (visited == 1.0f) return;
    mfc::CArray<std::uint8_t*> records{RecordArrayVtable(), nullptr, 0, 0, 0};
    mfc::CArray<std::uint8_t*> second{RecordArrayVtable(), nullptr, 0, 0, 0};
    records.SetSize(1);
    records[0] = static_cast<std::uint8_t*>(party->records);
    if (GamePartyInExit(&records, exit_, &second)) {
        InterfaceHasScreen() = false;
        GameLeaveThroughExit(exit_);
    }
    second.FreeData();
    records.FreeData();
}

EI_REPLACE_METHOD(0x00604130, CBriefingScreen::Update);
