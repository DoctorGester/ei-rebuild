#pragma once

// The layered path map (docs/notes/mods.md): folders whose files stand over the game folder's, read top first, with a
// place for writes and one for the saves. The game's builds map their paths through the chosen mod's (seams.cpp:
// SystemPath, SystemSearches); the harness's builds through the run's overlay with the mod's layer under it
// (tests/verify/harness.cpp, MapPath). Every path here is in the game's form (UTF-8, backslashes); "full" is a path
// from the root of its drive or system, the separators collapsed. For the backends and the harness, not for game code.

#include "platform/platform.h"
#include "platform/seams.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace platform {

// `path` in full: a relative one from the current folder, "." and ".." folded in; empty for an empty path or one
// relative to a drive's current folder ("C:name").
std::string FullGamePath(std::string_view path);

// The rest of `full` below `folder` (both full, the folder without a trailing separator unless it is a root): empty
// for the folder itself; false when `full` is not in it. Windows' case rules.
bool PathBelow(std::string_view full, std::string_view folder, std::string_view* rest);

// One layer: the files it holds for the game folder's.
struct PathLayer {
    std::string folder;   // in full, no trailing separator
    // The layer's path for the game folder's `relative` (a file or a folder; empty: the folder itself), or empty when
    // the layer never holds it. Without one: `folder` and `relative`.
    std::function<std::string(std::string_view relative)> place;
    // The files the layer answers for even when it lacks them: a read finds the missing file, the layers below it are
    // not asked (a folder search lists the layer's alone).
    std::function<bool(std::string_view relative)> owns;
    // The layer cannot be written (a zip): the game's write of a file it holds goes to the layer above it, an Update
    // copying the file up first. Without a layer above, the write is left to fail where it is.
    bool readOnly = false;

    std::string Place(std::string_view relative) const;
};

class PathLayers {
public:
    // The game folders the layers stand over, in full (the harness's settings folder may be a second).
    std::vector<std::string> bases;
    // Top first; the game folder itself is under the last.
    std::vector<PathLayer> layers;
    // Writes of the game folder's files go to the top layer (an Update copies the file from below first, when
    // `copiesUp` allows it); else each write goes to the top layer that has the file, or to the game folder.
    bool writesToTop = false;
    std::function<bool(std::string_view relative, const std::string& from)> copiesUp;
    // Where the game folder's saves\ is (empty: in the game folder).
    std::string saves;

    // What Map did with a path.
    enum class Placed : std::uint8_t { Kept, Saves, Layer };

    // The path the system is asked for when the game names `path` for `use`: the saves' place for saves\, the top layer
    // holding a read file, the place of a write; `path` itself when no layer has it or it is not the game folder's.
    std::string Map(std::string_view path, PathUse use, Placed* placed = nullptr) const;
    // The folder searches that make up a search of `pattern` (a folder, a separator and a name with * and ?), top
    // first; a name found by one hides the same name further down.
    std::vector<FolderSearch> Searches(std::string_view pattern) const;

private:
    // The game folder's `relative` path of `full`; false when the path is not below a base or is already in place
    // (the saves' folder, the written layer).
    bool Relative(const std::string& full, std::string* relative) const;
    // A read of `relative` from layer `from` down: the first layer's file that exists (or that owns it); empty when
    // the game folder's own is the one.
    // `layer` is the one that answered (layers.size() for the game's own).
    std::string Find(std::string_view relative, std::size_t from, std::size_t* layer = nullptr) const;
};

// The mod the game plays (platform.h, SwitchMod): its folder, its name (the folder's last part: its saves' folder), its
// layer (read only for a zip, with the layer of the files the game writes for it, `saves\<name>iles\`, above) over the game folder (every file in its folders, not its own root's files such as config.reg, and no .res
// archive: the mod's config.reg [RES] combines those with the game's inside them) and the game's map with it. Null
// without one.
struct Mod {
    std::string folder;
    std::string name;
    PathLayer layer;
    PathLayers map;
};
const Mod* CurrentMod();

// Each backend's folder search: the entries `systemPattern` (mapped) matches, their paths in the folder of
// `gamePattern` (as the game named it, made absolute), in the system's order ("." and ".." too on Windows).
std::vector<FolderEntry> SearchFolder(std::string_view systemPattern, std::string_view gamePattern);

} // namespace platform
