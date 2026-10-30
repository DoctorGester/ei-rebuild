// WinMain's frame step (the interface and the game ticked, the frame drawn and flipped, a
// screenshot per frame while recording a movie) and the intro movies (the list read from an .ini
// file, each played, the screen cleared before and after). See docs/notes/app.md.
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
#include "platform/platform.h"
#include "texture.h"   // TextureDevice, Surface
#include "ui.h"
#include "ui_cursor.h"

#include "lib/bytes.h"
#include "lib/crt.h"

#include "ei/main_loop.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace app {

namespace {

bool Running() { return app::g_active != 0 && app::g_interfaceHasScreen != 0; }

// The software cursor's lock, unless the harness hooks the call (ei/main_loop.h): its frame capture reads the
// finished frame before the cursor is blitted and the frame flipped (tests/verify/harness_capture.cpp).
void LockCursorAtCallSite() {
    if (auto hook = ei::mainloop::g_harnessHooks.cursorLock) hook(&g_cursor);
    else g_cursor.TakeLock();
}

std::uint32_t g_movieFrame = /*0079BC58*/ {};   // the number of the next frame file of the movie being written

} // namespace

// Nothing while the application is inactive (a 50 ms sleep when no window of the interface is active).
void FrameStep(bool draw, bool cursor) {
    if (!Running()) return;
    if (!g_interface.GetActiveWindow()) {
        platform::Sleep(50);
        return;
    }
    if (!g_sceneRenderer.BeginScene()) return;
    g_interface.Draw();
    g_sceneRenderer.EndScene();
    if (cursor) g_interface.DrawOverlays();
    if (!draw || !Running()) return;
    LockCursorAtCallSite();
    CursorDrawFrame(&g_cursor);
    if (DevicePresent(g_sceneRenderer.device)) game::FatalError("Can't flip render surface");    // "Can't flip render surface"
    g_cursor.ReleaseLock();
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
            const std::string full = g_discFolder.c_str() + std::string("Movies\\") + movie;
            PlayMovie(full, 0, true, false, false, loadingStep);
        }
    }
}

} // namespace app

// --- App device list ---------------------------------------------------------------------------------

// The lever table's records (LeverRecord, lever.h: Levers.ldb; Rebirth patches the start-up code
// that loads them from a chunk archive): the record's fields and the list's archiving. See docs/notes/app.md.

// The record's fields 0 .. 7 (strings 0, 5, 6, 7).
void LeverRecord::Archive(ChunkArchive* archive) {
    archive->StringField(0, &name);
    archive->Field(1, spots, 0x10);
    archive->Field(2, &spotCount);
    archive->Field(3, &farScale);
    archive->Field(4, &duration);
    archive->StringField(5, &unreadTextFirst);
    archive->StringField(6, &sound);
    archive->StringField(7, &unreadTextLast);
}

void ChunkArchive::SerializeLevers(int id, std::vector<LeverRecord>* list) {
    RecordChunks(this, id, list, &LeverRecord::Archive);
}

// --- App registry ------------------------------------------------------------------------------------

// Registers an object type with a factory in a two-way type registry (called twice by WinMain): a prototype
// made by the factory gives the vtable; vtable -> id in one map, id -> factory in the other (the same scheme as
// the network's TypeRegistry::Register). See docs/notes/app.md.

namespace app {

void TypeRegistryRegister(ChunkArchive::ClassRegistry* registry, std::uint32_t id,
                          ChunkArchive::Factory factory) {
    RefCounted* prototype = factory();
    if (prototype) prototype->AddRef();
    std::uintptr_t vtable;
    lib::LoadBytes(vtable, prototype);
    registry->classIds[vtable] = static_cast<std::int32_t>(id);
    registry->factories[id] = factory;
    if (prototype) prototype->Release();
}

} // namespace app
