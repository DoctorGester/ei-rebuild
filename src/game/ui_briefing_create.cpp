// The briefing / base screen's Create and per-frame Update
// CBriefingScreen, ui_briefing.h. See docs/notes/ui.md, "Briefing / base screen".
#include "app.h"
#include "camera.h"
#include "combat.h"
#include "core_maths.h"
#include "core_streams.h"
#include "core_transform.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "object_tables.h"
#include "options.h"
#include "party.h"
#include "player_client.h"
#include "player_messages.h"
#include "sound.h"
#include "text_split.h"
#include "ui.h"
#include "ui_briefing.h"
#include "ui_camera_keys.h"
#include "unit.h"
#include "unit_ai.h"
#include "unit_client_models.h"
#include "world.h"
#include "world_glue.h"

#include "lib/nan_audit.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include "ei/x87.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

constexpr const char* kSaveLoadTexture = "saveload";
constexpr const char* kDefaultAllod = "gipat";
constexpr const char* kTopicPrefix = "b.";
constexpr const char* kConstructorTopic = "constr";
constexpr const char* kNoZone = "none";
constexpr const char* kZoneVariablePrefix = "z.";

constexpr int kFromCamp = 5;              // CPlayer::lastScreen_
constexpr float kEdgeFade = 4.0f;
constexpr std::uint32_t kDarkBackdrop = 0x80000000;
constexpr std::uint32_t kTextBackdrop = 0xA0000000;
constexpr int kTopicBarChild = 0;
constexpr int kTextChild = 1;
constexpr int kTopicsSurface = 0;
constexpr int kTopicBackdrop = 0;
constexpr int kTopicFrame = 1;
constexpr int kTextBackdropMesh = 2;   // the text's backdrop, its frame and the people's slots after it

// The states (CBriefingScreen::state_).
constexpr int kWalking = 0;
constexpr int kChoosing = 1;
constexpr int kStarting = 2;
constexpr int kPhrases = 3;
constexpr int kRewards = 4;

constexpr float kMoverTimeout = 30.0f;
constexpr double kCloseEnough = 0.01;       // squared distance
constexpr int kEventOpenTopics = 1;
constexpr int kEventNoTopics = 2;
constexpr int kTopicNameParts = 3;
constexpr int kConstructorPrefix = 6;       // "constr"
constexpr std::uint32_t kOneBits = 0x3F800000;   // 1.0f

// A copy of `v`, normalised unless its length is zero.
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

// A mover that took too long is put at its place, facing its direction, at once.
void PlaceMover(BriefingMover* node) {
    CUnit* unit = g_serverWorld.FindUnit(node->person->id_);
    if (!unit) return;
    CUnitServer* controller = unit->ServerController();
    Vector3 target{node->target.x, node->target.y, node->target.z};
    g_serverWorld.aiMap_.sectorMap.Move(unit, &target);
    Vector3 facing = NormalisedUnlessZero(node->direction);
    unit->SetHeading(&facing.x);
    node->person->SetHeading(&facing.x);
    controller->ResyncWithUnit();
}

// A walking mover near its target starts turning (its server unit turns over a second).
void StepWalking(BriefingMover* node) {
    const CUnit* person = node->person;
    float dy = node->target.y - person->localPosition_.y;
    float dx = node->target.x - person->localPosition_.x;
    if (LengthSquared2DUnrounded(dx, dy) >= kCloseEnough) return;
    node->state = BriefingMover::kTurning;
    CUnit* unit = g_serverWorld.FindUnit(node->person->id_);
    if (!unit) return;
    Vector3 facing = NormalisedUnlessZero(node->direction);
    unit->ServerController()->brain.TurnTowards(&facing, 1.0f);
}

// A turning mover whose facing (the client unit's -y axis, flat) is near its direction is done.
void StepTurning(BriefingMover* node) {
    const CUnit* person = node->person;
    Vector3 axis;
    person->localRotation_.AxisY(&axis);
    float facingX = static_cast<float>(-axis.x);
    float facingY = -axis.y;
    Vector3 direction = NormalisedUnlessZero(node->direction);
    float dy = direction.y - facingY;
    float dx = direction.x - facingX;
    if (LengthSquared2DUnrounded(dx, dy) >= kCloseEnough) return;
    node->state = BriefingMover::kDone;
}

// Meshes `first` onwards shown or hidden.
void SetMeshesVisible(CUiWindow* window, int visible, int first = 0) {
    for (int i = first; i < std::ssize(window->sceneObjects_); ++i)
        window->SceneMesh(i)->visible_ = visible;
}

// The speaker's lips stop (CUnitClientSpecific's blink: eyes open).
void StopTalking(CUnit* speaker) {
    CUnitClientSpecific* model = speaker->ClientController();
    model->blinks = 0;
    model->BlinkOpen(true);
}

// Whether the voice `handle` has ended (neither playing nor waiting).
bool VoiceEnded(int handle) {
    CSoundManager* sounds = g_clientWorld.sounds_;
    return !sounds->FindPlaying(handle) && !sounds->FindDeferred(static_cast<std::uint32_t>(handle));
}

} // namespace

void CBriefingScreen::CreateScreen() {
    chatActive_ = 0;
    autosave_ = false;
    if (g_saveStream.size_ != 0)   // loading a game
        g_saveStream.Resize(0);
    else if (g_clientWorld.LocalView()->player_->lastScreen_ != kFromCamp)
        autosave_ = true;
    MapRecord* map = g_mapDatabase.FindMap(LocalPlayerZone());
    LoadPersonTexts(&personTexts_);
    LoadTexture(kSaveLoadTexture, TextureKind::Interface);

    // The backdrops: a dark one behind the topics with a frame, a lighter one behind the text
    // with a frame.
    CUiMesh* sprite = AddBlankSprite();
    sprite->SetRectangle(20.0f, 100.0f, 200.0f, 430.0f, 600.0f, 570.0f, 0, 0, 0x100, 0x100);
    sprite->texture_ = nullptr;
    sprite->flags_ |= FigureMeshFlag::Flat;
    sprite->flatColour_ = kDarkBackdrop;
    AddBlankSprite()->SetFrame(10.0f, 100.0f, 195.0f, 425.0f, 605.0f, 575.0f, 5.0f, 2, 0xFE, 0xB4, 0xF9);
    sprite = AddBlankSprite();
    sprite->SetRectangle(20.0f, 100.0f, 155.0f, 416.0f, 645.0f, 580.0f, 0, 0, 0x100, 0x100);
    sprite->texture_ = nullptr;
    sprite->flatColour_ = kTextBackdrop;
    sprite->flags_ |= FigureMeshFlag::Flat;
    AddBlankSprite()->SetFrame(20.0f, 100.0f, 150.0f, 411.0f, 650.0f, 585.0f, 5.0f, 2, 0xFE, 0xB4, 0xF9);

    // Surface 0 the topic rows, 1 a person's label.
    for (const RECT& area : {RECT{200, 430, 600, 570}, RECT{0, 0, 250, 30}}) {
        CUiSurface* surface = new CUiSurface(interface_, area.left, area.top, area.right, area.bottom, SurfaceBlend::Keyed, true);
        AddSurface(surface);
    }

    topicBar_ = new CUiScrollbar(interface_, RECT{584, 440, 594, 560}, 3.0f, 0, 1.0f);
    AddChild(topicBar_);
    text_ = new CUiScrollText(interface_, game::kEmptyText, RECT{165, 426, 635, 570}, 10.0f);
    AddChild(text_);

    SetMeshesVisible(this, 0);
    for (int i = 0; i < std::ssize(surfaces_); ++i) surfaces_[i]->Hide(false);
    for (int i = 0; i < std::ssize(children_); ++i) children_[i]->SetEnabled(false);
    state_ = kWalking;
    exitPending_ = true;
    exit_ = 0;
    leader_ = nullptr;
    savedEdgeFade_ = options::g_options->borderFogDistance_;
    options::g_options->borderFogDistance_ = kEdgeFade;

    if (MapRecord* record = g_mapDatabase.FindMap(LocalPlayerZone())) {
        g_clientWorld.music_->PlayBriefing(record->allod);
        noWalk_ = record->cage;
    } else {
        g_clientWorld.music_->PlayBriefing(kDefaultAllod);
        noWalk_ = false;
    }
    GameStartAmbience();
    GameSetWeather(0, 0.0f);

    // The screen's own input side (its third dword is left as allocated) and the view's.
    UiInputSide* input = new UiInertInput(interface_);   // 0x0C bytes
    AddInput(input);
    AddInput(new CCameraInput(interface_));

    // The camera: no longer free, kept within the zone's circle (the first lookup's record,
    // not checked for null), the alternate limits, no target.
    CCamera* camera = &g_camera.Get();
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
        chat_ = new CChatWindow(interface_);
        AddChild(chat_);
        PlayersChanged(1);
    } else {
        chat_ = nullptr;
    }

    // The people of the base: the local player's objects whose ids are text keys. The walk ends
    // at an empty entry.
    const auto& objects = g_clientWorld.LocalView()->player_->knownObjects_;
    for (auto node = objects.begin(); node != objects.end() && node->second; ++node) {
        int id = node->second->id_;
        if (IsTextKey(id)) people_.push_back(id);
    }
    markPhase_ = 0.0f;
}

void CBriefingScreen::Update() {
    markPhase_ = static_cast<float>(FrameTime() + markPhase_);
    while (markPhase_ >= 1.0f) markPhase_ = static_cast<float>(markPhase_ - 1.0f);
    CUiContainer::Update();
    SetMeshesVisible(this, 0);
    for (int i = 0; i < std::ssize(surfaces_); ++i) surfaces_[i]->Hide(false);
    children_[kTopicBarChild]->SetEnabled(false);
    children_[kTextChild]->SetEnabled(false);

    for (BriefingMover& mover : movers_) {
        mover.time = static_cast<float>(FrameTime() + mover.time);
        if (mover.time > kMoverTimeout) {
            PlaceMover(&mover);
            mover.state = BriefingMover::kDone;
        } else if (mover.state == BriefingMover::kWalking) {
            StepWalking(&mover);
        } else if (mover.state == BriefingMover::kTurning) {
            StepTurning(&mover);
        }
    }
    std::erase_if(movers_, [](const BriefingMover& mover) { return mover.state == BriefingMover::kDone; });

    switch (state_) {
    case kChoosing: {
        surfaces_[kTopicsSurface]->Show();
        SceneMesh(kTopicBackdrop)->visible_ = true;
        SceneMesh(kTopicFrame)->visible_ = true;
        topicBar_->SetEnabled(true);
        if (topicBar_->TakeMoved()) {
            topicTop_ = topicBar_->Position();
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
        for (const BriefingMover& mover : movers_) {
            if (mover.state > 0 && mover.waited) {
                state_ = kStarting;
                break;
            }
        }
        if (state_ == kPhrases) StartPhrases();
        break;
    case kPhrases:
        // The text's meshes; the topics' backdrop and frame stay hidden.
        SetMeshesVisible(this, 1, kTextBackdropMesh);
        text_->SetEnabled(true);
        if (voice_ != -1) {
            if (VoiceEnded(voice_)) NextPhrase(false);
        } else {
            phraseTime_ = static_cast<float>(phraseTime_ - FrameTime());
            EI_NAN_AUDIT(phraseTime_);
            if (phraseTime_ < 0.0f) NextPhrase(false);
        }
        break;
    case kRewards:
        // The text's meshes; the topics' backdrop and frame stay hidden.
        SetMeshesVisible(this, 1, kTextBackdropMesh);
        text_->SetEnabled(true);
        if (voice_ != -1) {
            if (speaking_ && VoiceEnded(voice_)) StopTalking(speaking_);
        } else {
            phraseTime_ = static_cast<float>(phraseTime_ - FrameTime());
            EI_NAN_AUDIT(phraseTime_);
            if (speaking_ && phraseTime_ < 0.0f) StopTalking(speaking_);
        }
        break;
    default:
        break;
    }
}

void CBriefingScreen::MarkPeople() {
    NamedFloatTable variables;
    PlayerZeroVariables(kTopicPrefix, &variables);
    std::vector<std::string> parts;
    // The people with a topic open (the list is sorted and deduplicated; only membership is used).
    std::vector<int> open;
    for (auto& node : variables) {
        std::uint32_t bits;
        bits = std::bit_cast<std::uint32_t>(node.second);
        if (bits != kOneBits) continue;
        text::Split(node.first, &parts, '.');
        if (parts.size() != kTopicNameParts) continue;
        if (text::CompareIgnoringCase(str::Left(parts[2], kConstructorPrefix), kConstructorTopic) == 0) continue;
        int id = TextKeyFromName(parts[1]);
        if (std::find(people_.begin(), people_.end(), id) == people_.end()) continue;
        open.push_back(id);
    }
    for (auto& id : people_) {
        if (!g_clientWorld.FindUnit(id)) continue;
        bool hasTopics = std::find(open.begin(), open.end(), id) != open.end();
        auto* player = g_serverWorld.GetPlayer(0);
        if (player && player->Server()) player->Server()->SendPair19(hasTopics ? kEventOpenTopics : kEventNoTopics, id);
    }
    open.clear();
}

void CBriefingScreen::LeaveIfExitOpen() {
    CParty* party = LocalPlayerData();
    if (!exitPending_ || std::ssize(party->members_) <= 0) return;
    std::string zone = GameExitTarget(exit_);
    if (text::CompareIgnoringCase(zone, kNoZone) == 0) return;
    std::string variable = kZoneVariablePrefix;
    variable += zone;
    float visited = PlayerZeroVariable(variable);
    EI_NAN_AUDIT(visited);
    if (visited == 1.0f) return;
    std::vector<PartyUnitRecord*> records{};
    std::vector<PartyUnitRecord*> second{};
    records.resize(1);
    records[0] = party->members_.data();
    if (GamePartyInExit(&records, exit_, &second)) {
        app::g_interfaceHasScreen = 0;
        GameLeaveThroughExit(exit_);
    }
}
