// The main menu (CMainMenu, ui_screens.h): its Create, version line, per-frame update, close, message
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
#include "item_db_text.h"
#include "map_object_figures.h"
#include "random.h"
#include "unit_create.h"
#include "world.h"

#include "ei/x87.h"
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
constexpr double kDawn = 7.0;
constexpr double kDusk = 21.0;
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
constexpr int kStoneAnimation[6] = {1, 3, 5, 7, 11, 9};

// A random word scaled to [0, scale) (the word read unsigned, as a 64-bit integer).
double RandomScaled(double scale) { return x87::FromUnsigned(g_random.Next()) * scale; }

} // namespace

// --- The interface's saved state cleared ----------------------------------------------

void CInterface3D::ResetSavedState() {
    hudPanelsIn_[0] = 0;
    hudPanelsIn_[1] = 0;
    hudPanelsIn_[2] = 0;
    hudBodyPart_ = 0;
    hudTopPage_ = 0;
    fastGame_ = 0;
}

// --- A region's SetRect from a RECT ---------------------------------------------------

void CUiRegion::SetRectFrom(RECT rect) { SetRect(rect.left, rect.top, rect.right, rect.bottom); }

// --- The version line --------------------------------------------------------------

void CMainMenu::AddVersionLine() {
    CUiSurface* surface = new CUiSurface(interface_, 100, 480, 780, 600, 5, 1);
    UiSurfaceGetDC(surface);
    surface->Clear();
    std::string text;
    g_clientWorld->texts_->CTextManager::GetString(kVersionKey, &text);
    RECT rect = {0, 0, 675, 120};
    surface->DrawTextIn(text, &rect, 2, 0xA7D7E4, 2, 1);
    UiSurfaceReleaseDC(surface);
    AddSurface(surface);
}

// --- Create ----------------------------------------------------------------

void CMainMenu::CreateScreen() {
    interface_->ResetSavedState();
    g_gameFlow->UseResources(false);
    UiInputSide* inert = new UiInertInput(interface_);
    AddInput(inert);
    hovered_ = -1;
    rect_ = RECT{0, 0, 800, 600};
    AddVersionLine();
    for (int i = 0; i < 6; ++i) {
        CUiRegion* region = AddRegion();
        RECT rect;
        gameapi::CopyRect(&rect, &g_menuRects[i]);
        region->SetRectFrom(rect);
    }
    GamePlayMusic(nullptr);
    savedEdgeFade_ = options::g_options->borderFogDistance_;
    options::g_options->borderFogDistance_ = kMenuEdgeFade;
    rain_ = (g_random.Next() & 1) == 1;
    // The hour is read three times (the unrounded result is compared each time).
    EI_NAN_AUDIT(GameServerTimeOfDay());
    daylight_ = GameServerTimeOfDay() > kDawn && GameServerTimeOfDay() < kDusk;
    if (GameServerTimeOfDay() < kDawn && rain_)
        GameSetWeather(kWeatherRain, kWeatherStrength);
    GameStartAmbience();
    GameShowCameraPath(kCameraPath);
    hour_ = 0.0f;
    g_clientWorld->music_->PlayMainMenu();
}

// --- Update (every frame) ---------------------------------------------------

void CMainMenu::Update() {
    UpdateBase();
    int hour = static_cast<int>(GameServerTimeOfDay());
    if (hour != static_cast<int>(hour_) && rain_) {
        if (hour == kRainStartHour) GameSetWeather(kWeatherRain, kWeatherStrength);
        else if (hour == kRainEndHour) GameSetWeather(kWeatherClear, kWeatherStrength);
    }
    if (!daylight_ && rain_ && hour < kLightningBeforeHour && (g_sceneRenderer->flags_ & 1) != 0) {
        std::uint32_t now = gameapi::GetTickCount();
        if (now > g_nextLightningTick) {
            g_nextLightningTick = now + 100;
            if (g_random.Next() % 41 == 4) {
                // A bolt from a random point high above the map to one near it on the ground; each
                // coordinate is stored as a float before it is used again.
                float fromX = static_cast<float>(RandomScaled(4.656612874161595e-09));
                float fromY = static_cast<float>(RandomScaled(1.3969838622484784e-08));
                float toX = static_cast<float>(RandomScaled(9.31322574832319e-10) + fromX - 2.0);
                float toY = static_cast<float>(RandomScaled(9.31322574832319e-10) + fromY - 2.0);
                ShowBolt(Vector3{fromX, fromY, 17.0f}, Vector3{toX, toY, 1.0f});
            }
        }
    }
    hour_ = static_cast<float>(GameServerTimeOfDay());
}

// --- Close -----------------------------------------------------------------

void CMainMenu::Close() {
    options::g_options->borderFogDistance_ = savedEdgeFade_;
    GameStopSounds(1);
    GameStopAmbience();
}

// --- A message box's answer -----------------------------------------------

void CMainMenu::OnMessageResult(int result) {
    g_gameFlow->UseResources(false);
    GameSetServerPaused(0);
    if (result == 0x10001) {
        {
            std::string path = *g_discFolder + kExitMovie;
            PlayMovie(path, 0, 0, 0, 1, 1);
        }
        app::QuitGame(0);
    } else if (result == 0x20001) {
        {
            std::string path = *g_discFolder + kIntroMovie;
            PlayMovie(path, 0, 1, 1, 1, 1);
        }
        app::g_interfaceHasScreen = 0;
        g_gameFlow->RequestState3();
    } else if (static_cast<std::uint32_t>(result) == 0xCCCCCCCC) {
        GameStartAmbience();
        AddVersionLine();
    }
}

// --- The mouse move and the keys -------------------------------------------

void CMainMenu::OnMouseMove(int buttons, int x, int y) {
    if (auto* stones = g_clientWorld->objectManager_->FindObjectAs<CObject3D>(kStonesObject)) {
        int region = RegionAt(x, y);
        std::string name(stones->name);
        if (region != hovered_) {
            FigureAnimation* animation;
            if (region >= 0 && region < 6)
                animation = stones->FindSkillAnimation(kStoneAnimation[region]);
            else
                animation = g_clientWorld->objectManager_->FindAnimation(&name, kRestPoseKind);
            stones->PlayAnimation(animation);
            interface_->PlaySound(kStoneSound, 0);
        }
        hovered_ = region;
    }
    CUiContainer::MouseMove(buttons, x, y);
}

void CMainMenu::OnKeyDown(int key, int repeat, int flags) {
    CUiContainer::KeyDown(key, repeat, flags);
    if (key != VK_ESCAPE) return;
    interface_->PushWindow(new CMessageBox(interface_, kQuitTitle, kQuitText, 0x10000, 3, 1), 1, 1);
}
