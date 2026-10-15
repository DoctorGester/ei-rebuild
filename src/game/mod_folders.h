#pragma once

// The standalone resource layer (S19b): Rebirth's mod-folder patches, which addon.dll v0.12.4.2 applies
// around the selected mod (HKCU\Software\Gipat.Ru\EI_Starter\Settings "AddonPath", a '\' added):
// - Maps (10 sites): every maps-folder user names its file first; the maps folder (0055E990) is the
//   mod's folder when the mod's Maps\ held that name at start, else the game's.
// - ResOverride (25 sites): the mod's config.reg [Res] lists archives that come before (or, with '-',
//   instead of) the game's twelve archives, plus extra figure and texture archives; every
//   CMappedResource::GetFile and ListFiles goes through addon (its vtable slot and all their calls).
// - MoviesPatch (the BinkOpen import), CameraPatch (0040ECD0's CFile::Open), StreamPatch (006BB3A0's
//   path): the mod's Movies\, Camera\ and Stream\ copy of the file when there is one.
// In a hooked Rebirth launch addon does all of it (the patched functions stay original there) and a
// vanilla launch has no mod, so the layer runs in the standalone image only, behind the flags. With
// the Standard mod (no folders, no [Res]) everything passes through, except two things addon changes
// for every archive: a failed GetFile of "t\<name>" / "s\<name>" is tried again as "<name>", and
// ListFiles appends to the caller's array instead of refilling it. docs/notes/rebirth.md, "Resources
// and paths"; addon's code: build\S19b (scratch disassembly).

#include "ei/rebirth.h"

#include <cstdint>
#include <string>
#include <vector>

class CMappedResource;
namespace mfc {
class CString;
struct CStringArray;
}

namespace modfolders {

// Whether the rewritten code applies patch `p` itself: the standalone image with the flag on.
bool Active(ei::rebirth::Patch p);

// The mod's folder with a trailing '\' (AddonPath), empty without one.
const std::string& ModPath();

// ---- ResOverride ----
// OpenResource's entry (00671730+8), before the archive opens: the first call loads the mod's
// archives; then `res` is recorded as opened from `path` (an archive the mod overrides or ignores).
void OnOpenResource(CMappedResource* res, const char* path);
// The destructor's entry (006714A0+2): `res` is forgotten.
void OnDestroyResource(const CMappedResource* res);
// CMaterialManager::Create's entry (0068E640+8): loads the mod's archives (once).
void LoadModArchives();
// CMappedResource::GetFile through addon: the mod's archives for this archive first, then the
// archive itself unless the mod ignores it, then "t\" / "s\" names once more without the prefix.
void* GetFile(CMappedResource* res, const char* name, std::uint32_t* size);
// CMappedResource::ListFiles through addon: appends the mod's archives' names and the archive's own
// (unless ignored) to `out`.
void ListFiles(const CMappedResource* res, mfc::CStringArray* out, const char* pattern);
// The mod's extra archives (their full paths): figures.res / menus.res entries for the figure
// manager (005AA510+34) and the link figure loader (004F68C0+92), textures.res / redress.res /
// menus.res entries for the texture manager (00691500+EF).
const std::vector<std::string>& FigureArchives();
const std::vector<std::string>& TextureArchives();

// ---- Maps ----
// The site kinds addon passes (1-9): NewZone's map and mob, MapPath, the client's LoadZone, the zone
// table's "*.mq" list, LoadZoneQuests (twice), MapFileExists, WriteMapFile.
enum class MapsSite : int {
    NewZoneMap = 1, NewZoneMob, MapPath, ClientLoadZone, ZoneTable, ZoneQuests, ZoneQuestsPath, FileExists, WriteFile,
};
// Before a maps-folder use: the folder becomes the mod's when its Maps\ has `name` (any case; the
// zone table: when its top folder has a ".mq" file), else the game's. It stays until the next one.
// Nothing without Active(Maps).
void SelectMaps(const char* name, MapsSite site);
// The folder MapsFolder (0055E990) starts from: the game's data folder (007AF460) or the mod's.
const mfc::CString& MapsRoot(const mfc::CString& dataFolder);

// ---- Camera, Stream, Movies ----
// CameraPatch: `path` (the camera\ file CCamera::LoadPath opens), or the mod's Camera\<last part>.
std::string CameraFile(const char* path);
// StreamPatch: whether the mod's Stream\ has `name` (then the stream opens <mod>Stream\<name>).
bool StreamInMod(const char* name);
const mfc::CString& StreamFolder();
// MoviesPatch: `name` (as BinkOpen gets it), or the mod's Movies\<last part>.
std::string MovieFile(const char* name);

} // namespace modfolders
