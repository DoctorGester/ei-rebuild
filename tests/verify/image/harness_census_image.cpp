// The interface drawing census's reads of the image's interface (image_side.h; ui_census.cpp).
#include "image_side.h"

#include "game/console.h"
#include "game/surface.h"
#include "game/game_singletons.h"
#include "game/texture.h"
#include "game/ui.h"
#include "game/ui_screens.h"

#include <typeinfo>

namespace ei::verify::image {

namespace {

void (*g_owner)(const void* window, const void* surface, bool added) = nullptr;
void (*g_pixels)(const void* surface, const char* what, bool writes, void* const* frame) = nullptr;

void OnOwner(const CUiWindow* window, const CUiSurface* surface, bool added) { g_owner(window, surface, added); }
void OnPixels(const CUiSurface* surface, const char* what, bool writes, void* const* frame) {
    g_pixels(surface, what, writes, frame);
}

} // namespace

void* ClientBitmap(const void* holder) { return ::ClientBitmap(holder); }
bool IsSurfaceMemoryDc(void* dc) { return ::IsSurfaceMemoryDc(static_cast<HDC>(dc)); }
bool IsSurfaceType(std::uintptr_t type) {
    return type == reinterpret_cast<std::uintptr_t>(&typeid(CUiSurface)) ||
           type == reinterpret_cast<std::uintptr_t>(&typeid(CUiScrollingSurface));
}
std::size_t SurfaceSize() { return sizeof(CUiSurface); }
void ReadSurfaceFields(const void* copy, SurfaceFields* fields) {
    const auto* surface = static_cast<const CUiSurface*>(copy);
    fields->dc = surface->dc_;
    fields->holder = surface->holder_;
    fields->width = surface->width_;
    fields->height = surface->height_;
}
// Our holders have no DirectDraw surface: the field reads as null.
const void* HolderSurfaceField(void*) {
    static void* const kNoSurface = nullptr;
    return &kNoSurface;
}
void WindowSurfaces(const void* window, const void* const** data, int* size) {
    const auto* w = static_cast<const CUiWindow*>(window);
    *data = reinterpret_cast<const void* const*>(w->surfaces_.data());
    *size = static_cast<int>(w->surfaces_.size());
}
void ReadInterfaceSurfaces(InterfaceSurfaces* surfaces) {
    const CInterface3D* ui = &g_interface.Get();
    surfaces->backdrop = ui->backdrop_;
    surfaces->secondBackdrop = ui->secondBackdrop_;
    surfaces->fps = ui->fpsSurface_;
    surfaces->status = ui->statusSurface_;
    if (const console::ConsoleWindow* console = ui->console_) surfaces->consoleSurfaceField = &console->surface_;
}
void ObserveSurfaces(void (*owner)(const void* window, const void* surface, bool added),
                     void (*pixels)(const void* surface, const char* what, bool writes, void* const* frame)) {
    g_owner = owner;
    g_pixels = pixels;
    g_uiSurfaceOwnerObserver = &OnOwner;
    g_uiSurfacePixelObserver = &OnPixels;
}

} // namespace ei::verify::image
