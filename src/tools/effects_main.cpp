// ei-effects: the effect viewer (docs/modding.md, "The effect viewer"; docs/notes/effects.md), a program of its own on
// the engine: the install's archives and the mod's layers, a scene of its own (effect_viewer_scene.h) and the viewer's
// docked windows over it (dear imgui).
// usage: ei-effects [<effect>] [--mod <folder>]
//   <effect> is picked at the start (an original type's name or a mod's data effect's). --mod plays a mod's files over
//   the install's (a folder, or a .zip; a relative one from the program's folder), as the game's [game] Mod does.
//   The game folder is the one EI_GAME_ROOT names (tools/run_all.py's), else the platform's
//   (platform::DefaultGameFolder: the exe's own, dropped into a game folder; on macOS beside the app or chosen).
//   The windows' layout is kept in <user data>\ei-rebuild-effects\layout.ini (in a harness run, its output folder).
#include "tools/effect_viewer.h"
#include "tools/effect_viewer_scene.h"
#include "tools/effects_app.h"
#include "tools/imgui_layer.h"

#include "engine/core/engine_globals.h"
#include "engine/core/program_window.h"
#include "engine/scene/device.h"
#include "engine/scene/scene_renderer.h"

#include "platform/log.h"
#include "platform/platform.h"
#include "render/render.h"

#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

namespace {

constexpr const char* kTitle = "Evil Islands: effects";
constexpr int kStartWidth = 1600;
constexpr int kStartHeight = 900;
constexpr const char* kFigure = "unhuma";   // the hero's figure (the effects' carrier)

std::string GameFolder() {
    std::string folder = platform::EnvironmentVariable("EI_GAME_ROOT");
    if (folder.empty()) folder = platform::DefaultGameFolder();
    while (folder.size() > 3 && (folder.back() == '\\' || folder.back() == '/')) folder.pop_back();
    return folder;
}

// A mod's folder as given: a relative one from the program's folder (app::UseMod's rule).
std::string ModPath(const std::string& given) {
    if (given.empty()) return {};
    const bool full = given[0] == '\\' || given[0] == '/' || (given.size() >= 2 && given[1] == ':');
    if (full) return given;
    const std::string program = platform::ProgramPath();
    return program.substr(0, program.find_last_of("\\/") + 1) + given;
}

std::string LayoutFile() {
    if (!platform::EnvironmentVariable("EI_OUT_DIR").empty()) return ei::OutputPath("layout.ini");
    const std::string user = platform::UserDataFolder();
    if (user.empty()) return {};
    const std::string folder = user + "ei-rebuild-effects";
    platform::CreateFolder(folder);
    return folder + "\\layout.ini";
}

// The frame follows the window's size, and the projection keeps the 4:3 part's proportions in it (the game's
// screen_layout::Configure): the axis scales are that part over the whole frame.
void FitView(int width, int height) {
    const bool wide = width * 3 >= height * 4;
    const float part43Width = wide ? static_cast<float>(height) * 4.0f / 3.0f : static_cast<float>(width);
    const float part43Height = wide ? static_cast<float>(height) : static_cast<float>(width) * 3.0f / 4.0f;
    g_sceneRenderer.view.axisScale = part43Width / static_cast<float>(width);
    g_sceneRenderer.view.axisScaleY = part43Height / static_cast<float>(height);
}

void FollowWindow(platform::NativeWindow window) {
    const std::optional<Size> client = platform::WindowPixelSize(window);
    TextureDevice* device = g_sceneRenderer.device;
    if (!client || !device || client->w < 1 || client->h < 1) return;
    if (device->screen.right == client->w && device->screen.bottom == client->h) return;
    FitView(client->w, client->h);
    g_sceneRenderer.ResizeScreen(client->w, client->h);
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::string effect, mod;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--mod") == 0 && i + 1 < argc) {
            mod = argv[++i];
        } else if (argv[i][0] != '-' && effect.empty()) {
            effect = argv[i];
        } else {
            std::fprintf(stderr, "usage: ei-effects [<effect>] [--mod <folder>]\n");
            return 2;
        }
    }
    const std::string folder = GameFolder();
    if (folder.empty()) {
        ei::Log("ei-effects: no game folder");
        return 1;
    }
    std::uint32_t error = 0;
    if (!platform::SetCurrentFolder(folder, &error)) {
        ei::Log("ei-effects: cannot enter the game folder %s (error %u)", folder, error);
        return 1;
    }
    ei::Log("ei-effects started in %s", folder);
    using tools::effects_app::g_harnessHooks;
    if (g_harnessHooks.start) g_harnessHooks.start(&mod);

    // The engine's folders and the renderer's start, as the game's settings give them by default.
    g_startFolder = platform::CurrentFolder() + '\\';
    g_dataFolder = ".\\";
    g_textFolder = ".\\res\\";
    EngineConfig& config = g_engineConfig;
    config.workPath = g_dataFolder;
    config.resourcesPath = g_textFolder;
    if (!mod.empty()) {
        const std::string path = ModPath(mod);
        if (platform::SwitchMod(folder, path)) ei::Log("ei-effects: the mod %s", platform::ModFolder());
        else ei::Log("ei-effects: no mod folder %s; the install's files alone", path);
    } else {
        platform::SwitchMod(folder, "");
    }

    platform::StartWindowSystem("ei-effects");
    render::PrepareDevice(1);
    platform::NativeWindow window = platform::CreateGameWindow(kTitle, kStartWidth, kStartHeight, false);
    if (!window) {
        ei::Log("ei-effects: no window");
        return 1;
    }
    g_mainWindow = window;   // the renderer presents on it
    if (g_harnessHooks.windowMade) g_harnessHooks.windowMade(window);
    platform::ShowGameWindow(window);
    const std::optional<Size> client = platform::WindowPixelSize(window);
    config.resolutionX = client ? client->w : kStartWidth;
    config.resolutionY = client ? client->h : kStartHeight;
    config.vsync = true;
    FitView(config.resolutionX, config.resolutionY);
    g_sceneRenderer.CreateRenderer(window);
    g_sceneRenderer.running = true;

    tools::effect_viewer::ViewerScene& scene = tools::effect_viewer::g_scene;
    if (!scene.Start()) {
        platform::ShowMessageBox(window, "The game's archives (res\\textures.res) were not found: put ei-effects into the "
                                         "game's folder.",
                                 kTitle, platform::MessageButtons::Ok, platform::MessageIcon::Error);
        return 1;
    }
    scene.BuildFigure(kFigure);
    scene.camera.focus = {0.0f, 0.0f, 1.0f};
    tools::imgui_layer::Start(LayoutFile());
    tools::effect_viewer::Open(effect, folder);

    bool running = true;
    while (running) {
        while (const std::optional<platform::Event> event = platform::TakeEvent()) {
            if (event->type == platform::EventType::Quit || event->type == platform::EventType::CloseRequested) {
                running = false;
                break;
            }
            if (!tools::effect_viewer::TakeEvent(*event)) scene.TakeEvent(*event);
        }
        if (!running) break;
        FollowWindow(window);
        scene.Advance(platform::FrameClock(), &tools::effect_viewer::SceneTicked);
        if (!g_sceneRenderer.BeginScene()) {
            platform::Sleep(10);
            continue;
        }
        scene.Draw();
        g_sceneRenderer.EndScene();
        tools::effect_viewer::DrawFrame();
        if (g_harnessHooks.beforePresent) g_harnessHooks.beforePresent();
        render::Present();
        if (g_harnessHooks.afterFrame) g_harnessHooks.afterFrame();
    }
    ei::Log("exit: the quit request");
    tools::imgui_layer::SaveLayout();
    tools::effect_viewer::Close();
    scene.Stop();
    g_sceneRenderer.DestroyRenderer();
    render::Shutdown();
    ei::Log("exit: the exit functions done");
    return 0;
}
