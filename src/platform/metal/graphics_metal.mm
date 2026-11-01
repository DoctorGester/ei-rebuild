// The Metal backend of the renderer's device and window (graphics.h; docs/notes/platform.md, "Graphics";
// docs/notes/macos.md), and sokol_gfx's implementation on it. The window's drawable is an SDL Metal view's layer.

#include "hook/log.h"
#include "platform/graphics.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstddef>
#include <cstdio>

// sokol_gfx (third_party/sokol, unmodified; SOKOL_METAL from CMakeLists.txt): its implementation, after its
// declarations came through graphics.h.
#define SOKOL_IMPL
#if defined(EI_RENDER_VALIDATION)
#define SOKOL_DEBUG
#define SOKOL_VALIDATE_NON_FATAL   // a failed check skips the call and is logged; the game goes on
#endif
#include "sokol_gfx.h"

namespace platform::graphics {

// Readback's copy: a texture's region copied into a shared buffer. Release, as Readback's: no destructor.
class Staging {
public:
    bool Map(id<MTLTexture> source, sg_pixel_format format, const Rect& region);
    const std::uint8_t* Row(int y) const { return data + static_cast<std::size_t>(y) * pitch; }
    void Unmap() { data = nullptr; }
    void Release();

private:
    const void* buffer = nullptr;   // id<MTLBuffer>, retained
    std::size_t length = 0;
    const std::uint8_t* data = nullptr;   // while mapped
    std::size_t pitch = 0;
};

namespace {

// ---- The device ---------------------------------------------------------------------------------------------------

id<MTLDevice> g_device;
char g_error[160] = "";

bool Failed(const char* what) {
    std::snprintf(g_error, sizeof(g_error), "%s", what);
    return false;
}

// sokol's (main thread).
id<MTLDevice> Device() { return (__bridge id<MTLDevice>)sg_mtl_device(); }
id<MTLCommandQueue> Queue() { return (__bridge id<MTLCommandQueue>)sg_mtl_command_queue(); }

id<MTLTexture> Texture(sg_image image) {
    const sg_mtl_image_info info = sg_mtl_query_image_info(image);
    return (__bridge id<MTLTexture>)info.tex[info.active_slot];
}

// A reference of ours to a Metal object: raw, released explicitly before the device goes.
const void* Retained(const void* object) { return object ? CFBridgingRetain((__bridge id)object) : nullptr; }

void Drop(const void*& object) {
    if (object) CFBridgingRelease(object);
    object = nullptr;
}

// sokol's work recorded so far committed. Metal runs a queue's command buffers in the order they were enqueued, and
// sokol enqueues a frame's at its first pass: a command buffer of ours waited on before sokol's commit would never
// finish. The empty compute pass gives the commit a command buffer: a commit without one moves sokol's uniform buffers
// on without waiting for the GPU.
void CommitRecorded() {
    sg_pass pass{};
    pass.compute = true;
    pass.label = "ei-commit";
    sg_begin_pass(&pass);
    sg_end_pass();
    sg_commit();
}

// ---- The window ---------------------------------------------------------------------------------------------------

SDL_Window* g_window = nullptr;
SDL_MetalView g_view = nullptr;
CAMetalLayer* g_layer;
id<CAMetalDrawable> g_drawable;   // this frame's, from BeginDrawable to EndDrawable

void DropWindow() {
    g_drawable = nil;
    g_layer = nil;
    if (g_view) SDL_Metal_DestroyView(g_view);
    g_view = nullptr;
    g_window = nullptr;
}

// ---- Buffers ------------------------------------------------------------------------------------------------------

// UpdateBuffer's copy: words from the bytes set on the encoder into the buffer.
constexpr const char* kCopyWords = R"(#include <metal_stdlib>
using namespace metal;
kernel void copy_words(constant uint* from [[buffer(0)]], device uint* to [[buffer(1)]],
                       constant uint& count [[buffer(2)]], uint i [[thread_position_in_grid]]) {
    if (i < count) to[i] = from[i];
})";
constexpr std::size_t kCopyChunk = 4096;   // the most bytes setBytes takes
id<MTLComputePipelineState> g_copy;

bool MakeCopy() {
    if (g_copy) return true;
    NSError* error = nil;
    id<MTLLibrary> library = [Device() newLibraryWithSource:@(kCopyWords) options:nil error:&error];
    id<MTLFunction> function = [library newFunctionWithName:@"copy_words"];
    if (function) g_copy = [Device() newComputePipelineStateWithFunction:function error:&error];
    if (!g_copy)
        ei::Log("render: no Metal buffer copy (%s)", error ? error.localizedDescription.UTF8String : "no function");
    return g_copy != nil;
}

}   // namespace

// ---- graphics.h ---------------------------------------------------------------------------------------------------

void PrepareInBackground() {}

bool Start(sg_environment* environment) {
    g_device = MTLCreateSystemDefaultDevice();
    if (!g_device) {
        Failed("no Metal device (MTLCreateSystemDefaultDevice failed)");
        ei::Log("render: %s", g_error);
        return false;
    }
    environment->metal.device = (__bridge const void*)g_device;
    ei::Log("render: Metal device ready (%s)", g_device.name.UTF8String);
    return true;
}

void Stop() {
    DropWindow();
    g_copy = nil;
    g_device = nil;
}

const char* Error() { return g_error; }

int MaxSamples(int most) {
    for (int samples = most; samples > 1; samples /= 2)
        if ([g_device supportsTextureSampleCount:static_cast<NSUInteger>(samples)]) return samples;
    return 1;
}

// Metal reports no lost device to the renderer.
std::optional<std::uint32_t> Lost() { return std::nullopt; }

bool BeginDrawable(NativeWindow window, int width, int height, const PresentMode& mode, sg_swapchain* drawable) {
    g_drawable = nil;
    auto* sdl = static_cast<SDL_Window*>(window);
    if (sdl != g_window) DropWindow();
    if (!g_view) {
        g_view = SDL_Metal_CreateView(sdl);
        if (!g_view) return Failed(SDL_GetError());
        g_window = sdl;
        g_layer = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(g_view);
        g_layer.device = Device();
        g_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        g_layer.framebufferOnly = NO;   // read back (Readback::ReadDrawable)
    }
    g_layer.drawableSize = CGSizeMake(width, height);
    g_layer.displaySyncEnabled = mode.vsync;
    @autoreleasepool {
        g_drawable = [g_layer nextDrawable];
    }
    if (!g_drawable) return Failed("no drawable from the window's Metal layer");
    *drawable = sg_swapchain{};
    drawable->width = width;
    drawable->height = height;
    drawable->sample_count = 1;
    drawable->color_format = SG_PIXELFORMAT_BGRA8;
    drawable->depth_format = SG_PIXELFORMAT_NONE;
    drawable->metal.current_drawable = (__bridge const void*)g_drawable;
    return true;
}

// sokol presented the drawable at the end of its pass, committed with the frame.
bool EndDrawable() {
    const bool shown = g_drawable != nil;
    g_drawable = nil;
    return shown;
}

void ApplyViewport(int x, int y, int width, int height) { sg_apply_viewport(x, y, width, height, true); }

void ApplyScissorRect(int x, int y, int width, int height) { sg_apply_scissor_rect(x, y, width, height, true); }

// The copy runs in a compute pass of sokol's, so it is ordered with the draws recorded in sokol's command buffer
// (Metal tracks the buffer between its passes); the bytes go in with the encoder (setBytes copies them), so nothing
// of ours outlives the call. `offset` and `bytes` are multiples of 4 (the pools' vertices and 32-bit indices).
void UpdateBuffer(sg_buffer buffer, std::size_t offset, const void* data, std::size_t bytes) {
    const sg_mtl_buffer_info info = sg_mtl_query_buffer_info(buffer);
    id<MTLBuffer> target = (__bridge id<MTLBuffer>)info.buf[info.active_slot];
    if (!target || bytes < 4 || !MakeCopy()) return;
    sg_pass pass{};
    pass.compute = true;
    pass.label = "ei-update-buffer";
    sg_begin_pass(&pass);
    id<MTLComputeCommandEncoder> encoder = (__bridge id<MTLComputeCommandEncoder>)sg_mtl_compute_command_encoder();
    if (encoder) {
        [encoder setComputePipelineState:g_copy];
        const auto* from = static_cast<const std::uint8_t*>(data);
        for (std::size_t at = 0; at + 4 <= bytes; at += kCopyChunk) {
            const auto words = static_cast<std::uint32_t>(std::min(kCopyChunk, bytes - at) / 4);
            [encoder setBytes:from + at length:words * 4 atIndex:0];
            [encoder setBuffer:target offset:offset + at atIndex:1];
            [encoder setBytes:&words length:sizeof(words) atIndex:2];
            [encoder dispatchThreadgroups:MTLSizeMake((words + 63) / 64, 1, 1)
                    threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
        }
    }
    sg_end_pass();
}

Readback::Readback() = default;
Readback::~Readback() = default;

bool Readback::Read(sg_image image, const Rect& region) {
    if (!staging) staging = std::make_unique<Staging>();
    CommitRecorded();
    return staging->Map(Texture(image), sg_query_image_pixelformat(image), region);
}

bool Readback::ReadDrawable(const Rect& region) {
    if (!g_drawable) return false;
    if (!staging) staging = std::make_unique<Staging>();
    CommitRecorded();
    return staging->Map(g_drawable.texture, SG_PIXELFORMAT_BGRA8, region);
}

const std::uint8_t* Readback::Row(int y) const { return staging->Row(y); }

void Readback::Unmap() {
    if (staging) staging->Unmap();
}

void Readback::Release() {
    if (staging) staging->Release();
    staging.reset();
}

// No GPU timestamps on Metal yet: Begin refuses.
struct GpuTimer::Queries {};

GpuTimer::GpuTimer() = default;
GpuTimer::~GpuTimer() = default;
bool GpuTimer::Begin() { return false; }
void GpuTimer::Stamp() {}
void GpuTimer::End() {}
bool GpuTimer::Read(std::vector<double>*) { return false; }
void GpuTimer::Release() { queries.reset(); }

bool Staging::Map(id<MTLTexture> source, sg_pixel_format format, const Rect& region) {
    Unmap();
    if (!source || region.left < 0 || region.top < 0 || region.right <= region.left || region.bottom <= region.top ||
        static_cast<NSUInteger>(region.right) > source.width || static_cast<NSUInteger>(region.bottom) > source.height)
        return false;
    const int width = region.right - region.left, height = region.bottom - region.top;
    const auto rowPitch = static_cast<std::size_t>(sg_query_row_pitch(format, width, 1));
    const std::size_t bytes = rowPitch * static_cast<std::size_t>(height);
    if (!buffer || length < bytes) {
        Release();
        buffer = Retained((__bridge const void*)[Device() newBufferWithLength:bytes
                                                                      options:MTLResourceStorageModeShared]);
        if (!buffer) return false;
        length = bytes;
    }
    pitch = rowPitch;
    id<MTLBuffer> copy = (__bridge id<MTLBuffer>)buffer;
    @autoreleasepool {
        id<MTLCommandBuffer> commands = [Queue() commandBuffer];
        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
        [blit copyFromTexture:source
                         sourceSlice:0
                         sourceLevel:0
                        sourceOrigin:MTLOriginMake(static_cast<NSUInteger>(region.left),
                                                   static_cast<NSUInteger>(region.top), 0)
                          sourceSize:MTLSizeMake(static_cast<NSUInteger>(width), static_cast<NSUInteger>(height), 1)
                            toBuffer:copy
                   destinationOffset:0
              destinationBytesPerRow:pitch
            destinationBytesPerImage:bytes];
        [blit endEncoding];
        [commands commit];
        [commands waitUntilCompleted];
        if (commands.status != MTLCommandBufferStatusCompleted) return false;
    }
    data = static_cast<const std::uint8_t*>(copy.contents);
    return true;
}

void Staging::Release() {
    Unmap();
    Drop(buffer);
    length = 0;
    pitch = 0;
}

}   // namespace platform::graphics
