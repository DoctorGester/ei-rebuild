# Cleanup phase 1: figures and models (cleanup plan, ticket C2f)

How the figure meshes (`figure_*.cpp / .h`), the link figures (`link_*.cpp`) and the model headers (`model.h`,
`figure_mesh.h`, `figure_manager.h`) reach other classes after the cleanup, and what is left. The classes themselves
are in `models.md`. Every rewritten address kept its hook and verify wrapper. The object code was checked with a
disassembly diff for each group. The only differences are register allocation, stack offsets, helpers in place of
the old `At` / `Member` / `ZeroArrays` calls, and `call [reg+slot*4]` in place of the out-of-line VCall.

## Declarations

- `CFigureMesh` (figure_mesh.h):
  - named fields: `worldPosition_` (+0x28), `textureOffsetU_` / `textureOffsetV_` (+0xCC / +0xD0), `poseMatrix_`
    (+0xE0) and `meshType_` (+0x15C);
  - vtable constants for the classes 0x41 (`kSharedMeshVtableAddress`, 0073FBF0), 0x42
    (`kPlainMeshVtableAddress`, 0073C1C4) and 0x43 (`kMeshVtableAddress`, 0073FC48);
  - `CFigureObjectVirtuals` / `FigureSlots` (moved here from figure_manager.h) reach slots 18-21 on top of
    `CTransformObject`.
- The mesh file slots take `mfc::CFileVirtuals*` (`Read` 13, `Write` 14).
- `RasterTriangle` and `CPixelConverter` slot 9 `RasteriseKeyedTriangle` are in texture.h. link_mesh_draw.cpp uses
  the same struct.
- `FigureModel` (model.h; the cached model, 0x20 bytes) replaces the old `LinkFigureSet` and figure_manager.h's
  copy. It holds the three archives (`mesh`, `bones`, `animations`, each a `CMappedResource`) and the animation
  cache.
  - The models go through `GetFile` / `CloseFile` / `OpenFromMemory` / `CloseResource` and `delete`.
  - The manager's own files go through `CFigureManager::GetFile` / `CloseFile` (slots 19 / 20).
- The figure factory builds the part meshes as `CFigureMesh*`, constructs the arrays through typed forwarders and
  sets `CEngineObject::type_`.

## Counts (these files, before / after)

| | Before | After |
|---|---|---|
| Offset access (At / Field) | 28 | 0 |
| VCall | 25 | 0 |
| Byte offset arithmetic | 5 | 0 |
| *At views | 0 | 0 |
| Per-file containers (incl. vector / map / set) | 1 | 0 |
| Duplicate forwarders | 0 | 0 |

## Left

- `DestroyFigureManager` keeps its `char*` parameter, because world_deleting_destructors.cpp (another area) declares
  it that way.
- `StringSlot` in figure_links.cpp and figure_models.cpp is a raw STLport string layout. The game's own string
  members build and free it through their addresses.
- Duplicate layouts for later tickets:
  - `FigurePart` (map_object_figures.h) duplicates `CFigureMesh`;
  - `CFigureModelPart` (map_object_figures.h, map_object_animation_bones.cpp) duplicates `CMappedResource`;
  - `CageInterpolator` (map_object_cage.h) duplicates part of `FigureBone`;
  - map_object_shadow.cpp has its own raster target.
- `CFigureMesh::CopyFrom` still calls `TransformCopy` (00401490) through its address.
- Outside this ticket's files: device_mesh.cpp (9 offset accesses), map_object_figures.cpp (1 VCall, 1 float pragma).

## F4: unknown fields of the scene, figure, terrain, map object and texture files (2026-10-14)

Every `unknown_XXX` member of scene_loader.h / .cpp, map_object_figures.h, terrain_sector.h, terrain.h,
terrain_landscape.cpp, camera.h, figure_mesh.h, figure.h, map_object.h, texture.h, link_figure.h,
map_object_shadow.h / .cpp, object_tables.h, figure_part_bounds.cpp, material.h, renderer_lines.h and model.h
(143) is named or `unused_XXX` (374 -> 231 across src). Method as F2: all renamed to unused_, the verify build
run with `-k 0`, each compile error a use. Fields only written (zero, -1, a constant) are `spare*` with "nothing
reads it"; gaps of view structs take the real class's member names.
- The views and what they mirror: `FigurePart` is a `CFigureMesh` (worldPosition / worldRotation, owner,
  buffers, textureOffsetU/V, poseMatrix, name, directory, colorsA/B and the mesh bounds now declared there);
  `FigurePieceRecord`'s piece (+0x1C) is a `LinkPartHeader`; `FigureMaterial` has `CMaterial`'s layout
  (diffuse +0x20, ambient +0x30, specular +0x40, emissive +0x50: CLight::SetLitColors scales +0x20 / +0x40);
  `SceneHelper` is a `CMaterial` (the helper record's vectors are ambient, diffuse, specular, power and the
  diffuse alpha); `TerrainFields` is `Terrain`, `TerrainSector` is `terrain::SectorData`, `TerrainOwner` is
  the client world; `SectorBox` +0x10 is ClipBounds' radius; the shadow's `SurfaceDescription` is a
  DDSURFACEDESC2.
- `CFigureMesh` +0x158 `materialValue_` and +0x15C `meshType_` are the scene mesh's material value and type
  (the numbers in its material group's name, scene_registry.cpp); the .fig file stores them as the two dwords
  after the header (value first), `LinkPartHeader` keeps them as meshType (+0x00) / materialValue (+0x04), and
  the object's parts get them as piece15C / piece158.
- `FigurePart` +0xD4 / +0xD8 are CFigureMesh's flatColour_ / flatSpecular_: the interface's model view sets
  them for every part (`SetPartsFlatColour` / `SetPartsFlatSpecular`, were SetPartsUnknownD4 / D8). +0x198 is
  the part box's radius (the blood particles' size).
- `FigureAnimation` +0x24 `stride` (float): the distance one play of the animation moves the unit; the client
  path code divides the movement by it for the animation speed.
- `CObject3D` +0x158 `eyeHeight` (the AI adds it to the eye), +0x11C / +0x120 the part names' CStringArray
  capacity and grow step. `CMapObject` +0x174 `updateCountdown`; `MapEffect` +0x0C `strength` (saved;
  Rebirth compares it).
- `CCamera` +0xD0 `zoomTarget_` (`SetZoomTarget`, was SetUnknownD0), +0x2F8 / +0x2FC the keys' capacity
  and grow step; `CameraKey` +0x04 `reserved`. `SceneSkin` +0x00 `owner` (the fake unit; `SetOwner`, was
  SetUnknown00).
- Terrain: `SectorData` +0x60 `clipMode`, +0x170 `detail`, +0x174 / +0x178 `seamBefore` / `seamAfter`.
  The map file's material +0x1C (`warpSpeed`, the name modding notes use) is copied to TerrainMaterial +0x30
  and never read.
- `ChannelLayout` +0x00 `bitCount` is read by the texture upload's size but never written, so that size is
  always 0 (the original's behaviour, kept).


## F4a: view structs folded into their classes (2026-10-14)

Each layout now has one declaration:
- `FigurePart` (map_object_figures.h) is `CFigureMesh`, and `CFigureMesh` now derives from `CTransformObject`
  (CEngineObject +0x00..0x1B, local/world position and rotation +0x1C..0x53). Its own slot bodies have new names
  so none of them overrides a base virtual (EI_REPLACE_METHOD would then register a vcall thunk): slot 2 is
  `CreateMesh` (0059F210), slot 18 `CopyFigure` (0059F0F0), and 0059F3D0 is `SetObject`. The CObject3D pointer at
  +0x98 is `object_`. The +0x94 drawer is `FigureMeshDrawer`, not CLinkMesh, though both have the screen RECT at
  +0x20. Box fields +0x160..0x1B3 are `bounds_` (ClipBounds) and +0x1B4 is `boxPivot_`. `using FigurePart =
  CFigureMesh` stays only for ui_model.cpp, player_client.cpp and verify_ui_model.cpp (W2 / F5 had them open).
- `FigurePieceRecord` is `LinkHeaderMap::Node`, whose `second` is `LinkPartHeader` (node 0x6C bytes, header at
  +0x1C, bounds +0x24, pivot +0x50, the same offsets the record used).
- `FigureMaterial` and `SceneHelper` are `CMaterial`. Ghidra: 00454E40 builds 0x27-dword elements with
  `CMaterial::Construct` (0068DBC0). The helper colour setters are `MaterialSetDiffuse / Ambient / Specular`.
- `TerrainFields` is `Terrain`, `TerrainSector` is `terrain::SectorData`, and the bounds file's local `LineBatch`
  is renderer_lines.h's. `TileUv` and `SurfaceClass` are now `Terrain` members.
- ui_flying_hit_points.cpp got a one-line rename (`part->drawer_->screen_`) although it is W2's area.

Release objects were compared with dumpbin (function by function, hook numbers and names normalized). Eleven
objects differ, all in names, commuted FADD operands, instruction scheduling, a helper that is now inlined
(`AsObject`, `FreeBlock`), or unreferenced inline COMDATs (`ei::SetVtable`, an `AlignedArray::operator[]`).

## F4c: CFigureInstance folded into CLinkMesh (2026-10-14)

`CFigureInstance` (map_object_figures.h) was a view of the figure a CObject3D holds at +0xF0. Every offset it used
matches CLinkMesh (link_figure.h, vtable 0073D790): refCount +0x04 (`refCount_`), the piece map +0x14 (`headers_`),
slot 0 the deleting destructor (`DeleteThis`). Its slot 4 `HasPiece` is CLinkMesh slot 4 `LoadPart` (004F30D0):
Ghidra shows 00517240 calling `[vtable+0x10]` on the figure, which loads the piece through the loader (004F7480)
unless the mesh has it; the local in CObject3D::LoadPart is now `loaded`. The view is gone:
- `CObject3D::figure` is `CLinkMesh*`; `CountedRef` (00519580 and its copies) holds a `CLinkMesh*` and releases it
  through `DeleteThis(1)`. CObject3D's destructor body and Create3D do the same.
- `CFigureCache` slots 3, 4 and 6 return (and slot 6 takes) `CLinkMesh*`. The cache itself is still a view of
  CLinkFigureLoader (model.h): its scale arguments are float bits (see F4d).
- map_object_shadow.cpp lost its two `reinterpret_cast<CLinkMesh*>(figure)`. figure_part_draw.cpp:681 still casts
  `owner->figure` to the type it already has (left alone: not this ticket's file).
No CLinkMesh virtual is replaced, so no slot name became an override.

Release objects of the six files that touch the figure were compared with dumpbin (function by function, hook
numbers and names normalized): identical. The first try reread `figure` after the slot 4 call in LoadPart (as the
original does); a local keeps the old code.
- map_object_client_effects.cpp: five locals `owner` (CompanionOwner(this)) renamed `mapObject`; since F4a they
  hid CMapObjectCompanion::owner (C4458 warnings on main). Release object identical.
- figure_mesh_life.cpp: `unknown_18` -> `prototypeId_` (main did not build: F4's rename met F4a's ConstructBase).
- Follow-up F4d: CFigureCache into CLinkFigureLoader (float arguments to check), PieceNames, CountedRef as a template.

## F4d: CFigureCache into CLinkFigureLoader (2026-10-14)

- `CFigureCache` (map_object_figures.h) is gone. CWorld slot 23 (vtable +0x5C) is `FigureLoader()` returning
  `CLinkFigureLoader*` (model.h). MakeFigure (00516DA0) calls `LoadLink` (slot 4) / `LoadFigure` (slot 3) and
  SetFigureScale (00517700) calls `Remorph` (slot 6) with the scale as floats and `fresh` false.
- The hand-made `PieceNames` is the loader's `stlp::vector<stlp::string>` (filled through Start / Finish /
  EndOfStorage as before, freed by destroying each string and FreeStorage).
- `CountedRef` is a template, `CountedRef<T>`, with `Assign` defined in object3d_lifetime.cpp for the three types
  the game's copies hold (explicit instantiations): 00519580 `CountedRef<CLinkMesh>` (the figure), 0051E570
  `CountedRef<WoundsMaterial>` and 0051E4F0 `CountedRef<CRenderPreset>` (Ghidra xrefs: 0051C3D0 calls 0051E570 for
  the wounds material at 0051C4E3 / 0051C56F and 0051E4F0 for its preset at 0051C587; 0051AF70 calls 0051E570 too).
  The count and the deletion are `CountOf` / `DeleteCounted` overloads there (inline: without `inline` the release
  build calls them, since it inlines only functions declared inline). The verify wrapper is a template too.
- Release objects compared with dumpbin: CountedRef<T>::Assign and DropReference identical to the old ones (names
  aside). MakeFigure and SetFigureScale are not: the scale floats now go through `fld / fstp` to the argument slots
  instead of being pushed as dwords (and registers / stack slots moved). An x87 float load and store is exact for
  every value but a signalling NaN, which a figure scale never is; checked with a suite run (below).

## F4e: ShowWounds onto CountedRef<T> (2026-10-14)

- unit_client_wounds.cpp lost its own `AssignCountedRoot` / `RefsOf` / `DeleteRoot`: ShowWounds (0051C3D0)
  assigns the wounds material and its preset through `CountedRef<WoundsMaterial>::Assign` /
  `CountedRef<CRenderPreset>::Assign` (object3d_lifetime.cpp, F4d), reached through a local inline
  `AsCountedRef(T*&)` (the slot viewed as the reference, like CObject3D::FigureRef).
- The old template was not inlined in release either (a call to a file-local copy), so the release code now
  calls the shared copies instead, which is what the original does (0051E570 / 0051E4F0). dumpbin before / after:
  the three calls change callee and pass the slot in ECX (thiscall) instead of pushing it; registers and the
  stack slot holding `&woundsMaterial` moved. Checked with combat, combat_more and units on both installs:
  all pass, ShowWounds 75 verify calls, 0 mismatches.
- party.h: CParty's slot 23 renamed `FigureLoader` (CWorld's name since F4d; still null there).
- Left: HideWounds (0051C920) and object3d_client_parts.cpp still drop the wounds material by hand (ticket F4f).
