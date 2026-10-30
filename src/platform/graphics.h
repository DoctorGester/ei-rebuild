#pragma once

// The renderer's device and window (docs/notes/platform.md, "Graphics"). sokol_gfx draws everything, and every pass,
// pipeline, buffer and image is the renderer's (src/render). A backend makes the device sokol draws on, gives it the
// window's drawable each frame and presents it, presents the last frame again from another thread (the cursor between
// frames), says when the device is lost, writes part of a buffer (which sokol cannot), and reads images back and times
// the GPU for the diagnostics. On Windows: Direct3D 11 (win32/graphics_win32.cpp, which also holds sokol's
// implementation). No backend type crosses this header.

#include "platform/platform.h"
#include "sokol_gfx.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace platform::graphics {

// ---- The device ---------------------------------------------------------------------------------------------------

// Starts making the device on a thread of its own while the game starts up; Start takes it.
void PrepareInBackground();
// sokol's environment on a new device (the one made in the background, if any). False, with Error() saying why, when
// there is none.
bool Start(sg_environment* environment);
// After sg_shutdown: the window's drawable, the device and everything else the backend made on it released.
void Stop();
const char* Error();
// The most samples, up to `most` (a power of two), the device draws an RGBA8 target with a depth buffer with (the
// renderer's frame target); 1: none.
int MaxSamples(int most);
// The device was lost (removed, reset, hung, a driver error), as a call that failed or the device itself says: the
// backend's code for it (for the log and the harness). Changes nothing.
std::optional<std::uint32_t> Lost();

// ---- The window ---------------------------------------------------------------------------------------------------

struct PresentMode {
    bool vsync = true;           // the present waits for the vertical blank
    bool allowTearing = false;   // without the wait, the display may show a frame mid-scan (fixed when the window's
                                 // drawable is first made)
};
// The window's drawable this frame, `width` x `height` pixels (its client area). False when there is none: it could
// not be made, or the device is lost (Lost).
bool BeginDrawable(NativeWindow window, int width, int height, const PresentMode& mode, sg_swapchain* drawable);
// After the frame's sg_commit: the drawable shown. False when it was not (a failure is logged; a lost device is seen
// by Lost).
bool EndDrawable();

// ---- Buffers ------------------------------------------------------------------------------------------------------

// `bytes` of `data` written into `buffer` from byte `offset` on, the rest of it kept: sokol writes a buffer only whole
// and once a frame. For a buffer made with usage.write_unsealed and sealed at once, without data (render_buffers.cpp's
// GpuPool). In order with the draws: those recorded before it still read the bytes it replaces.
void UpdateBuffer(sg_buffer buffer, std::size_t offset, const void* data, std::size_t bytes);

// ---- Presenting again from another thread -------------------------------------------------------------------------

// The cursor between frames (docs/notes/renderer.md, section 9): during a long load the game's cursor thread shows
// the last frame again with the cursor moved. sokol is the main thread's alone, so that thread never calls it: the
// main thread hands the backend the objects of the window's last present (KeepPresent) and the backend draws them
// again with its own calls (PresentAgain). The caller keeps the three apart from each other and from the main thread's draws into
// the kept views and its work on the window's drawable (the renderer's present lock).

// A present's draw: `pipeline` of `shader` with `views` and `sampler` (bindings 0..2 and 0), three vertices into
// `viewport` of the drawable cleared black.
struct PresentParts {
    const sg_shader_desc* shaderDesc;   // the shader's description: its uniform blocks' stages and slots
    sg_shader shader;
    sg_pipeline pipeline;
    sg_view views[3];
    sg_sampler sampler;
    Rect viewport;
};
// Main thread: the parts kept (the backend's own references, which outlive sokol's objects), in place of those kept
// before. False, nothing kept, when one of them is gone or the device cannot draw from another thread.
bool KeepPresent(const PresentParts& parts);
// Main thread: the kept parts let go (before the device goes).
void DropPresent();

// The harness build's check of a present made again: before it is shown, the region (0, 0)-(width, height) of the
// drawable and of views[0], and `cursor` of views[2], read back, 4 bytes a texel in their formats (the drawable B, G, R,
// A), rows packed. Empty when they could not be read.
struct PresentReads {
    int width = 0, height = 0;
    Rect cursor{0, 0, 0, 0};
    std::vector<std::uint8_t> drawable, frame, cursorTexels;
};
// Any thread: the kept present drawn again with `uniforms` (the shader's uniform blocks in order) and shown, with
// `reads` (may be null) read first. False when nothing was presented: nothing kept, or a failure (logged; a lost device
// is found at the main thread's next present).
bool PresentAgain(std::span<const sg_range> uniforms, PresentReads* reads);

// ---- Diagnostics --------------------------------------------------------------------------------------------------

// The backend's copy for reading texels back (win32/graphics_win32.h).
class Staging;

// Texels read back to the CPU, blocking (captures, the interface canvases' reads, the harness build's checks): copied
// into a staging copy and mapped until the next read, Unmap or Release. The copy is kept for the next read (made again
// for a larger region or another format) until Release, which every holder calls before the device goes and after a
// read it does not keep for later. The destructor never touches the device: some holders are globals, destroyed at
// exit while the cursor thread still presents. Main thread.
class Readback {
public:
    Readback();
    ~Readback();
    Readback(const Readback&) = delete;
    Readback& operator=(const Readback&) = delete;
    // `region` of the image's first level, or of the window's drawable as the frame's passes left it (before
    // EndDrawable). False, nothing mapped, when it cannot be read.
    bool Read(sg_image image, const Rect& region);
    bool ReadDrawable(const Rect& region);
    // Row `y` of the mapped region, in the source's texel format.
    const std::uint8_t* Row(int y) const;
    void Unmap();
    void Release();

private:
    std::unique_ptr<Staging> staging;
};

// Timestamps on the GPU between Begin and End, read some frames later without waiting (the performance overlay, the
// harness build's shadow timings). Release, as Readback's, before the device goes; the destructor leaves the device
// alone.
class GpuTimer {
public:
    static constexpr int kMostStamps = 64;   // a set's; later stamps are dropped
    GpuTimer();
    ~GpuTimer();
    GpuTimer(const GpuTimer&) = delete;
    GpuTimer& operator=(const GpuTimer&) = delete;
    // A new set (its queries made at the first use). False without timestamps on the device, or while the last set
    // was not read.
    bool Begin();
    void Stamp();
    void End();
    // The finished set: each stamp's milliseconds after the first (empty when the GPU's clock was not steady
    // meanwhile). False while the GPU has not finished it (or no set was ended).
    bool Read(std::vector<double>* milliseconds);
    void Release();

private:
    struct Queries;
    std::unique_ptr<Queries> queries;
};

} // namespace platform::graphics
