// WinMain's frame step (the interface drawn and the frame presented, a screenshot per frame while recording a movie)
// and the class registry's registration. See docs/notes/app.md.
#include "app.h"

#include "bink_video.h"
#include "chunk_archive.h"
#include "core_refcount.h"
#include "dib.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "ini_file.h"
#include "lever.h"
#include "model.h"
#include "texture.h"   // TextureDevice, Surface
#include "ui.h"
#include "ui_cursor.h"
#include "main_loop.h"
#include "lib/bytes.h"
#include "lib/crt.h"
#include "platform/platform.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace app {

namespace {

bool Running() { return app::g_active != 0 && app::g_interfaceHasScreen != 0; }

std::uint32_t g_movieFrame = /*0079BC58*/ {};   // the number of the next frame file of the movie being written

} // namespace

// Nothing while the application is inactive (a 50 ms sleep when no window of the interface is active).
void FrameStep(bool draw) {
    if (!Running()) return;
    if (!g_interface.GetActiveWindow()) {
        platform::Sleep(50);
        return;
    }
    if (!g_sceneRenderer.BeginScene()) return;
    g_interface.Draw();
    g_sceneRenderer.EndScene();
    g_interface.DrawOverlays();
    if (!draw || !Running()) return;
    // The harness's frame capture reads the finished frame here (game/main_loop.h).
    if (auto hook = ei::mainloop::g_harnessHooks.beforePresent) hook();
    CursorDrawFrame(&g_cursor);
    DevicePresent(g_sceneRenderer.device);
    if (!app::g_recordingFrames) return;
    char path[256];
    std::snprintf(path, sizeof(path), "%sshot%.4d.bmp", g_movieFolder.c_str(), g_movieFrame);
    platform::File file;
    if (!file.Open(path, platform::FileMode::Create)) {
        game::FatalError("Can't open file \"%s\" to write movie frame.\n", path);   // "Can't open file (%s) to write movie"
        app::g_recordingFrames = 0;
        return;
    }
    Dib* bitmap = CaptureScreen(g_sceneRenderer.device);
    if (bitmap) bitmap->Write(&file);
    delete bitmap;
    file.Close();
    ++g_movieFrame;
}

// Plays the movies listed under `section` in the .ini under the movie folder setting (each
// from the movie directory).
void PlayMovies(const std::string& section, bool loadingStep) {
    IniFile ini;
    ini.SetPath(g_dataFolder + "config\\movie.ini");
    for (const std::string& movie : ini.GetList(section.c_str(), "movies", ',')) {
        {
            const std::string full = g_dataFolder.c_str() + std::string("Movies\\") + movie;
            PlayMovie(full, true, false, loadingStep);
        }
    }
}

} // namespace app

// Registers an object type with a factory in a two-way type registry (called twice by WinMain): a prototype
// made by the factory gives the vtable; vtable -> id in one map, id -> factory in the other (the same scheme as
// the network's TypeRegistry::Register). See docs/notes/app.md.

namespace app {

void TypeRegistryRegister(ChunkArchive::ClassRegistry* registry, std::uint32_t id,
                          ChunkArchive::Factory factory) {
    RefCounted* prototype = factory();
    registry->factories[id] = factory;
    if (!prototype) return;
    prototype->AddRef();
    std::uintptr_t vtable;
    lib::LoadBytes(vtable, prototype);
    registry->classIds[vtable] = static_cast<std::int32_t>(id);
    prototype->Release();
}

} // namespace app
