# Cleanup phase 1: the device layer and the old renderer (cleanup plan, ticket C2d)

What changed in `device_*.cpp/.h`, `renderer_*.cpp/.h`, `weather_*.cpp`, `texture*.cpp`, `render_helper.cpp` and the headers that declare their
classes, and what is left. Conventions as in `cleanup-engine-base.md`. The local `ComCall` / `ThisCall` / `Com` slot helpers are gone from these
files.

## Declarations

- 3DfpFPU's transform object (`ITransformPipeline`) is gone (R31): `CTransformPipeline`'s own members in
  projection.cpp project, clip-test, map to the screen and fog (renderer.md section 10).
- `CDeviceMaterial` (`device_material.h`, 0x24 bytes): reference count, blend, source / dest blend, the
  render preset and the `MaterialStage` list (`texCoordIndex` was `unknown_00`). `WoundsMaterial`
  (map_object_shadow.h) derives from it; `CTextureDevice::material` points at one. device_draw.cpp's
  own Material / Technique copies are gone (Technique was `CRenderPreset`).
- `CPixelConverter` (texture.h): slot 10 `DecodeDxt` added (R28: the class is gone with the DLL's load).
- `CTextureDevice::distanceFog` was `unknown_158`.
- Entry points take `CSceneRenderer*` / `CTextureDevice*` (renderer_lines.h's `LineBatch::Draw` too). There is no
  Direct3D / DirectDraw device or device.h since R27c (renderer.md section 9).

## Left

- device_mesh.cpp keeps its own `MeshCorner` / `TexCoord`, the same as figure_mesh.h's (a name clash kept
  them apart).
- verify_state_setters.cpp's interface part still reads an offset (the ui files belong to the ui ticket).
- Raw game addresses (data) and the `EI_ORIGINAL` forwarders: C18 and C29.
