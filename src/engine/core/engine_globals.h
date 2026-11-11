#pragma once

// The engine's global objects (docs/notes/file-layout.md, "The engine library"): the scene renderer, the camera and the
// game's folders. The game's own (the worlds, the interface, the game flow) are in game/app/singletons.h, which
// includes this.

#include <string>

class SceneRenderer;
class Camera;

// The scene renderer.
extern SceneRenderer g_sceneRenderer;   // scene/renderer_scene.cpp

// The camera.
extern Camera g_camera;   // scene/camera_object.cpp

// The game's folders (set by app/app.cpp, defined in data/resource_manager.cpp): where the game was started, the text
// resources' and the data folder (with its trailing backslash; the movies, the voices and the saves are under it).
extern std::string g_startFolder;
extern std::string g_textFolder;
extern std::string g_dataFolder;
