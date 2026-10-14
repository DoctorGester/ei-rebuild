# Cleanup phase 1: the device layer and the old renderer (cleanup plan, ticket C2d)

What changed in `device_*.cpp/.h`, `renderer_*.cpp/.h`, `weather_*.cpp`, `texture*.cpp`, `render_helper.cpp`
and the headers that declare their classes, and what is left. Conventions as in `cleanup-engine-base.md`.

Every rewritten address keeps its hook and verify wrapper; calls that went through a game address still do.
Each group was compared with a disassembly diff of the release objects: the same vtable slots, the same x87
sequences, only the helper calls gone and register / stack moves. Nothing disagreed with the original.

## Counts in these files (before -> after)

| Count | Before | After |
|---|---|---|
| Offset access (At<>/Field<>) | 55 | 0 |
| VCall | 5 | 0 |
| Byte arithmetic | 1 | 0 |
| *At views | 0 | 0 |
| Per-file containers | 0 | 0 |
| Duplicate forwarders | 0 | 0 |

The local `ComCall` / `ThisCall` / `Com` slot helpers (not counted) are gone from these files too.

## Declarations

- `ITransformPipeline` (3DfpFPU's transform object) lives in `model.h`, before `CTransformPipeline`. Named
  slots: 8 `ProjectPointsClipped`, 9 `ProjectClipped`, 13 `ProjectPoints`, 14 `Project` (returns the
  outcode union; 004E9010 reads EAX), 24 `ProjectSprites`, 34 `ToScreenClipped`, 35 `FogVertices`,
  37 `ClipTriangles`. The others are `UnknownNN`.
- `CDeviceMaterial` (`device_material.h`, 0x24 bytes): reference count, blend, source / dest blend, the
  render preset and the `MaterialStage` list (`texCoordIndex` was `unknown_00`). `WoundsMaterial`
  (map_object_shadow.h) derives from it; `CTextureDevice::material_` points at one. device_draw.cpp's
  own Material / Technique copies are gone (Technique was `CRenderPreset`).
- `CPixelConverter` (texture.h): slot 10 `DecodeDxt` added.
- `CTextureDevice::distanceFog_` was `unknown_158`.
- Direct3D and DirectDraw go through the SDK interfaces: `device::D3D`, `device::DDraw`,
  `device::Surface` and `device::Lock()` in device.h. Entry points take `CSceneRenderer*` /
  `CTextureDevice*` (renderer_lines.h's `LineBatch::Draw` too).

## Left

- device_mesh.cpp keeps its own `MeshCorner` / `TexCoord`, the same as figure_mesh.h's (a name clash kept
  them apart).
- `CTextureDevice::converter_` stays `void*`: dx_surface.cpp, directdraw_holder.cpp and link_mesh_draw.cpp
  use it; texture_fill.cpp casts it to `CPixelConverter*`.
- texture_bmp.cpp / texture_shadow.cpp keep their surface descriptions as dword arrays, cast to
  `DDSURFACEDESC2*` for Lock.
- verify_state_setters.cpp's interface part still reads an offset (the ui files belong to the ui ticket).
- Raw game addresses (originals, data) and the naked `__asm` forwarders stay.
