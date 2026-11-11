# Area notes

One file per area, holding what the code does not say: how the systems work, the non-obvious decisions and why, the
formats and the checks. Current state only; history is in git. Each fact lives in one file, and other files link to
it by file name and section. How to write and keep them: docs/rewrite-guide.md, "Notes".

Engine and game:
- `core.md`: maths, transforms, reference counting, the object manager, streams and chunked files, values read and
  written as bytes, the camera, strings, containers.
- `float-maths.md`: plain float maths, where doubles stay, conversions, NaN, the vector helpers.
- `app.md`: start-up, the main loop, the frame step, options, key bindings.
- `console.md`: the developer console.
- `network.md`: sessions, connections, messages, the lobby, the LZ coder, delta replication.
- `objects.md`: world objects, diplomacy, lightning, the map database, players.
- `map_objects.md`, `units.md`, `combat.md`: map objects; units, statistics, levers, traps; the server unit controller.
- `ai.md`: the AI map, paths, motion.
- `unit_ai.md`: the brain and its modes.
- `world.md`: the world server and client, game flow, figure meshes, party records.
- `effects.md`: particles and world effects.
- `items_spells.md`: items, spells, texts, textures, registry files, trader stock, the party.
- `script.md`: the mission script engine and the text reader.
- `lua-scripting.md`: mission scripts on Lua: the generated Lua, the scheduler, saves (Eris).
- `models.md`: models, resources, the draw layer, terrain and ground prints.
- `sound.md`: sound, music and movies (src/audio, src/video).
- `rebirth.md`: what the Rebirth edition patches, and the patch flags.
- `mods.md`: Rebirth's mods from our exe: the settings' Mod, the layered path map, saves per mod, config.reg.

Drawing and the interface:
- `renderer.md`: the renderer (src/render). Renderer workers read it whole.
- `ui.md`: the interface's screens.
- `ui-native.md`: how the interface's surfaces and text draw on the GPU.

Program and platform:
- `file-layout.md`: src/game's and src/engine's folders and file names, the engine library, the map
  tools/layout_map.txt and its tool.
- `standalone.md`: the exe: how it runs, its data, the settings file, the library ports, UTF-8, the drop-in exe.
- `platform.md`: the platform layer (src/platform) and its rule.
- `linux.md`: the Linux build. `macos.md`: the macOS build (SDL3, POSIX, SDL_GPU's Metal).

Tests and tools:
- `harness.md`: running scenarios, the scenario steps, what a harness run does and leaves alone.
- `standalone-verification.md`: how changes are checked (goldens, unit checks, WideScreen references), what makes
  runs repeat, the harness's isolation and the diagnostic tools.
- `work-board.md`: tickets, workers and the test runs board.
