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
EI_SETTING(kPaths, "CD-ROM Path", ".\\", "a folder",
           "Where the movies (Movies\\) and the voices (res\\speech.res) are. If it differs from Work path, the "
           "game asks for disc 2 at start.")

// [video]
EI_SETTING(kVideo, "DrawingTarget", "0", "a number",
           "The original's choice of graphics card. Here any value but 0 only draws the wound marks on creatures "
           "as a second layer (meant to look the same).")
EI_SETTING(kVideo, "VideoResolution", "(desktop size)", "WxH",
           "The size the game draws at. Full screen, the picture is scaled to the monitor keeping its shape; in "
           "a window, the window's size.")
EI_SETTING(kVideo, "VideoColorDepthInBits", "32", "16 or 32",
           "16: the interface's drawn colours and screenshots are 16-bit (slight banding); 32: full colour. The "
           "3D world looks the same either way.")
EI_SETTING(kVideo, "FullScreen", "true", "true / false",
           "A borderless window covering the monitor; false: a window with a title bar, the resolution's size, "
           "centred on the main monitor (it can be moved and resized). The display mode never changes.")
EI_SETTING(kVideo, "Video", "2", "0, 1 or 2",
           "The game's movies: 0 none, 1 at their own size in the middle of the screen, 2 stretched to the "
           "screen's width. Loading screens show either way.")
EI_SETTING(kVideo, "Specular", "false", "true / false",
           "Next to no effect: it only sets the models' extra shine until the game first draws the sky or a "
           "panel, which switch the shine on and off themselves.")
EI_SETTING(kVideo, "TextureQuality", "0", "0, 1, 2 or 3",
           "Texture detail: 0 full size, 1 half, 2 a quarter, 3 an eighth (blurrier, less memory).")
EI_SETTING(kVideo, "Mipmapping", "true", "true / false",
           "Distant surfaces use smaller copies of their textures so they don't shimmer; false: always the "
           "full-size texture (grainy in the distance).")
EI_SETTING(kVideo, "Filtering", "2", "0, 1 or 2",
           "0: textures drawn blocky, without smoothing (and without anisotropic filtering); 1 or 2: smoothed "
           "(the two are the same here).")
EI_SETTING(kVideo, "FPSIndependentCursor", "true", "true / false",
           "The mouse cursor keeps moving smoothly when the game's frame rate is low; false: it moves only as "
           "often as the game draws a frame.")
EI_SETTING(kVideo, "Brightness", "0", "-1 .. 1",
           "Brightens (above 0) or darkens (below 0) the whole picture; 0 leaves it as it is. The options "
           "screen's slider sets it.")
EI_SETTING(kVideo, "Contrast", "0", "-1 .. 1",
           "Above 0 makes darks darker and lights lighter; below 0 flattens the picture towards grey; 0 leaves "
           "it as it is.")
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
EI_SETTING(kShadows, "ShadowQuality", "2", "0, 1 or 2",
           "Shadows of units, buildings and trees: 0 off, 1 or 2 on (both the same in this build).")
EI_SETTING(kShadows, "UnitShadow", "true", "true / false",
           "Units, heroes included, cast shadows (with ShadowQuality above 0).")
EI_SETTING(kShadows, "BuildingShadow", "true", "true / false",
           "Buildings and every other object that is not a unit or a tree cast shadows (with ShadowQuality above "
           "0).")
EI_SETTING(kShadows, "FloraShadow", "true", "true / false",
           "Trees and plants cast shadows (with ShadowQuality above 0).")
EI_SETTING(kShadows, "LightingFreq", "10000", "milliseconds",
           "With EnableBloodprintsRelight: how often blood stains are re-lit by the light around them. Its other "
           "use has no visible effect here.")
EI_SETTING(kShadows, "FloraSpecialLight", "false", "true / false",
           "Tree crowns are lit more evenly: their light is pulled most of the way to the crown's average, for "
           "softer foliage.")

// [terrain]
EI_SETTING(kTerrain, "BorderFogDistance", "16", "map units",
           "How wide the band along the map's edges is where land and objects fade into the fog. The menu and "
           "briefing maps use their own.")
EI_SETTING(kTerrain, "EnableWaterWaves", "true", "true / false",
           "Water rises and falls with rolling waves and crests. Off: still water (caves keep their waves).")
EI_SETTING(kTerrain, "EnableSelfShadowing", "true", "true / false",
           "Hills and cliffs cast the sun's shadows across the ground, whatever ShadowQuality is.")
EI_SETTING(kTerrain, "EnableFootprints", "true", "true / false",
           "Units leave footprints on the ground.")
EI_SETTING(kTerrain, "EnableBloodprints", "true", "true / false",
           "Wounds and deaths leave blood stains on the ground.")
EI_SETTING(kTerrain, "EnableFireprints", "true", "true / false",
           "Fire leaves scorch marks on the ground.")
EI_SETTING(kTerrain, "EnableBloodprintsRelight", "false", "true / false",
           "Blood stains are re-lit by the sun and nearby lights every [shadows] LightingFreq. Off: they keep "
           "the light they were made in.")

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
EI_SETTING(kGame, "DefaultWindForce", "0.4", "0 .. 1",
           "The strongest wind a map may have; smoke and particles drift with it. Not 0: it also sets how high "
           "and fast the water's waves run.")
EI_SETTING(kGame, "DefaultTimeScale", "60", "a number",
           "How slowly the day and night clock runs: bigger is slower; at 60 a game day lasts about 20 minutes. "
           "0 stops the clock.")
EI_SETTING(kGame, "SelectionType", "1", "0 or 1",
           "1: a small arrow on the ground under each selected unit shows where it faces. 0: no arrow, selected "
           "units are only highlighted.")
EI_SETTING(kGame, "DrawVisibleOnly", "true", "true / false",
           "On: only units your party can see are drawn. Off shows every unit; the game turns it back on "
           "whenever a battle zone or network game starts.")
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
EI_SETTING(kGame, "CD2 Label", "EICD2", "a disc label",
           "When CD-ROM Path is not the game's folder, the game asks for the disc with this volume label until "
           "it is in.")
EI_SETTING(kGame, "Keyboard Layout", "1033", "a Windows language id",
           "At start Windows' keyboard layout switches to this language (1033 US English, 1049 Russian) if "
           "installed, else to US English.")
EI_SETTING(kGame, "ObjectsLightingCoeff", "2", "a number",
           "How brightly the sun and lights light units, buildings and trees: their light is multiplied by it.")
EI_SETTING(kGame, "Hero Light R", "216", "0 .. 255",
           "Red of the light that moves with your heroes.")
EI_SETTING(kGame, "Hero Light G", "216", "0 .. 255",
           "Green of the light that moves with your heroes.")
EI_SETTING(kGame, "Hero Light B", "116", "0 .. 255",
           "Blue of the light that moves with your heroes.")
EI_SETTING(kGame, "Hero Light Radius", "5", "map units",
           "How far the light around your heroes reaches.")
EI_SETTING(kGame, "Quest Light R", "120", "0 .. 255",
           "Red of the glow on characters and objects a quest marks.")
EI_SETTING(kGame, "Quest Light G", "72", "0 .. 255",
           "Green of the glow on characters and objects a quest marks.")
EI_SETTING(kGame, "Quest Light B", "120", "0 .. 255",
           "Blue of the glow on characters and objects a quest marks.")
EI_SETTING(kGame, "Quest Light Radius", "5", "map units",
           "How far the glow on quest characters and objects reaches.")
EI_SETTING(kGame, "Difficulty Level", "0", "0 or 1",
           "1 Easy, 0 Hard, as the new game's question names them: scales the monsters' attack, defence and "
           "armour. Network games always play at 0.")
EI_SETTING(kGame, "DefaultMapName", "Zone1.mpr", "a map file",
           "The map loaded when the game is started with a .mob file on its command line and no map. Not used in "
           "normal play.")
EI_SETTING(kGame, "DefaultStreamingSound", "melody.mp3", "a music file",
           "The music that plays in the main menu.")
EI_SETTING(kGame, "FogNightStartDistance", "50", "distance",
           "At night, how far from the camera objects start fading into the fog; dawn and dusk blend to the day "
           "value.")
EI_SETTING(kGame, "FogDayStartDistance", "90", "distance",
           "By day and in caves, how far from the camera objects start fading into the fog; they are gone at "
           "FarClipDistance.")

// [network]
EI_SETTING(kNetwork, "Master Server Name", "a3master.nival.com", "host[:port]",
           "The server the multiplayer screen asks for games and hosted games announce to (port 28004 unless "
           "given). Not written back with EiPluginNewMaster.")


// [renderer]
EI_SETTING(kRenderer, "VSync", "true", "true / false",
           "Each frame waits for the monitor's refresh: no tearing. false: frames show as soon as they are "
           "drawn.")
EI_SETTING(kRenderer, "Msaa", "8", "1, 2, 4 or 8",
           "Multisampling of the 3D world: smoother edges on models and the ground. 1 is off; more than the "
           "graphics card offers takes its best.")
EI_SETTING(kRenderer, "Anisotropy", "16", "1 .. 16",
           "Sharper textures on surfaces seen at a slant, such as the ground into the distance. 1 is off.")
EI_SETTING(kRenderer, "PerfOverlay", "false", "true / false",
           "Shows frames per second, frame and graphics card times and draw counts over the game.")

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
           "StreamPatch) look in first. Empty: no mod.")

// [autoexec]
EI_SETTING(kAutoExec, "<name>", "", "a console line",
           "A console command run before autoexec.cfg each time the game runs it; one per line, any name (1 = "
           "<command>).")
