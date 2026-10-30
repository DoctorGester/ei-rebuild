// The Rebirth community edition's patches that remain options of the rebuilt code, one flag each (include/ei/rebirth.h).
//
// Named exactly as starter\addon.log prints them ("Applying patch <name>", addon.dll v0.12.4.2) or, prefixed
// EiPlugin, as EI_Plugin.ini [CONFIG] names the feature. Every patch, with what the rebuild does about it, is in
// config/rebirth_patches.csv and docs/notes/rebirth.md. Not here since C68 (user 2026-10-30): the bug fixes (always
// the rebuilt code's behaviour), the patches the rebuild replaces (addon's and the plugin's infrastructure, WideScreen:
// the layout follows the window) and the ones nothing implements (mod config options, inactive EI_Plugin features).
// A settings file, profile or spec that names one of those is understood as before: the name is logged and left out.
//
// The order is part of nothing: flags are looked up by name.
//
// Every flag is off by default (mod folders, multiplayer features, EI_Plugin's features). The scripting and console
// additions, the longer server names, the player numbers, the speed keys in multiplayer and the draw radii (the
// whole map is drawn) are the rebuilt code's own since C68 and have no flag either.
EI_REBIRTH_PATCH(DisableExitOnCrashReport)
EI_REBIRTH_PATCH(MQClient)
EI_REBIRTH_PATCH(Maps)
EI_REBIRTH_PATCH(ResOverride)
EI_REBIRTH_PATCH(MoviesPatch)
EI_REBIRTH_PATCH(CameraPatch)
EI_REBIRTH_PATCH(StreamPatch)
EI_REBIRTH_PATCH(EiPluginNPlayers)
EI_REBIRTH_PATCH(EiPluginPause)
EI_REBIRTH_PATCH(EiPluginNewMaster)
EI_REBIRTH_PATCH(EiPluginBriefings)
EI_REBIRTH_PATCH(EiPluginFixes)
