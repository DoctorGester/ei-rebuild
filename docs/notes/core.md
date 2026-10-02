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
