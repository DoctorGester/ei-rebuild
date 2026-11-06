# Frame cost plan (2026-11-06)

The user's order: delete work first, then make what stays more than 2x faster, run in parallel only as a last resort. Fewer
draws is a goal in its own right: driver cost and our own per-draw work both scale with the draw count.

The basis is R37e3's profile of the user's Lost in Astral save (gz1h, hundreds of palms, 3840 x 2160 msaa 8; renderer.md
section 13 item 2a): main thread 9.0-9.4 ms, GPU 3.25 ms, 5754 draws (sun map 1894, occlusion depth 1854, world 1858,
interface 140). Three read-only reviews traced the world objects, the terrain and water, and the renderer's end of frame.

## First: measure the product, not the harness

`EI_RENDER_VALIDATION` is on in every harness build (CMakeLists.txt:340). It adds work the shipped game never does: a
full rebuild-and-compare of every kept terrain page every 16th frame (`BuildAgain`, about 0.45 ms a frame), `render::Check`
(3.9%), the reuse hashing and two to four `QueryPerformanceCounter` calls per mesh. Roughly 0.8-1.2 ms of the 9 ms is the
harness's own. Every ticket below measures the release-equivalent cost (validation off) as well as the harness one.

## Delete (no golden change expected)

| What | Where | Why nobody needs it | Saves |
|---|---|---|---|
| A `std::string` and a `std::map` insert per part draw, for a screen rectangle nobody asked for | `link_mesh_draw.cpp` `KeepScreen`, `figure_part_draw.cpp` | Static objects ask for no rectangle; the one reader looks it up again by name | 0.3-0.6 ms |
| CPU projection of every vertex of meshes wholly inside the view | `device_mesh.cpp` | Only `BoundsOfProjected` reads it, and only when a rectangle is asked; the in-view verdict needs it only for meshes crossing the view | 0.35-0.45 ms |
| Sorting the four shadow lists, and dead shadow matrix pushes | `world_client_draw.cpp` | `DrawPartsShadow` no longer draws; its two calls don't depend on order | 0.2 ms |
| Re-walking objects already drawn as casters outside the view | `world_client_shadow_casters.cpp` `HandOverOutsideCasters` | Their parts were handed over this frame already | 0.2 ms |
| Terrain pages read again though unchanged (colours compared every frame, per-page lookups of per-sector facts) | `render_terrain.cpp` | Colours change only at Build and AnimateWaves, both signalled (R37e4, running) | 0.45-0.55 ms |
| Frame-wide uniforms (shadow, point shadow) memcmp'd per draw; frame-wide textures bound per draw | `render_mesh.cpp` `DrawMeshBatch`, `gpu.cpp` | Same for the whole pass | 0.1-0.25 ms |
| The occlusion's depth pass (1854 draws) | `render_occlusion.cpp` | The world pass already writes depth; use last frame's, reprojected | 0.19 ms CPU, 0.17 ms GPU; **visual change, the user's call** |

## Faster (same output)

- Per-draw state in the world pass: pipelines and samplers found once per batch by a packed key, not by hashing a
  100-byte description per draw; uniforms in fixed arrays (`DrawMeshBatch`): 0.93 to about 0.45 ms.
- Object sort with precomputed 64-bit keys (texture rank, figure rank, morph), same permutation (`object_order.cpp`):
  0.3 ms.
- `CollectVisible` by 16-unit cells, cached world spheres for still objects: 0.70 to about 0.3 ms.
- Mesh cache handles on the mesh instead of five hash lookups per mesh (`render_mesh_cache.cpp`): 0.2-0.3 ms.
- Water pages: one fused loop over positions and specular instead of full vertex rebuilds; `AnimateWaves` without
  library `lround` (bit-exact inline): about 0.6 ms (R37e4).
- Per-frame heap: unordered maps rebuilt each frame replaced by indexed vectors; diagnostics only when asked: 0.2-0.3 ms.

## Fewer draws: 5754 to about 500

Every palm keeps its own copy of its geometry on the GPU because the cache is keyed by the owning object, so every
palm is its own draw in each of three passes. Palms of one morph already share one link figure on the CPU.

- **Instancing**: key kept meshes by (link figure, part); consecutive sorted draws with the same mesh, texture and
  pass state become one instanced draw with a per-instance record (3x4 matrix, lit colour, edge fade, caster strength,
  light indices). The 1.33 KB of uniforms per draw shrinks to about 128 bytes per instance. Visibility, picking and the
  LRU stay per object on the CPU. **Order choice for the user**: grouping by part draws all trunks then all crowns,
  which can move a few pixels where blended leaves overlap; grouping by whole figure keeps today's order exactly.
- **Caster passes through multi-draw indirect** (SDL_GPU has it): sun map 1894 to about 60 draws.
- **Terrain**: sun map one draw per sector and occlusion split by texture though it binds none; both become one draw
  (R37e4). The world pass's 108 ground draws can become 2 with a texture array; low CPU value, later.

Expected end state: main thread about 4-5 ms in the save (half of today), about 400-700 draws, no parallelism.

## Tickets

| Ticket | Scope | Golden change |
|---|---|---|
| R37e4 (running) | Terrain and water: skip unchanged pages, water fused loop, `lround`, terrain draws merged in the sun map and occlusion | none |
| R37f1 | The deletions: `KeepScreen`, inside-view projection, shadow-list sorts, outside casters re-walk, heap churn | none |
| R37f2 | Per-draw state once per batch: pipelines, samplers, frame-wide uniforms and textures; sort keys; `CollectVisible` cells; mesh cache handles | none |
| R37f3 | Instancing: kept meshes by shared figure part, instanced world and caster draws, indirect caster passes | none if grouped by figure |
| R37f4 | Occlusion from last frame's depth, its depth pass deleted | visual, the user's call |

The older R37e5-R37e9 are folded into these; R37e10 (GPU shading) waits until the GPU is the limit.

## Measured after R37f1

The save at 3840 x 2160 msaa 8 (R37e3's kit and probe), before and after alternated three times each, medians; the
machine was busier than in R37e3's run (the same build measured 9.1 ms there, 11.7 here; untouched parts such as the
ground moved by up to 10% between runs, so differences under about 0.1 ms are noise). Harness: the verify build;
release-equivalent: the same without `EI_RENDER_VALIDATION`.

| Part of the main thread (ms a frame) | Harness before | after | Release-equivalent before | after |
|---|---|---|---|---|
| Whole main thread | 11.73 | 10.29 | 8.01 | 6.44 |
| Objects' draws (KeepScreen strings and the inside-view projection) | 3.97 | 2.97 | 2.66 | 1.69 |
| of it the device's mesh draws (the projection; harness timer only) | 1.01 | 0.36 | - | - |
| The shadow lists (sorts, matrix pushes) | 0.33 | 0.11 | 0.28 | 0.07 |
| Casters outside the view (76% of the reached objects now passed over) | 0.40 | 0.42 | 0.25 | 0.26 |
| PrepareShadows (light matrices, units' fronts by lists) | 0.49 | 0.45 | 0.41 | 0.35 |
| The sun map's CPU side (caster counts only in the harness build) | 0.50 | 0.49 | 0.43 | 0.29 |

The outside walk's skip saves nothing measurable: what it skips (one strength lookup and the parts' drawn-part
searches) was cheap; the sphere, pose and placement of every object in the area stay (`CollectVisible` cells and
cached world spheres, R37f2, are where its time is).
