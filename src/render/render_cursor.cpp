// The software cursor between frames (render::CursorBetweenFrames): the cursor thread presents the last frame
// again with the cursor at its new place through the graphics backend, never through sokol. See
// docs/notes/renderer.md, "The renderer's source files".
#include "render.h"
#include "render_internal.h"

#include "../hook/log.h"

#include <cstdint>
#include <mutex>
#include <vector>

namespace render {

namespace {

using namespace detail;

// The main thread's frames show the cursor themselves: the cursor thread presents only when none was
// presented for this long (its own period is 41 ms).
constexpr double kBetweenFramesMs = 40.0;

// The main thread's window present (BeginWindowPresent .. EndWindowPresent) and the cursor thread's take turns
// under it; it guards everything below. Re-entered as a critical section was: a locked call may reach another that
// locks it again.
using Lock = std::recursive_mutex;
using Hold = std::lock_guard<Lock>;
Lock g_presentLock;

long long g_lastPresent = 0;   // Ticks() of the main thread's last present on the game window

// The picture the last frame's cursor came from (render_blit.cpp's sheet: alpha 255 the image's texel, 0 the
// background shows).
struct Sheet {
    const void* holder = nullptr;
    const void* mask = nullptr;
    int width = 0, height = 0;
    sg_view view{};
} g_sheet;

// The main thread's last present on the game window; `g_kept` while the backend keeps its parts with the sheet in
// the cursor's slot (until the next frame's draws begin: the frame target is partly drawn then).
WindowParts g_window{};
bool g_kept = false;

// The present's parts handed to the backend; without a sheet the cursor's slot holds the ramp, as the present's
// does without a cursor.
void Keep() {
    const platform::Rect placed{g_window.placed.left, g_window.placed.top, g_window.placed.right, g_window.placed.bottom};
    const platform::graphics::PresentParts parts{present_shader_desc(sg_query_backend()),
                                                 g_window.shader,
                                                 g_window.pipeline,
                                                 {g_window.frame, g_window.ramp, g_sheet.holder ? g_sheet.view : g_window.ramp},
                                                 LinearSampler(),
                                                 placed};
    g_kept = platform::graphics::KeepPresent(parts);
}

CursorPresentInfo g_info;

// Why the cursor thread did not present (counted; the first few of each logged).
enum class Refusal { Picture, NoFrame, Recent, Outside };
bool Refused(Refusal why) {
    static const char* const kWhy[] = {"the cursor's picture was not drawn by a frame yet", "no frame presented on the game window",
                                       "a frame was presented less than 40 ms ago", "the rectangle is not inside the frame"};
    int i = static_cast<int>(why);
    ++g_info.refused[i];
    if (why != Refusal::Recent && g_info.refused[i] <= 3) ei::Log("render: cursor between frames: not presented: %s", kWhy[i]);
    return false;
}

#if defined(EI_RENDER_VALIDATION)
// Verify build: the back buffer the cursor thread drew, read back before it was shown and checked against the frame,
// the cursor's texels where their alpha is a half or more and the gamma ramp as uploaded (one to one placement only; a
// channel off by more than one counts as a difference); kept for the harness (ReadCursorPresent).
platform::graphics::PresentReads g_reads;
std::vector<std::uint8_t> g_lastBgr;
int g_lastWidth = 0, g_lastHeight = 0;

void CheckPresented(const Rect& to) {
    const int width = g_reads.width, height = g_reads.height;
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    const int cursorWidth = to.right - to.left;
    if (g_reads.drawable.size() != pixels * 4 || g_reads.frame.size() != pixels * 4 ||
        g_reads.cursorTexels.size() != static_cast<std::size_t>(cursorWidth) * (to.bottom - to.top) * 4)
        return;
    int differing = 0;
    g_lastBgr.resize(pixels * 3);
    const std::uint8_t* table = g_window.rampTable;
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* b = &g_reads.drawable[static_cast<std::size_t>(y) * width * 4];   // B, G, R, A
        const std::uint8_t* f = &g_reads.frame[static_cast<std::size_t>(y) * width * 4];      // R, G, B, A
        std::uint8_t* out = &g_lastBgr[static_cast<std::size_t>(y) * width * 3];
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* want = f + x * 4;
            if (x >= to.left && x < to.right && y >= to.top && y < to.bottom) {
                const std::uint8_t* t =
                    &g_reads.cursorTexels[(static_cast<std::size_t>(y - to.top) * cursorWidth + (x - to.left)) * 4];
                if (t[3] >= 128) want = t;
            }
            int wantB = table[want[2] * 4 + 2], wantG = table[want[1] * 4 + 1], wantR = table[want[0] * 4 + 0];
            const std::uint8_t* p = b + x * 4;
            if (p[0] - wantB > 1 || wantB - p[0] > 1 || p[1] - wantG > 1 || wantG - p[1] > 1 || p[2] - wantR > 1 || wantR - p[2] > 1)
                ++differing;
            out[x * 3 + 0] = p[0];
            out[x * 3 + 1] = p[1];
            out[x * 3 + 2] = p[2];
        }
    }
    g_lastWidth = width;
    g_lastHeight = height;
    ++g_info.checks;
    if (differing) {
        ++g_info.mismatches;
        if (g_info.mismatches <= 10)
            ei::Log("render: MISMATCH cursor between frames: %d pixels of the presented frame differ (cursor at %d,%d-%d,%d)",
                    differing, to.left, to.top, to.right, to.bottom);
    } else if (g_info.checks == 1 || g_info.checks % 50 == 0) {
        ei::Log("render: cursor between frames: %d presented frames checked, %d differed", g_info.checks, g_info.mismatches);
    }
}

// The reads of a one to one placement, else null.
platform::graphics::PresentReads* ReadsFor(const Rect& from) {
    const WindowParts& w = g_window;
    if (w.placed.left != 0 || w.placed.top != 0 || w.placed.right != w.frameWidth || w.placed.bottom != w.frameHeight ||
        w.clientWidth != w.frameWidth || w.clientHeight != w.frameHeight)
        return nullptr;
    g_reads.width = w.frameWidth;
    g_reads.height = w.frameHeight;
    g_reads.cursor = platform::Rect{from.left, from.top, from.right, from.bottom};
    return &g_reads;
}
#endif

} // namespace

namespace detail {

void SetCursorSheet(const void* holder, const void* mask, int width, int height, sg_view view) {
    Hold hold(g_presentLock);
    if (g_sheet.holder == holder && g_sheet.mask == mask && g_sheet.view.id == view.id) return;
    g_sheet = Sheet{holder, mask, width, height, view};
    if (g_kept) Keep();
}

void DropCursorSheet(const void* holder) {
    Hold hold(g_presentLock);
    if (g_sheet.holder == holder) g_sheet = Sheet{};
}

void BeginWindowPresent() {
    g_presentLock.lock();
    g_kept = false;
}

void EndWindowPresent(bool presented) {
    if (presented) {
        g_lastPresent = Ticks();
        if (CurrentWindowParts(&g_window)) Keep();
    }
    g_presentLock.unlock();
}

void DropCursorPresents() {
    Hold hold(g_presentLock);
    platform::graphics::DropPresent();
    g_kept = false;
    g_sheet = Sheet{};
}

} // namespace detail

bool CursorBetweenFrames(const void* imageHolder, const void* mask, const Rect& from, const Rect& to) {
    if (!imageHolder || !mask) return false;
    Hold hold(g_presentLock);
    ++g_info.calls;
    if (g_sheet.holder != imageHolder || g_sheet.mask != mask || from.left < 0 || from.top < 0 || from.right > g_sheet.width ||
        from.bottom > g_sheet.height || from.right <= from.left || from.bottom <= from.top)
        return Refused(Refusal::Picture);
    if (!g_kept) return Refused(Refusal::NoFrame);
    if ((Ticks() - g_lastPresent) * kMsPerTick < kBetweenFramesMs) return Refused(Refusal::Recent);
    // The cursor's rectangle is clipped to its bounds by the game; kept inside the frame.
    if (to.left < 0 || to.top < 0 || to.right > g_window.frameWidth || to.bottom > g_window.frameHeight ||
        to.right - to.left != from.right - from.left || to.bottom - to.top != from.bottom - from.top)
        return Refused(Refusal::Outside);
    // The present's uniforms (render_window.cpp): the whole frame; the cursor's rectangle, the frame's size and the
    // cursor texels per frame pixel (one), the size and the top left corner of the cursor's part of its picture.
    const present_params_t whole{{0.0f, 0.0f, 1.0f, 1.0f}};
    const cursor_params_t cursor{
        {static_cast<float>(to.left), static_cast<float>(to.top), static_cast<float>(to.right), static_cast<float>(to.bottom)},
        {static_cast<float>(g_window.frameWidth), static_cast<float>(g_window.frameHeight), 1.0f, 1.0f},
        {static_cast<float>(from.right - from.left), static_cast<float>(from.bottom - from.top), static_cast<float>(from.left),
         static_cast<float>(from.top)}};
    sg_range uniforms[2];
    uniforms[UB_present_params] = SG_RANGE(whole);
    uniforms[UB_cursor_params] = SG_RANGE(cursor);
    platform::graphics::PresentReads* reads = nullptr;
#if defined(EI_RENDER_VALIDATION)
    reads = ReadsFor(from);
#endif
    platform::FloatingPointGuard fpu;
    const bool presented = platform::graphics::PresentAgain(uniforms, reads);
#if defined(EI_RENDER_VALIDATION)
    if (reads) CheckPresented(to);
#endif
    if (!presented) {
        g_kept = false;   // until the main thread's next present
        return false;
    }
    ++g_info.presents;
    g_info.lastTo = to;
    return true;
}

CursorPresentInfo CursorPresents() {
    Hold hold(g_presentLock);
    return g_info;
}

bool ReadCursorPresent(int* width, int* height, std::vector<std::uint8_t>* bgr) {
#if defined(EI_RENDER_VALIDATION)
    Hold hold(g_presentLock);
    if (g_lastBgr.empty()) return false;
    *width = g_lastWidth;
    *height = g_lastHeight;
    *bgr = g_lastBgr;
    return true;
#else
    (void)width;
    (void)height;
    (void)bgr;
    return false;
#endif
}

} // namespace render
