#pragma once

// The standalone resource layer: Rebirth's mod-folder patches, which addon.dll v0.12.4.2 applies around the
// selected mod. They apply whenever a mod is played (the settings file's [game] Mod, a '\' added; Active), with no
// flag: - Maps (9 sites): every maps-folder user names its file first; the maps folder is the mod's folder when the
// mod's Maps\ held that name at start, else the game's. - ResOverride (25 sites): the mod's config.reg [Res] lists archives that
// come before (or, with '-', instead of) the game's twelve archives, plus extra figure and texture archives;
// MappedResource::GetFile and ListFiles look in them (ModArchivesOf, Ignores). -
// MoviesPatch (the BinkOpen import), CameraPatch (the camera path's file open), StreamPatch (the stream
// file's path): the mod's Movies\, Camera\ and Stream\ copy of the file when there is one.
// More: docs/notes/rebirth.md, "Resources and paths".

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class MappedResource;
namespace modfolders {

// The mod's folder with a trailing '\': the settings' [game] Mod (platform::ModFolder); empty without one.
const std::string& ModPath();
// Whether a mod is played (ModPath is not empty): the resource sites go through the layer only then.
bool Active();
// The mod changes (a switch of mods, after the game's stop): the mod's archives closed, and what the layer read of the
// mod (its folder, [Res] lists, Maps\ listing, the maps folder chosen) read again at the next use.
void Reset();

// OpenResource's entry (+8), before the archive opens: the first call loads the mod's
// archives; then `res` is recorded as opened from `path` (an archive the mod overrides or ignores).
void OnOpenResource(MappedResource* res, const std::string& path);
// The destructor's entry (+2): `res` is forgotten.
void OnDestroyResource(const MappedResource* res);
// MaterialManager::Create's entry (+8): loads the mod's archives (once).
void LoadModArchives();
// The mod's archives that come before `res` (MappedResource::GetFile and ListFiles look in them first); none without
// a mod or when the mod does not name `res`'s archive.
std::vector<MappedResource*> ModArchivesOf(const MappedResource* res);
// Whether the mod ignores `res` ('-'): GetFile and ListFiles leave its folder and entries out.
bool Ignores(const MappedResource* res);
// MappedResource::CloseFile of a file GetFile took from one of the mod's archives in `res`'s place: that archive (null
// when none of them holds `data`). Addon left such a file loaded and the game's archive logged "File already closed".
MappedResource* ArchiveHolding(const MappedResource* res, void* data);
// The mod's extra archives (their full paths): figures.res / menus.res entries for the figure manager and the
// link figure loader, textures.res / redress.res / menus.res entries for the texture manager.
const std::vector<std::string>& FigureArchives();
// The archives the figure manager and the link figure loader open, in order: the mod's figure archives first
// (Rebirth's ResOverride), then "figures.res" and "menus.res" of `folder`.
std::vector<std::string> FigureArchivePaths(const std::string& folder);
const std::vector<std::string>& TextureArchives();

// The site kinds addon passes (1-8; its 9, WriteMapFile, went with the client's writing of shared files): NewZone's
// map and mob, MapPath, the client's LoadZone, the zone table's "*.mq" list, LoadZoneQuests (twice), MapFileExists.
enum class MapsSite : int {
    NewZoneMap = 1, NewZoneMob, MapPath, ClientLoadZone, ZoneTable, ZoneQuests, ZoneQuestsPath, FileExists,
};
// Before a maps-folder use: the folder becomes the mod's when its Maps\ has `name` (any case; an archive also as its
// folder, docs/notes/mods.md "Loose files"; the zone table: when its top folder has a quest file, mods/maps_folder.h
// IsQuestFile), else the game's. It stays until the next one. Nothing without a mod.
void SelectMaps(std::string_view name, MapsSite site);
// The folder MapsFolder starts from: the game's data folder or the mod's.
const std::string& MapsRoot(const std::string& dataFolder);

// ---- Camera, Stream, Movies ----
// CameraPatch: `path` (the camera\ file Camera::LoadPath opens), or the mod's Camera\<last part> when it has that
// file or its TOML.
std::string CameraFile(std::string_view path);
// StreamPatch: whether the mod's Stream\ has `name` (then the stream opens <mod>Stream\<name>).
bool StreamInMod(std::string_view name);
const std::string& StreamFolder();
// MoviesPatch: `name` (as BinkOpen gets it), or the mod's Movies\<last part>.
std::string MovieFile(std::string_view name);

} // namespace modfolders
