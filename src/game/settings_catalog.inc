// The settings catalog: every setting of ei_settings.ini the game reads, with the default a new player gets (what
// settings::Defaults writes, else what the game takes when the line is absent), the values it takes and what it does
// for the player. Included by settings_document.cpp (settings::Describe; the defaults file's comments) and read by
// tools/gen_settings_doc.py, which writes docs/settings.md and checks the rows against the settings the code reads.
//
// EI_SETTING(section, name, default, values, description): "<n>" in a name stands for a number, "<name>" for any
// name; "(profile)" as a default: the game folder's Rebirth profile decides.

// [paths]
EI_SETTING(kPaths, "Work path", ".\\", "a folder",
           "The game's own folder: saves, multiplayer heroes, config files, maps. Relative to the game's folder; "
           "end it with a backslash.")
EI_SETTING(kPaths, "Resources path", ".\\res\\", "a folder",
           "Where the game's .res archives are: databases, texts, sound effects, models, textures. Relative to "
           "the game's folder; end it with a backslash.")

// [video]
EI_SETTING(kVideo, "VideoResolution", "(desktop size)", "WxH",
           "The size the game draws at. Full screen, the picture is scaled to the monitor keeping its shape; in "
           "a window, the window's size.")
EI_SETTING(kVideo, "FullScreen", "true", "true / false",
           "A borderless window covering the monitor; false: a window with a title bar, the resolution's size, "
           "centred on the main monitor (it can be moved and resized). The display mode never changes. The "
           "options screen's display mode row sets it while the game runs.")
EI_SETTING(kVideo, "Video", "2", "0, 1 or 2",
           "The game's movies: 0 none, 1 at their own size in the middle of the screen, 2 stretched to the "
           "screen's width. Loading screens show either way.")
EI_SETTING(kVideo, "Brightness", "0", "-1 .. 1",
           "Brightens (above 0) or darkens (below 0) the whole picture; 0 leaves it as it is. The options "
           "screen's slider sets it.")
EI_SETTING(kVideo, "Gamma", "0", "-1 .. 1",
           "Above 0 lightens the mid-tones, below 0 darkens them (black and white stay); 0 leaves the picture as "
           "it is.")

// [sound]
EI_SETTING(kSound, "Audio", "true", "true / false",
           "Sound on; false: no sound effects, voices, music or movie sound at all.")
EI_SETTING(kSound, "AudioStreaming", "100", "0 .. 100",
           "The volume of the music and of the movies' sound; 0 turns the music off.")
EI_SETTING(kSound, "AudioSFX", "100", "0 .. 100",
           "The volume of the sound effects.")
EI_SETTING(kSound, "AudioSpeech", "100", "0 .. 100",
           "The volume of the characters' voices.")
EI_SETTING(kSound, "Reverse Stereo", "false", "true / false",
           "Swaps the left and right speakers.")

// [shadows]
EI_SETTING(kShadows, "Shadows", "true", "true / false",
           "Units, buildings and trees cast shadows, and hills and cliffs cast the sun's shadows across the "
           "ground. The options screen's switch sets it.")

// [terrain]
EI_SETTING(kTerrain, "BorderFogDistance", "16", "map units",
           "How wide the band along the map's edges is where land and objects fade into the fog. The menu and "
           "briefing maps use their own.")

// [camera]
EI_SETTING(kCamera, "CameraFlipXAxis", "false", "true / false",
           "Reverses which way the camera turns when you drag sideways with the right mouse button. The keys are "
           "not affected.")
EI_SETTING(kCamera, "CameraFlipYAxis", "false", "true / false",
           "Reverses which way the camera tilts when you drag up or down with the right mouse button. The keys "
           "are not affected.")
EI_SETTING(kCamera, "CameraMinHeight", "3", "world units",
           "The lowest the camera may sit above the ground under it or the tops of nearby trees and buildings; "
           "it is pushed up to keep this clearance.")
EI_SETTING(kCamera, "CameraMaxHeight", "70", "world units",
           "The most a tall object next to the camera (a tree, a building) can lift it above the ground. It does "
           "not limit zooming out.")
EI_SETTING(kCamera, "CameraDefaultHeight", "2", "world units",
           "The camera's height when the game launches. The menu scene and every map place the camera themselves "
           "right away, so it has no visible effect.")
EI_SETTING(kCamera, "CameraDefaultXDeploy", "30", "map X, world units",
           "The camera's map X position when the game launches. The menu scene and every map place the camera "
           "themselves right away, so it has no visible effect.")
EI_SETTING(kCamera, "CameraDefaultYDeploy", "30", "map Y, world units",
           "The camera's map Y position when the game launches. The menu scene and every map place the camera "
           "themselves right away, so it has no visible effect.")
EI_SETTING(kCamera, "CameraMinDistanceToCarrier", "10", "world units",
           "The closest you normally zoom in during play: the wheel stops here and a key zoom past it drifts "
           "back. Panning speeds up as you zoom out from here.")
EI_SETTING(kCamera, "CameraMaxDistanceToCarrier", "50", "world units",
           "The farthest you normally zoom out during play: the wheel stops here and a key zoom past it drifts "
           "back. Panning speeds up as you zoom out toward it.")
EI_SETTING(kCamera, "CameraMinLimitDistanceToCarrier", "3", "world units",
           "The hard limit on zooming in during play: a key zoom can pass the normal closest distance and drift "
           "back, but never gets nearer than this.")
EI_SETTING(kCamera, "CameraMaxLimitDistanceToCarrier", "70", "world units",
           "The hard limit on zooming out during play: a key zoom can pass the normal farthest distance and "
           "drift back, but never goes beyond this.")
EI_SETTING(kCamera, "CameraDefaultDistanceToCarrier", "30", "world units",
           "How far the camera starts from the point it looks at when you first enter a map after launching; "
           "later map changes keep your own zoom.")
EI_SETTING(kCamera, "NearClipDistance", "2", "world units",
           "Anything closer to the camera than this is cut away and not drawn. Read when the game starts.")
EI_SETTING(kCamera, "FarClipDistance", "100", "world units",
           "How far the view reaches: anything farther is not drawn, and the distance fog is at its thickest "
           "here.")
EI_SETTING(kCamera, "CameraDefaultMinPitch", "30", "degrees (0 level, 90 straight down)",
           "The flattest you can tilt the camera during play, in degrees down from level.")
EI_SETTING(kCamera, "CameraDefaultMaxPitch", "60", "degrees (0 level, 90 straight down)",
           "The steepest you can tilt the camera during play; 90 would look straight down.")
EI_SETTING(kCamera, "CameraBriefingMinDistanceToCarrier", "6", "world units",
           "The closest you normally zoom in on the village screens between missions: the wheel stops here and a "
           "key zoom past it drifts back.")
EI_SETTING(kCamera, "CameraBriefingMaxDistanceToCarrier", "27", "world units",
           "The farthest you normally zoom out on the village screens between missions: the wheel stops here and "
           "a key zoom past it drifts back.")
EI_SETTING(kCamera, "CameraBriefingMinLimitDistanceToCarrier", "3", "world units",
           "The hard limit on zooming in on the village screens between missions: the camera never gets nearer "
           "than this.")
EI_SETTING(kCamera, "CameraBriefingMaxLimitDistanceToCarrier", "30", "world units",
           "The hard limit on zooming out on the village screens between missions: the camera never goes beyond "
           "this.")
EI_SETTING(kCamera, "CameraBriefingMinPitch", "40", "degrees (0 level, 90 straight down)",
           "The flattest you can tilt the camera on the village screens between missions, in degrees down from "
           "level.")
EI_SETTING(kCamera, "CameraBriefingMaxPitch", "60", "degrees (0 level, 90 straight down)",
           "The steepest you can tilt the camera on the village screens between missions; 90 would look straight "
           "down.")
EI_SETTING(kCamera, "CameraBorderScrollArea", "1", "pixels, 0 = off",
           "How close to a screen edge the mouse must be to pan the camera; 0 turns edge scrolling off. Edge "
           "panning ignores the scroll speed settings.")
EI_SETTING(kCamera, "CameraFrameSelectionSensetiveArea", "30", "pixels, 0 = off",
           "How far the mouse must move with the left button held before it starts a selection box; 0 turns box "
           "selection off.")
EI_SETTING(kCamera, "CameraTerrainSensetiveArea", "10", "world units, above 0",
           "The camera's look-at point rides on the average ground height this far around it; larger smooths out "
           "bumps. Keep it above 0 (0 hangs the game).")
EI_SETTING(kCamera, "CameraDefaultScrollSpeed", "1.5", "multiplier",
           "Base speed of panning with the camera keys and with Ctrl or Alt + right-drag; multiplied by the "
           "keyboard or mouse power below.")
EI_SETTING(kCamera, "CameraDefaultScrollPowerMouse", "1", "multiplier, 0..2 from the slider",
           "Mouse factor for panning with Ctrl or Alt + right-drag. The options window's mouse speed slider sets "
           "all four mouse factors.")
EI_SETTING(kCamera, "CameraDefaultScrollPowerKeyboard", "0.46", "multiplier, 0..2 from the slider",
           "Keyboard factor for panning with the camera keys. The options window's keyboard speed slider sets "
           "all four keyboard factors.")
EI_SETTING(kCamera, "CameraDefaultRotateSpeed", "0.15", "multiplier",
           "Base speed of turning the camera: right-drag sideways, or Ctrl or Alt + the left/right camera keys.")
EI_SETTING(kCamera, "CameraDefaultRotatePowerMouse", "1", "multiplier, 0..2 from the slider",
           "Mouse factor for turning the camera with a right-drag. The options window's mouse speed slider sets "
           "all four mouse factors.")
EI_SETTING(kCamera, "CameraDefaultRotatePowerKeyboard", "0.46", "multiplier, 0..2 from the slider",
           "Keyboard factor for turning the camera with Ctrl or Alt + the left/right camera keys. The keyboard "
           "speed slider sets all four.")
EI_SETTING(kCamera, "CameraDefaultPitchSpeed", "0.0015", "multiplier",
           "Base speed of tilting the camera: right-drag up or down, or Ctrl or Alt + the up/down camera keys. "
           "Small because the game scales it up.")
EI_SETTING(kCamera, "CameraDefaultPitchPowerMouse", "1", "multiplier, 0..2 from the slider",
           "Mouse factor for tilting the camera with a right-drag. The options window's mouse speed slider sets "
           "all four mouse factors.")
EI_SETTING(kCamera, "CameraDefaultPitchPowerKeyboard", "0.46", "multiplier, 0..2 from the slider",
           "Keyboard factor for tilting the camera with Ctrl or Alt + the up/down camera keys. The keyboard "
           "speed slider sets all four.")
EI_SETTING(kCamera, "CameraDefaultZoomSpeed", "2", "multiplier",
           "Base speed of zooming with the zoom keys and the mouse wheel; at the defaults one wheel notch zooms "
           "about 10%.")
EI_SETTING(kCamera, "CameraDefaultZoomPowerMouse", "1", "multiplier, 0..2 from the slider",
           "Mouse factor for zooming with the wheel. The options window's mouse speed slider sets all four mouse "
           "factors.")
EI_SETTING(kCamera, "CameraDefaultZoomPowerKeyboard", "0.46", "multiplier, 0..2 from the slider",
           "Keyboard factor for zooming with the zoom keys. The options window's keyboard speed slider sets all "
           "four keyboard factors.")
EI_SETTING(kCamera, "CameraShortcut <n> AxisX", "0", "unit axis component",
           "Part of the facing stored with camera view n (Ctrl or Alt + a camera view key saves one); recalling "
           "a view does not turn the camera, so it has no effect.")
EI_SETTING(kCamera, "CameraShortcut <n> AxisY", "0", "unit axis component",
           "Part of the facing stored with camera view n (Ctrl or Alt + a camera view key saves one); recalling "
           "a view does not turn the camera, so it has no effect.")
EI_SETTING(kCamera, "CameraShortcut <n> AxisZ", "1", "unit axis component",
           "Part of the facing stored with camera view n (Ctrl or Alt + a camera view key saves one); recalling "
           "a view does not turn the camera, so it has no effect.")
EI_SETTING(kCamera, "CameraShortcut <n> Angle", "0", "radians",
           "The turn stored with camera view n's facing (Ctrl or Alt + a camera view key saves one); recalling a "
           "view does not turn the camera, so it has no effect.")
EI_SETTING(kCamera, "CameraShortcut <n> Distance", "30", "world units",
           "The zoom of camera view n (0-3). Ctrl or Alt + camera view key n+1 saves the current zoom and tilt; "
           "the key alone brings them back.")
EI_SETTING(kCamera, "CameraShortcut <n> Pitch", "45", "radians (a saved view writes radians)",
           "The tilt of camera view n, recalled with its zoom by camera view key n+1. Saved in radians: the "
           "default 45 tilts about 58 degrees down.")

// [game]
EI_SETTING(kGame, "SelectionType", "1", "0 or 1",
           "1: a small arrow on the ground under each selected unit shows where it faces. 0: no arrow, selected "
           "units are only highlighted.")
EI_SETTING(kGame, "DrawPath", "true", "true / false",
           "When you order a move, a trail of particles shows the path and a marker the destination.")
EI_SETTING(kGame, "DrawFlyingHP", "true", "true / false",
           "Damage numbers float up over units your party sees when they are hit.")
EI_SETTING(kGame, "ShowTutorial", "true", "true / false",
           "Tutorial messages and each screen's help open by themselves the first time. Off: help shows only "
           "when you ask for it.")
EI_SETTING(kGame, "Autosave", "true", "true / false",
           "The game saves itself on entering a zone (savea) and on arriving at the world map (saveb).")
EI_SETTING(kGame, "ToolTip Time", "0.5", "seconds",
           "How long the pointer must rest on a control before its tooltip shows. 0: no tooltips.")
EI_SETTING(kGame, "SwitchFilters", "true", "true / false",
           "In the camp's screens the item lists jump to the tab that fits the page or item you pick.")
EI_SETTING(kGame, "Difficulty Level", "0", "0 or 1",
           "1 Easy, 0 Hard, as the new game's question names them: scales the monsters' attack, defence and "
           "armour. Network games always play at 0.")
EI_SETTING(kGame, "Mod", "", "a folder",
           "The mod to play (docs/notes/mods.md): its folder, relative to the exe's folder (Rebirth's "
           "starter\\Mods\\LostInAstral) or in full. Its files take the place of the game's, its config.reg's "
           "archives are combined with the game's, and its saves are kept apart (saves\\<the folder's name>). "
           "Read at the start. Empty: the game without a mod.")

// [network]
EI_SETTING(kNetwork, "Master Server Name", "a3master.nival.com", "host[:port]",
           "The server the multiplayer screen asks for games and hosted games announce to (port 28004 unless "
           "given). Not written back with EiPluginNewMaster.")


// [renderer]
EI_SETTING(kRenderer, "VSync", "true", "true / false",
           "Each frame waits for the monitor's refresh: no tearing. false: frames show as soon as they are "
           "drawn. The options screen's vertical sync row sets it.")
EI_SETTING(kRenderer, "Msaa", "8", "1, 2, 4 or 8",
           "Multisampling of the 3D world: smoother edges on models and the ground. 1 is off; more than the "
           "graphics card offers takes its best. The options screen's antialiasing row sets it.")
EI_SETTING(kRenderer, "Anisotropy", "16", "1 .. 16",
           "Sharper textures on surfaces seen at a slant, such as the ground into the distance. 1 is off. The "
           "options screen's anisotropic filtering row sets it (1, 2, 4, 8 or 16).")
EI_SETTING(kRenderer, "PerfOverlay", "false", "true / false",
           "Shows frames per second, frame and graphics card times and draw counts over the game. The options "
           "screen's performance overlay row sets it.")

// [patches]
EI_SETTING(kPatches, "DisableExitOnCrashReport", "(profile)", "true / false",
           "After a fatal error the game writes it to its log and goes on, instead of showing it and quitting.")
EI_SETTING(kPatches, "MQClient", "(profile)", "true / false",
           "Multiplayer: files a server sends are received but not saved on this computer.")
EI_SETTING(kPatches, "Maps", "(profile)", "true / false",
           "Maps are looked for in the mod's folder ([rebirth] AddonPath) before the game's.")
EI_SETTING(kPatches, "ResOverride", "(profile)", "true / false",
           "Resource archives and files are looked for in the mod's folder before the game's.")
EI_SETTING(kPatches, "MoviesPatch", "(profile)", "true / false",
           "Movies are looked for in the mod's folder before the game's.")
EI_SETTING(kPatches, "CameraPatch", "(profile)", "true / false",
           "Cut-scene camera paths are looked for in the mod's folder before the game's.")
EI_SETTING(kPatches, "StreamPatch", "(profile)", "true / false",
           "Music streams are looked for in the mod's folder before the game's.")
EI_SETTING(kPatches, "EiPluginNPlayers", "(profile)", "true / false",
           "Multiplayer games for up to NPlayers players: the lobby's player slider and the portraits column "
           "follow it.")
EI_SETTING(kPatches, "EiPluginPause", "(profile)", "true / false",
           "Multiplayer pausing follows Pause: who may pause, and whether pausing and resuming are announced.")
EI_SETTING(kPatches, "EiPluginNewMaster", "(profile)", "true / false",
           "Master server fixes: answers from any address are taken, a link that closes early is kept and the "
           "server name is not saved; with SecondMaster a second server is asked too.")
EI_SETTING(kPatches, "EiPluginBriefings", "(profile)", "true / false",
           "Multiplayer: a client also runs the script that follows a finished briefing.")
EI_SETTING(kPatches, "EiPluginFixes", "(profile)", "true / false",
           "Multiplayer interface fixes: a face's sex from its model name, every unit's skin refreshed, damage "
           "shown with one decimal.")
EI_SETTING(kPatches, "NPlayers", "7", "a number",
           "With EiPluginNPlayers: the most players a multiplayer game can hold (the portraits show up to 7).")
EI_SETTING(kPatches, "Pause", "1", "1, 2 or 3",
           "With EiPluginPause: 1 the game's leader pauses, pausing and resuming announced; 2 any player pauses; "
           "3 the leader pauses, nothing announced.")
EI_SETTING(kPatches, "SecondMaster", "", "host[:port]",
           "With EiPluginNewMaster: a second master server the multiplayer screen also asks for games. Empty: "
           "none.")

// [rebirth]
EI_SETTING(kRebirth, "AddonPath", "", "a folder",
           "The mod's folder, which the mod folder patches (Maps, ResOverride, MoviesPatch, CameraPatch, "
           "StreamPatch) look in first, when [game] Mod names none. Empty: no mod.")

// [autoexec]
EI_SETTING(kAutoExec, "<name>", "", "a console line",
           "A console command run before autoexec.cfg each time the game runs it; one per line, any name (1 = "
           "<command>).")
