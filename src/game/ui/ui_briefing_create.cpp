// The briefing / base screen's Create and per-frame Update
// BriefingScreen, ui_briefing.h. See docs/notes/ui.md, "Briefing / base screen".
#include "game/app/app.h"
#include "game/world/camera.h"
#include "game/combat/combat.h"
#include "engine/core/core_maths.h"
#include "engine/core/core_streams.h"
#include "engine/core/core_transform.h"
#include "game/app/game_flow.h"
#include "game/app/singletons.h"
#include "game/map/map_database.h"
#include "engine/data/text_manager.h"
#include "game/app/options.h"
#include "game/players/party.h"
#include "game/players/player_client.h"
#include "game/players/player_messages.h"
#include "game/sound/sound.h"
#include "engine/core/text_split.h"
#include "game/ui/ui.h"
#include "game/ui/ui_mesh.h"
#include "game/ui/ui_scrollbar.h"
#include "game/ui/ui_scroll_text.h"
#include "game/ui/ui_briefing.h"
#include "game/ui/ui_companions.h"
#include "game/ui/ui_camera_input.h"
#include "game/units/unit.h"
#include "game/ai/unit_ai.h"
#include "game/players/player.h"
#include "game/units/unit_client_models.h"
#include "game/world/world.h"
#include "game/terrain/terrain.h"

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
constexpr const char* kConstructorTopic = "constr";
constexpr const char* kNoZone = "none";

constexpr int kFromCamp = 5;              // Player::lastScreen
constexpr float kEdgeFade = 4.0f;
constexpr std::uint32_t kDarkBackdrop = 0x80000000;
constexpr std::uint32_t kTextBackdrop = 0xA0000000;
constexpr int kTopicBarChild = 0;
constexpr int kTextChild = 1;
constexpr int kTopicsSurface = 0;


// The states (BriefingScreen::state).
constexpr int kWalking = 0;
constexpr int kChoosing = 1;
constexpr int kStarting = 2;
constexpr int kPhrases = 3;
constexpr int kRewards = 4;

constexpr float kMoverTimeout = 30.0f;
constexpr float kCloseEnough = 0.01f;       // squared distance
constexpr int kConstructorPrefix = 6;       // "constr"

// A mover that took too long is put at its place, facing its direction, at once.
void PlaceMover(BriefingMover* node) {
    Unit* unit = g_serverWorld.FindUnit(node->person->id);
    if (!unit) return;
    UnitServer* controller = unit->ServerController();
    Vector3 target = node->target;
    g_serverWorld.aiMap.sectorMap.Move(unit, target);
    Vector3 facing = NormalisedUnlessZero(node->direction);
    unit->SetHeading(facing);
    node->person->SetHeading(facing);
    controller->ResyncWithUnit();
}

// A walking mover near its target starts turning (its server unit turns over a second).
void StepWalking(BriefingMover* node) {
    const Unit* person = node->person;
    if (DistanceSquaredXY(node->target, person->localPosition) >= kCloseEnough) return;
    node->state = BriefingMover::kTurning;
    Unit* unit = g_serverWorld.FindUnit(node->person->id);
    if (!unit) return;
    Vector3 facing = NormalisedUnlessZero(node->direction);
    unit->ServerController()->brain.TurnTowards(facing, 1.0f);
}

// A turning mover whose facing (the client unit's -y axis, flat) is near its direction is done.
void StepTurning(BriefingMover* node) {
    const Unit* person = node->person;
    const Vector3 facing = -person->localRotation.AxisY();
    if (DistanceSquaredXY(NormalisedUnlessZero(node->direction), facing) >= kCloseEnough) return;
    node->state = BriefingMover::kDone;
}

// The meshes shown or hidden, but those in `keep`.
void SetMeshesVisible(UiWindow* window, int visible, std::initializer_list<const UiMesh*> keep = {}) {
    for (UiMesh* mesh : window->sceneObjects)
        if (std::ranges::find(keep, mesh) == keep.end()) mesh->visible = visible;
}

} // namespace

void BriefingScreen::CreateScreen() {
    g_interface.briefing = this;
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
    topicBackdrop = AddBlankSprite();
    topicBackdrop->SetRectangle(20.0f, kSpriteScale, 200.0f, 430.0f, 600.0f, 570.0f, 0, 0, kUiTextureSize,
                                kUiTextureSize);
    topicBackdrop->texture = nullptr;
    topicBackdrop->flags |= FigureMeshFlag::Flat;
    topicBackdrop->flatColour = kDarkBackdrop;
    topicFrame = AddBlankSprite();
    topicFrame->SetFrame(10.0f, 100.0f, 195.0f, 425.0f, 605.0f, 575.0f, 5.0f, 2, 0xFE, 0xB4, 0xF9);
    UiMesh* sprite = AddBlankSprite();
    sprite->SetRectangle(20.0f, kSpriteScale, 155.0f, 416.0f, 645.0f, 580.0f, 0, 0, kUiTextureSize, kUiTextureSize);
    sprite->texture = nullptr;
    sprite->flatColour = kTextBackdrop;
    sprite->flags |= FigureMeshFlag::Flat;
    AddBlankSprite()->SetFrame(20.0f, 100.0f, 150.0f, 411.0f, 650.0f, 585.0f, 5.0f, 2, 0xFE, 0xB4, 0xF9);

    // Surface 0 the topic rows, 1 a person's label.
    AddSurface(std::make_unique<UiSurface>(Rect{200, 430, 600, 570}, SurfaceBlend::Keyed, true))
        ->PaintWith([this](UiSurface& out) { PaintLabels(out, topicLabels); });
    AddSurface(std::make_unique<UiSurface>(Rect{0, 0, 250, 30}, SurfaceBlend::Keyed, true))
        ->PaintWith([this](UiSurface& out) { PaintLabels(out, personLabel); });

    topicBar = AddChild(std::make_unique<UiScrollbar>(Rect{584, 440, 594, 560}, 3.0f, 0, 1.0f));
    text = AddChild(std::make_unique<UiScrollText>("", Rect{165, 426, 635, 570}, 10.0f));

    SetMeshesVisible(this, 0);
    for (const auto& surface : surfaces) surface->Hide();
    for (const auto& child : children) child->SetEnabled(false);
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
    g_clientWorld.terrain->landscapeFlags |= terrain::LandscapeFlag::Ambience;
    g_serverWorld.SetWeather(0, 0.0f);

    // The view's input.
    AddInput(std::make_unique<CameraInput>());

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
        chat = AddChild(std::make_unique<ChatWindow>(this));
        PlayersChanged(1);
        // Ours: the companions are picked here.
        if (CompanionsOffered()) AddChild(std::make_unique<CompanionButton>());
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
    markPhase = FrameTime() + markPhase;
    while (markPhase >= 1.0f) markPhase = markPhase - 1.0f;
    UiContainer::Update();
    SetMeshesVisible(this, 0);
    for (const auto& surface : surfaces) surface->Hide();
    children[kTopicBarChild]->SetEnabled(false);
    children[kTextChild]->SetEnabled(false);

    for (BriefingMover& mover : movers) {
        mover.time = FrameTime() + mover.time;
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
        topicBackdrop->visible = true;
        topicFrame->visible = true;
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
        // The text's meshes and the people's slots; the topics' backdrop and frame stay hidden.
        SetMeshesVisible(this, 1, {topicBackdrop, topicFrame});
        text->SetEnabled(true);
        if (voice != -1) {
            if (!g_clientWorld.sounds->IsPlaying(voice)) NextPhrase(false);
        } else {
            phraseTime -= FrameTime();
            if (phraseTime < 0.0f) NextPhrase(false);
        }
        break;
    case kRewards:
        // The text's meshes and the people's slots; the topics' backdrop and frame stay hidden.
        SetMeshesVisible(this, 1, {topicBackdrop, topicFrame});
        text->SetEnabled(true);
        if (voice != -1) {
            if (speaking && !g_clientWorld.sounds->IsPlaying(voice)) StopTalking(speaking);
        } else {
            phraseTime -= FrameTime();
            if (speaking && phraseTime < 0.0f) StopTalking(speaking);
        }
        break;
    default:
        break;
    }
}

void BriefingScreen::MarkPeople() {
    // The people with a topic open (the list is sorted and deduplicated; only membership is used).
    std::vector<int> open;
    for (const auto& parts : OpenTopicNames(kTopicPrefix)) {
        if (text::CompareIgnoringCase(str::Left(parts[2], kConstructorPrefix), kConstructorTopic) == 0) continue;
        int textKey = TextKeyFromName(parts[1]);
        if (std::find(people.begin(), people.end(), textKey) == people.end()) continue;
        open.push_back(textKey);
    }
    // Marked on the local view directly, where the game is served: a mark sent through the server's player 0
    // reaches the view a tick or two after the screen's first frame.
    auto* player = g_serverWorld.GetPlayer(0);
    if (!player || !player->Server()) return;
    for (auto& personId : people) {
        Unit* unit = g_clientWorld.FindUnit(personId);
        if (!unit) continue;
        g_clientWorld.MarkPerson(unit, std::find(open.begin(), open.end(), personId) != open.end());
    }
    open.clear();
}

void BriefingScreen::LeaveIfExitOpen() {
    Party* localParty = LocalPlayerData();
    if (!exitPending || std::ssize(localParty->members) <= 0) return;
    std::string zone = g_clientWorld.ExitTarget(exit);
    if (text::CompareIgnoringCase(zone, kNoZone) == 0) return;
    if (ZoneVisited(zone)) return;
    std::vector<PartyUnitRecord*> records{};
    std::vector<PartyUnitRecord*> second{};
    records.resize(1);
    records[0] = localParty->members.data();
    if (g_clientWorld.PartyInExit(records, exit, &second)) {
        app::g_interfaceHasScreen = 0;
        g_gameFlow.LeaveThroughExit(exit);
    }
}
