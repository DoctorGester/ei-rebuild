#pragma once

// The engine's global objects (docs/notes/file-layout.md, "The engine library"): the scene renderer, the config the
// program gives the engine at its start and the game's folders. The game's own (the worlds, the camera, the
// interface, the game flow) are in game/app/singletons.h, which includes this.

#include <string>

class SceneRenderer;

// The scene renderer.
extern SceneRenderer g_sceneRenderer;   // scene/renderer_scene.cpp

// What the engine takes from the program's settings, filled once at its start before anything is loaded (the game:
// app/main_loop.cpp from its options and [renderer]): the game's folders as the settings give them, and the
// renderer's start (SceneRenderer::CreateRenderer, the device object's constructor).
struct EngineConfig {
    std::string workPath;        // [paths] Work path: the textures' loose .bmp files, a mod's speech.res
    std::string resourcesPath;   // [paths] Resources path: a mod's config.reg archives
    int resolutionX = 800;       // the frame's size
    int resolutionY = 600;
    bool fullScreen = false;     // a borderless window over the monitor
    bool perfOverlay = false;    // the performance overlay (where render::PerfOverlaySupported)
    bool vsync = false;          // where render::VsyncSwitchSupported
    int msaa = 1;                // the render quality (where render::QualitySettingsSupported)
    int anisotropy = 1;
};
extern EngineConfig g_engineConfig;   // data/resource_manager.cpp

// The game's folders (set by app/app.cpp, defined in data/resource_manager.cpp): where the game was started, the text
// resources' and the data folder (with its trailing backslash; the movies, the voices and the saves are under it).
extern std::string g_startFolder;
extern std::string g_textFolder;
extern std::string g_dataFolder;
