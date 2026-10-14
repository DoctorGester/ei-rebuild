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
- `ui.md`: the interface. `items_spells.md`: items, spells, texts, textures, registry, trader stock, party. `sound.md`.
- `models.md`: 3D library, databases, device layer, terrain sectors, resources. `renderer.md`: the sidecar renderer and the
  switch-over (renderer workers read it whole).
- `rebirth.md`: what the Rebirth edition patches and the patch flags of the rebuild.
- `platform-layer.md`: effort estimate for a cross-platform build (not an area note).
- `standalone.md`: what still ties the code to the exe, the MFC / STLport / CRT replacement plan, the UTF-8 switch (not an area note).
- `inventory.md` (library / dead code classification), `cleanup-tooling.md` (checks, progress counts, shared forwarder
  headers), `cleanup-engine-base.md` / `cleanup-network.md` / `cleanup-map-objects.md` (how those classes are declared after
  cleanup phase 1).
