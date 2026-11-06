# Frame cost plan (2026-11-06)

The user's order: delete work first, then make what stays more than 2x faster, run in parallel only as a last resort. Fewer
draws is a goal in its own right: driver cost and our own per-draw work both scale with the draw count.

The basis is R37e3's profile of the user's Lost in Astral save (gz1h, hundreds of palms, 3840 x 2160 msaa 8; renderer.md
section 13 item 2a): main thread 9.0-9.4 ms, GPU 3.25 ms, 5754 draws (sun map 1894, occlusion depth 1854, world 1858,
interface 140). Three read-only reviews traced the world objects, the terrain and water, and the renderer's end of frame.

## First: measure the product, not the harness

`EI_RENDER_VALIDATION` is on in every harness build (CMakeLists.txt:340). It adds work the shipped game never does: a
full rebuild-and-compare of every kept terrain page every 16th frame (`BuildAgain`, about 0.45 ms a frame; deleted with
the page pool by R37e4b), `render::Check`
(3.9%), the reuse hashing and two to four `QueryPerformanceCounter` calls per mesh. Roughly 0.8-1.2 ms of the 9 ms is the
harness's own. Every ticket below measures the release-equivalent cost (validation off) as well as the harness one.

## Delete (no golden change expected)

| What | Where | Why nobody needs it | Saves |
|---|---|---|---|
| A `std::string` and a `std::map` insert per part draw, for a screen rectangle nobody asked for | `link_mesh_draw.cpp` `KeepScreen`, `figure_part_draw.cpp` | Static objects ask for no rectangle; the one reader looks it up again by name | 0.3-0.6 ms |
| CPU projection of every vertex of meshes wholly inside the view | `device_mesh.cpp` | Only `BoundsOfProjected` reads it, and only when a rectangle is asked; the in-view verdict needs it only for meshes crossing the view | 0.35-0.45 ms |
| Sorting the four shadow lists, and dead shadow matrix pushes | `world_client_draw.cpp` | `DrawPartsShadow` no longer draws; its two calls don't depend on order | 0.2 ms |
| Re-walking objects already drawn as casters outside the view | `world_client_shadow_casters.cpp` `HandOverOutsideCasters` | Their parts were handed over this frame already | 0.2 ms |
| Terrain pages read again though unchanged (colours compared every frame, per-page lookups of per-sector facts) | `render_terrain.cpp` | Colours change only at Build and AnimateWaves, both signalled (R37e4 done; R37e4b: no per-frame page work at all) | 0.45-0.55 ms |
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
- **Terrain**: done (R37e4b): the zone's ground and water are two meshes kept on the GPU with the page textures in a
  Texture2DArray; one draw each in the world pass, one ground draw in the occlusion's depth and one in the sun map. The
  save's terrain hand-over 0.41-0.46 -> 0.12 ms in release terms (0.20 with the moved water's writes); the main thread
  5.7-5.9 ms in release terms, 7.0 in the harness build (renderer.md section 13, item 2a).

Expected end state: main thread about 4-5 ms in the save (half of today), about 400-700 draws, no parallelism.

## Tickets

| Ticket | Scope | Golden change |
|---|---|---|
| R37e4 (done) | Terrain and water: skip unchanged pages, water fused loop, `lround`, terrain draws merged in the sun map and occlusion | none |
| R37e4b | The zone's terrain as two GPU meshes, page textures in an array, one draw per pass; the page pool and its checks deleted | none |
| R37f1 | The deletions: `KeepScreen`, inside-view projection, shadow-list sorts, outside casters re-walk, heap churn | none |
| R37f2 | Per-draw state once per batch: pipelines, samplers, frame-wide uniforms and textures; sort keys; `CollectVisible` cells; mesh cache handles | none |
| R37f3 | Instancing: kept meshes by shared figure part, instanced world and caster draws, indirect caster passes | none if grouped by figure |
| R37f4 | Occlusion from last frame's depth, its depth pass deleted | visual, the user's call |

The older R37e5-R37e9 are folded into these; R37e10 measured the ground's GPU cost (below).

## Ground GPU cost (R37e10)

The user: "the world pass is 2 ms, mostly ground shading - what are we doing in the shader that it's so slow?" Measured
on the save and the town at night (4K, msaa 8, anisotropy 16; R37e3's kit, `gputiming pass`, the world's pass with and
without the ground's draws). The ground costs **0.85-1.0 ms** in the save (world pass 1.83-2.23 ms with it, 0.96-1.26
without) and **0.65-0.75 ms** in the town; R37e3's 1.1 ms came from cutting the pass in two. No single part is slow: about
half is writing 8 samples a pixel at 4K, half is shading, and the save shades 1.5 times the pixels it shows.

How: one frame's numbers move by 0.3 ms from run to run while the main thread is the limit (the GPU idles and its clock
drops), so the probe drew the ground 8 times in the same pass (the later draws shade the visible ground once, without
overdraw): the GPU is then the limit and a part's cost is its change of the world pass over 8. The shader's parts were
compiled out one at a time (a program per variant, so the registers they hold went too); the geometry and overdraw come
from R37e3's RenderDoc captures (every ground triangle's clip position read back and rasterised offline at 8 samples,
then replayed in draw order against a depth buffer). Probe patch, scenarios and scripts: build/r37e10 in the main
checkout (probe.patch, kit/).

**By cause** (ms for one draw of the visible ground; the save / the town):

| Cause | Save | Town | Notes |
|---|---|---|---|
| The ground drawn at all (a draw of the visible ground) | 0.75 | 0.61 | the frame's ground is more: overdraw below |
| ... with a trivial shader (texture times the sun), msaa 8 | 0.47 | 0.42 | the floor: rasterising and writing 8 samples |
| ... trivial shader at msaa 4 / msaa 1 | 0.21 / 0.085 | | msaa 8 doubles the floor at 4K |
| ... full shader at msaa 1 | 0.59 | | the shading alone |
| Sun shadow (PCSS) compiled out | 0.26 | 0.20 | with it out the ground is at the floor (0.49) |
| - its quad sharing (the 2 x 2 neighbours' taps) | 0.07 | 0.04 | |
| - 16 filter taps where 8 would do | 0.05 | 0.07 | |
| - the blocker search's 8 taps | 0.03 | 0.04 | |
| - sqrt / sin / cos per tap (constant table instead) | 0.01-0.02 | 0.00-0.03 | not the cost |
| Point-light loop (12 / 6 lights, 4 with shadow maps) | 0.19 | 0.13 | every pixel walks every light |
| Weather code, weather off (wet, puddles, snow, roofs) | 0.08 | 0.04 | dead code still holds registers |
| Screen occlusion lookups | 0.07 | 0.07 | |
| Cloud shadows, clouds off | 0.02 | 0.02 | |
| Distance fog | 0.01 | 0.02 | |
| The water's path in the same program, blending, anisotropy 16 | 0 | 0 | |

The parts do not add up: at msaa 8 shading and sample writes overlap, so cutting one shading part saves less than its
cost at msaa 1, and cutting the shadow alone brings the ground to the floor.

**Overdraw.** The save shades 10.05 M ground pixels for 8.16 M shown (1.23: pages in no depth order; front to back it
would be 1.00); the town 1.06. The opaque models are drawn after the ground and hide some of it: drawing the ground after
them took 0.29 ms off the save's world pass (2.23 to 1.94 ms, four pairs) and nothing in the town.

**Geometry.** The save draws 207 K ground triangles (132 K facing the camera on screen, 51 K of them under 16 pixels),
250 K vertex shader runs: the vertex cost is nil. Its 132 K triangles cover 11.0 M pixel centres (13.2 M pixels with
any of the 8 samples: a pixel on an edge is shaded once per triangle) and take 16.6 M shader lanes in 2 x 2 quads: 1.51
lanes a shown-or-hidden pixel, 1.26 of it the quads' (2.2-2.7 for triangles under 4 pixels, 1.1 over 256). The view
reaches clip depth 175 at most (distance 50, pitch 30): the map's edge is not in it. The town draws 3.8 K triangles, all
large (1.06 lanes a pixel). A far-tile LOD prototype (tiles past clip depth 50 drawn as 2 triangles, not 8) takes 13% of
the lanes offline and measured 0.07 ms a ground draw, 0.10 ms of the save's frame; none in the town.

**Fixes, ranked** (delete first; all wait while the main thread is the limit, R37f1-R37f3):

| Ticket | What | Saves (save / town) | Picture |
|---|---|---|---|
| R37e10a | The ground after the opaque models, its pieces nearest first | 0.29 + up to 0.15 / 0.04 | the same where the ground's alpha is 1 (it is drawn blended) |
| R37e10b | Point lights per draw: only those reaching the draw's box | up to 0.19 / 0.13 | the same |
| R37e10c | Weather, clouds and roofs compiled in only when on (program variants) | 0.06-0.10 / 0.04-0.06 | the same |
| R37e10d | The sun's shadow as a half-size screen mask (depth-aware upsample as the occlusion), the 2 x 2 sharing gone | 0.15-0.25 / 0.15-0.2 | softer edge detail; the user's call |
| R37e10e | Far tiles at lower detail | 0.10 / 0 | cracks to close at the joins; least value |

Not filed: msaa 8 costs the ground 0.26 ms a draw over msaa 4 at 4K (the player's quality setting); the disc table
and the water's branch (nothing to win); parallel work (no need). Expected after R37e10a-c: the save's ground about
0.5 ms, the town's about 0.5 ms.
