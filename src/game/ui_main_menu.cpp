// The main menu (CMainMenu, ui_screens.h): its Create, version line, per-frame update, close, message
// answers, mouse move and keys; the interface's saved-state reset and the regions' SetRect from a
// RECT. The click is in ui_layouts.cpp. See docs/notes/ui.md, section 4.
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

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_core.h"
#include "originals_engine.h"
#include "originals_ui.h"
#include "originals_world.h"
#include "world_glue.h"
#include "bink_video.h"
#include "game_data.h"
#include "lib/nan_audit.h"
#include "game_singletons.h"

#include <cstdint>

namespace {

// The game flow.

// The width of the fade at the map's edges (read by 006CB020 / 006CB118; 00578D60 / 00578D70 get
// and set it). The menu shows its world with 6.
float& MapEdgeFade() { return options::TheOptions()->borderFogDistance_; }
// The main menu's six rectangles of menus.reg (loaded by 005ACE80).
const RECT* MenuRects() { return ei::GamePointer<const RECT>(0x007C8048); }
// Interface options (bit 0: the menu's lightning).
std::uint32_t InterfaceOptions() { return ei::GameData<const volatile std::uint32_t>(0x007AF064); }
// The tick count of the menu's next lightning chance.
std::uint32_t g_nextLightningTick = /*007C811C*/ {};

// An input side whose every slot does nothing.
void** InertInputVtable() { return ei::GamePointer<void*>(0x00741154); }
constexpr int kStonesObject = 2;                            // the object manager's id of the stones
constexpr std::uint32_t kRestPoseKind = 0x80590000;
constexpr float kMenuEdgeFade = 6.0f;
constexpr float kWeatherStrength = 20.0f;
constexpr int kWeatherClear = 0;
constexpr int kWeatherRain = 1;
constexpr double kDawn = 7.0;    // 0073FF24
constexpr double kDusk = 21.0;   // 0074054C
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

// A random word scaled to [0, scale) as the original loads it (FILD qword of the unsigned word).
double RandomScaled(double scale) { return x87::FromUnsigned(IsaacRandom::Global().Next()) * scale; }

} // namespace

// --- 005F51F0: the interface's saved state cleared ----------------------------------------------

void CInterface3D::ResetSavedState() {
    hudPanelsIn_[0] = 0;
    hudPanelsIn_[1] = 0;
    hudPanelsIn_[2] = 0;
    hudBodyPart_ = 0;
    hudTopPage_ = 0;
    fastGame_ = 0;
}
EI_REWRITTEN(0x005F51F0, CInterface3D::ResetSavedState);

// --- 0062AC90: a region's SetRect from a RECT ---------------------------------------------------

void CUiRegion::SetRectFrom(RECT rect) { CallSetRect(rect.left, rect.top, rect.right, rect.bottom); }
EI_REWRITTEN(0x0062AC90, CUiRegion::SetRectFrom);

// --- 006335E0: the version line --------------------------------------------------------------

void CMainMenu::AddVersionLine() {
    CUiSurface* surface = nullptr;
    if (void* memory = gamecrt::operator_new(sizeof(CUiSurface)))
        surface = static_cast<CUiSurface*>(memory)->Construct(interface_, 100, 480, 780, 600, 5, 1);
    UiSurfaceGetDC(surface);
    SurfaceClearOriginal(surface);
    mfc::CString text;
    TextsGetString(TheTextManager(), nullptr, kVersionKey, &text);
    RECT rect = {0, 0, 675, 120};
    SurfaceDrawText(surface, nullptr, text, &rect, 2, 0xA7D7E4, 2, 1);
    UiSurfaceReleaseDC(surface);
    AddSurface(surface);
}
EI_REWRITTEN(0x006335E0, CMainMenu::AddVersionLine);

// --- 006326B0: Create (slot 25) --------------------------------------------------------------

void CMainMenu::Create() {
    interface_->ResetSavedState();
    TheGameFlow()->UseResources(false);
    UiInputSide* inert = static_cast<UiInputSide*>(gamecrt::operator_new(0xC));
    if (inert) {
        inert->vtable_ = InertInputVtable();
        inert->owner_ = interface_;
    }
    static_cast<CUiContainer*>(this)->AddInput(inert);
    hovered_ = -1;
    rect_ = RECT{0, 0, 800, 600};
    AddVersionLine();
    for (int i = 0; i < 6; ++i) {
        CUiRegion* region = AddRegion();
        RECT rect;
        gameapi::CopyRect(&rect, &MenuRects()[i]);
        region->SetRectFrom(rect);
    }
    GamePlayMusic(nullptr);
    savedEdgeFade_ = MapEdgeFade();
    MapEdgeFade() = kMenuEdgeFade;
    rain_ = (IsaacRandom::Global().Next() & 1) == 1;
    // The hour is read three times (the original compares the unrounded result each time).
    EI_NAN_AUDIT(GameServerTimeOfDay());
    daylight_ = GameServerTimeOfDay() > kDawn && GameServerTimeOfDay() < kDusk;
    if (GameServerTimeOfDay() < kDawn && rain_)
        GameSetWeather(kWeatherRain, kWeatherStrength);
    GameStartAmbience();
    GameShowCameraPath(kCameraPath);
    hour_ = 0.0f;
    TheMusicSystem()->PlayMainMenu();
}
EI_REPLACE_METHOD(0x006326B0, CMainMenu::Create);

// --- 00632B10: Update (slot 24, every frame) ---------------------------------------------------

void CMainMenu::Update() {
    UiUpdateChildrenOriginal(this);
    int hour = static_cast<int>(GameServerTimeOfDay());
    if (hour != static_cast<int>(hour_) && rain_) {
        if (hour == kRainStartHour) GameSetWeather(kWeatherRain, kWeatherStrength);
        else if (hour == kRainEndHour) GameSetWeather(kWeatherClear, kWeatherStrength);
    }
    if (!daylight_ && rain_ && hour < kLightningBeforeHour && (InterfaceOptions() & 1) != 0) {
        std::uint32_t now = gameapi::GetTickCount();
        if (now > g_nextLightningTick) {
            g_nextLightningTick = now + 100;
            if (IsaacRandom::Global().Next() % 41 == 4) {
                // A bolt from a random point high above the map to one near it on the ground; each
                // coordinate is stored as a float before it is used again.
                float fromX = static_cast<float>(RandomScaled(4.656612874161595e-09));    // 0073CD60
                float fromY = static_cast<float>(RandomScaled(1.3969838622484784e-08));   // 00742110
                float toX = static_cast<float>(RandomScaled(9.31322574832319e-10) + fromX - 2.0);   // 0073FA20, 0073CDB8
                float toY = static_cast<float>(RandomScaled(9.31322574832319e-10) + fromY - 2.0);
                ShowBolt(Vector3{fromX, fromY, 17.0f}, Vector3{toX, toY, 1.0f});
            }
        }
    }
    hour_ = static_cast<float>(GameServerTimeOfDay());
}
EI_REPLACE_METHOD(0x00632B10, CMainMenu::Update);

// --- 00632910: Close (slot 26) -----------------------------------------------------------------

void CMainMenu::Close() {
    MapEdgeFade() = savedEdgeFade_;
    GameStopSounds(1);
    GameStopAmbience();
}
EI_REPLACE_METHOD(0x00632910, CMainMenu::Close);

// --- 00632940: a message box's answer (slot 27) -----------------------------------------------

void CMainMenu::OnMessageResult(int result) {
    TheGameFlow()->UseResources(false);
    GameSetServerPaused(0);
    if (result == 0x10001) {
        {
            mfc::CString path = *GameBasePathString() + kExitMovie;
            PlayMovie(path, 0, 0, 0, 1, 1, 1);
        }
        app::QuitGame(0);
    } else if (result == 0x20001) {
        {
            mfc::CString path = *GameBasePathString() + kIntroMovie;
            PlayMovie(path, 0, 1, 1, 1, 1, 1);
        }
        InterfaceHasScreen() = false;
        TheGameFlow()->RequestState3();
    } else if (static_cast<std::uint32_t>(result) == 0xCCCCCCCC) {
        GameStartAmbience();
        AddVersionLine();
    }
}
EI_REPLACE_METHOD(0x00632940, CMainMenu::OnMessageResult);

// --- 00633400 / 00633540: the mouse move and the keys -------------------------------------------

void CMainMenu::OnMouseMove(int buttons, int x, int y) {
    if (auto* stones = reinterpret_cast<CObject3D*>(TheObjectManager()->FindObject(kStonesObject))) {
        int region = RegionAt(x, y);
        mfc::CString name(stones->name);
        if (region != hovered_) {
            void* animation;
            if (region >= 0 && region < 6)
                animation = stones->FindSkillAnimation(kStoneAnimation[region]);
            else
                animation = reinterpret_cast<CMapObjectManager*>(TheObjectManager())->FindAnimation(&name, kRestPoseKind);
            stones->PlayAnimation(static_cast<FigureAnimation*>(animation), 1);
            interface_->PlaySound(kStoneSound, 0);
        }
        hovered_ = region;
    }
    InputOf(this)->OnMouseMove(buttons, x, y);
}
EI_REPLACE_METHOD(0x00633400, InputForward<&CMainMenu::OnMouseMove>::Call);

void CMainMenu::OnKeyDown(int key, int repeat, int flags) {
    InputOf(this)->OnKeyDown(key, repeat, flags);
    if (key != VK_ESCAPE) return;
    void* box = gamecrt::operator_new(0x180);
    if (box) box = static_cast<CMessageBox*>(box)->Construct(interface_, kQuitTitle, kQuitText, 0x10000, 3, 1);
    interface_->PushWindow(static_cast<CUiWindow*>(box), 1, 1);
}
EI_REPLACE_METHOD(0x00633540, InputForward<&CMainMenu::OnKeyDown>::Call);
