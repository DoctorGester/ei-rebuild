# Core engine utilities (00401000-00430000)

Files: `src/game/core_maths.*`, `src/game/core_transform.*`, `src/game/core_refcount.*`,
`tests/verify/verify_core_*.cpp`. Shared header touched: `src/game/maths.h` (declarations of the new
`Vector3`/`Quaternion` members only).

## Rewritten

| Address | Name | Verify |
|---|---|---|
| 0040CDC0 | `AddVectors` (operator+, `__fastcall` out/a in ECX/EDX, b on stack) | pure, side by side |
| 00402200 | `NegateVector` | pure |
| 00410770 | `ScaleVector` | pure |
| 00410750 | `Vector3::AddInPlace` (returns the argument, as the original's EAX) | in place, scratch copy |
| 004022D0 | `ClampUnit` (`__stdcall`, [-1, 1], NaN gives -1) | pure |
| 0040CB00 | `Quaternion::SetIdentity` (returns this) | pure |
| 004018A0 | `Quaternion::PreMultiply` (`*this = r * *this`) | in place, scratch copy |
| 0040D940 / 0040F560 / 0040D6B0 | `Quaternion::AxisX/AxisY/AxisZ` (rotated basis vectors) | pure |
| 0040E3D0 | `CubicSegment::SetBSpline` | writes its object only |
| 0040E440 | `CubicSegment::Evaluate` | pure |
| 00401490 | `CTransformObject::CopyTransform` (slot 13, returns this) | scratch copy |
| 0040CB80 | `CTransformObject::CombineRotation` (slot 14) | scratch copy |
| 0040CC30 | `CTransformObject::UpdateWorld` (slot 15) | original on a scratch copy (see below) |
| 00401510 | `CTransformObject::SaveTransform` (slot 16) | recorded file calls |
| 00401540 | `CTransformObject::LoadTransform` (slot 17) | recorded file calls |
| 0040E810 | `CCamera::GroundPoint` | output only |
| 0040F360 / 0040F3C0 | `CCamera::SetTurnRate` / `SetTiltRate` | scratch copy of the camera |
| 0040F420 | `CCamera::SetUnknownD0` | scratch copy |
| 0040D3B0 / 0040D420 | `CCamera::SetTarget` / `SetTargetId` | scratch copy |
| 0040A3A0 | `CRefObject::Release` | dry run on stand-ins |
| 00417DF0 | `CRefObject::Shutdown` | dry run |
| 00419CD0 | `RefPtr::Assign` | dry run |
| 00418BE0 | `RefPtr::Share` | dry run |
| 00413270 | `RefPtr::Destroy` | dry run |

All names are provisional and set in Ghidra.

### Classes

- `CTransformObject` (vtable 0073BA38, abstract): CEngineObject plus local position (+0x1C), world
  position (+0x28), local rotation (+0x34), world rotation (+0x44); 0x54 bytes. Slots 13-17 are the
  five transform methods above; ~90 vtables share them. Slot 14 is overridden by 00516370 in the unit
  classes (it writes only the world rotation). Slots 16/17 take an MFC `CFile*` and call its
  slot 14 (Write) / slot 13 (Read).
- `CCamera` ("CCamera", class id 0x43, vtable 0073B9E0) derives from it. Only the fields used here
  are named.
- `CRefObject`: the 8-byte header of the engine's reference-counted script objects (16-bit count at
  +4, copy-on-share flag at +6 bit 0, detached flag at +7 bit 7; slot 0 deleting destructor, 2
  shutdown, 3 clone). `RefPtr` is the one-dword owning pointer to it.

### Floating point

Order of operations follows the instruction sequence. Values the original rounds through a float
temporary (FSTP to a float local, or float arguments to the Vector3 constructor) go through
`StoreFloat` (a volatile store); values it keeps on the FPU stack are written inline, never as named
float locals (doubling is written `* 2.0f`, exact like FADD ST0,ST0). `/fp:fast` distributed the
1/6 factor in `SetBSpline` over the sum (different results in 24-bit precision), so that function is
compiled under `#pragma float_control(precise, on)`.

All 28 functions were also checked offline (not committed; scratch harness): game.exe mapped at
0x400000 in a suspended child process, original and rewrite run side by side on random inputs
(including NaN/inf/denormal bit patterns, aliased outputs) under x87 precision control 24, 53 and
64 bits, linking the objects from `build/release`; then the verify wrappers themselves were run
with installed trampolines. No mismatches.

## Forwarders added

None. Calls through original code: `UpdateWorld` calls slot 14 through the object's vtable; the
transform save/load call the file's vtable; the refcount helpers call the objects' vtables; the
camera reads the engine's zero vector at 0079B5C8 (set by a static initialiser at 0040C7E0).
`UpdateWorld` calls our `AddVectors` directly (the original calls 0040CDC0 and 00401DE0).

## Original bugs kept on purpose

- `CRefObject::Release` / `RefPtr` helpers test the object pointer for null after already
  decrementing through it (dead check; omitted, behaviour identical).
- `RefPtr::Destroy` leaves the pointer set (it is the owner's destructor body).

## Verify wrappers

- Pure (maths): ours runs first into a scratch output (so an output aliasing an input is read
  unchanged), then the original on the real output; output bytes and returned pointer compared.
- `CopyTransform`, `CombineRotation`: ours on a scratch copy of the 0x54-byte transform, original on
  the real object; whole 0x54 bytes compared.
- `UpdateWorld`: the original runs on a scratch copy whose vtable is the base one (its slot 14 call
  then cannot reach the real object), ours on the real object. World position and local position are
  compared always, the world rotation only when the object's own slot 14 is 0040CB80.
- `SaveTransform`/`LoadTransform`: the original against a recording stand-in file (Read fills
  nothing, so the object is unchanged), ours against the real file through a recording proxy; the
  sequence of (slot, offset from object, size) must match.
- `GroundPoint`: output only, ours into scratch first.
- Camera setters: the original on a copy of the first 0x120 bytes of the camera (they touch nothing
  beyond), ours on the real one; the 0x120 bytes compared.
- Refcount helpers: both the original and ours are dry-run on stand-ins (copies of the 8-byte headers
  with a vtable that only records destructor/shutdown/clone calls); headers, call records, return
  value and resulting pointer compared. Then ours runs on the real objects.

## Skipped

- STLport instantiations: vector/string storage release on the node allocator (0040C720, 0040C770,
  00425040, 00424AF0, 00406590, 00421260), string construction/assign/append (0040C5A0, 0041F460,
  00428DF0, 00428F10, 0042A2B0, 0042A3D0, 0042A4F0, 0042B700, 0041F4E0, 0040B3C0, 00426A80), hash
  (004298D0), lower_bound/copy helpers (00408A50, 00408C20, 004193D0, 0042BE40), node allocator.
- Functions that are a bare RET or `return 0` (00403010, 0040C760, 004128B0, 00424C00, 00402CC0,
  00402CD0, ...): nothing to rewrite and too short for the 5-byte jump.
- Base-destructor vtable stores (00413430, 004174E0, 0042BEF0, 00412720, 00419720, 0040CB70): wait
  for the class hierarchies.
- 004092B0, 004098D0, 00409980, 00409A60: a chunked data reader built on the iterator 00409070; needs
  that class first.
- 00401000/00401170/00401310/00401340: the game's own buffered archive (inline buffer fast path, slot
  calls on overflow); worth doing together with the archive class (004DA170 etc.).
- 0042BEC0..0042C580: a stream wrapper class (vtable 0073BE94); same.
- 0040F230, 00401E50, 00402220, 0040F5C0: camera/transform functions calling not-yet-rewritten maths
  (0049E790, 0049E800, 0040D9B0); good next candidates once those exist.

## Camera (0040D9B0-004114F0)

Files: `src/game/camera.cpp`, `tests/verify/verify_camera.cpp`, scenario `tests/scenarios/camera.txt`.
Shared header changed: `src/game/core_transform.h` (CCamera's unknown blocks split into named
fields, new methods, small types CameraLimits / CameraViewRecord / CameraSettings / CameraView /
CameraShake / CameraKey / CubicCurve3, and the SampleTerrain declaration; nothing removed).

The camera object is at 0079B2A8 (flags at 0079B2B0); 007AF7C8 points to it for the camera-file
player (007AFEA5). The per-frame call is `CCamera::Update` from the game frame 00456440 (Rebirth
patches that caller, not the camera).

| Address | Name | Verify |
|---|---|---|
| 0040FB30 | `CCamera::Update(dt)` (listener, follow / track, pan, tilt, turn, zoom, limits, ApplyView, shakes) | scratch copy (0x120); ours alone while shakes run |
| 0040F8B0 | `CCamera::ApplyView` (position from focus, distance, heading, pitch; re-aims when 0040E4C0 lifted it) | scratch copy |
| 0040D9B0 | `SampleTerrain` (`__stdcall`, 9 args: average / min / max surface height on a grid) | outputs: ours into scratch |
| 0040F5C0 | `CCamera::SetFocus` (minimap clicks, scripts) | scratch copy |
| 0040F230 | `CCamera::Pan` (scroll keys / border) | scratch copy |
| 0040F430 | `CCamera::StopMotion` | scratch copy |
| 004107A0 | `CCamera::ZoomBy` (mouse wheel; 1.05^steps through `_CIpow`) | scratch copy |
| 00410860 / 004108D0 | `CCamera::SaveView` / `RestoreView` (F9-F12, with a modifier to save) | scratch copy (0x1E0) + settings record |
| 00411470 | `CCamera::SetDistancePitch` | scratch copy |
| 00410D70 / 00410D10 | `CCamera::ViewsToSettings` / `ViewsFromSettings` (axis-angle through `_CIacos`) | settings record restored between runs / scratch copy |
| 004109E0 | `CCamera::SetHeading` | scratch copy |
| 00410E50 / 00411060 / 00411360 | `CCamera::SetFree` / `LookFrom` / `SetBounds` | scratch copy |
| 004113A0 | `CCamera::Shake` | original on a scratch camera with a private list (its node read and erased again), ours on the real list; new node compared |
| 0040F1B0 / 0040EFB0 | `CCamera::PlaybackStart` / `PlaybackStep` (camera\*.cam paths, PlayCamera) | scratch copy (0x308) + result |
| 0040E460 | `CubicCurve3::Evaluate` | output only |

### Layout (CCamera, beyond the transform)

+0x94 settings (CameraSettings: clearance +0xFC, limits +0x110 normal / +0x124 briefing (flag
0x200), briefing pitch +0x134/+0x138, view records +0x178 (4 x 0x18), pitch +0x1D8/+0x1DC in
degrees), +0x98 pan velocity, +0xA4 its target, +0xB0 pan response, +0xB4/+0xB8/+0xBC turn rate /
target / response, +0xC0..+0xC8 tilt, +0xCC..+0xD4 zoom, +0xE0 focus, +0xEC focus height, +0xF0
distance, +0xF4 heading quaternion, +0x104 pitch (degrees), +0x108 shake list (STLport list,
nodes {elapsed, duration, amplitude, decay}), +0x10C/+0x118 bounds circle (flag 0x100), +0x11C
target id, +0x120 eight saved views {distance, rotation, pitch}, +0x1E0 a view stack (deque;
00410E70 push / 00410EF0 pop, not functions in Ghidra), +0x2EC file name, +0x2F0 keys
(0x24 bytes: time, ?, position, rotation), +0x2F4 count, +0x300 time offset, +0x304 key index.
Flags: 0x8 follow the target (behind it), 0x10 file open, 0x20 playing, 0x40 free placement,
0x100 bounded, 0x200 briefing limits.

### Floating point

`camera.cpp` is compiled with `float_control(precise, on)`: values the original keeps on the FPU
are `double`, stores go through `Store`. Constants the original multiplies at run time (pi, 1/180,
90, the 1.05 zoom base, the 0 of StopMotion) are read from the exe through volatile reads so they
are not folded. Calls whose float result the original uses unrounded (TerrainOwner::SurfaceHeight
00564070, Vector3::Length 00401E20) go through naked forwarders declared `double`. The shake's
`exp` is the original's inline FLDL2E / FRNDINT / F2XM1 / FSCALE sequence (naked `Exp`). The
quaternion products in ApplyView and Update are written out separately: they add the terms in
different orders.

### Forwarders added

0040E4C0 `ClampAboveObjects` (lifts the camera over objects; STLport list of nearby objects),
0040D710 `AlignToTarget`, 004110D0 `LookDirection` (asin / acos), 00401940 (transform: turn
towards a direction), 0049E790 / 0049E800 quaternion from angle and axis (two argument orders),
0049ED20 slerp, 006B9750 sound listener, 006FD026 `_hypot`, 006E7320 `_CIacos`, 006E81A0
`_CIpow`, the shake list's STLport end / insert / erase (00411530 / 00411630 / 00411540),
00564070 and 00401E20 (as `double`, see above).

### Original behaviour kept

- RestoreView sets distance and pitch only: the saved rotation goes to the settings record (and
  back into the slots) but never to the heading.
- SampleTerrain counts the centre sample once more than the grid (it is also on the grid when the
  step divides the half size); its second and third arguments are unused.
- StopMotion multiplies the view axes by 0 for the pan target (so it is (+-0, +-0, 0), NaN for a
  NaN axis) and replaces the turn / tilt rate only when it is NaN (SetTurnRate(0) inlined).
- PlaybackStep, called when the index already is at the last key, places the camera there but does
  not clear the playing flag (the loop path does).
- The script commands SetCameraPosition (0x13) and SetCameraOrientation (0x14) have no effect in
  ScriptRun::Execute.
- Update tracks a target only with flag 8 clear; panning clears the target and flag 8.

### Skipped

- 0040ECD0 camera file load (Rebirth-patched), 0040E910 / 0040EB60 / 0040ECB0 camera recording
  (CFile / CString), 00410AB0 / 00410C10 camera to / from a save (slots 0073BA04 / 0073BA08).
- 0040E4C0 (STLport list of nearby objects from 0056DAF0), 0040D710, 004110D0, 0040DB50 (2 KB, no
  references: a spline camera path with 0040E460), 0040D470, 0040D1D0, 0040CE60 / 0040CFB0 /
  0040D0A0 (camera construction / settings load).
- 0040F640 / 0040F7B0 (focus moves with no references).
- 00410E70 / 00410EF0: view stack push / pop (no Ghidra function; reached through a vtable).

### Scenario

`camera.txt`: walks the hero (Update / ApplyView / SampleTerrain every frame), pans with the
arrow keys (Pan), F9-F12 (RestoreView), N, the two script camera commands, then PlayCamera on
camera\bz3g.cam (PlaybackStart / PlaybackStep) until IsCameraPlaying is 0. Shake runs when an
effect shakes the camera (the Mushroom effect, 00534B60, 00678EC0); ZoomBy needs the mouse wheel.
