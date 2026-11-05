// The figure loader's constructor: the client world's setup news it with the device object and the game's
// folder. See docs/notes/renderer.md (section 12).
#include "game/link_figure.h"   // LinkMesh: the cache's entries
#include "game/mod_folders.h"
#include "game/model.h"
#include "game/texture.h"   // TextureDevice

#include <cstddef>
#include <string>

// The device object counted, then the figure archives of `folder` opened (a failure is only logged).
LinkFigureLoader::LinkFigureLoader(TextureDevice* device, const std::string& folder) {
    renderer.Assign(device);
    for (const std::string& path : modfolders::FigureArchivePaths(folder)) AddArchive(path);
}
