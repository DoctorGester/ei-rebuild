# Area notes

One file per area (`<area>.md`), written by whoever rewrites that area. See `docs/rewrite-guide.md`.

- `rebirth.md`: what the Rebirth edition patches (addon.dll, EI_Plugin.asi), the patch flags of
  the rebuild (`include/ei/rebirth.h`) and how flagged rewrites are verified.
- `cleanup-tooling.md`: the cleanup artefact counts in `tools/progress.py` and the duplicate
  forwarder report (`tools/gen_originals.py`, `docs/originals-report.md`).
- `core.md`: engine base: maths, transforms, reference counting, object manager, streams and chunked
  files, clipping, camera.
- `network.md`: sessions, connections, messages, lobby, LZ coder, delta replication, message links.
- `app.md`: application layer: frame step, registries, option / device records, key bindings.
- `particles.md`: particle sources and their per-type callbacks (weather, fog and the first callbacks: `effects.md`).
