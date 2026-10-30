#pragma once

// The renderer's device and window (docs/notes/platform.md, "Graphics"). sokol_gfx draws everything, and every pass,
// pipeline, buffer and image is the renderer's (src/render). A backend makes the device sokol draws on, gives it the
// window's drawable each frame and presents it, says when the device is lost, and reads images back and times the GPU
// for the diagnostics. On Windows: Direct3D 11 (win32/graphics_win32.cpp, which also holds sokol's implementation).
// No backend type crosses this header.

#include "platform/platform.h"
#include "sokol_gfx.h"

#include <cstdint>
#include <memory>
#include <optional>
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
