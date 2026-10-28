#pragma once

// The standalone resource layer: Rebirth's mod-folder patches, which addon.dll v0.12.4.2 applies around the
// selected mod (HKCU\Software\Gipat.Ru\EI_Starter\Settings "AddonPath", a '\' added): - Maps (10 sites): every
// maps-folder user names its file first; the maps folder is the mod's folder when the mod's Maps\ held that
// name at start, else the game's. - ResOverride (25 sites): the mod's config.reg [Res] lists archives that
// come before (or, with '-', instead of) the game's twelve archives, plus extra figure and texture archives;
// every CMappedResource::GetFile and ListFiles goes through addon (its vtable slot and all their calls). -
// MoviesPatch (the BinkOpen import), CameraPatch (the camera path's CFile::Open), StreamPatch (the stream
// file's path): the mod's Movies\, Camera\ and Stream\ copy of the file when there is one.
// More: docs/notes/comments-c22i.md, "mod_folders.h".

#include "ei/rebirth.h"

#include <cstdint>
#include <string>
#include <vector>

class CMappedResource;
namespace modfolders {

// Whether the rewritten code applies patch `p` itself: its flag is on.
bool Active(ei::rebirth::Patch p);

// The mod's folder with a trailing '\' (AddonPath), empty without one.
const std::string& ModPath();

// OpenResource's entry (+8), before the archive opens: the first call loads the mod's
// archives; then `res` is recorded as opened from `path` (an archive the mod overrides or ignores).
void OnOpenResource(CMappedResource* res, const char* path);
// The destructor's entry (+2): `res` is forgotten.
void OnDestroyResource(const CMappedResource* res);
// CMaterialManager::Create's entry (+8): loads the mod's archives (once).
void LoadModArchives();
// CMappedResource::GetFile through addon: the mod's archives for this archive first, then the
// archive itself unless the mod ignores it, then "t\" / "s\" names once more without the prefix.
void* GetFile(CMappedResource* res, const char* name, std::uint32_t* size);
// CMappedResource::ListFiles through addon: appends the mod's archives' names and the archive's own
// (unless ignored) to `out`.
void ListFiles(const CMappedResource* res, std::vector<std::string>* out, const char* pattern);
// The mod's extra archives (their full paths): figures.res / menus.res entries for the figure manager and the
// link figure loader, textures.res / redress.res / menus.res entries for the texture manager.
const std::vector<std::string>& FigureArchives();
const std::vector<std::string>& TextureArchives();

// The site kinds addon passes (1-9): NewZone's map and mob, MapPath, the client's LoadZone, the zone
// table's "*.mq" list, LoadZoneQuests (twice), MapFileExists, WriteMapFile.
enum class MapsSite : int {
    NewZoneMap = 1, NewZoneMob, MapPath, ClientLoadZone, ZoneTable, ZoneQuests, ZoneQuestsPath, FileExists, WriteFile,
};
// Before a maps-folder use: the folder becomes the mod's when its Maps\ has `name` (any case; the
// zone table: when its top folder has a ".mq" file), else the game's. It stays until the next one.
// Nothing without Active(Maps).
void SelectMaps(const char* name, MapsSite site);
// The folder MapsFolder starts from: the game's data folder or the mod's.
const std::string& MapsRoot(const std::string& dataFolder);

// ---- Camera, Stream, Movies ----
// CameraPatch: `path` (the camera\ file CCamera::LoadPath opens), or the mod's Camera\<last part>.
std::string CameraFile(const char* path);
// StreamPatch: whether the mod's Stream\ has `name` (then the stream opens <mod>Stream\<name>).
bool StreamInMod(const char* name);
const std::string& StreamFolder();
// MoviesPatch: `name` (as BinkOpen gets it), or the mod's Movies\<last part>.
std::string MovieFile(const char* name);

} // namespace modfolders
