// Metal device, SDL drawables and texture transfers behind the shared sokol renderer.
// The sokol_gfx implementation, selecting the host's backend (third_party/sokol, unmodified).
#define SOKOL_IMPL
#if defined(EI_RENDER_VALIDATION) || defined(EI_NATIVE_VALIDATION)
#define SOKOL_DEBUG
#define SOKOL_VALIDATE_NON_FATAL   // a failed check skips the call and is logged; the game goes on
#endif
#include "sokol_gfx.h"
#undef SOKOL_IMPL
#undef SOKOL_GFX_IMPL

#include "hook/log.h"
#include "platform/graphics.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
namespace platform::graphics {
namespace {
// Empty commits rotate sokol's uniform-buffer slots without a GPU fence; native blits only flush
// when a render/compute command buffer exists. This revision has no public command-buffer query.
bool HasPendingCommands() { return _sg.mtl.cmd_buffer != nil; }
void (*g_flush)() = nullptr;
char g_error[256]{};
id<MTLDevice> g_device;
SDL_MetalView g_view = nullptr;
CAMetalLayer* g_layer;
id<CAMetalDrawable> g_drawable;
id<MTLTexture> Texture(sg_image image) {
    const auto info = sg_mtl_query_image_info(image);
    return (__bridge id<MTLTexture>)info.tex[info.active_slot];
}
id<MTLCommandBuffer> Commands() {
    // A blit must follow every recorded sokol pass and precede the next one on the same queue.
    if (HasPendingCommands()) g_flush();
    return [(__bridge id<MTLCommandQueue>)sg_mtl_command_queue() commandBuffer];
}
}

bool CreateEnvironment(sg_environment* environment, bool, void (*flush)()) {
    g_flush = flush;
    g_device = MTLCreateSystemDefaultDevice();
    environment->metal.device = (__bridge const void*)g_device;
    std::snprintf(g_error, sizeof(g_error), "%s", g_device ? "" : "Metal device initialization failed");
    return g_device != nil;
}

int SampleCount() {
    for (int count = 8; count > 1; count /= 2)
        if ([g_device supportsTextureSampleCount:count]) return count;
    return 1;
}

void DropWindow() {
    g_drawable = nil;
    g_layer = nil;
    if (g_view) SDL_Metal_DestroyView(g_view);
    g_view = nullptr;
}

void DropDevice() {
    DropWindow();
    g_device = nil;
}

bool AcquireDrawable(void* window, bool vsync, bool, sg_swapchain* drawable) {
    g_error[0] = 0;
    sg_swapchain swap{};
    auto* sdl = static_cast<SDL_Window*>(window);
    if (!sdl || SDL_GetWindowFlags(sdl) & SDL_WINDOW_MINIMIZED) return false;
    if (!g_view) {
        g_view = SDL_Metal_CreateView(sdl);
        if (!g_view) {
            std::snprintf(g_error, sizeof(g_error), "%s", SDL_GetError());
            return false;
        }
        g_layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(g_view);
        g_layer.device = g_device;
        g_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        g_layer.framebufferOnly = NO;
        int pointsWidth = 0, pointsHeight = 0, pixelWidth = 0, pixelHeight = 0;
        SDL_GetWindowSize(sdl, &pointsWidth, &pointsHeight);
        SDL_GetWindowSizeInPixels(sdl, &pixelWidth, &pixelHeight);
        ei::Log("render: window %dx%d points, drawable %dx%d pixels (density %.2f)", pointsWidth, pointsHeight,
                pixelWidth, pixelHeight, SDL_GetWindowPixelDensity(sdl));
    }
    int width = 0, height = 0;
    SDL_GetWindowSizeInPixels(sdl, &width, &height);
    if (width < 1 || height < 1) return false;
    g_layer.drawableSize = CGSizeMake(width, height);
    g_layer.displaySyncEnabled = vsync;
    g_drawable = [g_layer nextDrawable];
    swap.width = width;
    swap.height = height;
    swap.sample_count = 1;
    swap.color_format = SG_PIXELFORMAT_BGRA8;
    swap.depth_format = SG_PIXELFORMAT_NONE;
    swap.metal.current_drawable = (__bridge const void*)g_drawable;
    *drawable = swap;
    return g_drawable != nil;
}

void CopyImage(sg_image from, sg_image to, Region region, int x, int y) {
    @autoreleasepool {
        id<MTLCommandBuffer> commands = Commands();
        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
        [blit copyFromTexture:Texture(from)
                  sourceSlice:0
                  sourceLevel:0
                 sourceOrigin:MTLOriginMake(region.left, region.top, 0)
                   sourceSize:MTLSizeMake(region.right - region.left, region.bottom - region.top, 1)
                    toTexture:Texture(to)
             destinationSlice:0
             destinationLevel:0
            destinationOrigin:MTLOriginMake(x, y, 0)];
        [blit endEncoding];
        [commands commit];
    }
}

void WriteImage(sg_image image, Region region, const void* bytes, int pitch) {
    @autoreleasepool {
        const std::size_t stride = (static_cast<std::size_t>(pitch) + 255) & ~std::size_t{255};
        const int height = region.bottom - region.top;
        id<MTLBuffer> buffer = [g_device newBufferWithLength:stride * height options:MTLResourceStorageModeShared];
        for (int y = 0; y < height; ++y)
            std::memcpy(static_cast<char*>(buffer.contents) + y * stride, static_cast<const char*>(bytes) + y * pitch,
                        pitch);
        id<MTLCommandBuffer> commands = Commands();
        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
        [blit copyFromBuffer:buffer
                   sourceOffset:0
              sourceBytesPerRow:stride
            sourceBytesPerImage:stride * height
                     sourceSize:MTLSizeMake(region.right - region.left, height, 1)
                      toTexture:Texture(image)
               destinationSlice:0
               destinationLevel:0
              destinationOrigin:MTLOriginMake(region.left, region.top, 0)];
        [blit endEncoding];
        [commands commit];
    }
}

void GenerateMipmaps(sg_image image) {
    @autoreleasepool {
        id<MTLCommandBuffer> commands = Commands();
        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
        [blit generateMipmapsForTexture:Texture(image)];
        [blit endEncoding];
        [commands commit];
    }
}

bool ReadTexture(id<MTLTexture> texture, std::size_t pixel, unsigned x, unsigned y, unsigned width, unsigned height,
                 std::vector<std::uint8_t>* bytes, std::size_t* pitch) {
    @autoreleasepool {
        if (!texture || !width || !height || x + width > texture.width || y + height > texture.height) return false;
        *pitch = (width * pixel + 255) & ~std::size_t{255};
        id<MTLBuffer> buffer = [g_device newBufferWithLength:*pitch * height options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> commands = Commands();
        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
        [blit copyFromTexture:texture
                         sourceSlice:0
                         sourceLevel:0
                        sourceOrigin:MTLOriginMake(x, y, 0)
                          sourceSize:MTLSizeMake(width, height, 1)
                            toBuffer:buffer
                   destinationOffset:0
              destinationBytesPerRow:*pitch
            destinationBytesPerImage:*pitch * height];
        [blit endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        if (commands.status == MTLCommandBufferStatusError) return false;
        bytes->resize(*pitch * height);
        std::memcpy(bytes->data(), buffer.contents, bytes->size());
        return true;
    }
}

bool ReadImage(sg_image image, unsigned x, unsigned y, unsigned width, unsigned height,
               std::vector<std::uint8_t>* bytes, std::size_t* pitch) {
    const auto format = sg_query_image_desc(image).pixel_format;
    const std::size_t pixel = format == SG_PIXELFORMAT_R16UI     ? 2
                              : format == SG_PIXELFORMAT_R8      ? 1
                              : format == SG_PIXELFORMAT_RGBA32F ? 16
                                                                 : 4;
    return ReadTexture(Texture(image), pixel, x, y, width, height, bytes, pitch);
}

bool ReadDrawable(unsigned x, unsigned y, unsigned width, unsigned height, std::vector<std::uint8_t>* bytes,
                  std::size_t* pitch) {
    return ReadTexture(g_drawable.texture, 4, x, y, width, height, bytes, pitch);
}

bool WindowSize(void* window, int* width, int* height) {
    return window && SDL_GetWindowSize(static_cast<SDL_Window*>(window), width, height);
}

void PrepareDevice(bool) {}
const char* DeviceError() { return g_error; }
bool PollDeviceLoss(long*, long*) { return false; }
bool Removable() { return false; }
bool RequestRemoval() { return false; }
void RemoveIfAsked() {}
int Removals() { return 0; }
bool PresentDrawable(bool) { return g_drawable != nil; }
bool CanPresentOnWorker() { return false; }
sg_image MakeMipmappedImage(const sg_image_desc& desc) { return sg_make_image(desc); }
bool Timestamps::Begin() { return false; }
void Timestamps::Stamp() {}
void Timestamps::End() {}
bool Timestamps::Read(std::vector<double>*) { return false; }
void Timestamps::Release() {}
// The control-state protection is specific to the Windows renderer's existing contract.
std::uint32_t SaveFloatingPointState() { return 0; }
void RestoreFloatingPointState(std::uint32_t) {}
void ReleaseReadbacks() {}
std::uint32_t ChainFlagsFor(bool, bool) { return 0; }
std::uint32_t SyncIntervalFor(bool vsync) { return vsync ? 1 : 0; }
std::uint32_t PresentFlagsFor(bool, std::uint32_t) { return 0; }
}
