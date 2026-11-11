// The main menu (MainMenu, ui_screens.h): its Create, version line, per-frame update, close, message
// answers, mouse move and keys; the interface's saved-state reset and the regions' SetRect from a
// RECT. The click is in ui_layouts.cpp. See docs/notes/ui.md, section 4.
#include "game/app/app.h"
#include "game/app/game_flow.h"
#include "engine/mods/mod_list.h"
#include "engine/data/options.h"
#include "game/sound/sound.h"
#include "game/ui/ui.h"
#include "game/ui/ui_message_box.h"
#include "game/ui/ui_screens.h"

#include "engine/core/base_manager.h"
#include "engine/core/core_maths.h"
#include "engine/data/text_manager.h"
#include "game/objects/object3d.h"
#include "engine/core/random.h"
#include "game/world/world.h"

#include "lib/rect.h"
#include "platform/platform.h"
#include <cstdint>
#include <memory>
#include <string>
#include "engine/scene/camera.h"
#include "game/terrain/terrain.h"
#include "game/app/movies.h"
#include "game/app/singletons.h"
#include "engine/scene/scene_renderer.h"
#include "engine/models/figure_manager.h"


namespace {

// The width of the fade at the map's edges. The menu shows its world with 6.
// Interface options (bit 0: the menu's lightning).
// The tick count of the menu's next lightning chance.
std::uint32_t g_nextLightningTick = /*007C811C*/ {};

constexpr int kStonesObject = 2;                            // the object manager's id of the stones
constexpr std::uint32_t kRestPoseKind = 0x80590000;
constexpr float kMenuEdgeFade = 6.0f;
constexpr float kWeatherStrength = 20.0f;
constexpr int kWeatherClear = 0;
constexpr int kWeatherRain = 1;
constexpr float kDawn = 7.0f;
constexpr float kDusk = 21.0f;
constexpr int kRainStartHour = 0;
constexpr int kRainEndHour = 7;
constexpr int kLightningBeforeHour = 6;

const char* const kStoneSound = "buttons\\menu\\stone.wav";
const char* const kQuitTitle = "quit";
const char* const kQuitText = "quit_msg";
const char* const kVersionKey = "version_name";
const char* const kCameraPath = "MainMenu.cam";
constexpr Rect kModRow = {16, 566, 256, 582};   // the list of mods
const char* const kExitMovie = "Movies\\Exit.bik";
const char* const kIntroMovie = "Movies\\Intro.bik";

// The stone animation each button plays while hovered.
constexpr SkillAnimation kStoneAnimation[MainMenu::kButtons] = {SkillAnimation::Briefing01, SkillAnimation::Briefing03,
                                               SkillAnimation::Briefing05, SkillAnimation::Briefing07,
                                               SkillAnimation::Briefing11, SkillAnimation::Briefing09};

// The stones (a unit of the menu's world) play the hovered button's animation, or their rest pose for none.
void PlayStoneAnimation(Object3D* stones, int button) {
    FigureAnimation* animation;
    if (button >= 0)
        animation = stones->FindSkillAnimation(kStoneAnimation[button]);
    else
        animation = g_clientWorld.objectManager->FindAnimation(stones->name, kRestPoseKind);
    stones->PlayAnimation(animation);
}

} // namespace

// --- The interface's saved state cleared ----------------------------------------------

void Interface3D::ResetSavedState() {
    hudPanelsIn[0] = 0;
    hudPanelsIn[1] = 0;
    hudPanelsIn[2] = 0;
    hudBodyPart = 0;
    hudTopPage = 0;
    fastGame = 0;
}

// --- The version line --------------------------------------------------------------

void MainMenu::AddVersionLine() {
    UiSurface* surface = AddSurface(std::make_unique<UiSurface>(100, 480, 780, 600, SurfaceBlend::Keyed, true));
    surface->PaintWith([text = g_clientWorld.texts->TextManager::GetString(kVersionKey)](UiSurface& out) {
        Rect area = {0, 0, 675, 120};
        out.DrawTextIn(text, area, TextStyle{2, kUiTextColour, render::text::Format::Right});
    });
}

// --- The list of mods ----------------------------------------------------------------

void MainMenu::AddModList() {
    modList = nullptr;
    mods = modlist::Scan(app::GameFolder(), platform::ModFolder());
    if (mods.size() < 2) return;
    modList = AddChild(std::make_unique<UiDropList>(this, kModRow));
    for (const modlist::Entry& mod : mods) modList->AddText(mod.title);
    modShown = modlist::IndexOf(mods, platform::ModFolder());
    modList->SetValue(modShown);
    modList->TakeChanged();
}

// --- Create ----------------------------------------------------------------

void MainMenu::CreateScreen() {
    g_interface.ResetSavedState();
    g_gameFlow.UseResources(false);
    hovered = -1;
    // The stones start in their rest pose, as after a hover: a unit playing no animation is taken for lying (its
    // client tick sets flag 8) and casts no shadow until it plays one (docs/notes/ui.md, section 4).
    if (auto* stones = g_clientWorld.objectManager->FindObjectAs<Object3D>(kStonesObject))
        PlayStoneAnimation(stones, hovered);
    rect = Rect{0, 0, 800, 600};
    AddVersionLine();
    for (int i = 0; i < kButtons; ++i) {
        buttons[i] = AddRegion();
        buttons[i]->SetRect(g_menuRects[i]);
    }
    AddModList();
    g_clientWorld.sounds->RequestStream(options::kDefaultStreamingSound, StreamSwitch::Now);
    savedEdgeFade = options::g_options.borderFogDistance;
    options::g_options.borderFogDistance = kMenuEdgeFade;
    rain = (g_clientRandom.Next() & 1) == 1;
    // The hour is read three times.
    daylight = g_serverWorld.TimeOfDay() > kDawn && g_serverWorld.TimeOfDay() < kDusk;
    if (g_serverWorld.TimeOfDay() < kDawn && rain)
        g_serverWorld.SetWeather(kWeatherRain, kWeatherStrength);
    g_clientWorld.terrain->landscapeFlags |= terrain::LandscapeFlag::Ambience;
    if (g_clientWorld.camera->LoadPath(kCameraPath, 0)) g_clientWorld.camera->PlaybackStart();
    hour = 0.0f;
    g_clientWorld.music->PlayMainMenu();
}

// --- Update (every frame) ---------------------------------------------------

void MainMenu::Update() {
    UiContainer::Update();
    if (modList && modList->TakeChanged() && modList->value != modShown) {
        const int picked = modList->value;
        // A refused switch (a network session) leaves the list as it was.
        if (app::RequestModSwitch(mods[picked].setting)) modShown = picked;
        else modList->SetValue(modShown);
        modList->TakeChanged();
    }
    int serverHour = static_cast<int>(g_serverWorld.TimeOfDay());
    if (serverHour != static_cast<int>(hour) && rain) {
        if (serverHour == kRainStartHour) g_serverWorld.SetWeather(kWeatherRain, kWeatherStrength);
        else if (serverHour == kRainEndHour) g_serverWorld.SetWeather(kWeatherClear, kWeatherStrength);
    }
    if (!daylight && rain && serverHour < kLightningBeforeHour && g_sceneRenderer.running) {
        std::uint32_t now = platform::CoarseClock();
        if (now > g_nextLightningTick) {
            g_nextLightningTick = now + 100;
            if (g_clientRandom.Next() % 41 == 4) {
                // A bolt from a random point high above the map to one near it on the ground; each
                // coordinate is stored as a float before it is used again.
                float fromX = g_clientRandom.Below(20.0f);
                float fromY = g_clientRandom.Below(60.0f);
                float toX = g_clientRandom.Below(4.0f) + fromX - 2.0f;
                float toY = g_clientRandom.Below(4.0f) + fromY - 2.0f;
                ShowBolt(Vector3{fromX, fromY, 17.0f}, Vector3{toX, toY, 1.0f}, g_clientRandom);
            }
        }
    }
    hour = g_serverWorld.TimeOfDay();
}

// --- Close -----------------------------------------------------------------

void MainMenu::CloseScreen() {
    options::g_options.borderFogDistance = savedEdgeFade;
    g_clientWorld.sounds->StopAll(true);
    g_clientWorld.terrain->StopAmbience();
}

// --- A message box's answer -----------------------------------------------

void MainMenu::MessageResult(std::uint32_t result) {
    g_gameFlow.UseResources(false);
    g_serverWorld.SetServerPaused(0);
    if (result == BoxResult(MessageQuestion::First, MessageAnswer::Ok)) {
        PlayMovie(g_dataFolder + kExitMovie, false, true, true);
        platform::PostQuit(0);
    } else if (result == BoxResult(MessageQuestion::Second, MessageAnswer::Ok)) {
        PlayMovie(g_dataFolder + kIntroMovie, true, true, true);
        app::g_interfaceHasScreen = 0;
        g_gameFlow.RequestState3();
    } else if (result == 0xCCCCCCCC) {
        g_clientWorld.terrain->landscapeFlags |= terrain::LandscapeFlag::Ambience;
        AddVersionLine();
    }
}

// --- The mouse move and the keys -------------------------------------------

void MainMenu::MouseMove(UiEvent& event) {
    if (auto* stones = g_clientWorld.objectManager->FindObjectAs<Object3D>(kStonesObject)) {
        const int button = RegionIndex(buttons, RegionUnder(event.at));
        if (button != hovered) {
            PlayStoneAnimation(stones, button);
            g_interface.PlayInterfaceSound(kStoneSound);
        }
        hovered = button;
    }
    UiContainer::OnEvent(event);
}

void MainMenu::KeyDown(UiEvent& event) {
    UiContainer::OnEvent(event);
    if (event.key != platform::Key::Escape) return;
    g_interface.PushWindow(std::make_unique<MessageWindow>(kQuitTitle, kQuitText, MessageQuestion::First, MessageButtons::OkCancel), WindowBackdrop::FirstWindow);
}

void MainMenu::DrawSprites(SceneRenderer* pass) { DrawScreen(this, pass, false); }
