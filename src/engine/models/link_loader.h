#pragma once

// The link figure loader: the .lnk files and the figures they name, their parts' archives and the figures' cached
// models (link_loader.cpp). See docs/notes/renderer.md (section 12) and docs/notes/models.md.

#include "engine/core/ref.h"

#include "lib/str.h"   // ViewHash

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class MappedResource;
class TextureDevice;

// An archive of LinkFigureLoader's: its path and the open archive.
struct LinkArchive {
    std::string name;
    MappedResource* resource = nullptr;   // a MMIOResource (the loader's destructor deletes it)
};

// A figure's cached model: the mesh, bones and animation archives of "<name>.mod / .bon / .anm" (null when
// missing), and the animations read from the last by name. FigureManager::models and the link figure
// loader's figure sets cache them by name.
struct FigureModel {
    // An empty animation map, then the three archives as new CMMIOResources.
    FigureModel();
    // Every cached animation deleted, the map cleared, each archive closed (CloseResource) and deleted.
    ~FigureModel();
    FigureModel(const FigureModel&) = delete;
    FigureModel& operator=(const FigureModel&) = delete;
    MappedResource* FindAnimation(std::string_view name);

    MappedResource* mesh = nullptr;  // the .mod archive
    MappedResource* bones = nullptr;
    MappedResource* animations = nullptr;
    // each a MMIOResource over the animation's data
    std::unordered_map<std::string, MappedResource*, str::ViewHash, std::equal_to<>> animationCache;

    // The link figure loader's sets open the three archives from the images of the figure's .mod / .bon /
    // .anm files (OpenFromMemory); a null image leaves its archive alone.
    void Open(const void* mod, const void* bon, const void* anm);
};

// The model `name`: the data of "<name>.mod", ".bon" and ".anm" from getFile(path), the .mod's first. Without a
// .mod nothing is created, null is returned and the other two files' data go to `discard`.
template <class GetFile, class Discard>
FigureModel* OpenFigureModel(std::string_view name, GetFile&& getFile, Discard&& discard) {
    const std::string path(name);
    auto mod = getFile(path + ".mod");
    auto bon = getFile(path + ".bon");
    auto anm = getFile(path + ".anm");
    if (!mod) {
        discard(bon);
        discard(anm);
        return nullptr;
    }
    auto* model = new FigureModel();
    model->Open(mod, bon, anm);
    return model;
}

// The three coefficients (s, t, u) a link figure's parts are morphed by.
struct Morph {
    float s = 0.0f, t = 0.0f, u = 0.0f;
};

// Loads .lnk link files and the figures they name (created at startup with figures.res and menus.res). A
// figure is a LinkMesh of parts morphed by three coefficients (s, t, u); the loader keeps every mesh it made
// in a multimap by name (a counted reference each) and hands out a cached one whose coefficients are within
// 1e-5 (squared distance) unless asked for a fresh one. Parts come from the figure's own archive set when
// there is one, else from the open archives as "<figure><part>.fig".
class LinkMesh;

class LinkFigureLoader final {
public:
    // The figure and menu archives of `folder` opened (a failure is only logged).
    LinkFigureLoader(TextureDevice* renderer, const std::string& folder);
    ~LinkFigureLoader();
    // Figure `name` with the parts `parts`: the first cached mesh of that name with close coefficients, given the
    // parts it lacks; otherwise a new one (MakeFigure). No reference is added for the caller.
    LinkMesh* LoadFigure(std::string_view name, const std::vector<std::string>& parts, Morph morph);
    // The same for a .lnk figure: a cached one, or LoadLinkFile.
    LinkMesh* LoadLink(std::string_view name, Morph morph);
    void ClearCache();
    // `mesh` with new coefficients: a mesh others also hold (more than two references) is looked up again by
    // its name; otherwise its parts are morphed in place (the mesh takes the new coefficients, then each part). Null stays null.
    LinkMesh* Remorph(LinkMesh* mesh, Morph morph);

    void AddRef() { ++refCount; }
    // Drops a reference; the last one deletes the loader.
    void Release() {
        if (--refCount == 0) delete this;
    }

    LinkFigureLoader(const LinkFigureLoader&) = delete;
    LinkFigureLoader& operator=(const LinkFigureLoader&) = delete;

    // Opens `path` as a memory-mapped archive and appends it to the list, unless an archive of that exact
    // name is already open. Returns 1 on success or when already open.
    int AddArchive(const std::string& path);
    // Reads a FIG / FIG8 part record at *cursor (moved past it) into `mesh` as part `name`: its header (two
    // dwords, bounds fitted to the vertices morphed by `morph`) and its elements (LinkMesh::AddPart).
    void LoadFigPart(LinkMesh* mesh, const std::string& name, const char** cursor, Morph morph);
    // The same for the older record without a FIG header (6 morph corners).
    void LoadOldPart(LinkMesh* mesh, const std::string& name, const char** cursor, Morph morph);

    // A new mesh from .lnk file `name` (from the figure set, else the archives' "<name>.lnk"): a count, then
    // per part its name and a second, unused name (an empty name keeps the previous one). Cached (one
    // reference). No link file ends the game.
    LinkMesh* LoadLinkFile(std::string_view name, Morph morph);
    // A new mesh `name` with the parts `parts`, loaded as LoadLinkFile does (a part missing from the figure
    // set is skipped); cached.
    LinkMesh* MakeFigure(std::string_view name, const std::vector<std::string>& parts, Morph morph);
    // Part `name` of `mesh` (its own coefficients) unless it has it: from its figure set or the archives; false
    // without a record.
    bool LoadPartInto(LinkMesh* mesh, const std::string& name);
    // The record of part `name` of `mesh`: in the figure set's model, else "<mesh name><name>.fig" in the first
    // open archive that has it; null without one.
    const char* FindPartData(const LinkMesh* mesh, FigureModel* set, const std::string& name);
    // Part `name` loaded into `mesh` from its record (FindPartData): false without one.
    bool LoadPart(LinkMesh* mesh, FigureModel* set, const std::string& name, Morph morph);
    // Figure `name`'s own archive set: the cached one, else (when an open archive has "<name>.mod") a new set
    // stored under the name and loaded from the first archives with "<name>.mod" / ".bon" / ".anm"; null
    // without a .mod.
    FigureModel* FindFigureSet(std::string_view name);
    // Part `name` of `mesh` morphed again by `morph` from its record (FindPartData): false without one.
    bool RemorphPart(LinkMesh* mesh, FigureModel* set, const std::string& name, Morph morph);
    // The record at *cursor (moved past the vertices) morphed into the part's vertex packets and its header's
    // bounds refitted. The FIG one reads every record as FIG8 (header, bounds and vertices).
    void RemorphFigPart(LinkMesh* mesh, const std::string& name, const char** cursor, Morph morph);
    void RemorphOldPart(LinkMesh* mesh, const std::string& name, const char** cursor, Morph morph);

    int refCount = 0;
    std::vector<LinkArchive> archives;
    std::unordered_map<std::string, FigureModel*, str::ViewHash, std::equal_to<>> figureSets;   // by figure name
    Ref<TextureDevice> renderer;                                 // the device object
    std::multimap<std::string, HeldRef<LinkMesh>, std::less<>> cache;
};
