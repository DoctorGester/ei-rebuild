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
| Terrain pages read again though unchanged (colours compared every frame, per-page lookups of per-sector facts) | `render_terrain.cpp` | Colours change only at Build, signalled (R37e4 done; R37e4b: no per-frame page work at all; R37e4c: the waves' crests are the shader's) | 0.45-0.55 ms |
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
  library `lround` (bit-exact inline): about 0.6 ms (R37e4). Since R37e4c the waves are the vertex shader's: no
  per-frame wave pass and no water writes (renderer.md section 13, item 3a): the save's RunTick walk 0.17 -> 0.012 ms,
  334 KB -> 0 of vertex writes a frame (about 0.08 ms), at msaa 1 the main thread's median 4.79 -> 4.64 ms.
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
- Done by R37f3 (below, "Measured after R37f3"): the save's 5524 draw calls are 191, grouped by whole figure (today's
  order). Kept meshes share by their built bytes (hash, then compared whole), not by (link figure, part): that also finds
  equal parts of different figures and never shares two that differ.
- **Terrain**: done (R37e4b): the zone's ground and water are two meshes kept on the GPU with the page textures in a
  Texture2DArray; one draw each in the world pass, one ground draw in the occlusion's depth and one in the sun map. The
  save's terrain hand-over 0.41-0.46 -> 0.12 ms in release terms (0.20 with the moved water's writes); the main thread
  5.7-5.9 ms in release terms, 7.0 in the harness build (renderer.md section 13, item 2a).

Expected end state: main thread about 4-5 ms in the save (half of today), about 400-700 draws, no parallelism.

## Tickets

| Ticket | Scope | Golden change |
|---|---|---|
| R37e4 (done) | Terrain and water: skip unchanged pages, water fused loop, `lround`, terrain draws merged in the sun map and occlusion | none |
| R37e4b (done) | The zone's terrain as two GPU meshes, page textures in an array, one draw per pass; the page pool and its checks deleted | none |
| R37e4c | Waves in the vertex shader, water vertices static on the GPU; the CPU works a wave height out where asked; the camera reads the calm water | the waves' digest group; camera-over-water runs |
| R37f1 | The deletions: `KeepScreen`, inside-view projection, shadow-list sorts, outside casters re-walk, heap churn | none |
| R37f2 | Per-draw state once per batch: pipelines, samplers, frame-wide uniforms and textures; sort keys; `CollectVisible` cells; mesh cache handles | none |
| R37f3 (done) | Instancing: kept meshes by shared figure part, instanced world and caster draws, indirect caster passes | none (pixels: 1-3 levels on about 0.01%, shader rounding) |
| R37f4 | Occlusion from last frame's depth, its depth pass deleted | visual, the user's call |
| R37f6 | PrepareShadows' walks: landscape matrices once, casters by frame stamp, roofs and point casters where they matter | none |
| R37f7 | Caster draws grouped without a comparison sort | none |
| R37f8 | The view test once per object: parts of objects wholly inside skip ClipTest and the projection | none (verdicts checked) |
| R37f9 | Still objects' draws kept across frames (design first) | none |
| R37f10 (later) | The per-mesh hand-over 2x faster, for what R37f9 leaves | none |
| R37f11 (later) | The interface in fewer calls (84 of 191) | none |
| R37f12 (later) | The GPU's other passes, where the town is GPU-bound | to measure |

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

## Measured after R37f2

The save at 3840 x 2160, msaa 1 and anisotropy 1, windowed (R37e3's probe; quiet runs, H64: no other game or build
beside them), before and after alternated three times, medians; one msaa 8 run each as the worst case. Before is main
at R37f1's merge (the terrain's R37e4b not in either). The main thread is the CPU's whole frame, so msaa moves it little.

| Part of the main thread (ms a frame) | Harness before | after | Release-equivalent before | after |
|---|---|---|---|---|
| Whole main thread, msaa 1 | 6.24 | 5.22 | 4.84 | 4.28 |
| Whole main thread, msaa 8 (one run) | 6.23 | 5.31 | 4.69 | 4.23 |
| Objects' visibility (`CollectCells`: cells, kept world boxes, read ahead) | 0.50 | 0.27 | 0.49 | 0.34 |
| The draw lists' sorts (keys made once per object) | 0.20 | 0.08 | 0.21 | 0.08 |
| The world pass's mesh draws (`DrawMeshBatch`: keyed pipelines and samplers, the pass's textures and blocks once) | 0.92 | 0.43 | 0.80 | 0.48 |
| Casters outside the view (cells whose spheres cannot reach passed over) | 0.25 | 0.21 | 0.22 | 0.21 |
| Objects' draws (of it the hand-over, harness timer: mesh slots) | 1.75 (0.82) | 1.68 (0.76) | 1.18 | 1.20 |

- **Visibility**: the time was memory and library calls, not arithmetic: `std::isfinite` is a CRT call under /fp:fast
  (now a bit test), every object's lines missed the cache (read eight objects ahead), and an object's world boxes cost
  about 280 cycles (kept per object, taken again only when its pose, place, bounds or scale changed). A cell's union of
  boxes decides all of its objects when it clears a plane by a margin (2^-12 of the magnitudes), an object's own box
  next, else the exact `ClipTest`; every object is still placed every frame (its drawn pose, lights and steps).
  Checked against the exact tests on every object of the save and seven scenarios: no verdict differed.
- **Sorts**: the texture's and figure's ranks among the list's (names compared once per distinct key) and the depth's
  bits make one 64-bit key; the same `IntroSort` on keys that order as `DrawBefore` does gives the same permutation
  (checked on every sort of the save).
- **Mesh slots** save less than planned (about 0.06 ms of the hand-over): the five lookups were in warm tables.
- `ShadowFrame::casters` was a map rebuilt every frame: R37f6 made it a list by a frame stamp kept on the object. The
  point shadows' faces list their casters in the world meshes' order, not the map's: the map's order fed only the
  objects' own state (holds, fronts).
  The 1.33 KB of uniforms a draw and the draw count are R37f3's (instancing).

## Measured after R37f3

The save at 3840 x 2160, msaa 1 and anisotropy 1, windowed (R37e3's probe; quiet runs), before (main at R37f2's merge,
R37e4b's terrain in it) and after alternated four times (release terms) and twice (harness), medians; one msaa 8 run
each; the GPU by `gputiming frame` (twice each) and `gputiming pass` (msaa 8, once each). Kit: build/r37f3 in the main
checkout (probe_r37f3.patch, kit/). The machine was noisy this time: one build's run moved by up to 0.5 ms, untouched
parts (the game, the objects' walk) by up to 20% between runs, so only the changed parts are measured with confidence.

| What | Before | After |
|---|---|---|
| Draw calls a frame (sun map / occlusion depth / world models / interface meshes) | 1876 / 1753 / 1749 / 72 | 43 / 29 / 29 / 16 |
| All the frame's draw calls | 5524 | 191 |
| Per-draw data a frame (uniform blocks; after: records, lights and blocks) | 2425 KB | 983 KB |
| Whole main thread, release terms, msaa 1 | 4.45 | 4.33 |
| ... harness | 5.01 | 4.94 |
| ... release terms, msaa 8 (one run) | 4.47 | 4.36 |
| The world's mesh draws (records, runs, the calls), release terms | 0.43 | 0.28 |
| The sun map / occlusion depth's CPU side, release terms | 0.27 / 0.18 | 0.29 / 0.20 |
| GPU a frame, msaa 1 (median) | 2.13-2.15 | 2.29-2.30 |
| GPU by pass, msaa 8: the copies before the passes / the world pass / the sun map | 0.16 / 1.78 / 0.28 | 0.27 / 1.84 / 0.27 |

The town at night (bz1g, msaa 1, harness): 126 world draws in 26 calls (before 126), the sun map 479 in 75, with the
crowd 475 world draws in 37 calls, the sun map 975 in 86, the point faces 420 draws in 42 calls, the occlusion depth
413 in 28; the main thread 0.92 to 0.84 ms (1.91 to 1.68 with the crowd), the world's mesh draws 0.23 to 0.12.

- **The main thread gains little**: gpu.h's filter (R37f2) had already made a call cheap; what is left per draw is the
  record (`FillRecord`: matrices, fog, lights, the run's fragment block compared), about 150 ns, and the casters' adding
  and sorting, which instancing does not touch (the sort now also orders by indices; 0.01-0.02 ms more).
- **The GPU lost 0.15 ms**: the records and lights are copied before the passes (0.98 MB a frame from the upload heap,
  +0.11 ms of copy), where the uniform blocks were read in place; the world pass reads its per-draw values from storage
  buffers (+0.06 ms at msaa 8). The first version (one 736-byte record a draw, the fragment stage copying it whole into
  a local) cost the same copy; splitting the lights out (one for consecutive draws lit alike: 1832 draws, about 750
  lights) and reading fields in place changed the copy's time little, so it is the copy's fixed cost more than its bytes.
  While the main thread is the frame's limit (the save: 4.3 against 2.3 ms) this costs no frame time.
- **Per-part grouping** (all trunks, then all crowns) would save few calls: a run already merges every draw with the same
  pipeline, textures and blocks into one indirect call, so the calls follow the state changes (the save's world: 29 calls,
  48 first-texture changes in the whole frame); per-part grouping would merge more of the indirect commands into instance
  counts, which costs the GPU nothing measurable, and could move blended pixels. Not worth the picture risk.
- Left: the record copy (upload only what changed since the last frame: most of the save's records are the same each
  frame; costs a compare on the main thread), smaller records (the light matrix's 4th row is unused), the casters' sort by
  a precomputed key, the occlusion's depth pass (R37f4 deletes it).

## Measured after R37f3 and R37e4

R37f5, on main after R37f3 and R37e4b/c (R37e4d's shader split not in it): the save and the town at night at 3840 x 2160
windowed, msaa 1 and anisotropy 1 (the user's settings), one msaa 8 run of the save as the worst case. Quiet runs
(`work.py test --quiet`), medians of three runs (the save) and two (the town); the harness build (`EI_RENDER_VALIDATION`)
beside the release-equivalent one (the same without it). Kit: build/r37f5 in the main checkout (probe_r37f5.patch: R37f3's
probe, the copy passes counted, a records-twice switch, and a sampling profiler of the main thread in the harness:
`EI_R37F5_SAMPLE=<path>` suspends the main thread every 250 us between two `renderstats`, unwinds its stack and writes
exclusive, inclusive and caller > callee counts through dbghelp; kit/: build script, runs, table, scenarios).

**The whole frame.** The save's main thread is 4.02 ms (harness build 4.55; msaa 8, one run: 3.58 / 4.70), of which about
0.2 ms is the harness's own (its held server update and the state digest, in "game"): the product is about 3.8 ms. The
GPU is 2.28 ms a frame (msaa 1), so the save is still the main thread's. The town at night is the other way round: the
main thread 0.78 ms (1.51 with the crowd), frames 2.0-2.3 ms, the main thread waiting in the swap chain's acquire: at 4K
the GPU limits it.

**The save's main thread by part** (ms a frame, release terms / harness; the probe's parts, then the sampling profile of
two 2400-frame runs, about 40 000 samples each, shares times 4.02 ms):

| Part | Release | Harness | Inside it (sampling) |
|---|---|---|---|
| Objects' draws | 1.34 | 1.80 | the game's part walk about 0.55 (ClipTest 0.17, PushMatrix 0.09, PartLighting 0.05, the matrices); the device's per-vertex projection of crossing parts 0.19; BeginMesh 0.23 (FigureLight 0.13); MeshPass 0.27 (KeepMesh 0.13, ReadPassState 0.06) |
| PrepareShadows | 0.37 | 0.35 | the point shadows' ListCasters 0.16; PrepareUnitWeather 0.17 (CollectUnitsAndRoofs 0.12: 8 corners of every caster mesh's box); ReceiverMatrices 0.04; CameraInverse::Under 0.13 of it, the same products made twice |
| Objects' visibility (`CollectCells`) | 0.37 | 0.33 | Place 0.10 (every object's drawn pose), the kept boxes' inputs compared 0.08 |
| The sun map's CPU side | 0.27 | 0.37 | AddDrawnCasters 0.09 (the casters map found per batch), CasterDraws::Sort 0.05, DrawMap 0.07 |
| PlanMeshDraws (the world's records and runs) | 0.25 | 0.38 | FillRecord 0.11, BindCachedMesh 0.05 |
| The casters handed over (ground, outside the view) | 0.23 | 0.24 | the sun map's fit to the visible ground (FitMap's SeeGround) 0.12, the outside walk 0.11 |
| The occlusion depth's CPU side | 0.21 | 0.19 | CasterDraws::Sort 0.05, BindMeshBatch 0.05 |
| The game's tick (with the harness's 0.2) | 0.28 | 0.18 | |
| Submit (the driver) | 0.16 | 0.13 | |
| The interface and overlays | 0.14 | 0.15 | |
| The draw lists' sort, the shadow lists | 0.09, 0.06 | 0.09, 0.06 | |

The harness build's extra 0.5 ms is in the objects' draws (0.46: the reuse checks and timers per mesh), the records
(0.13) and the sun map (0.11). The heap (operator new, RtlAllocateHeap) is about 0.05 ms, memcmp 0.08: no churn left.

**The GPU by pass** (`gputiming pass`, which itself adds about 0.3 ms to the frame; ms):

| Pass | The save, msaa 1 | msaa 8 | The town at night, msaa 1 |
|---|---|---|---|
| The world | 1.33 | 1.82 (+ 0.13 the interface's frame pass) | 1.26 |
| The sun map / its range passes | 0.28 / 0.13 | 0.27 / 0.13 | 0.18 / 0.17 |
| The occlusion: depth / find / blurs | 0.19 / 0.16 / 0.09 | 0.19 / 0.16 / 0.09 | 0.08 / 0.21 / 0.12 |
| The uploads (4-5 copy passes a frame) | 0.20 | 0.20 | 0.23 |
| The present (the gamma ramp's full-screen pass) | 0.13 | 0.13 | 0.14 |
| The water's sky and scene copies | 0.08 | 0.08 | 0.14 |
| The point shadows | 0 | 0 | 0.06 |

The record copy R37f3 added costs little in bytes: copying the save's 1.0 MB of records twice (570 KB more) moved the
GPU frame 2.28 -> 2.32 ms, and the town (GPU-bound, 210 KB uploaded) showed no change beyond its noise. Uploading only
the changed records would save about 0.05 ms of a GPU that waits on the main thread in the save: not filed. What the
"upload" pass shows is mostly the copy passes' fixed cost (4 in the save, 5 in the town; R37f12).

**Draw calls a frame**: the save 191 (the sun map 43 for 1874 draws, the occlusion depth 29, the world 33 for 1832
draws, the interface 84: 61 quad batches, 16 meshes, 7 others; the water's copy and the present 2); the town at night
131 (the sun map 50, the point shadows' faces 31, the occlusion depth 22, the world 26 for 126 draws), with the crowd
the sun map 51, the point faces 42, the occlusion 28, the world 37 for 475 draws. The interface is now the largest
group (R37f11).

**What R37f4 would save** (the occlusion from last frame's depth, the user's call): the save 0.21 ms of main thread and
0.19 ms of GPU, the town 0.03-0.05 ms and 0.08 ms of GPU.

**Ranked** (the user's order: delete, then more than 2x faster, then parallel; expected gains in the save's release terms):

| Rank | Ticket | Kind | Saves (main thread) | Notes |
|---|---|---|---|---|
| 1 | R37f6 | delete | about 0.3 | PrepareShadows 0.40 -> 0.12, AddDrawnCasters 0.09 -> 0.05; ShadowFrame::casters goes |
| 2 | R37f7 | delete / faster | about 0.09 | caster sorts 0.11 -> 0.02; small, independent |
| 3 | R37f8 | delete | about 0.25 | ClipTest and projection 0.36 -> 0.1; verdicts checked as R37f2 did |
| 4 | R37f9 | delete | about 1.0 | still objects re-sent from what was kept: objects' draws 1.34 -> 0.4, records 0.25 -> 0.08; design first, after R37f8 |
| 5 | R37f4 | delete | 0.21 + 0.19 GPU | waits for the user |
| 6 | R37f10 (later) | faster | about 0.4 | the per-mesh hand-over 2x; for units and moving objects, or if R37f9 is rejected |
| - | R37e10a-c | GPU | 0.1-0.3 GPU | the ground; they count where the GPU is the limit (the town at 4K, the save after R37f9) |
| - | R37f12 (later) | GPU, measure | | the other passes, the town being GPU-bound |
| - | R37f11 (later) | fewer draws | 84 -> about 25 calls | the interface; no time to speak of |

Not filed: the sun map's fit to the visible ground (FitMap 0.12 ms; a looser fit moves shadow texels: visual), Place for
still objects in CollectCells (0.10; drawn::Observe needs every object each frame), Submit (the driver's), uploading
only changed records (above), parallel work (no need: after R37f6-R37f9 the save's main thread should be about 2.3-2.6
ms, under the GPU's 2.3, and the GPU becomes the limit there too).

## Measured after R37f6 and R37f7

The save at the user's settings with R37f5's kit (probe patch, quiet runs, the sampler; the logs and the probe as
applied to this tree are in build/R37f6 of the main checkout). The machine moved by up to 40% between runs this time
(untouched parts: the objects' draws 1.22-1.88 ms), so the table takes the three runs whose untouched parts matched
R37f5's (the objects' draws 1.22-1.52 ms, their visibility 0.35), and the sampler's shares times R37f5's 4.02 ms.

| Part (release terms, ms a frame) | R37f5 | After | Sampler (share of the main thread) |
|---|---|---|---|
| PrepareShadows | 0.37 | 0.20 | 9.95% -> 5.76% |
| ... the point shadows' ListCasters / the unit weather | | | 3.94% -> 2.64% / 4.14% -> 1.69% |
| The sun map's CPU side | 0.27 | 0.20 | 7.24% -> 5.37% |
| The occlusion depth's CPU side | 0.21 | 0.16 | 5.39% -> 4.68% |
| CasterDraws::Sort (all passes) | | | 2.62% -> 0.97% |
| The game's shadow draws (CastsShadow) | 0.06 | 0.02 | 1.03% -> 0 |
| Together | | about -0.3 | |

Calls a frame unchanged (the save's sun map 1874 draws in 41 calls, the occlusion 29; the town's point faces 31 / 42).
AddDrawnCasters took on the tables' lookups the sort lost (2.25% -> 2.99%: `CasterDraws::Add` finds the binding and
group). Left in PrepareShadows (about 0.2 ms): the GPU-lit meshes' landscape matrices (CameraInverse::Under 2.3% of the
thread, 1.5% of it FigureLight's own product at the hand-over: R37f6a), the point casters' gate (about 1%), the roofs'
loose boxes while units are in view (0.3%), ReceiverMatrices (0.9%).
