# Area notes

One file per area (`<area>.md`), written by whoever rewrites that area (`docs/rewrite-guide.md`, "Notes"). Current state only:
history is in git. Each fact lives in one file; others link to it by file name and section.

- `core.md`: engine base (maths, transforms, reference counting, object manager, streams, chunked files, clipping, camera) and
  the shared container templates. `vector-math.md`: the original's sum orders, `src/lib/vecmath.h`, the V3 migration plan.
- `network.md`: sessions, connections, messages, lobby, LZ coder, delta replication, links. `app.md`: frame step, registries,
  option / device records, key bindings. `console.md`: the developer console.
- `objects.md`: world objects, scenes, options, map database, players and their message handlers. `script.md`: the mission
  script engine, text reader and the rest of 0x00480000-0x00500000 (streams, decals, link figures, sky, party manager).
- `map_objects.md`, `units.md`, `combat.md`: map objects, units / statistics / levers / traps, the server unit controller.
- `ai.md` (AI map, paths, motion), `unit_ai.md` (brain, modes), `world.md` (world server / client, game flow, figure meshes,
  party records), `effects.md` (world effects), `particles.md`.
- `ui-native.md` (plan and state of the native 2D interface layer, tickets N / P), `ui-census.md` (what each interface surface is drawn
  with, measured), `lua-scripting.md` (plan for Lua mission scripts, tickets L).
- `ui.md`: the interface. `items_spells.md`: items, spells, texts, textures, registry, trader stock, party. `sound.md`.
- `models.md`: 3D library, databases, device layer, terrain sectors, resources. `renderer.md`: the sidecar renderer and the
  switch-over (renderer workers read it whole).
- `rebirth.md`: what the Rebirth edition patches and the patch flags of the rebuild.
- `platform-layer.md`: effort estimate for a cross-platform build (not an area note).
- `standalone.md`: how the standalone image runs, where the MFC / STLport / CRT ports live and what they must keep, the UTF-8 plan, the hook retirement's outcome (not an area note).
- `standalone-verification.md`: how changes are checked (goldens, function vectors, WideScreen references), what makes runs repeat, the harness's isolation and diagnostic tools.
- `inventory.md` (library / dead code classification), `cleanup-tooling.md` (checks, progress counts, shared forwarder
  headers), `cleanup-engine-base.md` / `cleanup-network.md` / `cleanup-map-objects.md` / `cleanup-units.md` / `cleanup-items.md` / `cleanup-effects.md` / `cleanup-ui.md` /
  `cleanup-world.md` / `cleanup-figures.md` / `cleanup-ai.md` / `cleanup-script.md` / `cleanup-device.md` (how those classes are declared after cleanup phase 1).
