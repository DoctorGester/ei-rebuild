# Cleanup phase 1: figures and models (cleanup plan, ticket C2f, F4 series)

How the figure meshes (`figure_*.cpp / .h`), the link figures (`link_*.cpp`) and the model headers (`model.h`, `figure_mesh.h`,
`figure_manager.h`) reach other classes after the cleanup, and what is left. The classes themselves are in `models.md`.

## Declarations

- `CFigureMesh` (figure_mesh.h): named fields `worldPosition_` (+0x28), `textureOffsetU_` / `textureOffsetV_` (+0xCC / +0xD0),
  `poseMatrix_` (+0xE0), `meshType_` (+0x15C), `materialValue_` (+0x158; the scene mesh's material value and type are the numbers in its
  material group's name, scene_registry.cpp; the .fig file stores them as the two dwords after the header, value first, and
  `LinkPartHeader` keeps them as meshType +0x00 / materialValue +0x04), `flatColour_` / `flatSpecular_` (+0xD4 / +0xD8: the interface's
  model view sets them for every part), the part box `bounds_` (ClipBounds, +0x160..0x1B3; +0x198 is its radius, the blood particles'
  size) and `boxPivot_` (+0x1B4). Vtable constants for the classes 0x41 (`kSharedMeshVtableAddress`, 0073FBF0), 0x42
  (`kPlainMeshVtableAddress`, 0073C1C4) and 0x43 (`kMeshVtableAddress`, 0073FC48). `CFigureObjectVirtuals` / `FigureSlots` reach slots
  18-21 on top of `CTransformObject`.
- `CFigureMesh` derives from `CTransformObject` (CEngineObject +0x00..0x1B, local / world position and rotation +0x1C..0x53). Its slot
  bodies have names that override no base virtual (EI_REPLACE_METHOD would register a vcall thunk): slot 2 `CreateMesh` (0059F210), slot 18
  `CopyFigure` (0059F0F0), 0059F3D0 `SetObject`; the CObject3D pointer at +0x98 is `object_`; the +0x94 drawer is `FigureMeshDrawer`, not
  CLinkMesh, though both have the screen RECT at +0x20. `using FigurePart = CFigureMesh` stays only for ui_model.cpp, player_client.cpp and
  verify_ui_model.cpp. Mesh file slots take `mfc::CFileVirtuals*` (`Read` 13, `Write` 14).
- `RasterTriangle` and `CPixelConverter` slot 9 `RasteriseKeyedTriangle` are in texture.h; link_mesh_draw.cpp uses the same struct.
- `FigureModel` (model.h; the cached model, 0x20 bytes) holds the three archives (`mesh`, `bones`, `animations`, each a
  `CMappedResource`) and the animation cache. Models go through `GetFile` / `CloseFile` / `OpenFromMemory` / `CloseResource` and `delete`;
  the manager's own files through `CFigureManager::GetFile` / `CloseFile` (slots 19 / 20). The figure factory builds the part meshes as
  `CFigureMesh*` and sets `CEngineObject::type_`.
- Layout views folded into their classes: `FigurePieceRecord` is `LinkHeaderMap::Node` (node 0x6C bytes, `LinkPartHeader` at +0x1C, bounds
  +0x24, pivot +0x50); `FigureMaterial` and `SceneHelper` are `CMaterial` (diffuse +0x20, ambient +0x30, specular +0x40, emissive +0x50:
  CLight::SetLitColors scales +0x20 / +0x40; the helper record's vectors are ambient, diffuse, specular, power and the diffuse alpha;
  00454E40 builds 0x27-dword elements with `CMaterial::Construct` 0068DBC0); `TerrainFields` is `Terrain`, `TerrainSector` is
  `terrain::SectorData`, `TerrainOwner` is the client world, `SectorBox` +0x10 is ClipBounds' radius; the shadow's `SurfaceDescription` is
  a DDSURFACEDESC2.
- `CFigureInstance` (the figure a CObject3D holds at +0xF0) is `CLinkMesh` (link_figure.h, vtable 0073D790): refCount +0x04, piece map +0x14
  (`headers_`), slot 0 the deleting destructor (`DeleteThis`), slot 4 `LoadPart` (004F30D0; 00517240 calls `[vtable+0x10]`, which loads the
  piece through the loader 004F7480 unless the mesh has it; the local in CObject3D::LoadPart is `loaded`, and it must not reread `figure`
  after the slot 4 call). `CObject3D::figure` is `CLinkMesh*`; `CountedRef` holds a `CLinkMesh*` and releases it through `DeleteThis(1)`.
- `CFigureCache` is gone: CWorld slot 23 (vtable +0x5C) is `FigureLoader()` returning `CLinkFigureLoader*` (model.h; party.h's slot 23 is
  named the same, still null there). MakeFigure (00516DA0) calls `LoadLink` (slot 4) / `LoadFigure` (slot 3) and SetFigureScale (00517700)
  calls `Remorph` (slot 6) with the scale as floats and `fresh` false (x87 float load / store is exact except for a signalling NaN, which a
  figure scale never is). `PieceNames` is the loader's `stlp::vector<stlp::string>`.
- `CountedRef<T>` has `Assign` defined in object3d_lifetime.cpp for the three types the game's copies hold: 00519580
  `CountedRef<CLinkMesh>` (the figure), 0051E570 `CountedRef<WoundsMaterial>`, 0051E4F0 `CountedRef<CRenderPreset>` (0051C3D0 ShowWounds
  calls 0051E570 for the wounds material and 0051E4F0 for its preset; 0051AF70 DestroyClient calls 0051E570). `CountOf` / `DeleteCounted`
  overloads are inline there (without `inline` the release build calls them); `AsCountedRef(T*&)` (map_object_figures.h) views a slot as
  the reference. The client half's other drops are inlined in the original: HideWounds (0051C920), the constructor (0051AB00) and
  CreateClient (0051AEC0) inline the assign of null, DestructBody (0051AD90) the reference's destructor (no slot write); they use
  `CObject3DClientSpecific::ClearWoundsMaterial()` (map_object_shadow.h, through `CDeviceMaterial::Release`).

## Meanings of formerly unknown fields

- `FigureAnimation` +0x24 `stride` (float): the distance one play of the animation moves the unit; the client path code divides the movement
  by it for the animation speed. `CObject3D` +0x158 `eyeHeight` (the AI adds it to the eye), +0x11C / +0x120 the part names' CStringArray
  capacity and grow step. `CMapObject` +0x174 `updateCountdown`; `MapEffect` +0x0C `strength` (saved; Rebirth compares it).
- `CCamera` +0xD0 `zoomTarget_`, +0x2F8 / +0x2FC the keys' capacity and grow step; `CameraKey` +0x04 `reserved`. `SceneSkin` +0x00 `owner`
  (the fake unit; `SetOwner`).
- Terrain: `SectorData` +0x60 `clipMode`, +0x170 `detail`, +0x174 / +0x178 `seamBefore` / `seamAfter`. The map file's material +0x1C
  (`warpSpeed`, the name modding notes use) is copied to TerrainMaterial +0x30 and never read.
- `ChannelLayout` +0x00 `bitCount` is read by the texture upload's size but never written, so that size is always 0 (the original's
  behaviour, kept).
- Fields only written (zero, -1, a constant) are `spare*` with "nothing reads it"; gaps of view structs take the real class's member names.

## Left

- `DestroyFigureManager` keeps its `char*` parameter, because world_deleting_destructors.cpp declares it that way. `StringSlot` in
  figure_links.cpp and figure_models.cpp is a raw STLport string layout; the game's own string members build and free it through their
  addresses.
- Duplicate layouts: `CFigureModelPart` (map_object_figures.h, map_object_animation_bones.cpp) duplicates `CMappedResource`;
  `CageInterpolator` (map_object_cage.h) duplicates part of `FigureBone`; map_object_shadow.cpp has its own raster target.
  `CFigureMesh::CopyFrom` still calls `TransformCopy` (00401490) through its address. figure_part_draw.cpp:681 casts `owner->figure` to the
  type it already has. Outside the F4 files: device_mesh.cpp (9 offset accesses), map_object_figures.cpp (1 VCall, 1 float pragma).
