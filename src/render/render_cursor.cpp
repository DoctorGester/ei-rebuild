// Between-frame cursors reuse the normal sokol presentation pass. The GPU lock serializes them with
// main-thread rendering; backends whose drawables require that thread decline worker presents.
#include "hook/log.h"
#include "render_internal.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace render {
namespace {
using namespace detail;
std::recursive_mutex g_presentLock;
using Hold = std::lock_guard<std::recursive_mutex>;
struct Sheet {
    const void* holder = nullptr;
    const void* mask = nullptr;
    int width = 0, height = 0;
    sg_view texture{};
} g_sheet;
bool g_ready = false;
std::chrono::steady_clock::time_point g_lastPresent;
CursorPresentInfo g_info;
std::vector<std::uint8_t> g_lastBgr;
int g_lastWidth = 0, g_lastHeight = 0;
enum class Refusal { Picture, NoFrame, Recent, Outside };
bool Refused(Refusal why) {
    ++g_info.refused[static_cast<int>(why)];
    return false;
}

#if defined(EI_RENDER_VALIDATION)
void CheckPresented(const BlitBatch& cursor) {
    WindowParts parts{};
    if (!CurrentWindowParts(&parts) || parts.placed.left || parts.placed.top || parts.clientWidth != parts.frameWidth ||
        parts.clientHeight != parts.frameHeight || parts.placed.right != parts.frameWidth ||
        parts.placed.bottom != parts.frameHeight)
        return;
    StagingCopy back, frame, sheet;
    const Rect& to = cursor.destination;
    const Rect& from = cursor.source;
    const sg_image picture = sg_query_view_desc(cursor.texture).texture.image;
    const unsigned width = parts.frameWidth, height = parts.frameHeight;
    if (back.MapDrawable(0, 0, width, height) && frame.Map(FrameImage()) &&
        sheet.Map(picture, from.left, from.top, from.right - from.left, from.bottom - from.top)) {
        int differing = 0;
        g_lastBgr.resize(static_cast<std::size_t>(width) * height * 3);
        for (unsigned y = 0; y < height; ++y) {
            for (unsigned x = 0; x < width; ++x) {
                const auto* expected = frame.Row(y) + x * 4;
                if (static_cast<int>(x) >= to.left && static_cast<int>(x) < to.right && static_cast<int>(y) >= to.top &&
                    static_cast<int>(y) < to.bottom) {
                    const auto* texel = sheet.Row(y - to.top) + (x - to.left) * 4;
                    if (texel[3] >= 128) expected = texel;
                }
                const auto* actual = back.Row(y) + x * 4;
                for (int c = 0; c < 3; ++c) {
                    const int channel = 2 - c;
                    const int want = parts.rampTable[expected[channel] * 4 + channel];
                    if (std::abs(static_cast<int>(actual[c]) - want) > 1) {
                        ++differing;
                        break;
                    }
                }
                std::memcpy(g_lastBgr.data() + (static_cast<std::size_t>(y) * width + x) * 3, actual, 3);
            }
        }
        g_lastWidth = width;
        g_lastHeight = height;
        ++g_info.checks;
        if (differing) {
            ++g_info.mismatches;
            if (g_info.mismatches <= 10) ei::Log("render: MISMATCH cursor between frames: %d pixels", differing);
        }
    }
    back.Release();
    frame.Release();
    sheet.Release();
}
#endif
}
namespace detail {
GpuGuard::GpuGuard() {
    g_presentLock.lock();
    saved = platform::graphics::SaveFloatingPointState();
}
GpuGuard::~GpuGuard() {
    platform::graphics::RestoreFloatingPointState(saved);
    g_presentLock.unlock();
}

void SetCursorSheet(const void* holder, const void* mask, int width, int height, sg_view texture) {
    Hold hold(g_presentLock);
    g_sheet = {holder, mask, width, height, texture};
}

void DropCursorSheet(const void* holder) {
    Hold hold(g_presentLock);
    if (g_sheet.holder == holder) g_sheet = {};
}

void BeginWindowPresent() {
    g_presentLock.lock();
    g_ready = false;
}

void EndWindowPresent(bool presented) {
    if (presented) {
        g_lastPresent = std::chrono::steady_clock::now();
        g_ready = true;
    }
    g_presentLock.unlock();
}

void DropCursorPresents() {
    Hold hold(g_presentLock);
    g_ready = false;
    g_sheet = {};
    g_lastBgr.clear();
}
}

bool CursorBetweenFrames(const void* holder, const void* mask, const Rect& from, const Rect& to) {
    if (!holder || !mask) return false;
    Hold hold(g_presentLock);
    ++g_info.calls;
    if (g_sheet.holder != holder || g_sheet.mask != mask || from.left < 0 || from.top < 0 ||
        from.right > g_sheet.width || from.bottom > g_sheet.height || from.right <= from.left ||
        from.bottom <= from.top || sg_query_view_state(g_sheet.texture) != SG_RESOURCESTATE_VALID)
        return Refused(Refusal::Picture);
    WindowParts parts{};
    if (!g_ready || !platform::graphics::CanPresentOnWorker() || !CurrentWindowParts(&parts))
        return Refused(Refusal::NoFrame);
    if (std::chrono::steady_clock::now() - g_lastPresent < std::chrono::milliseconds(40))
        return Refused(Refusal::Recent);
    if (to.left < 0 || to.top < 0 || to.right > parts.frameWidth || to.bottom > parts.frameHeight ||
        to.right - to.left != from.right - from.left || to.bottom - to.top != from.bottom - from.top)
        return Refused(Refusal::Outside);
    GpuGuard gpu;
    const BlitBatch cursor{g_sheet.texture, g_sheet.width, g_sheet.height, to, from};
    if (!DrawToWindow(&cursor, false)) {
        g_ready = false;
        return false;
    }
    CommitGpu();
#if defined(EI_RENDER_VALIDATION)
    CheckPresented(cursor);
#endif
    if (!platform::graphics::PresentDrawable(parts.vsync)) {
        g_ready = false;
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
    Hold hold(g_presentLock);
    if (g_lastBgr.empty()) return false;
    *width = g_lastWidth;
    *height = g_lastHeight;
    *bgr = g_lastBgr;
    return true;
}
}
