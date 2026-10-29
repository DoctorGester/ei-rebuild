// WinMain's frame step (the interface and the game ticked, the frame drawn and flipped, a
// screenshot per frame while recording a movie) and the intro movies (the list read from an .ini
// file, each played, the screen cleared before and after). See docs/notes/app.md.
#include "app.h"
#include "dib.h"
#include "game_strings.h"
#include "model.h"
#include "texture.h"   // CTextureDevice, Surface

#include "ei/main_loop.h"
#include "lib/file_io.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include <cstdint>
#include <string>
#include "lib/static_object.h"
#include "ui.h"
#include "ui_cursor.h"
#include "ini_file.h"
#include "bink_video.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstdio>
#include <cstring>
#include <memory>

namespace app {

namespace {


bool Running() { return app::g_active != 0 && app::g_interfaceHasScreen != 0; }

// The software cursor's lock, unless the harness hooks the call (ei/main_loop.h): its frame capture reads the
// finished frame before the cursor is blitted and the frame flipped (tests/verify/harness_capture.cpp).
void LockCursorAtCallSite() {
    if (auto hook = ei::mainloop::g_harnessHooks.cursorLock) hook(&g_cursor);
    else g_cursor.TakeLock();
}

// The movie list's .ini path: the folder and the file name joined into `a0`.
std::string* ConcatText(std::string* a0, const std::string* a1, const char* a2) { *a0 = *a1 + a2; return a0; }

std::uint32_t g_movieFrame = /*0079BC58*/ {};   // the number of the next frame file of the movie being written

} // namespace

// Nothing while the application is inactive (a 50 ms sleep when no window of the interface is active).
void FrameStep(bool draw, bool cursor) {
    if (!Running()) return;
    if (!g_interface.GetActiveWindow()) {
        gameapi::Sleep(50);
        return;
    }
    if (!g_sceneRenderer->BeginScene()) return;
    g_interface.Draw();
    g_sceneRenderer->EndScene();
    if (cursor) g_interface.DrawOverlays();
    if (!draw || !Running()) return;
    LockCursorAtCallSite();
    CursorDrawFrame(&g_cursor);
    if (DevicePresent(g_sceneRenderer->device_)) game::FatalError("Can't flip render surface");   // "Can't flip render surface"
    g_cursor.ReleaseLock();
    if (!app::g_recordingFrames) return;
    char path[256];
    std::snprintf(path, sizeof(path), "%sshot%.4d.bmp", g_movieFolder.c_str(), g_movieFrame);
    lib::File file;
    if (!file.Open(path, lib::File::modeCreate | lib::File::modeWrite)) {
        game::FatalError("Can't open file \"%s\" to write movie frame.\n", path);   // "Can't open file (%s) to write movie"
        app::g_recordingFrames = 0;
        return;
    }
    CDib* bitmap = CaptureScreen(g_sceneRenderer->device_);
    if (bitmap) bitmap->Write(&file);
    delete bitmap;
    file.Close();
    ++g_movieFrame;
}

// Plays the movies listed under `section` in the .ini under the movie folder setting (each
// from the movie directory).
void PlayMovies(const std::string& section, int a) {
    std::vector<std::string> list;
    CIniFile ini;
    {
        std::string path;
        ini.SetPath((*ConcatText(&path, &g_dataFolder, "config\\movie.ini")));
    }
    ini.GetList(section.c_str(), "movies", &list, ',');
    for (const std::string& movie : list) {
        {
            const std::string full = g_discFolder.c_str() + std::string("Movies\\") + movie;
            PlayMovie(full, 0, 1, 0, 0, static_cast<char>(a));
        }
    }
}

} // namespace app
