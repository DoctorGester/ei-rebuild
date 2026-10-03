# Range 006B0000-006E0000 outside sound: terrain, water, lighting, prints, databases

Files: `src/game/terrain_sector.h`, `src/game/terrain_sector.cpp`, `tests/verify/verify_terrain_sector.cpp`.
Also `src/game/terrain.cpp` (the 006C8930 forwarder removed; its rewrite lives in terrain_sector.cpp).
Shared headers: none changed (terrain.h is untouched; the sector and landscape fields used here are
declared in the new terrain_sector.h as `terrain::SectorData` / `terrain::TerrainFields`, views of the
same memory as terrain.h's `TerrainSector` / `Terrain`).
Sound (006B4350-006BDDB0, 006C68B0's ambient part, 006CB390) is in `sound.md`; the prints/acks
databases and water level changes in `models.md`.

## Map

R = rendering (Direct3D / renderer calls), D = game data / logic, L = library instantiation.

| Range | Kind | Contents |
|---|---|---|
| 006B01F0-006B4230 | L | STLport / MFC instantiations used by 006AExxx-006AFxxx (strings, vectors, maps) |
| 006B4350-006BDDB0 | - | sound and music (`sound.md`) |
| 006BDDD0-006BE300 | L | hash_map instantiations of the sound manager |
| 006BE3A0 | D | PlaneHeight (done, terrain.cpp) |
| 006BF4A0 / 006BF580 | D | helpers of the light updates (006CAA70) |
| 006BF5F0 / 006BF8A0 | D | **SectorData** constructor / destructor (0x394 bytes) |
| 006BFB00-006BFC80 | R | sector visibility / per-quarter draw setup (called from the terrain draw 006C6460) |
| 006BFF40 | R | **terrain draw of one sector** (5 KB): fills the shared vertex / index buffers per texture page (static CWordArray / CByteArray at 007C8864..007C88C8), strips seams, then submits through the render device (vtable +0x88 / +0x94, D3D render state 0x1D through 007AF080+0x54, the draw context's +0x30). Left original: Direct3D submission |
| 006C1390 | D | setter used by 006C66E0 |
| 006C13A0 | D | **SectorData::Load**: `<zone>XXXYYY.sec` from the map archive (magic CF4BF774, flags byte: 1 water, 2 water tiles), vertex positions (x/y displacement bytes, u16 height, packed normal), water tile materials. Rewritten |
| 006C1C00 (+ chunk 006C29BD) | D | **SectorData::Build** (5.3 KB): per-vertex records (normals, water materials, depth), tile textures / rotations, per quarter and texture page the tile flags and 3x3 tile vertex lists with UVs, animated tiles (random start frame), texture class counts, bounding boxes, point lights, water materials and levels. Rewritten |
| 006C30E0 | D | SectorData::ApplyWaterLevels. Rewritten |
| 006C31C0 / 006C36B0 / 006C3A00 | D | vertex lighting of a sector (colours of the records; 3A00 switches the FPU to chop rounding with `_controlfp(0x300, 0x300)`; 36B0 tests shadows through 006C92C0) |
| 006C3C80 / 006C3D70 | D | SectorData::Clear / reset fields |
| 006C3DD0 | D | SectorData::Ensure (build 2 / light 4 on demand). Rewritten |
| 006C3E30-006C3E80 | D | animated tile frames (per frame, 006CB1E0) |
| 006C42A0-006C4790 | D | lightmap / shadow-map helpers of 006C67E0 (reloads `%s%3.3i%3.3i%s`) |
| 006C4690-006C5230 | D | element constructors / destructors of the sector arrays, the light-reference pool release (006C5150) |
| 006C53C0 | D | TerrainFields::TileUv. Rewritten |
| 006C5430 / 006C54D0 / 006CB1E0 | D | the landscape object's (vtable 0073F6A8) Create (sine table), Destroy, per-frame Update (light countdown 006C8ED0, animated tiles, terrain ambience 006CB390, frame counter +0x218) |
| 006C5620-006C57E0, 006C7750-006C7EB0 | D | landscape reset / free, light map setup (006C7A80), 006C7D70 / 006C7EB0 (time-of-day colour) |
| 006C5AA0 | D | ambient / light-map loading for 006C68B0 (`%sL%2.2i`, `%s%3.3i`) |
| 006C5EA0-006C6070 | D | no callers (5EA0, 5F50) / transform slot 14 (6070) |
| 006C6230 / 006C6330 / 006C6460 / 006C66E0 | R | terrain draw passes (from the renderer 00569E40, Rebirth-patched) |
| 006C67E0 | D | reload of a sector's light data |
| 006C68B0 | D | **map loader** (`.mp` header, materials, water rows +0xAEC, sector grid, ambient sounds; Rebirth-patched, 3.7 KB). Not rewritten |
| 006C8140 | D | **Terrain::SectorAt**: the sector cache (grid +0x1F8 of MFC list nodes, LRU list +0x200/+0x204, 240-frame age limit, loads through 006C13A0 and builds through 006C3DD0). Not rewritten |
| 006C84E0 | D | no callers (frees all sectors) |
| 006C85A0 / 006C8680 | D | VertexAt / HeightAt (done) |
| 006C8930 | D | Terrain::GroundAndWaterDepth. Rewritten |
| 006C8990 / 006C9560 | D | no callers (dead code) |
| 006C8D40 | D | TerrainFields::SurfaceClass (texture class under a point: footprints, blood, effects). Rewritten |
| 006C8ED0 / 006C92C0 | D | periodic light recomputation / shadow test of a point |
| 006C98F0 / 006CA170.. / 006CA260 | R | light / shadow map textures for the renderer (00569E40); 98F0 is reached from code Ghidra has no function for (006CA255) |
| 006CA5D0-006CAC10 | D | point lights (CEffectPointLight, map object lights) registering with the vertices they reach (pooled 16-byte LightRef nodes, blocks of 1000 at 007C8840) |
| 006CB020 | D | point test used by prints, map objects and lighting (8 callers) |
| 006CB390 / 006CBC00 | D | terrain ambience sounds around the listener (sound.md) |
| 006CBE60-006CC8D0 | D | terrain decal / patch projection (006CC530 builds from VertexAt; 006CC700 maths), 006CC7F0 water level apply (models.md forwarder) |
| 006CCF20 / 006CEDE0 | R | vertex buffers of terrain patches (shadow / selection projection, 00565xxx-0056Cxxx renderer) |
| 006CD540-006CEA10 | L | hash_map / list instantiations (lights, sectors) |
| 006CF370-006CFEA0 | D | foot / blood / fire prints (`footprints`, `bloodprints`, `fireprints`) |
| 006D0000-006D3F10 | L/D | prints.db loader 006D0630 (Rebirth-patched) and its STLport instantiations (models.md) |
| 006D41D0-006D49C0 | D | prints records, water level tweens (done, models.md) |
| 006D49E0-006D6FC0 | D | CDib bitmaps (models.md; not used in a normal game) |
| 006D7620-006E0000 | L/D | acks.db loader 006D7620 (Rebirth-patched), 006D8430 weapon crash sounds, STLport instantiations (006DE370 2 KB is a hash_map copy) |

## Sector data (terrain_sector.h)

- The sector file: 5-byte header (magic, flags), 33x33 x 8-byte vertices per layer {int8 dx, int8 dy,
  u16 height, u32 normal 10:11:11}, 256 u16 tile words (texture 14 bits, rotation 2 bits), with
  water tiles a second 256, and with water 256 u16 water materials per tile. Sizes 0x240D / 0x4815,
  +0x200 with water tiles.
- Vertex records (0x54, packed): +0x19 position (water), +0x25 water material, +0x27 depth (ground),
  +0x2B normal, +0x43 material colour, +0x4F material byte, +0x50 light references.
- Texture pages: `pages` (landscape +0x11C) pages of `perPage` (+0x128) textures, then 8 per page;
  water tiles use pages + 2 + page. Quarters are 8x8 tiles; counts are indexed quarter * 256 + page.

## Rewritten

| Address | Size | Name | Verification |
|---|---|---|---|
| 006C1C00 | 5336 (+chunk 006C29BD 1818) | SectorData::Build | original on a scratch sector (rebuilt arrays empty, private vertices and water list), random generator and the owner's water row restored, ours on the real one; every written field and array compared (pointers as indices, light lists by light), scratch destroyed through the original's record destructor |
| 006C13A0 | 2129 | SectorData::Load | ours on the real sector, original on a scratch copy with emptied arrays (the extra GetFile reference dropped with CloseFile); header fields, vertices, water tiles compared |
| 006C30E0 | 212 | SectorData::ApplyWaterLevels | snapshot of records, vertices and water row; original, record, restore, ours, compare |
| 006C3DD0 | 93 | SectorData::Ensure | ours only: result and built flags |
| 006C53C0 | 78 | TerrainFields::TileUv | pure |
| 006C8D40 | 328 | TerrainFields::SurfaceClass | side by side (the sector lookup only refreshes the cache) |
| 006C8930 | 93 | Terrain::GroundAndWaterDepth | outputs, ours into scratch first |

Ensure calls Build through its original address so the installed hook (or verify wrapper) runs.

### Floating point

terrain_sector.cpp is compiled with `float_control(precise, on)`. The running x / y of the vertex
grid, heights in flight and the bounding box intermediates (half size, centre z from the unrounded
`minZ - 1`) are doubles, every float store goes through `Store`. All constants are read from the exe.
Heights from HeightAt are taken unrounded through a double-returning forwarder to 006C8680.

## Forwarders added (the standalone build must replace these)

006C3C80 Clear, 006C3A00 Light, 006C1C00 (Ensure's call), 006C4EE0 / 006C4F90 / 006C4F60 / 006C5050
/ 006C5010 / 006C5120 element constructors / destructors, 006FF64A CByteArray::SetSize, 006FF7DC
CByteArray::SetAtGrow, 006CA9C0 the owner's LightSector, 006C8680 (as double). The archive's
GetFile is called through its vtable (slot 5). None is Rebirth-patched; FatalError (00480080, patched)
is called through `game::FatalError`.

## Original bugs kept on purpose

- Load passes the error message to FatalError as its format string.
- Build's texture class index is compared against perPage * pages (not 16) before falling back to 15.
- Load picks the water material of a vertex by lowest priority; Build picks the first one found.

## Skipped

- The other draw functions (R above): Direct3D submission. (006BFF40 was done later, see below.)
- 006C68B0 map loader (Rebirth-patched; also the ambient sounds), 006D0630 / 006D7620 database loaders
  (Rebirth-patched).
- 006C8140 SectorAt (the cache: stateful, evicts and deletes sectors; next candidate, verify by
  invariants), the vertex lighting 006C31C0 / 006C36B0 / 006C3A00 (chop rounding mode), the point
  light registration 006CAC10 (global node pool), 006C3E80 animated tiles.
- Dead code: 006C8990, 006C9560, 006C5EA0, 006C5F50, 006C84E0, 006C4790 / 006C4A40 / 006C4C90 (vtable
  slots of array classes only).
- STLport / MFC instantiations listed above.

## Pitfall found in game

Ghidra's listing of 006C1C00 drops the FNSTSW AX at 006C1F48 (bytes DF E0 between the FCOMP and the
next MOV), which made the minimum-height select look like it tested bit 8 of the minimum's bits.
It is an ordinary minimum on the comparison status. Check the raw bytes when a listing's addresses
skip.

## Risks

- Build / Load run once per sector cache miss; zone1 loads several sectors in every scenario, water
  sectors only where the map has water. The animated-tile path draws from the game's random
  generator (as the original), so a mistake there would shift later random draws.
- Build's verify allocates and frees a scratch sector each call (game heap); the light reference
  nodes of the scratch go back to the shared pool, so node addresses differ from a plain run.

## The sector page draw (006BFF40), later round

Code: `src/game/terrain_draw.cpp` (`DrawSectorPage`, __fastcall for the thiscall with four stack
arguments: the draw context, an unused one, the texture page, an unused one). Verify:
`tests/verify/verify_terrain_draw.cpp` (ours only: result 0 / 1, the vertex buffer's size within its
capacity and a multiple of 4). Reached by every scenario (the terrain pass 006C6460 draws every
visible sector page each frame). Shared files changed: none (`terrain_sector.h` is only included).

What it does for `SectorData` `s` and page `p`:

- Returns 1 at once when `s` +0x60 is 2. Constructs the function-local statics on first use (guard
  byte 007C88A0: the 32-byte vertex buffer 007C8890, the index CWordArray 007C8878, the clipped index
  CWordArray 007C8860, the per-vertex CByteArray 007C8848; each registers its exit handler
  006C1370 / 006C1360 / 006C1350 / 006C1340 with the game's atexit). Applies pending water levels
  (006C30E0) when the sector has water.
- For each quarter whose mode (+0x144) is not 2: the page's tile entries (`tiles[q][p]`, 9 per tile)
  at the level of detail +0x170 (9, 6 or 4 vertices and 8, 4 or 2 triangles per tile, tables
  0079699C / 007969A8 read from the exe): the vertex buffer is sized to 4 x the vertex count (its
  element is 32 bytes, so it is four times larger than needed, as in the original), the byte array
  likewise, the index arrays to 3 x triangles and 12 x triangles. Each vertex is the record's first
  16 bytes, its 8 bytes at +0x11 and the entry's (u, v) (the original copies the qwords with FILD /
  FISTP, which is exact); the record's byte +0x10 goes to the byte array. Level 1 uses entries
  0, 2, 3, 5, 6, 8 of each tile, level 2 the corners 0, 2, 6, 8.
- Indices per tile from fixed patterns; when +0x174 or +0x178 is set (a coarser neighbour) the tile
  edge flags `tileFlags[q][p]` pick variants without the triangles of that edge (level 0: bit 0 with
  +0x174, bit 1 with +0x178, 21 indices instead of 24; level 1: bit 2 / bit 3, 9 instead of 12).
  The patterns were checked against the raw instructions (a script over the disassembly), not only
  Ghidra's listing.
- Mode 1 (crossing the view): the context's clipper (arg +8 -> +0xC) slot 37 clips into the clipped
  index array and updates both counts; the vertices the clipped triangles use are kept in order and
  the indices renumbered (static aligned arrays 007C88B8 words / 007C88A8 bytes, guard 007C88C8, exit
  handlers 006C1320 / 006C1300; these two do not keep their contents when they grow).
- With indices: clipper slot 34 (vertices, count), render state 0x1D on (the device at
  *007AF080 + 0x54, IDirect3DDevice7 slot 20), the context's slot 12 draws (0: return 0 at once,
  the state stays on), render state 0x1D off.

Risks: not run in game; every frame goes through it, so a mistake shows at once (missing or
garbled terrain, or a crash). The level of detail outside 0..2 reads past the tables as the
original does.
