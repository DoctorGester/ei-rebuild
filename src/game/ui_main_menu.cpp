// The main menu (CMainMenu, ui_screens.h): its Create, version line, per-frame update, close, message
// answers, mouse move and keys; the interface's saved-state reset and the regions' SetRect from a
// RECT. The click is in ui_layouts.cpp. See docs/notes/ui.md, section 4.
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

#include <cstdint>

namespace {

// The game's glue object (a thiscall target the original passes in ECX; it is not read) and the
// game flow.
void* GlueA() { return reinterpret_cast<void*>(0x007AFEA4); }
GameFlow* Flow() { return reinterpret_cast<GameFlow*>(0x007AFE20); }
void* MusicSystem() { return *reinterpret_cast<void* const*>(0x007AF760); }
IsaacRandom& Random() { return *reinterpret_cast<IsaacRandom*>(0x007A9D40); }
CBaseManager* ObjectManager() { return *reinterpret_cast<CBaseManager* const*>(0x007AF5F8); }

// The width of the fade at the map's edges (read by 006CB020 / 006CB118; 00578D60 / 00578D70 get
// and set it). The menu shows its world with 6.
float& MapEdgeFade() { return *reinterpret_cast<float*>(0x0079BD34); }
// The main menu's six rectangles of menus.reg (loaded by 005ACE80).
const RECT* MenuRects() { return reinterpret_cast<const RECT*>(0x007C8048); }
// Interface options (bit 0: the menu's lightning).
std::uint32_t InterfaceOptions() { return *reinterpret_cast<const volatile std::uint32_t*>(0x007AF064); }
// The tick count of the menu's next lightning chance.
std::uint32_t& NextLightningTick() { return *reinterpret_cast<std::uint32_t*>(0x007C811C); }

// An input side whose every slot does nothing.
void** InertInputVtable() { return reinterpret_cast<void**>(0x00741154); }
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

const char* const kStoneSound = reinterpret_cast<const char*>(0x00793FCC);    // "buttons\menu\stone.wav"
const char* const kQuitTitle = reinterpret_cast<const char*>(0x0079239C);     // "quit"
const char* const kQuitText = reinterpret_cast<const char*>(0x00793FE4);      // "quit_msg"
const char* const kVersionKey = reinterpret_cast<const char*>(0x0079426C);    // "version_name"
const char* const kCameraPath = reinterpret_cast<const char*>(0x0079421C);    // "MainMenu.cam"
const char* const kExitMovie = reinterpret_cast<const char*>(0x0079422C);     // "Movies\Exit.bik"
const char* const kIntroMovie = reinterpret_cast<const char*>(0x0079423C);    // "Movies\Intro.bik"

// The stone animation each region plays while hovered.
constexpr int kStoneAnimation[6] = {1, 3, 5, 7, 11, 9};

// A random word scaled to [0, scale) as the original loads it (FILD qword of the unsigned word).
double RandomScaled(double scale) { return x87::FromUnsigned(Random().Next()) * scale; }

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
EI_REPLACE_METHOD(0x005F51F0, CInterface3D::ResetSavedState);

// --- 0062AC90: a region's SetRect from a RECT ---------------------------------------------------

void CUiRegion::SetRectFrom(RECT rect) { CallSetRect(rect.left, rect.top, rect.right, rect.bottom); }
EI_REPLACE_METHOD(0x0062AC90, CUiRegion::SetRectFrom);

// --- 006335E0: the version line --------------------------------------------------------------

void CMainMenu::AddVersionLine() {
    CUiSurface* surface = nullptr;
    if (void* memory = gamecrt::operator_new(sizeof(CUiSurface)))
        surface = static_cast<CUiSurface*>(memory)->Construct(interface_, 100, 480, 780, 600, 5, 1);
    SurfaceGetDC(surface);
    SurfaceClearOriginal(surface);
    mfc::CString text;
    TextsGetString(GameTexts(), nullptr, kVersionKey, &text);
    RECT rect = {0, 0, 675, 120};
    SurfaceDrawText(surface, nullptr, text, &rect, 2, 0xA7D7E4, 2, 1);
    SurfaceReleaseDC(surface);
    AddSurface(surface);
}
EI_REPLACE_METHOD(0x006335E0, CMainMenu::AddVersionLine);

// --- 006326B0: Create (slot 25) --------------------------------------------------------------

void CMainMenu::Create() {
    interface_->ResetSavedState();
    Flow()->UseResources(false);
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
        RegionSetRectFromOriginal(region, nullptr, rect.left, rect.top, rect.right, rect.bottom);
    }
    GamePlayMusic(nullptr);
    savedEdgeFade_ = MapEdgeFade();
    MapEdgeFade() = kMenuEdgeFade;
    rain_ = (Random().Next() & 1) == 1;
    // Unordered compares (NaN) count as false, as FCOMP with TEST AH,0x41 / AH,1.
    // The hour is read three times (FCOMP of the unrounded result): after dawn (NaN: no), before
    // dusk (NaN: yes), before dawn (NaN: yes).
    daylight_ = GameServerTimeOfDay() > kDawn && !(GameServerTimeOfDay() >= kDusk);
    if (!(GameServerTimeOfDay() >= kDawn) && rain_)
        GameSetWeather(kWeatherRain, kWeatherStrength);
    GameStartAmbience();
    GameShowCameraPath(kCameraPath);
    hour_ = 0.0f;
    static_cast<CMusicSystem*>(MusicSystem())->PlayMainMenu();
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
        if (now > NextLightningTick()) {
            NextLightningTick() = now + 100;
            if (Random().Next() % 41 == 4) {
                // A bolt from a random point high above the map to one near it on the ground; each
                // coordinate is stored as a float before it is used again.
                float fromX = static_cast<float>(RandomScaled(4.656612874161595e-09));    // 0073CD60
                float fromY = static_cast<float>(RandomScaled(1.3969838622484784e-08));   // 00742110
                float toX = static_cast<float>(RandomScaled(9.31322574832319e-10) + fromX - 2.0);   // 0073FA20, 0073CDB8
                float toY = static_cast<float>(RandomScaled(9.31322574832319e-10) + fromY - 2.0);
                ShowBoltOriginal(GlueA(), nullptr, fromX, fromY, 17.0f, toX, toY, 1.0f);
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
    Flow()->UseResources(false);
    GameSetServerPaused(0);
    if (result == 0x10001) {
        {
            mfc::CString path = *GameBasePathString() + kExitMovie;
            PlayMovieFile(path, 0, 0, 0, 1, 1, 1);
        }
        QuitGame(0);
    } else if (result == 0x20001) {
        {
            mfc::CString path = *GameBasePathString() + kIntroMovie;
            PlayMovieFile(path, 0, 1, 1, 1, 1, 1);
        }
        InterfaceHasScreen() = false;
        Flow()->RequestState3();
    } else if (static_cast<std::uint32_t>(result) == 0xCCCCCCCC) {
        GameStartAmbience();
        AddVersionLine();
    }
}
EI_REPLACE_METHOD(0x00632940, CMainMenu::OnMessageResult);

// --- 00633400 / 00633540: the mouse move and the keys -------------------------------------------

void CMainMenu::OnMouseMove(int buttons, int x, int y) {
    if (auto* stones = reinterpret_cast<CObject3D*>(ManagerFindObject(ObjectManager(), nullptr, kStonesObject))) {
        int region = RegionAt(x, y);
        mfc::CString name(stones->name);
        if (region != hovered_) {
            void* animation;
            if (region >= 0 && region < 6)
                animation = stones->FindSkillAnimation(kStoneAnimation[region]);
            else
                animation = reinterpret_cast<CMapObjectManager*>(ObjectManager())->FindAnimation(&name, kRestPoseKind);
            static_cast<CObject3D*>(stones)->PlayAnimation(static_cast<FigureAnimation*>(animation), 1);
            interface_->PlaySound(kStoneSound, 0);
        }
        hovered_ = region;
    }
    InputOf(this)->BaseMouseMove(buttons, x, y);
}
namespace {
} // namespace
EI_REPLACE_METHOD(0x00633400, InputForward<&CMainMenu::OnMouseMove>::Call);

void CMainMenu::OnKeyDown(int key, int repeat, int flags) {
    InputOf(this)->BaseKeyDown(key, repeat, flags);
    if (key != VK_ESCAPE) return;
    void* box = gamecrt::operator_new(0x180);
    if (box) box = MessageBoxConstructOriginal(box, nullptr, interface_, kQuitTitle, kQuitText, 0x10000, 3, 1);
    interface_->PushWindow(static_cast<CUiWindow*>(box), 1, 1);
}
namespace {
} // namespace
EI_REPLACE_METHOD(0x00633540, InputForward<&CMainMenu::OnKeyDown>::Call);
