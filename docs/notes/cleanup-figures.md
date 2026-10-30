# Cleanup phase 1: figures and models (cleanup plan, ticket C2f, F4 series)

How the figure meshes (`figure_*.cpp / .h`), the link figures (`link_*.cpp`) and the model headers (`model.h`, `figure_mesh.h`,
`figure_manager.h`) reach other classes after the cleanup, and what is left. The classes themselves are in `models.md`.

## Declarations

- `FigureMesh` (figure_mesh.h): named fields `worldPosition` (+0x28), `textureOffsetU` / `textureOffsetV` (+0xCC / +0xD0),
  `poseMatrix` (+0xE0), `meshType` (+0x15C), `materialValue` (+0x158; the scene mesh's material value and type are the numbers in its
  material group's name, scene_registry.cpp; the .fig file stores them as the two dwords after the header, value first, and
  `LinkPartHeader` keeps them as meshType +0x00 / materialValue +0x04), `flatColour` / `flatSpecular` (+0xD4 / +0xD8: the interface's
  model view sets them for every part), the part box `bounds` (ClipBounds, +0x160..0x1B3; +0x198 is its radius, the blood particles'
  size) and `boxPivot_` (+0x1B4). The classes 0x41 (`SharedFigureMesh`), 0x42 (`PlainFigureMesh`) and 0x43 (`UiMesh`)
  are C++ classes with the compiler's vtables (C19 batch 6); FigureMesh declares slots 18-21, PlainFigureMesh 22 / 23.
- `FigureMesh` derives from `TransformObject` (EngineObject +0x00..0x1B, local / world position and rotation +0x1C..0x53). Its slot
  bodies have names that override no base virtual (the virtuals call them; C28 folds them in): slot 2
  `CreateMesh` (0059F210), slot 18 `CopyFigureImpl` (0059F0F0), slots 19-23 `LoadFromMemoryImpl`, `LoadFigImpl`, `LoadOldImpl`,
  `MirrorXImpl`, `MirrorYImpl`, 0059F3D0 `SetObject`; the Object3D pointer at +0x98 is `object`; the +0x94 drawer is `FigureMeshDrawer`, not
  LinkMesh, though both have the screen RECT at +0x20. `using FigurePart = FigureMesh` stays only for ui_model.cpp, player_client.cpp and
  verify_ui_model.cpp. Mesh file slots take `platform::File*` (`Read` 13, `Write` 14).
- `FigureModel` (model.h; the cached model, 0x20 bytes) holds the three archives (`mesh`, `bones`, `animations`, each a
  `MappedResource`) and the animation cache. Models go through `GetFile` / `CloseFile` / `OpenFromMemory` / `CloseResource` and `delete`;
  the manager's own files through `FigureManager::GetFile` / `CloseFile` (slots 19 / 20). The figure factory builds the part meshes as
  `CFigureMesh*` and sets `EngineObject::type`.
- Layout views folded into their classes: `FigurePieceRecord` is `LinkHeaderMap::Node` (node 0x6C bytes, `LinkPartHeader` at +0x1C, bounds
  +0x24, pivot +0x50); `FigureMaterial` and `SceneHelper` are `Material` (diffuse +0x20, ambient +0x30, specular +0x40, emissive +0x50:
  Light::SetLitColors scales +0x20 / +0x40; the helper record's vectors are ambient, diffuse, specular, power and the diffuse alpha;
  00454E40 builds 0x27-dword elements with `Material::Construct` 0068DBC0); `TerrainFields` is `Terrain`, `TerrainSector` is
  `terrain::SectorData`, `TerrainOwner` is the client world, `SectorBox` +0x10 is ClipBounds' radius; the shadow's `SurfaceDescription` is
  a DDSURFACEDESC2.
- `CFigureInstance` (the figure a Object3D holds at +0xF0) is `LinkMesh` (link_figure.h, vtable 0073D790): refCount +0x04, piece map +0x14
  (`headers`), slot 0 the deleting destructor (`DeleteThis`), slot 4 `LoadPart` (004F30D0; 00517240 calls `[vtable+0x10]`, which loads the
  piece through the loader 004F7480 unless the mesh has it; the local in Object3D::LoadPart is `loaded`, and it must not reread `figure`
  after the slot 4 call). `Object3D::figure` is a `Ref<LinkMesh>`; LinkMesh's Release deletes it at the last reference.
- `CFigureCache` is gone: World slot 23 (vtable +0x5C) is `FigureLoader()` returning `CLinkFigureLoader*` (model.h; party.h's slot 23 is
  named the same, still null there). MakeFigure (00516DA0) calls `LoadLink` (slot 4) / `LoadFigure` (slot 3) and SetFigureScale (00517700)
  calls `Remorph` (slot 6) with the scale as floats and `fresh` false (x87 float load / store is exact except for a signalling NaN, which a
  figure scale never is). `PieceNames` is the loader's `std::vector<std::string>`.
- `Ref<T>::Assign` (ref.h) is the game's three copies of the counted assign: 00519580 for the figure, 0051E570 for the wounds
  material, 0051E4F0 for its render preset (0051C3D0 ShowWounds calls 0051E570 for the wounds material and 0051E4F0 for its preset;
  0051AF70 DestroyClient calls 0051E570). `Object3D::figure`, `Object3DClientSpecific::woundsMaterial` and `DeviceMaterial::preset`
  are Refs; LinkMesh, DeviceMaterial and RenderPreset count with AddRef / Release. The client half's other drops are inlined in the original: HideWounds (0051C920), the constructor (0051AB00) and
  CreateClient (0051AEC0) inline the assign of null, DestructBody (0051AD90) the reference's destructor (no slot write); they use
  `Object3DClientSpecific::ClearWoundsMaterial()` (map_object_shadow.h, through `DeviceMaterial::Release`).

## Meanings of formerly unknown fields

- `FigureAnimation` +0x24 `stride` (float): the distance one play of the animation moves the unit; the client path code divides the movement
  by it for the animation speed. `Object3D` +0x158 `eyeHeight` (the AI adds it to the eye), +0x11C / +0x120 the part names' CStringArray
  capacity and grow step. `MapObject` +0x174 `updateCountdown`; `MapEffect` +0x0C `strength` (saved; Rebirth compares it).
- `Camera` +0xD0 `zoomTarget`, +0x2F8 / +0x2FC the keys' capacity and grow step; `CameraKey` +0x04 `reserved`.
- Terrain: `SectorData` +0x60 `clipMode`, +0x170 `detail`, +0x174 / +0x178 `seamBefore` / `seamAfter`. The map file's material +0x1C
  (`warpSpeed`, the name modding notes use) is copied to TerrainMaterial +0x30 and never read.
- `ChannelLayout` +0x00 `bitCount` is read by the texture upload's size but never written, so that size is always 0 (the original's
  behaviour, kept).
- Fields only written (zero, -1, a constant) are `spare*` with "nothing reads it"; gaps of view structs take the real class's member names.

## Left

- `DestroyFigureManager` keeps its `char*` parameter, because world_deleting_destructors.cpp declares it that way.
- Duplicate layouts: map_object_shadow.cpp has its own raster target.
  `FigureMesh::CopyFrom` still calls `TransformCopy` (00401490) through its address. figure_part_draw.cpp:681 casts `owner->figure` to the
  type it already has. Outside the F4 files: device_mesh.cpp (9 offset accesses), map_object_figures.cpp (1 VCall, 1 float pragma).
