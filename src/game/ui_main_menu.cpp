// The main menu (MainMenu, ui_screens.h): its Create, version line, per-frame update, close, message
// answers, mouse move and keys; the interface's saved-state reset and the regions' SetRect from a
// RECT. The click is in ui_layouts.cpp. See docs/notes/ui.md, section 4.
#include "app.h"
#include "app_helpers.h"
#include "options.h"
#include "sound.h"
#include "ui.h"
#include "ui_screens.h"

#include "base_manager.h"
#include "core_maths.h"
#include "text_manager.h"
#include "map_object_figures.h"
#include "random.h"
#include "unit_create.h"
#include "world.h"

#include "lib/gameapi.h"
#include <cstdint>
#include <string>
#include "world_glue.h"
#include "bink_video.h"
#include "lib/nan_audit.h"
#include "game_singletons.h"
#include "model.h"
#include "figure_manager.h"


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
const char* const kExitMovie = "Movies\\Exit.bik";
const char* const kIntroMovie = "Movies\\Intro.bik";

// The stone animation each region plays while hovered.
constexpr SkillAnimation kStoneAnimation[6] = {SkillAnimation::Briefing01, SkillAnimation::Briefing03,
                                               SkillAnimation::Briefing05, SkillAnimation::Briefing07,
                                               SkillAnimation::Briefing11, SkillAnimation::Briefing09};

// A random word scaled to [0, scale).
float RandomScaled(float scale) { return g_clientRandom.Next() * scale; }

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

// --- A region's SetRect from a RECT ---------------------------------------------------

void UiRegion::SetRectFrom(RECT rect) { SetRect(rect.left, rect.top, rect.right, rect.bottom); }

// --- The version line --------------------------------------------------------------

void MainMenu::AddVersionLine() {
    UiSurface* surface = new UiSurface(ui, 100, 480, 780, 600, SurfaceBlend::Keyed, true);
    surface->Clear();
    std::string text = g_clientWorld.texts->TextManager::GetString(kVersionKey);
    RECT area = {0, 0, 675, 120};
    surface->DrawTextIn(text, &area, 2, 0xA7D7E4, 2, 1);
    AddSurface(surface);
}

// --- Create ----------------------------------------------------------------

void MainMenu::CreateScreen() {
    ui->ResetSavedState();
    g_gameFlow.UseResources(false);
    UiInputSide* inert = new UiInertInput(ui);
    AddInput(inert);
    hovered = -1;
    rect = RECT{0, 0, 800, 600};
    AddVersionLine();
    for (int i = 0; i < 6; ++i) {
        UiRegion* region = AddRegion();
        RECT area;
        gameapi::CopyRect(&area, &g_menuRects[i]);
        region->SetRectFrom(area);
    }
    GamePlayMusic(nullptr);
    savedEdgeFade = options::g_options.borderFogDistance;
    options::g_options.borderFogDistance = kMenuEdgeFade;
    rain = (g_clientRandom.Next() & 1) == 1;
    // The hour is read three times.
    EI_NAN_AUDIT(GameServerTimeOfDay());
    daylight = GameServerTimeOfDay() > kDawn && GameServerTimeOfDay() < kDusk;
    if (GameServerTimeOfDay() < kDawn && rain)
        GameSetWeather(kWeatherRain, kWeatherStrength);
    GameStartAmbience();
    GameShowCameraPath(kCameraPath);
    hour = 0.0f;
    g_clientWorld.music->PlayMainMenu();
}

// --- Update (every frame) ---------------------------------------------------

void MainMenu::Update() {
    UiContainer::Update();
    int serverHour = static_cast<int>(GameServerTimeOfDay());
    if (serverHour != static_cast<int>(hour) && rain) {
        if (serverHour == kRainStartHour) GameSetWeather(kWeatherRain, kWeatherStrength);
        else if (serverHour == kRainEndHour) GameSetWeather(kWeatherClear, kWeatherStrength);
    }
    if (!daylight && rain && serverHour < kLightningBeforeHour && (g_sceneRenderer.flags & 1) != 0) {
        std::uint32_t now = gameapi::GetTickCount();
        if (now > g_nextLightningTick) {
            g_nextLightningTick = now + 100;
            if (g_clientRandom.Next() % 41 == 4) {
                // A bolt from a random point high above the map to one near it on the ground; each
                // coordinate is stored as a float before it is used again.
                float fromX = RandomScaled(4.656613e-09f);
                float fromY = RandomScaled(1.3969839e-08f);
                float toX = RandomScaled(9.313226e-10f) + fromX - 2.0f;
                float toY = RandomScaled(9.313226e-10f) + fromY - 2.0f;
                ShowBolt(Vector3{fromX, fromY, 17.0f}, Vector3{toX, toY, 1.0f});
            }
        }
    }
    hour = static_cast<float>(GameServerTimeOfDay());
}

// --- Close -----------------------------------------------------------------

void MainMenu::CloseScreen() {
    options::g_options.borderFogDistance = savedEdgeFade;
    GameStopSounds(1);
    GameStopAmbience();
}

// --- A message box's answer -----------------------------------------------

void MainMenu::MessageResult(int result) {
    g_gameFlow.UseResources(false);
    GameSetServerPaused(0);
    if (result == 0x10001) {
        {
            std::string path = g_discFolder + kExitMovie;
            PlayMovie(path, 0, 0, 0, 1, 1);
        }
        app::QuitGame(0);
    } else if (result == 0x20001) {
        {
            std::string path = g_discFolder + kIntroMovie;
            PlayMovie(path, 0, 1, 1, 1, 1);
        }
        app::g_interfaceHasScreen = 0;
        g_gameFlow.RequestState3();
    } else if (static_cast<std::uint32_t>(result) == 0xCCCCCCCC) {
        GameStartAmbience();
        AddVersionLine();
    }
}

// --- The mouse move and the keys -------------------------------------------

void MainMenu::MouseMove(int buttons, int x, int y) {
    if (auto* stones = g_clientWorld.objectManager->FindObjectAs<CObject3D>(kStonesObject)) {
        int region = RegionAt(x, y);
        std::string name(stones->name);
        if (region != hovered) {
            FigureAnimation* animation;
            if (region >= 0 && region < 6)
                animation = stones->FindSkillAnimation(kStoneAnimation[region]);
            else
                animation = g_clientWorld.objectManager->FindAnimation(&name, kRestPoseKind);
            stones->PlayAnimation(animation);
            ui->PlaySound(kStoneSound);
        }
        hovered = region;
    }
    UiContainer::MouseMove(buttons, x, y);
}

void MainMenu::KeyDown(int key, int repeat, int modifiers) {
    UiContainer::KeyDown(key, repeat, modifiers);
    if (key != VK_ESCAPE) return;
    ui->PushWindow(new MessageWindow(ui, kQuitTitle, kQuitText, 0x10000, 3, 1), true, true);
}
