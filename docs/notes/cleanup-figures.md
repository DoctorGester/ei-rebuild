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
