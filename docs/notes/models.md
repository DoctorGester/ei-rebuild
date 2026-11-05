# Models, resources, the draw layer, terrain and ground prints

Code: `model.h/.cpp`, `model_db.cpp` (databases, water level changes), `figure_*.cpp`, `link_figure.cpp`,
`link_loader.cpp`, `renderer_init.cpp`, `renderer_strip.cpp`, `renderer_sprites.cpp`, `renderer_lines.cpp`,
`device_*.cpp`, `unit_skins.cpp`, `terrain*.cpp`, `ground_print.cpp`, `ground_decal.cpp`, `acks_db.cpp`, `dib.cpp`,
`resource*.cpp`. Algorithms are described in each file's header comment. Scenario: `tests/scenarios/models.txt` (the
draw functions run in every scenario; ribbons and sprites in `spells` / `spells_cast`).

## Resources

- Archives (`.res`) are `MappedResource`s over a `platform::MappedFile`, opened read only (the game never opened one
  writable). `ResourceManager` lists them; `FindEntry`, `ListFiles` and `WildcardMatch` look names up. Mod folders and
  Rebirth's archive overrides sit in front (rebirth.md, "Resources and paths").
- The sound system's file reader (`ResourceStream`) reads loose files only; the original's archive path in it had no
  callers.
- Kept: `ResourceStream::Seek` treats "from the current position" of a loose file as "from the start"; `ListFiles`
  copies a name into a 1 KB buffer without a length check; `MappedResource::CloseFile`'s message prints the data
  pointer as the name; `GetFile` leaks its buffer on a failed read and on an in-memory archive does not report the
  size; the texture loader leaks the buffer on an unrecognised .mmp header and a .bmp error reports format 0.

## Databases (prints.db, acks.db)

- Both are read once at startup from `database.res` (a failure is reported as "databases.res"), the file copied into a
  memory stream and each table read through a `ChunkArchive` of its own. `ChunkArchive`: two memory streams (index and
  data), the open chunk list (the last node is the current chunk; its occurrence index picks which chunk the next
  EnterChunk looks for), a loading flag; Serialize functions read and write through the same code. A failed EnterChunk
  when loading still pushes a chunk (callers never pop it; `RestoreTop` pops them).
- prints.db (`PrintsDatabase`, model.h): chunk 1 blood and bloody footprints, 2 footprints, 3 scorch; chunks 2 / 3 of a
  record are {alpha, life, lifetime} (`PrintLook`).
- acks.db (acks_db.h): three tables {vector of records, hash_map<string, record*>}: units by prototype name (kinds up to
  0x29; 0x29 the environment speeches, matched against `WorldClient::EnvironmentFlags`), models by model name
  (0x2E..0x32, fallback through the prototype's model), extras by prototype name (0x2A..0x2D); each record a
  hash_map<kind, vector<SpeechRecord>*>. The load fills the units' kind map: 0..0x15 to lists 0..21, 0x16..0x1B null,
  0x1C..0x1F to 22..25, 0x20..0x22 to 28..30, 0x23 / 0x24 to 26 / 27, 0x27 / 0x28 to 31 / 32, 0x2A the named speeches.
  `Find` adds a null entry for a missing name. The original picked through a temporary vector of pointers; ours counts
  and indexes (same draws).
- Water level changes (`TweenArray`): SetTarget (script `SetWaterLevel`), Tick every world update (the removal test is
  "value == 0 or unordered"), Save, Load (applies each level). Kept: Load reads the count into its stream argument's
  slot; SetTarget appends an uninitialised entry before overwriting all of it.

## Figure models

The figure manager's `FindFigureModel` (a hash_map by name) opens `<name>.mod / .bon / .anm` (nothing without the
.mod); `LoadSkeletonLinks` reads `<name>.lnk` ((child, parent) pairs), `LoadFigureBones` `<name>.bon` (0x48-byte records
are prisms, others cubes), `LoadFigureBounds` `<name>.fig` ("FIG" headers or the older headerless records). Bones and
animation: map_objects.md ("Skeleton").

## Link figures

`LinkMesh` (link_figure.h): part headers (map<string, LinkPartHeader>), the morph coefficients, six arrays (vertex
packets, normal packets, texture coordinates, 6-byte corners, dwords, words), ranges by name, the loader. The loader
(`LinkFigureLoader`, model.h; one per world view) has the figure sets, the device object and a cache by name; it opens
figures.res and menus.res at startup. A .lnk lists the parts; each is a FIG / FIG8 (or older headerless) record morphed
by three coefficients s, t, u and appended. Morphs are plain floats: FIG8 lerp(lerp(lerp(c0,c1,s), lerp(c2,c3,s), t),
lerp(lerp(c4,c5,s), lerp(c6,c7,s), t), u); old records lerp(lerp(lerp(c0,c1,s), c2, t), lerp(lerp(c3,c4,s), c5, t), u).
Kept: RemorphFigPart reads every record as FIG8; an empty part
name in a .lnk keeps the previous one; a missing link file gives null; `AddArchive` leaks the archive when opening fails
and passes the `std::string` by value to "%s".

## Renderer

`SceneRenderer` (the global renderer) holds the transform pipeline (matrix stacks; projection.cpp, renderer.md) and the
device holder. Its draws hand their triangles to src/render.
- **Renderer creation**: the depth range comes from the options' far clip and fog day start distance; the sun and
  ambient go to the GPU lighting only.
- **Mesh draws**: callers are the figures and the 3D library's meshes; flag 4 overwrites the mesh's colours with the
  device's override colour / alpha. `DeviceDrawFlat` is used by the selection marker and the ground prints. Of the old
  CPU draw's shared buffers only the projected vertices and their outcodes are left (device_buffers.h).
- **Figure parts**: mesh flags (FigureMeshFlag, figure_mesh.h) 0x40 drawn (cleared first), 0x80 hidden (nothing more,
  not even the finishing call), 0x10 lit, 0x100 flat colours; mesh types 13..16 animated. With the crown option set, the
  mesh named "crown" has every colour pulled 0.6 of the way to the average. The plain variant clears the drawn flag only
  after the hidden test and sets it whenever the draw did not return 2.
- **Material draw**: the device's current material: blend flag, source / destination blend, technique (state block
  handles per pass), stage records (coordinate index, texture name, the counted texture looked up on first use).
- **Unit skins** (`BuildUnitSkins`): run by the zone load, from the prototype and model tables (texture prefixes).
- **Sprites** (`DrawSprites`): every point (x, y, z, size) becomes a screen-facing quad (colour, texture rectangle, four
  TL vertices placed by `ProjectSprites`). Batches of up to 511 sprites are sorted back to front by the first corner's z
  (a radix pass and a merge pass); sprites whose z is outside 0..1 are dropped; two triangles each, the GPU clips.
  Particles draw through it, additive when their source asks.
- **Ribbons** (`DrawRibbon`): a polyline of world points projected (with clipping flags when asked); every point becomes
  a pair of screen vertices offset across the line's screen direction by width * rhw * 15, u along the ribbon, v
  across; the ends are transparent (colour 0x00FFFFFF). Lightning bolts and similar strips.
- **Lines**: two untextured one-colour strips (integer screen points, no caller; projected points, the debug bounds and
  line lists); stage 0's colour and alpha operations off around the draw (renderer.md, "Pass state").
- `ClipTest` answers as the original's 8 corner codes of the cube, then of the box, but decides first from each clip
  plane's range over the volume, with a margin of 2^-16 of a bound; the corner codes are computed only when a plane is
  within the margin, or a value is NaN or infinite (checked equal on 12.6 M calls; saves 0.1-0.3 ms a frame in forests).
- Kept: `DrawFigurePart` without a rectangle never sets the drawn flag, and a singular model matrix uses the previous
  call's inverse; `DrawSprites` returns 0 on a failed draw without restoring the blend states, and its sort writes a
  dropped sprite's key into the next free record; `ShearMatrix` sets m[10] to 0 after zero-filling.

## Terrain

Code: `terrain_sector.h/.cpp` (`terrain::SectorData`; the sector file's format and sizes are named in
terrain_sector.h), `terrain.cpp`, `terrain_landscape.cpp`, `terrain_lights.cpp`, `terrain_ambience.cpp`.
- **The map file** `<map>.MP` (`Terrain::LoadMap`): 0x26 bytes of header (magic CE4AF672, height range, sectors x / y,
  pages, page size, texture count, tile size, material count (16 bits), animated texture count), the materials (0x2C
  bytes each), a class per texture, (texture, frames) word pairs; the size must be exactly that. Missing: FatalError
  "Unable to open file \"path\""; short or mismatched: "Wrong file format: path". Width / height = sectors x 32; tiles
  per row = page / tile; a material's absorption is 1 / (1 - opacity), its colour the file's light factor x colour
  capped at 1. Sixteen extra textures of class 15 follow the map's for the extra pages. The ambient sound set is
  "Dungeon", or "Ingos" / "Suslanger" by zone, else "Gipat"; the one-shot count per sound class is how many of
  `ambient\<set>\<class>\day\<n>.wav` exist (each probe holds a reference on the wave that is never given back).
- **Sectors** (`<zone>XXXYYY.sec`, magic CF4BF774): Load, Build (the random generator and the owner's water row matter;
  Build writes the edge fade into each vertex's specular word, rounded to nearest), ApplyWaterLevels, Ensure (build /
  light on demand). `SectorAt` is a cache over the grid with an LRU list and a 240-frame age limit (the list owns the
  sectors, the grid only points at them). `SurfaceClass` is the texture class under a point (footprints, blood,
  effects); `SurfaceHeight` the ground raised to the water surface.
- **Landscape passes, light tests**: `DrawGround` / `DrawWater` differ only in the page range (ground
  0..LayerPages()-1, water the next LayerPages(); LayerPages() = pages + the two extra pages); a page's texture is
  bound at the first sector with tiles on it. `DrawPages` per visible sector: clip test, projection, per page the
  texture and `DrawPage` (src/render draws it). Every sector draws its full mesh (the original's distance LOD levels
  and seams are gone; the two LOD options stay in the settings file, read by nothing). `EdgeFade` is the map edge's
  fade byte. The GPU lights the terrain: the original's vertex lighting, sun shadow ray march, point light areas and
  relight countdowns are not ported (renderer.md says what looks different); `AddLight` / `RemoveLight` keep only the
  table of shown lights (it holds them: removing a light it never registered releases nothing).
- Waves and animated tiles: `SectorData::AnimateTiles` / `AnimateWaves` (terrain_sector.cpp); the tiles draw from the client effects' generator
  (`g_clientRandom`, core.md "Random generators"). Animate's sine index uses 256 / pi as a double.
- Ambience: sixteen sound classes (name, variants, weight, pan limit, chance %); the class votes are sorted by the
  game's qsort (`ByWeight`).
- The original's faults, fixed: HeightAt with only a cell's diagonal corners (v00, v11) gives their mean height
  (the original retried the same cell forever); a tile word without a texture (kNoTile, or past the pages and class
  table) is skipped by both tile passes and gives `SurfaceClass` 8 (the original counted it into its tables past
  their end); a texture class outside 0..15 falls back to 15 like one not below perPage * pages, the original's test
  (LostInAstral's maps have class 16); an archive without a map file fails as a missing one.
- Kept: Load picks a vertex's water material by lowest priority, Build the first found; RemoveLight / MoveLight /
  AddLight do nothing before a map is loaded.

## Ground prints

`GroundPrint` (scorch marks), `Footprint`, `BloodPrint` in the client world's lists (`bloodPrints`, `footprints`,
`scorchMarks`); `WorldClient::AddBloodPrint` / `AddFootprint` / `AddScorchMark`. A new print is built on the stack,
copied into its list and destroyed; its prints.db entry (by the ground's surface class) gives alpha and life.
- Blood: spin = 30 / life degrees per tick, spreads min(life / 2, 45) ticks (rebuilt each tick), the angle a random
  multiple of 0.001 below 2 pi. Footprints take the kind of a death's blood print they step into (fresh hits' are
  skipped) and return it.
- Draw's uv loop rounds the fourth vertex's u part to a float, the others their v part (kept). Load does not restore
  alpha (the next tick does).
- Colour: footprints and scorch marks white; blood the sun's colour at its making, then every 10 s (Segment,
  `kLightingPeriod`) a grey of the brightest of the sun's luminance and the lights reaching it. That colour is saved
  but no longer drawn: a print is drawn in `DayColour` (white; blood the grey of the daylight sun,
  `WorldClient::DaylightColours`: the table's noon, a cave's hour 0) and the renderer lights it as the ground under it
  (renderer.md section 13, item 10, "Ground prints").
- `WorldClient::ProjectDecal` (world_client_lights.cpp) lays a print on the ground on its first draw: the platform faces
  (an object with several faces adds its parts once per face, kept), each part's matrix pushed on the pipeline's decal
  stack (multiplied through `MatrixMultiply`'s order, `PushDecalMatrix`), the part's packets transformed and each
  triangle added; `Terrain::AddDecalTriangles` adds the terrain triangles under it.

## Ground decals

The decal mesh builder (ground_decal.h): every ground triangle under the projected quad is clipped to the decal (a plane
facing along the projection, then u >= 0, v >= 0, u <= 1, v <= 1; one recursive `ClipStage` with a buffer per stage)
and appended to the caller's arrays. A corner is inside when its distance's sign bit is clear (-0 outside, NaN with a
clear sign inside); 1 - u computed as 1.0 - u (u = 1 gives +0, inside). Vertex arrays are 4-vertex SoA packets;
`Reserve` grows the packets by triangles * 3 / 4 + 1 + the vertex count (over-reserves, kept); `Finish` pads the last
packet with copies of vertex 0. The texture coordinate array stays at its reserved size, so compare only the vertices
the indices use.

## Dib

`Dib` (dib.h): colour table, bits (`std::vector`), header, image size, colour count, masks; used for loose
`textures\*.bmp` overrides and screenshots (Scroll Lock; `CaptureSurfaceRect` / `CaptureSurface` in surface.cpp copy
16, 24 and 32-bit rows from the last to the first). Rows are padded to four bytes (`DibRowBytes`; the original sized them
unpadded and skewed screenshots whose row was not a multiple of four bytes). `Read` / `Write` read and write BMP files.
