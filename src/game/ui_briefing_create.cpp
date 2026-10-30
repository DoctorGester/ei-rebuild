// The briefing / base screen's Create and per-frame Update
// BriefingScreen, ui_briefing.h. See docs/notes/ui.md, "Briefing / base screen".
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

constexpr int kFromCamp = 5;              // CPlayer::lastScreen
constexpr float kEdgeFade = 4.0f;
constexpr std::uint32_t kDarkBackdrop = 0x80000000;
constexpr std::uint32_t kTextBackdrop = 0xA0000000;
constexpr int kTopicBarChild = 0;
constexpr int kTextChild = 1;
constexpr int kTopicsSurface = 0;
constexpr int kTopicBackdrop = 0;
constexpr int kTopicFrame = 1;
constexpr int kTextBackdropMesh = 2;   // the text's backdrop, its frame and the people's slots after it

// The states (BriefingScreen::state).
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
    CUnit* unit = g_serverWorld.FindUnit(node->person->id);
    if (!unit) return;
    UnitServer* controller = unit->ServerController();
    Vector3 target{node->target.x, node->target.y, node->target.z};
    g_serverWorld.aiMap.sectorMap.Move(unit, target);
    Vector3 facing = NormalisedUnlessZero(node->direction);
    unit->SetHeading(&facing.x);
    node->person->SetHeading(&facing.x);
    controller->ResyncWithUnit();
}

// A walking mover near its target starts turning (its server unit turns over a second).
void StepWalking(BriefingMover* node) {
    const CUnit* person = node->person;
    float dy = node->target.y - person->localPosition.y;
    float dx = node->target.x - person->localPosition.x;
    if (LengthSquared2DUnrounded(dx, dy) >= kCloseEnough) return;
    node->state = BriefingMover::kTurning;
    CUnit* unit = g_serverWorld.FindUnit(node->person->id);
    if (!unit) return;
    Vector3 facing = NormalisedUnlessZero(node->direction);
    unit->ServerController()->brain.TurnTowards(facing, 1.0f);
}

// A turning mover whose facing (the client unit's -y axis, flat) is near its direction is done.
void StepTurning(BriefingMover* node) {
    const CUnit* person = node->person;
    Vector3 axis = person->localRotation.AxisY();
    float facingX = static_cast<float>(-axis.x);
    float facingY = -axis.y;
    Vector3 direction = NormalisedUnlessZero(node->direction);
    float dy = direction.y - facingY;
    float dx = direction.x - facingX;
    if (LengthSquared2DUnrounded(dx, dy) >= kCloseEnough) return;
    node->state = BriefingMover::kDone;
}

// Meshes `first` onwards shown or hidden.
void SetMeshesVisible(UiWindow* window, int visible, int first = 0) {
    for (int i = first; i < std::ssize(window->sceneObjects); ++i)
        window->SceneMesh(i)->visible = visible;
}

// The speaker's lips stop (UnitClientSpecific's blink: eyes open).
void StopTalking(CUnit* speaker) {
    UnitClientSpecific* model = speaker->ClientController();
    model->blinks = 0;
    model->BlinkOpen(true);
}

// Whether the voice `handle` has ended (neither playing nor waiting).
bool VoiceEnded(int handle) {
    SoundManager* sounds = g_clientWorld.sounds;
    return !sounds->FindPlaying(handle) && !sounds->FindDeferred(static_cast<std::uint32_t>(handle));
}

} // namespace

void BriefingScreen::CreateScreen() {
    chatActive = 0;
    autosave = false;
    if (g_saveStream.size != 0)    // loading a game
        g_saveStream.Resize(0);
    else if (g_clientWorld.LocalView()->player->lastScreen != kFromCamp)
        autosave = true;
    MapRecord* map = g_mapDatabase.FindMap(LocalPlayerZone());
    LoadPersonTexts(&personTexts);
    LoadTexture(kSaveLoadTexture, TextureKind::Interface);

    // The backdrops: a dark one behind the topics with a frame, a lighter one behind the text
    // with a frame.
    UiMesh* sprite = AddBlankSprite();
    sprite->SetRectangle(20.0f, 100.0f, 200.0f, 430.0f, 600.0f, 570.0f, 0, 0, 0x100, 0x100);
    sprite->texture = nullptr;
    sprite->flags |= FigureMeshFlag::Flat;
    sprite->flatColour = kDarkBackdrop;
    AddBlankSprite()->SetFrame(10.0f, 100.0f, 195.0f, 425.0f, 605.0f, 575.0f, 5.0f, 2, 0xFE, 0xB4, 0xF9);
    sprite = AddBlankSprite();
    sprite->SetRectangle(20.0f, 100.0f, 155.0f, 416.0f, 645.0f, 580.0f, 0, 0, 0x100, 0x100);
    sprite->texture = nullptr;
    sprite->flatColour = kTextBackdrop;
    sprite->flags |= FigureMeshFlag::Flat;
    AddBlankSprite()->SetFrame(20.0f, 100.0f, 150.0f, 411.0f, 650.0f, 585.0f, 5.0f, 2, 0xFE, 0xB4, 0xF9);

    // Surface 0 the topic rows, 1 a person's label.
    for (const Rect& area : {Rect{200, 430, 600, 570}, Rect{0, 0, 250, 30}}) {
        UiSurface* surface = new UiSurface(ui, area.left, area.top, area.right, area.bottom, SurfaceBlend::Keyed, true);
        AddSurface(surface);
    }

    topicBar = new UiScrollbar(ui, Rect{584, 440, 594, 560}, 3.0f, 0, 1.0f);
    AddChild(topicBar);
    text = new UiScrollText(ui, game::kEmptyText, Rect{165, 426, 635, 570}, 10.0f);
    AddChild(text);

    SetMeshesVisible(this, 0);
    for (int i = 0; i < std::ssize(surfaces); ++i) surfaces[i]->Hide(false);
    for (int i = 0; i < std::ssize(children); ++i) children[i]->SetEnabled(false);
    state = kWalking;
    exitPending = true;
    exit = 0;
    leader = nullptr;
    savedEdgeFade = options::g_options.borderFogDistance;
    options::g_options.borderFogDistance = kEdgeFade;

    if (MapRecord* record = g_mapDatabase.FindMap(LocalPlayerZone())) {
        g_clientWorld.music->PlayBriefing(record->allod);
        noWalk = record->cage;
    } else {
        g_clientWorld.music->PlayBriefing(kDefaultAllod);
        noWalk = false;
    }
    GameStartAmbience();
    GameSetWeather(0, 0.0f);

    // The screen's own input side (its third dword is left as allocated) and the view's.
    UiInputSide* input = new UiInertInput(ui);           // 0x0C bytes
    AddInput(input);
    AddInput(new CameraInput(ui));

    // The camera: no longer free, kept within the zone's circle (the first lookup's record,
    // not checked for null), the alternate limits, no target.
    Camera* camera = &g_camera;
    camera->flags &= ~Camera::kFree;
    if (map->restricted) {
        Vector3 centre{map->restrictX, map->restrictY, 0.0f};
        camera->SetBounds(centre, map->restrictRadius);
    }
    camera->flags |= Camera::kAlternate;
    camera->SetTargetId(-1);
    briefingStarted = false;
    voice = -1;
    speaking = nullptr;

    if (IsMultiplayerClient()) {
        chat = new ChatWindow(ui);
        AddChild(chat);
        PlayersChanged(1);
    } else {
        chat = nullptr;
    }

    // The people of the base: the local player's objects whose ids are text keys. The walk ends
    // at an empty entry.
    const auto& known = g_clientWorld.LocalView()->player->knownObjects;
    for (auto node = known.begin(); node != known.end() && node->second; ++node) {
        int secondId = node->second->id;
        if (IsTextKey(secondId)) people.push_back(secondId);
    }
    markPhase = 0.0f;
}

void BriefingScreen::Update() {
    markPhase = static_cast<float>(FrameTime() + markPhase);
    while (markPhase >= 1.0f) markPhase = static_cast<float>(markPhase - 1.0f);
    UiContainer::Update();
    SetMeshesVisible(this, 0);
    for (int i = 0; i < std::ssize(surfaces); ++i) surfaces[i]->Hide(false);
    children[kTopicBarChild]->SetEnabled(false);
    children[kTextChild]->SetEnabled(false);

    for (BriefingMover& mover : movers) {
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
    std::erase_if(movers, [](const BriefingMover& mover) { return mover.state == BriefingMover::kDone; });

    switch (state) {
    case kChoosing: {
        surfaces[kTopicsSurface]->Show();
        SceneMesh(kTopicBackdrop)->visible = true;
        SceneMesh(kTopicFrame)->visible = true;
        topicBar->SetEnabled(true);
        if (topicBar->TakeMoved()) {
            topicTop = topicBar->Position();
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
        state = kPhrases;
        for (const BriefingMover& mover : movers) {
            if (mover.state > 0 && mover.waited) {
                state = kStarting;
                break;
            }
        }
        if (state == kPhrases) StartPhrases();
        break;
    case kPhrases:
        // The text's meshes; the topics' backdrop and frame stay hidden.
        SetMeshesVisible(this, 1, kTextBackdropMesh);
        text->SetEnabled(true);
        if (voice != -1) {
            if (VoiceEnded(voice)) NextPhrase(false);
        } else {
            phraseTime = static_cast<float>(phraseTime - FrameTime());
            EI_NAN_AUDIT(phraseTime);
            if (phraseTime < 0.0f) NextPhrase(false);
        }
        break;
    case kRewards:
        // The text's meshes; the topics' backdrop and frame stay hidden.
        SetMeshesVisible(this, 1, kTextBackdropMesh);
        text->SetEnabled(true);
        if (voice != -1) {
            if (speaking && VoiceEnded(voice)) StopTalking(speaking);
        } else {
            phraseTime = static_cast<float>(phraseTime - FrameTime());
            EI_NAN_AUDIT(phraseTime);
            if (speaking && phraseTime < 0.0f) StopTalking(speaking);
        }
        break;
    default:
        break;
    }
}

void BriefingScreen::MarkPeople() {
    NamedFloatTable variables;
    PlayerZeroVariables(kTopicPrefix, &variables);
    std::vector<std::string> parts;
    // The people with a topic open (the list is sorted and deduplicated; only membership is used).
    std::vector<int> open;
    for (auto& node : variables) {
        std::uint32_t bits;
        bits = std::bit_cast<std::uint32_t>(node.second);
        if (bits != kOneBits) continue;
        parts = text::Split(node.first, '.');
        if (parts.size() != kTopicNameParts) continue;
        if (text::CompareIgnoringCase(str::Left(parts[2], kConstructorPrefix), kConstructorTopic) == 0) continue;
        int textKey = TextKeyFromName(parts[1]);
        if (std::find(people.begin(), people.end(), textKey) == people.end()) continue;
        open.push_back(textKey);
    }
    for (auto& personId : people) {
        if (!g_clientWorld.FindUnit(personId)) continue;
        bool hasTopics = std::find(open.begin(), open.end(), personId) != open.end();
        auto* player = g_serverWorld.GetPlayer(0);
        if (player && player->Server()) player->Server()->SendPair19(hasTopics ? kEventOpenTopics : kEventNoTopics, personId);
    }
    open.clear();
}

void BriefingScreen::LeaveIfExitOpen() {
    CParty* localParty = LocalPlayerData();
    if (!exitPending || std::ssize(localParty->members) <= 0) return;
    std::string zone = GameExitTarget(exit);
    if (text::CompareIgnoringCase(zone, kNoZone) == 0) return;
    std::string variable = kZoneVariablePrefix;
    variable += zone;
    float visited = PlayerZeroVariable(variable);
    EI_NAN_AUDIT(visited);
    if (visited == 1.0f) return;
    std::vector<PartyUnitRecord*> records{};
    std::vector<PartyUnitRecord*> second{};
    records.resize(1);
    records[0] = localParty->members.data();
    if (GamePartyInExit(&records, exit, &second)) {
        app::g_interfaceHasScreen = 0;
        GameLeaveThroughExit(exit);
    }
}
