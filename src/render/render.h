#pragma once

// The new renderer: a sidecar module that draws the game's frame with sokol_gfx (Direct3D 11) from
// data pulled out of the game (docs/notes/renderer.md) and presents it on the game window. The game
// code that used to talk to the old Direct3D 7 path hands its data here instead; the Direct3D 7 calls
// left in game code go to stand-ins that draw nothing (device.h).
//
// Switch: the environment variable EI_RENDER (or the harness step "render <spec>" in a run of the
// image), a comma-separated list. "new" turns the new renderer on: when the device object is built
// (StartPresenting) the game gets no DirectDraw or Direct3D 7 device at all (ticket R9a), the new
// renderer presents through its own swap chain on the game window, the holders of 004EF7B0 /
// 004F0060 and the texture manager's textures have no DirectDraw surface (the CPU-written, shadow
// and .bmp ones only memory the new renderer owns, SurfaceMemory; the .mmp ones only its decoded
// copy, TextureFromMmp), and captures write the new frame. "present" and "cpuwindows" are what "new"
// means (accepted: the launchers and scenarios name them). Without "new" (or when the Direct3D 11
// device cannot be made) nothing here runs and the game builds its DirectDraw device as the original
// does. The standalone image (ei::Standalone) without EI_RENDER runs "new,present"; the harness's
// "render" step adds the words of EI_RENDER to its own.

#include <cstdint>
#include <vector>

struct Matrix4;   // lib/vecmath.h

namespace terrain {
struct SectorData;
}

namespace render {

// Sets the switch from `spec` (the format of EI_RENDER). Without a call the environment is read at
// the first use.
void Configure(const char* spec);

// The sidecar renders frames.
bool Active();

// The off-screen surfaces the game makes through 004EF7B0 (interface windows, the software cursor,
// movies) and 004F0060 (system-memory pictures) keep their pixels in memory the new renderer owns
// (NewSurfaceMemory) and have no DirectDraw surface. The new renderer reads a holder's (CDXSurface)
// pixels there wherever it draws it. Texture holders have such memory too (the save / load screen's
// pictures, the textures of 00691D80, the shadow and the .bmp textures): a mesh pass that binds one
// reads the memory, again whenever it changed. On with "new" (also when the Direct3D 11 device
// could not be made).
bool CpuWindows();
// A surface holder's pixels: a zeroed top-down DIB section (GDI can draw into it through a memory
// DC) of `width` x `height` pixels, 16 or 32 bits each, the channels at the given masks (`alphaMask`
// 0 without alpha). The game code writes it (GDI, pixel code) and the new renderer reads it.
struct SurfaceMemory {
    void* bits = nullptr;
    void* bitmap = nullptr;   // the DIB section (HBITMAP)
    int pitch = 0, width = 0, height = 0, bitsPerPixel = 0;
    std::uint32_t redMask = 0, greenMask = 0, blueMask = 0, alphaMask = 0;
};
// Makes the memory of `holder` (replacing any it had); null when it cannot be made. The pointer
// stays valid until the memory is freed.
const SurfaceMemory* NewSurfaceMemory(const void* holder, int width, int height, int bitsPerPixel, std::uint32_t redMask,
                                      std::uint32_t greenMask, std::uint32_t blueMask, std::uint32_t alphaMask);
// The memory of `holder`, or null.
const SurfaceMemory* SurfaceMemoryOf(const void* holder);
void FreeSurfaceMemory(const void* holder);
// The holder is destroyed (004EFD20): its memory freed, what the renderer kept for it forgotten.
void HolderGone(const void* holder);
// A holder whose surface was loaded from an 8888 .mmp picture (004EFC60: the software cursor's
// images): the new renderer keeps the picture's pixels from the file data.
void PictureFromMmp(const void* holder, const void* data);

// The new renderer is on and has not failed (the same as Active): the new frame is what the present
// (004EF610) shows; the old path draws nothing that is handed over.
bool Presenting();

// The device object is being built (004EDD70): the new renderer starts now and from here on
// presents through its own swap chain on the game window; the game then builds its device without
// DirectDraw (device.h). "new" stays on whatever is configured later. False (and nothing changes)
// without "new" or when the new renderer cannot start: the game builds its DirectDraw device as the
// original does.
bool StartPresenting();
// StartPresenting succeeded: the new renderer shows the frames itself.
bool PresentsItself();
// Whether presenting onto the game window waits for the vertical blank (default on). The harness
// turns it off under its virtual clock.
void SetWaitForVsync(bool on);

// A mesh draw of the old path (004E9010 / 004E9A50 on the device object), after the game has
// decided it is visible and set its colours: the mesh in model space and the matrix the old path
// projects it with. Handed over between BeginMesh and EndMesh; every pass the old path draws in
// between (004EB550 / 004EB370) calls MeshPass, which takes the old device's state for that pass
// (blend, alpha test, depth, culling, fog, texture stages, the bound textures) and returns whether
// the old path still draws it (only when the new renderer did not take it).
struct MeshDraw {
    const float* positions;            // blocks of four vertices: x[4], y[4], z[4], 4 unused
    int positionCount;
    const std::uint32_t* colours;      // D3DCOLOR, by the corners' colour index
    const std::uint32_t* speculars;
    const float* uvs;                  // (u, v) pairs, by the corners' texture index
    const std::uint16_t* corners;      // (position, colour, texture) index triples; null: vertex i
                                       // is position i with uv i and colours[0] / speculars[0]
                                       // (the flat geometry draw 004EA4A0)
    int cornerCount;                   // without corners: the vertex count
    const std::uint16_t* indices;      // a triangle list over the corners
    int indexCount;
    float uOffset, vOffset;            // added to the texture coordinates when `moveUv`
    bool moveUv;
    const Matrix4* matrix;             // row-major, out = M * (x, y, z, 1)
    bool screenMatrix;                 // `matrix` gives screen pixels after the divide (the pipeline's
                                       // +0x1C stack), else Direct3D clip space (+0x24)
    bool clipped;                      // the old path clips it (else only projects it: the depth is
                                       // not cut at the near and far planes)
    bool distanceFog;                  // the old path fogs the vertices by distance (device +0x158)
    float zScale;                      // the depth is multiplied by it (0 taken as 1)
};

// A texture loaded from its .mmp file (00690430): `data` the file in memory, the levels from
// `mip` on (`levels` of them) that the game loads into the surface of `holder` (a CDXSurface). The
// new renderer decodes them from the file instead of reading the surface back, keyed by the
// holder: in the switch-over the holder has no DirectDraw surface at all and the decoded texture is
// kept until the holder goes (HolderGone). False when it was not decoded (the sidecar is off, or
// the format is not one it reads: the caller then gives the holder a DirectDraw texture).
bool TextureFromMmp(const void* holder, const void* data, unsigned mip, unsigned levels);

// The game made new pixels for a texture on the CPU (the shadow textures, 0051CE60) and copies
// them into its surface (CDXSurface): the new renderer takes them from the buffer (`pitch` bytes
// per row, `bitsPerPixel` 16 or 32, the channel masks of the surface's format). Nothing for a holder
// with SurfaceMemory (the switch-over's shadow textures): it is read from that memory.
void TexturePixels(const void* holder, const void* pixels, int width, int height, int pitch, int bitsPerPixel,
                   std::uint32_t redMask, std::uint32_t greenMask, std::uint32_t blueMask, std::uint32_t alphaMask);
void BeginMesh(void* device, const MeshDraw& mesh);
bool MeshPass(void* device);
void EndMesh();

// Pre-transformed vertices the old path draws as they are (FVF 0xC4: screen x, y, depth z, rhw,
// colour, specular; rhw taken as 1): the renderer's rectangle fill and outline (005041F0). A
// triangle list over `indices`, or a line strip over the vertices (`indices` null). Takes the old
// device's state for the draw (`device`: the device object); returns whether the old path still
// draws it.
struct ScreenVertex {
    float x, y, z, rhw;
    std::uint32_t colour, specular;
};
bool ScreenDraw(void* device, const ScreenVertex* vertices, int count, const std::uint16_t* indices, int indexCount);

// A Direct3D 7 draw of pre-transformed vertices the old path is about to make (particles and
// sprites 00503680, ribbons and lightning 00502A20, the renderer's triangle draw 00501BB0: flying hit
// points): `fvf` 0x1C4 (x, y, z, rhw, colour, specular, u, v) or 0xC4 (no texture coordinates),
// `primitive` a D3DPRIMITIVETYPE (2 line list, 3 line strip, 4 triangle list, 5 strip, 6 fan),
// indexed when `indices`. The vertices' fog (specular alpha) is already applied. Takes the old
// device's state for the draw; returns whether the old path still draws it.
bool TlDraw(void* device, const void* vertices, int fvf, int vertexCount, const std::uint16_t* indices, int indexCount,
            int primitive);

// A window surface's blit onto the frame (005FDA80): the surface holder (CDXSurface), the screen
// rectangle, the source rectangle (null: the whole surface, stretched) and the colour key (in the
// surface's pixel format) when `keyed`.
struct Rect {
    int left, top, right, bottom;
};
void SurfaceBlit(const void* holder, const Rect& destination, const Rect* source, bool keyed, std::uint32_t key);

// A movie's decoded frame (CBinkVideo 005DBD80): R, G, B, X bytes per pixel (BinkCopyToBuffer's
// 32-bit "R" type), kept for `key` (the video object) until the next one; MovieBlit draws the last
// frame into the screen rectangle `to`.
void MovieFrame(const void* key, const std::uint8_t* rgbx, int width, int height);
void MovieBlit(const void* key, const Rect& to);

// A solid rectangle `to` in `colour` (D3DCOLOR; alpha ignored): the movie player's fill of the screen
// (005DB9E0) before its first frame.
void Fill(const Rect& to, std::uint32_t colour);

// The software cursor (005E54A0): the part `from` of the cursor image's surface (holder) with its
// mask (`mask`: the image's full width, the surface's pixel format; the old path draws
// (background & mask) | image) at the screen rectangle `to`. Texels whose mask is not zero are left
// out.
void Cursor(const void* imageHolder, const void* mask, const Rect& from, const Rect& to);

// The software cursor between frames (the cursor thread 005E5690 in the switch-over without
// DirectDraw; render_cursor.cpp, docs/notes/renderer.md section 9, R9c2). Called from the cursor
// thread with the cursor's lock held: the last frame presented on the game window (as drawn before
// its cursor) is presented again with the part `from` of the cursor image (holder and mask as for
// Cursor; the image must have been drawn by Cursor before) at `to`. Only when the new renderer
// presents itself and nothing was presented on the game window for 40 ms (the main thread's frames
// show the cursor themselves); false when nothing was presented. Thread-safe: no sokol call, the
// Direct3D 11 work recorded on a deferred context and run as one command list.
bool CursorBetweenFrames(const void* imageHolder, const void* mask, const Rect& from, const Rect& to);
// The cursor thread's presents so far, the last one's rectangle and, in the verify build, the
// checks of the frame it composed (the last frame without its cursor, the cursor's texels where its
// mask is clear) and how many differed.
struct CursorPresentInfo {
    std::uint32_t presents = 0;
    std::uint32_t calls = 0;       // moves the cursor thread handed over
    std::uint32_t refused[4] = {};   // not presented: picture not drawn yet, no frame, a frame within 40 ms, outside
    Rect lastTo{0, 0, 0, 0};
    int checks = 0, mismatches = 0;
};
CursorPresentInfo CursorPresents();
// Verify build: the frame the cursor thread presented last (before the gamma ramp), as 24-bit BGR
// rows from the top. False when there is none.
bool ReadCursorPresent(int* width, int* height, std::vector<std::uint8_t>* bgr);

// The old path clears the depth buffer to `z` in the middle of the frame (before the interface's
// panels and 3D previews draw): `count` screen rectangles, or all of it when `rects` is null.
void ClearDepth(float z, const Rect* rects, int count);

// 006BFF40 (DrawSectorPage) hands every sector page the game draws: the sector, the renderer object
// (007AF060, whose transform pipeline holds the current clip matrix) and the page (ground pages
// 0..pages+1, water pages after them).
void TerrainPage(const terrain::SectorData* sector, void* renderer, int page);

// Copies the new frame's `source` rectangle (screen pixels, as drawn so far this frame) into the
// SurfaceMemory of `holder` (a CDXSurface), stretched to its size: the screen copies of 005FD3D0
// (the escape menu's background, the save game's picture). False when the sidecar is off, the holder
// has no such memory or the copy failed.
bool CopyFrameTo(const void* holder, const Rect& source);

// The end of the game's frame: draws what was handed over since the last present into the
// off-screen target and presents it on the game window. Once per frame; captures call it before
// reading.
void EndFrame();

// The game presents (004EF610): EndFrame if not done yet, then a new frame begins.
void Present();

// The last frame EndFrame drew, as 24-bit BGR rows from the top. False when the sidecar is off or
// has no frame.
bool ReadFrame(int* width, int* height, std::vector<std::uint8_t>* bgr);

// The switch-over's own present on the game window (render_window.cpp; docs/notes/renderer.md,
// section 9). The display's gamma ramp (00504A60's three 256-entry ramps, full scale 65280; null
// channels identity) applied as the frame goes to the window; captures read the frame before it.
// GammaRamp copies the lookup in use (256 RGBA8 entries).
void SetGammaRamp(const std::uint16_t* red, const std::uint16_t* green, const std::uint16_t* blue);
void GammaRamp(std::uint8_t* rgba256);
// Frames presented on the game window so far; the verify build's checks of the presented frame
// against the gamma ramp (after each SetGammaRamp, at a grid of points, when the frame shows one
// to one): how many ran, the last one's points and differing points, whether one is still due.
struct WindowPresentInfo {
    std::uint32_t presents = 0;
    int rampChecks = 0, rampPoints = 0, rampDiffering = 0;
    bool rampCheckPending = false;
    bool rampIdentity = true;   // the ramp in use is the identity
    bool failed = false;   // no swap chain, or the device was lost and could not be made again
    int deviceLosses = 0;       // Direct3D 11 devices lost (or LoseDevice) ...
    int deviceRecoveries = 0;   // ... and made again with everything on them
    std::uint32_t presentsAtRecovery = 0;   // `presents` when the last new device was made
};
WindowPresentInfo WindowPresents();
// Acts as if the Direct3D 11 device were lost at the next present (the harness's "losedevice"):
// the swap chain's Present is not called but reported DXGI_ERROR_DEVICE_REMOVED (without a swap
// chain of the game window's, the loss is taken at the end of the frame). Then, as for a real loss,
// the device, sokol, the swap chain and every image are made again and textures uploaded again
// from what they were decoded from.
void LoseDevice();
// Where the frame shows in the game window's client area: the frame's size and its rectangle there
// (aspect kept, centred; the rest black). False when the new renderer does not present itself.
bool FrameInWindow(int* frameWidth, int* frameHeight, Rect* placed);
// Client-area point to frame pixel (outside the frame: beyond its edges) and back (the centre of the
// frame pixel's window pixels). Unchanged when the frame fills the client area one to one or the new
// renderer does not present itself.
void WindowToFrame(int* x, int* y);
void FrameToWindow(int* x, int* y);

} // namespace render
