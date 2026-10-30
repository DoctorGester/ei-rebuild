#include "render/render_internal.h"

#include <mutex>

namespace render {
namespace {
std::recursive_mutex g_gpuLock;
CursorPresentInfo g_info;
}
namespace detail {
// Native presents stay on SDL's main thread; the frame's normal present includes its software cursor.
void SetCursorSheet(const void*, const void*, int, int, sg_view) {}
void DropCursorSheet(const void*) {}
void ProtectDevice(ID3D11Device*, ID3D11DeviceContext*) {}
void BeginWindowPresent() { g_gpuLock.lock(); }
void EndWindowPresent(bool) { g_gpuLock.unlock(); }
void DropCursorPresents() {}
}
bool CursorBetweenFrames(const void*, const void*, const Rect&, const Rect&) {
    std::lock_guard guard(g_gpuLock);
    ++g_info.calls;
    return false;
}
CursorPresentInfo CursorPresents() {
    std::lock_guard guard(g_gpuLock);
    return g_info;
}
bool ReadCursorPresent(int*, int*, std::vector<std::uint8_t>*) { return false; }
}
