#pragma once

// The renderer's GPU on SDL3's GPU API (docs/notes/renderer.md, "gpu.h"): the device (Direct3D 12 or Vulkan), the
// window's swap chain, textures, buffers, samplers and pipelines, passes and draws, an upload ring for every write from
// the CPU and downloads read once their commands are done. The renderer's files include this header and no SDL one.
// Everything runs on the main thread but the device PrepareInBackground makes and the pipelines made on it ahead.
//
// The commands go into one command buffer, the frame's, taken at the first command and sent by Submit (which presents
// the swap chain's texture when one was taken). Writes and copies are recorded between passes, in order with the
// draws: a draw recorded before a write still reads what it replaces. Handles are plain values; a texture, buffer or
// download released while recorded commands still use it lives until they are done.

#include "lib/rect.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

struct SDL_Window;
struct SDL_GPUDevice;
struct SDL_GPUTexture;
struct SDL_GPUBuffer;
struct SDL_GPUSampler;
struct SDL_GPUGraphicsPipeline;

namespace render::gpu {

// ---- The device ---------------------------------------------------------------------------------------------------

struct DeviceOptions {
    // SDL's driver name ("direct3d12", "vulkan"); null: Direct3D 12, else Vulkan, on Windows; Vulkan elsewhere.
    const char* driver = nullptr;
    bool debug = false;   // the driver's validation layer, where the system has it
};
struct DeviceLimits;
struct PipelineDesc;
// Appends to `out` the descriptions the renderer always uses. Called once, on the thread that made the device, so it
// touches nothing of the main thread's.
using PipelineLister = std::function<void(const DeviceLimits& limits, std::vector<PipelineDesc>& out)>;
// Starts making the device on a thread of its own; Start takes it. SDL's video is not started here (the platform
// layer starts it after naming the window class): a driver that needs it (Vulkan) fails there without it, and Start
// makes the device again on the calling thread.
// With a `lister`, the same thread makes the pipelines it names into the cache, shaders with them, before Start
// returns (a first start otherwise pays for each at its first draw, hundreds of milliseconds in all). One that
// cannot be made is skipped without a message: MakePipeline tries it again and says why. Nothing is shared with the
// main thread: Start joins the thread, then takes the device and the pipelines (a device Start does not take, for
// other options, goes with them).
void PrepareInBackground(const DeviceOptions& options = {}, PipelineLister lister = nullptr);
// The device made in the background, or one made now. False, with Error() saying why, when there is none.
bool Start(const DeviceOptions& options = {});
// Waits for the GPU, then releases the window, everything kept and the device. Handles released after it are ignored.
void Stop();
bool Started();
const char* Error();
const char* DriverName();   // SDL's: "direct3d12", "vulkan"
const char* DeviceName();   // the adapter's
// Misused calls so far (each logged): a write inside a pass, a draw without a pipeline, a bad description, ...
int Problems();

// ---- Formats, textures and samplers -------------------------------------------------------------------------------

enum class Format : std::uint8_t {
    None,
    Rgba8,
    Bgra8,         // the swap chain's on Direct3D 12 and Vulkan
    R8,
    R8Uint,
    R16Uint,
    R32Float,
    Rgba32Float,
    Bc1,
    Bc2,
    Bc3,
    Depth32Float,
};
// The most samples, up to `most` (a power of two), a target of `colour` with a `depth` buffer (None: without) is drawn
// with; 1: none.
int MaxSamples(Format colour, Format depth, int most);
// Whether a target of `colour` with a `depth` buffer (None: without) is drawn with exactly `samples`: a device lists the
// counts it has (lavapipe: 1 and 4, no 2), and a count it does not list is not drawn (a pass with it is invalid).
bool SupportsSamples(Format colour, Format depth, int samples);

enum TextureUse : std::uint8_t {
    kSampled = 1,   // not with an integer format (SDL_GPU samples none)
    kColourTarget = 2,
    kDepthTarget = 4,
};
// Whether textures of `format` can be made for `use` (TextureUse bits).
bool FormatSupported(Format format, std::uint8_t use);
struct TextureDesc {
    int width = 1, height = 1;
    int levels = 1;
    int samples = 1;
    Format format = Format::Rgba8;
    std::uint8_t use = kSampled;   // TextureUse bits
    const char* label = nullptr;
};
struct Texture {
    SDL_GPUTexture* texture = nullptr;
    int width = 0, height = 0;
    int levels = 0;
    int samples = 1;
    Format format = Format::None;
    bool swapchain = false;   // the window's this frame (AcquireSwapchain): a target only, never read back
    explicit operator bool() const { return texture != nullptr; }
};
// An empty handle when it cannot be made (logged).
Texture MakeTexture(const TextureDesc& desc);
void Release(Texture& texture);

enum class Filter : std::uint8_t { Nearest, Linear };
enum class Wrap : std::uint8_t { Repeat, Mirror, Clamp };
enum class Compare : std::uint8_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
struct SamplerDesc {
    Filter min = Filter::Linear, mag = Filter::Linear, mip = Filter::Nearest;
    Wrap u = Wrap::Repeat, v = Wrap::Repeat;
    // The levels sampled: a texture shown from level k on is sampled with minLod k (there are no views).
    float minLod = 0.0f, maxLod = 1000.0f;
    int anisotropy = 1;               // above 1: anisotropic at that level
    Compare compare = Compare::Never;   // Never: no comparison; else a depth comparison sampler
    bool operator==(const SamplerDesc&) const = default;
};
struct Sampler {
    SDL_GPUSampler* sampler = nullptr;
    explicit operator bool() const { return sampler != nullptr; }
};
// Kept by description: the same description gives the same sampler, made at its first use and kept until Stop.
Sampler MakeSampler(const SamplerDesc& desc);
// Point filtered, clamped to the edge: what the passes read their targets and images with.
inline Sampler NearestClampSampler() {
    return MakeSampler({Filter::Nearest, Filter::Nearest, Filter::Nearest, Wrap::Clamp, Wrap::Clamp});
}

// ---- Buffers, writes, copies and downloads ------------------------------------------------------------------------

enum class BufferUse : std::uint8_t { Vertex, Index };
struct Buffer {
    SDL_GPUBuffer* buffer = nullptr;
    std::uint32_t size = 0;
    explicit operator bool() const { return buffer != nullptr; }
};
Buffer MakeBuffer(BufferUse use, std::uint32_t bytes, const char* label = nullptr);
void Release(Buffer& buffer);

// What a write leaves of the rest of its buffer or texture. Discard: later commands read nothing else of it (a stream
// written from its start again, a texture written whole), so the write may go into a fresh copy while the GPU still
// reads the old one for the commands before it.
enum class Contents : std::uint8_t { Keep, Discard };
// Copied into the upload ring now, uploaded before the next pass. False (logged) inside a pass or out of range.
bool Write(const Buffer& buffer, std::uint32_t offset, const void* data, std::uint32_t bytes,
           Contents contents = Contents::Keep);
// `region` of level `level` from `rows` (`pitch` bytes apart; a compressed format's rows are rows of 4x4 blocks).
bool Write(const Texture& texture, int level, const Rect& region, const void* rows, std::size_t pitch,
           Contents contents = Contents::Keep);
// `region` of `from`'s level `fromLevel` into `to`'s level `toLevel` at (x, y); same format, outside passes.
bool Copy(const Texture& from, int fromLevel, const Rect& region, const Texture& to, int toLevel, int x, int y);

// A download: texels read back to the CPU once the commands before it are done.
struct Ticket {
    std::uint32_t id = 0;
    explicit operator bool() const { return id != 0; }
};
// `region` of level `level` (of a texture with one sample; a depth texture's whole level on Direct3D 12) recorded
// in order with the frame's commands. An empty ticket when it cannot be.
Ticket Download(const Texture& texture, int level, const Rect& region);
// Done without waiting: its commands were submitted and the GPU finished them.
bool Done(Ticket ticket);
// Waits until it is done; submits the commands recorded so far first when they hold it (presenting the swap chain's
// texture if one was taken). False when it cannot finish (logged).
bool Wait(Ticket ticket);
struct Texels {
    const std::uint8_t* data = nullptr;
    std::size_t pitch = 0;
    int width = 0, height = 0;
    Format format = Format::None;
    const std::uint8_t* Row(int y) const { return data + static_cast<std::size_t>(y) * pitch; }
};
// A done ticket's texels, mapped until Release. False while it is not done.
bool Read(Ticket ticket, Texels* texels);
void Release(Ticket& ticket);

// ---- Pipelines ----------------------------------------------------------------------------------------------------

enum class VertexFormat : std::uint8_t { Float, Float2, Float3, Float4, UByte4Norm };
struct VertexAttribute {
    std::uint8_t location = 0;   // the program's input (gpu_shaders' kAttr...)
    std::uint8_t buffer = 0;     // the vertex buffer it is read from
    VertexFormat format = VertexFormat::Float4;
    std::uint16_t offset = 0;
    bool operator==(const VertexAttribute&) const = default;
};
struct VertexBuffer {
    std::uint16_t stride = 0;
    bool perInstance = false;
    bool operator==(const VertexBuffer&) const = default;
};
enum class Primitive : std::uint8_t { Triangles, Lines };
enum class Cull : std::uint8_t { None, Front, Back };
enum class Winding : std::uint8_t { CounterClockwise, Clockwise };   // the front faces', on screen
enum class BlendFactor : std::uint8_t {
    Zero, One, SourceColour, OneMinusSourceColour, DestColour, OneMinusDestColour, SourceAlpha, OneMinusSourceAlpha,
    DestAlpha, OneMinusDestAlpha, SourceAlphaSaturate,
};
enum class BlendOp : std::uint8_t { Add, Subtract, ReverseSubtract, Min, Max };
struct Blend {
    bool on = false;
    BlendFactor sourceColour = BlendFactor::One, destColour = BlendFactor::Zero;
    BlendFactor sourceAlpha = BlendFactor::One, destAlpha = BlendFactor::Zero;
    BlendOp colourOp = BlendOp::Add, alphaOp = BlendOp::Add;
    bool operator==(const Blend&) const = default;
};
enum ColourMask : std::uint8_t { kWriteRed = 1, kWriteGreen = 2, kWriteBlue = 4, kWriteAlpha = 8, kWriteAll = 15 };

constexpr int kMostVertexBuffers = 4;
constexpr int kMostVertexAttributes = 8;
// A program (an index into gpu_shaders::kPrograms) drawn into one kind of target. The vertex buffers run from 0
// without gaps (SDL's Direct3D 12 backend looks a buffer's layout up by its index).
struct PipelineDesc {
    int program = -1;
    int bufferCount = 0;
    VertexBuffer buffers[kMostVertexBuffers] = {};
    int attributeCount = 0;
    VertexAttribute attributes[kMostVertexAttributes] = {};
    Primitive primitive = Primitive::Triangles;
    Cull cull = Cull::None;
    Winding front = Winding::CounterClockwise;
    Format colour = Format::Rgba8;   // None: depth only
    std::uint8_t colourMask = kWriteAll;
    Blend blend = {};
    Format second = Format::None;    // a second colour target (PassDesc::second), never blended
    std::uint8_t secondMask = kWriteAll;
    Format depth = Format::None;     // None: without a depth buffer
    Compare depthCompare = Compare::Always;
    bool depthWrite = false;
    float depthBias = 0.0f, depthBiasSlope = 0.0f, depthBiasClamp = 0.0f;
    int samples = 1;
    bool operator==(const PipelineDesc&) const = default;
};
struct Pipeline {
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    explicit operator bool() const { return pipeline != nullptr; }
};
// Kept by description (the pipeline cache): the same description gives the same pipeline, made with its program's
// shaders at its first use and kept until Stop. Empty (logged) when it cannot be made.
Pipeline MakePipeline(const PipelineDesc& desc, const char* label = nullptr);
int PipelineCount();

// What a PipelineLister may ask of the device.
struct DeviceLimits {
    SDL_GPUDevice* device = nullptr;
    // The most samples up to `most` (a power of two) that a target of `colour` and `depth` (None: without) takes.
    int MaxSamples(Format colour, Format depth, int most) const;
    bool SupportsSamples(Format colour, Format depth, int samples) const;
};

// ---- Passes and draws ---------------------------------------------------------------------------------------------

// What a pass does with a target's contents at its start.
enum class Load : std::uint8_t { Keep, Clear, DontCare };
struct ColourTarget {
    Texture texture;   // empty: no colour target
    int level = 0;
    Load load = Load::Keep;
    float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    bool store = true;   // false: its contents are not needed after the pass
    Texture resolve;     // a multisampled target resolved into it at the pass's end
    int resolveLevel = 0;
};
struct DepthTarget {
    Texture texture;   // empty: no depth target
    int level = 0;
    Load load = Load::Keep;
    float clear = 1.0f;
    bool store = true;
};
struct PassDesc {
    ColourTarget colour;
    ColourTarget second;   // a second colour target (SV_Target1); empty: none
    DepthTarget depth;
    const char* label = nullptr;
};
// The viewport and scissor are the whole target until set. False (logged) when the pass cannot begin.
bool BeginPass(const PassDesc& pass);
void EndPass();
bool InPass();
// In target pixels from the top-left corner on every driver.
void SetViewport(const Rect& rect);
void SetScissor(const Rect& rect);

enum class Stage : std::uint8_t { Vertex, Fragment };
struct BufferBinding {
    Buffer buffer;
    std::uint32_t offset = 0;
};
struct TextureBinding {
    Texture texture;
    Sampler sampler;
};
// What the draws below sent to SDL, and what the filter below left out, since the last ResetCounts.
struct DrawCounts {
    std::uint64_t draws = 0;
    std::uint64_t pipelinesBound = 0, pipelinesSkipped = 0;
    std::uint64_t vertexBound = 0, vertexSkipped = 0;       // calls (each binds the changed range)
    std::uint64_t indexBound = 0, indexSkipped = 0;
    std::uint64_t texturesBound = 0, texturesSkipped = 0;   // calls
    std::uint64_t pushes = 0, pushesSkipped = 0;
    std::uint64_t pushedBytes = 0;
};
DrawCounts Counts();
void ResetCounts();
// Whether a binding or uniform set again unchanged is left out (the default). Off only for tools/gpu_check's bench, to
// price what the filter saves.
void SetStateFiltering(bool on);
// Bindings and uniforms last for the pass, across pipelines; one set again unchanged is not sent again.
void ApplyPipeline(Pipeline pipeline);
void BindVertexBuffers(int first, std::span<const BufferBinding> buffers);
void BindIndexBuffer(const BufferBinding& binding);   // 32-bit indices
// Texture and sampler pairs from slot `first` on (gpu_shaders' kSampler...).
void BindTextures(Stage stage, int first, std::span<const TextureBinding> textures);
// A uniform block into slot `slot` (gpu_shaders' kBlock...).
void PushUniforms(Stage stage, int slot, const void* data, std::uint32_t bytes);
template <class Block>
void PushUniforms(Stage stage, int slot, const Block& block) {
    PushUniforms(stage, slot, &block, static_cast<std::uint32_t>(sizeof(Block)));
}
void Draw(std::uint32_t first, std::uint32_t count, std::uint32_t instances = 1);
void DrawIndexed(std::uint32_t first, std::uint32_t count, std::uint32_t instances = 1,
                 std::uint32_t firstInstance = 0);

// ---- The window ---------------------------------------------------------------------------------------------------

// The swap chain's on `window`, which then has the device's (one window at a time).
bool ClaimWindow(SDL_Window* window);
void ReleaseWindow();
enum class PresentMode : std::uint8_t {
    Vsync,       // each present waits for the vertical blank
    Immediate,   // no wait; the display may show a frame mid-scan (tearing)
    Mailbox,     // no wait; the newest frame is shown at the vertical blank, others dropped
};
bool PresentModeSupported(PresentMode mode);
// The mode set: an unsupported Immediate becomes Mailbox, an unsupported Mailbox Vsync (always supported).
PresentMode SetPresentMode(PresentMode mode);
Format SwapchainFormat();
// The window's texture for this frame, at the window's size (waits while too many frames are in flight). True with
// `target` empty when there is nothing to draw into (the window minimised or hidden); false (Error()) on a failure.
bool AcquireSwapchain(Texture* target);
// The frame's commands sent to the GPU, the swap chain's texture presented if one was taken. False (logged) when
// SDL refused them.
bool Submit();
std::uint64_t SubmitCount();
// The submissions whose fences are still kept: each is let go at a later Submit once it has finished.
std::size_t SubmissionsHeld();

// The whole frame's GPU time from the submissions' fences (there are no timestamp queries): from a submission (or the
// end of the one before, if later) to the end of its work, found by a thread polling the fences every 0.2 ms while it
// is on.
void SetFrameTiming(bool on);
// The latest submission finished since the last call: its milliseconds. False when none has.
bool FrameGpuTime(double* milliseconds);

} // namespace render::gpu
