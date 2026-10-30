// The figure loader's constructor (returns this; the client world's setup news it
// with the device object and the game's folder). Rebirth patches it (its resource override, at the
// first archive's name), so this is the vanilla body, installed in the vanilla launch only, and
// callers go through its address. See docs/notes/renderer.md (section 12).
#include "mod_folders.h"
#include "model.h"
#include "texture.h"   // TextureDevice

#include <cstddef>
#include <string>

namespace {

const char* FiguresArchive() { return "figures.res"; }
const char* MenusArchive() { return "menus.res"; }

} // namespace

// An empty archive list, figure set table and cache, the device object counted, then the two archives of `folder` opened (a failure is only logged). The original also
// leaves bytes of its empty hash, equality and compare objects in the containers' padding (stack contents,
// and the low byte of `renderer` in the cache's); nothing reads them and they are not written here.
LinkFigureLoader::LinkFigureLoader(TextureDevice* device, const std::string* folder) {
    renderer.Assign(device);
    // Rebirth's ResOverride: the mod's figure archives come first.
    if (ei::rebirth::Enabled(ei::rebirth::Patch::ResOverride)) {
        for (const std::string& modPath : modfolders::FigureArchives()) {
            const std::string path = modPath;
            AddArchive(&path);
        }
    }
    {
        const std::string path = *folder + FiguresArchive();
        AddArchive(&path);
    }
    {
        const std::string path = *folder + MenusArchive();
        AddArchive(&path);
    }
}
