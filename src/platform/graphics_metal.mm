// The Metal backend of the renderer's device and window (graphics.h; docs/notes/platform.md, "Graphics";
// docs/notes/macos.md), and sokol_gfx's implementation on it. The window's drawable is an SDL Metal view's layer.

#include "platform/graphics.h"

#include "hook/log.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>

// sokol_gfx (third_party/sokol, unmodified; SOKOL_METAL from cmake/native.cmake): its implementation, after its
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

// A reference of ours to a Metal object: raw, no destructor (globals are destroyed at exit while the cursor thread may
// still present).
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
    if (!g_copy) ei::Log("render: no Metal buffer copy (%s)", error ? error.localizedDescription.UTF8String : "no function");
    return g_copy != nil;
}

// ---- Presenting again from another thread -------------------------------------------------------------------------

// KeepPresent's references to sokol's Metal objects and its queue (the cursor thread never calls sokol).
struct Kept {
    bool valid = false;
    const sg_shader_desc* shaderDesc = nullptr;
    const void* queue = nullptr;      // id<MTLCommandQueue>
    const void* pipeline = nullptr;   // id<MTLRenderPipelineState>
    const void* depth = nullptr;      // id<MTLDepthStencilState>
    const void* textures[3] = {};     // id<MTLTexture>: the views' images
    const void* sampler = nullptr;    // id<MTLSamplerState>
    Rect viewport{0, 0, 0, 0};
} g_kept;

} // namespace

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
    DropPresent();
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

bool KeepPresent(const PresentParts& parts) {
    DropPresent();
    const sg_mtl_pipeline_info pipeline = sg_mtl_query_pipeline_info(parts.pipeline);
    g_kept.shaderDesc = parts.shaderDesc;
    g_kept.queue = Retained(sg_mtl_command_queue());
    g_kept.pipeline = Retained(pipeline.rps);
    g_kept.depth = Retained(pipeline.dss);
    bool textures = true;
    for (int i = 0; i < 3; ++i) {
        const sg_image image = sg_query_view_image(parts.views[i]);
        g_kept.textures[i] = image.id ? Retained((__bridge const void*)Texture(image)) : nullptr;
        textures = textures && g_kept.textures[i];
    }
    g_kept.sampler = Retained(sg_mtl_query_sampler_info(parts.sampler).smp);
    g_kept.viewport = parts.viewport;
    g_kept.valid = parts.shaderDesc && g_kept.queue && g_kept.pipeline && textures && g_kept.sampler;
    if (!g_kept.valid) DropPresent();
    return g_kept.valid;
}

void DropPresent() {
    Drop(g_kept.queue);
    Drop(g_kept.pipeline);
    Drop(g_kept.depth);
    for (const void*& texture : g_kept.textures) Drop(texture);
    Drop(g_kept.sampler);
    g_kept = Kept{};
}

// The reads stay empty (the harness's check is Direct3D's).
bool PresentAgain(std::span<const sg_range> uniforms, PresentReads* reads) {
    if (reads) {
        reads->drawable.clear();
        reads->frame.clear();
        reads->cursorTexels.clear();
    }
    if (!g_kept.valid || !g_layer || uniforms.size() > SG_MAX_UNIFORMBLOCK_BINDSLOTS) return false;
    @autoreleasepool {
        id<CAMetalDrawable> drawable = [g_layer nextDrawable];
        if (!drawable) return false;
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = drawable.texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLCommandBuffer> commands = [(__bridge id<MTLCommandQueue>)g_kept.queue commandBuffer];
        id<MTLRenderCommandEncoder> encoder = [commands renderCommandEncoderWithDescriptor:pass];
        [encoder setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)g_kept.pipeline];
        if (g_kept.depth) [encoder setDepthStencilState:(__bridge id<MTLDepthStencilState>)g_kept.depth];
        const Rect& v = g_kept.viewport;
        const MTLViewport viewport{static_cast<double>(v.left), static_cast<double>(v.top),
                                   static_cast<double>(v.right - v.left), static_cast<double>(v.bottom - v.top), 0.0, 1.0};
        [encoder setViewport:viewport];
        const sg_shader_desc& desc = *g_kept.shaderDesc;
        for (std::size_t i = 0; i < uniforms.size(); ++i) {
            const sg_shader_uniform_block& block = desc.uniform_blocks[i];
            if (block.stage == SG_SHADERSTAGE_VERTEX)
                [encoder setVertexBytes:uniforms[i].ptr length:uniforms[i].size atIndex:block.msl_buffer_n];
            else if (block.stage == SG_SHADERSTAGE_FRAGMENT)
                [encoder setFragmentBytes:uniforms[i].ptr length:uniforms[i].size atIndex:block.msl_buffer_n];
        }
        for (int i = 0; i < 3; ++i) {
            const sg_shader_texture_view& view = desc.views[i].texture;
            id<MTLTexture> texture = (__bridge id<MTLTexture>)g_kept.textures[i];
            if (view.stage == SG_SHADERSTAGE_VERTEX) [encoder setVertexTexture:texture atIndex:view.msl_texture_n];
            else if (view.stage == SG_SHADERSTAGE_FRAGMENT) [encoder setFragmentTexture:texture atIndex:view.msl_texture_n];
        }
        const sg_shader_sampler& sampler = desc.samplers[0];
        id<MTLSamplerState> state = (__bridge id<MTLSamplerState>)g_kept.sampler;
        if (sampler.stage == SG_SHADERSTAGE_VERTEX) [encoder setVertexSamplerState:state atIndex:sampler.msl_sampler_n];
        else if (sampler.stage == SG_SHADERSTAGE_FRAGMENT) [encoder setFragmentSamplerState:state atIndex:sampler.msl_sampler_n];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [encoder endEncoding];
        [commands presentDrawable:drawable];
        [commands commit];
    }
    return true;
}

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
            [encoder dispatchThreadgroups:MTLSizeMake((words + 63) / 64, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
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
    pitch = static_cast<std::size_t>(sg_query_row_pitch(format, width, 1));
    const std::size_t bytes = pitch * static_cast<std::size_t>(height);
    if (!buffer || length < bytes) {
        Release();
        buffer = Retained((__bridge const void*)[Device() newBufferWithLength:bytes options:MTLResourceStorageModeShared]);
        if (!buffer) return false;
        length = bytes;
    }
    id<MTLBuffer> copy = (__bridge id<MTLBuffer>)buffer;
    @autoreleasepool {
        id<MTLCommandBuffer> commands = [Queue() commandBuffer];
        id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
        [blit copyFromTexture:source
                         sourceSlice:0
                         sourceLevel:0
                        sourceOrigin:MTLOriginMake(static_cast<NSUInteger>(region.left), static_cast<NSUInteger>(region.top), 0)
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

} // namespace platform::graphics
