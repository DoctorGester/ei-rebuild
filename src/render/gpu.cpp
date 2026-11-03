// The renderer's GPU on SDL_GPU (gpu.h; docs/notes/renderer.md, "gpu.h").
#include "gpu.h"

#include "../platform/log.h"

#include "shaders/gpu_shaders.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace render::gpu {

namespace {

// ---- The device ---------------------------------------------------------------------------------------------------

SDL_GPUDevice* g_device = nullptr;
std::string g_error;
std::string g_driverName, g_deviceName;
bool g_debug = false;
// Direct3D 12 copies a compressed texture's region only in whole 4x4 blocks, also where a small level is less than one;
// Vulkan wants the level's own size there.
bool g_wholeBlocks = false;
bool g_videoStarted = false;   // SDL's video subsystem started here (SDL_GPU's Vulkan driver loads through it)
int g_problems = 0;

struct Prepared {
    std::thread thread;
    SDL_GPUDevice* device = nullptr;
    std::string driver;
    bool debug = false;
    std::string error;
};
Prepared g_prepared;

void Problem(const char* format, ...) SDL_PRINTF_VARARG_FUNC(1);
void Problem(const char* format, ...) {
    if (g_problems++ >= 50) return;
    char text[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    ei::Log("gpu: %s", text);
}

double NowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::uint32_t Align(std::uint32_t value, std::uint32_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

// Each driver of `driver` (or the system's order) tried in turn; null with `error` saying why when none made one.
SDL_GPUDevice* CreateDevice(const std::string& driver, bool debug, std::string* error) {
    std::vector<std::string> drivers;
    if (!driver.empty()) drivers.push_back(driver);
#if defined(_WIN32)
    else drivers = {"direct3d12", "vulkan"};
#elif defined(__APPLE__)
    else drivers = {"metal"};
#else
    else drivers = {"vulkan"};
#endif
    error->clear();
    for (const std::string& name : drivers) {
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, name.c_str());
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, debug);
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true);
#if defined(_WIN32)
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true);
#elif defined(__APPLE__)
        SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, true);
#endif
        SDL_GPUDevice* device = SDL_CreateGPUDeviceWithProperties(props);
        SDL_DestroyProperties(props);
        if (device) return device;
        *error += (error->empty() ? "" : "; ") + name + ": " + SDL_GetError();
    }
    *error = "no GPU device (" + *error + ")";
    return nullptr;
}

// ---- Formats ------------------------------------------------------------------------------------------------------

SDL_GPUTextureFormat ToSdl(Format f) {
    switch (f) {
    case Format::Rgba8: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    case Format::Bgra8: return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    case Format::R8: return SDL_GPU_TEXTUREFORMAT_R8_UNORM;
    case Format::R8Uint: return SDL_GPU_TEXTUREFORMAT_R8_UINT;
    case Format::R16Uint: return SDL_GPU_TEXTUREFORMAT_R16_UINT;
    case Format::R32Float: return SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
    case Format::Rgba32Float: return SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
    case Format::Bc1: return SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM;
    case Format::Bc2: return SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM;
    case Format::Bc3: return SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM;
    case Format::Depth32Float: return SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    case Format::None: break;
    }
    return SDL_GPU_TEXTUREFORMAT_INVALID;
}

bool Compressed(Format f) { return f == Format::Bc1 || f == Format::Bc2 || f == Format::Bc3; }
bool Integer(Format f) { return f == Format::R8Uint || f == Format::R16Uint; }

// A copy's width or height in texels as the driver takes it.
std::uint32_t CopyExtent(Format f, int texels) {
    return static_cast<std::uint32_t>(Compressed(f) && g_wholeBlocks ? (texels + 3) / 4 * 4 : texels);
}

// A row of `width` texels (of 4x4 blocks when compressed) in bytes, and the rows of `height`.
std::uint32_t RowBytes(Format f, int width) {
    const std::uint32_t block = SDL_GPUTextureFormatTexelBlockSize(ToSdl(f));
    return static_cast<std::uint32_t>(Compressed(f) ? (width + 3) / 4 : width) * block;
}
std::uint32_t Rows(Format f, int height) {
    return static_cast<std::uint32_t>(Compressed(f) ? (height + 3) / 4 : height);
}

// Rows a transfer buffer holds for a texture are this far apart, and a texture's data starts at a multiple of
// kTexturePlacement: Direct3D 12's copy alignments, so SDL copies nothing again to align it.
constexpr std::uint32_t kRowPitchAlignment = 256;
constexpr std::uint32_t kTexturePlacement = 512;

// The texels a padded row of `pitch` bytes holds, as SDL's pixels_per_row.
std::uint32_t PixelsPerRow(Format f, std::uint32_t pitch) {
    const std::uint32_t block = SDL_GPUTextureFormatTexelBlockSize(ToSdl(f));
    return Compressed(f) ? pitch / block * 4 : pitch / block;
}

SDL_GPUSampleCount SampleCount(int samples) {
    switch (samples) {
    case 2: return SDL_GPU_SAMPLECOUNT_2;
    case 4: return SDL_GPU_SAMPLECOUNT_4;
    case 8: return SDL_GPU_SAMPLECOUNT_8;
    default: return SDL_GPU_SAMPLECOUNT_1;
    }
}

SDL_GPUCompareOp ToSdl(Compare c) {
    switch (c) {
    case Compare::Never: return SDL_GPU_COMPAREOP_NEVER;
    case Compare::Less: return SDL_GPU_COMPAREOP_LESS;
    case Compare::Equal: return SDL_GPU_COMPAREOP_EQUAL;
    case Compare::LessEqual: return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    case Compare::Greater: return SDL_GPU_COMPAREOP_GREATER;
    case Compare::NotEqual: return SDL_GPU_COMPAREOP_NOT_EQUAL;
    case Compare::GreaterEqual: return SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
    case Compare::Always: break;
    }
    return SDL_GPU_COMPAREOP_ALWAYS;
}

SDL_GPUBlendFactor ToSdl(BlendFactor f) {
    switch (f) {
    case BlendFactor::Zero: return SDL_GPU_BLENDFACTOR_ZERO;
    case BlendFactor::One: return SDL_GPU_BLENDFACTOR_ONE;
    case BlendFactor::SourceColour: return SDL_GPU_BLENDFACTOR_SRC_COLOR;
    case BlendFactor::OneMinusSourceColour: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
    case BlendFactor::DestColour: return SDL_GPU_BLENDFACTOR_DST_COLOR;
    case BlendFactor::OneMinusDestColour: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
    case BlendFactor::SourceAlpha: return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    case BlendFactor::OneMinusSourceAlpha: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DestAlpha: return SDL_GPU_BLENDFACTOR_DST_ALPHA;
    case BlendFactor::OneMinusDestAlpha: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
    case BlendFactor::SourceAlphaSaturate: return SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE;
    }
    return SDL_GPU_BLENDFACTOR_ONE;
}

SDL_GPUBlendOp ToSdl(BlendOp op) {
    switch (op) {
    case BlendOp::Add: return SDL_GPU_BLENDOP_ADD;
    case BlendOp::Subtract: return SDL_GPU_BLENDOP_SUBTRACT;
    case BlendOp::ReverseSubtract: return SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
    case BlendOp::Min: return SDL_GPU_BLENDOP_MIN;
    case BlendOp::Max: return SDL_GPU_BLENDOP_MAX;
    }
    return SDL_GPU_BLENDOP_ADD;
}

SDL_GPUVertexElementFormat ToSdl(VertexFormat f) {
    switch (f) {
    case VertexFormat::Float: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
    case VertexFormat::Float2: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    case VertexFormat::Float3: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    case VertexFormat::Float4: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
    case VertexFormat::UByte4Norm: return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    }
    return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
}

SDL_GPUSamplerAddressMode ToSdl(Wrap w) {
    switch (w) {
    case Wrap::Repeat: return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    case Wrap::Mirror: return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
    case Wrap::Clamp: return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    }
    return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
}

// ---- Submissions and the frame's GPU time -------------------------------------------------------------------------

// A command buffer sent to the GPU: its fence until it is done. The frame timer's thread marks them done too, so the
// list is read and changed under g_fenceLock (the fences are released only by the main thread).
struct Submission {
    std::uint64_t serial;
    SDL_GPUFence* fence;   // null: SDL refused it, nothing to wait for
    double submitted;   // NowMs()
    bool done = false;
    double gpuMs = -1.0;   // the frame timer's, when it saw it finish
};
std::mutex g_fenceLock;
std::deque<Submission> g_submissions;
std::uint64_t g_serial = 1;      // the open command buffer's: the next submission's
std::uint64_t g_doneBefore = 1;  // every submission before this serial is done and dropped
double g_lastFinished = 0.0;     // the frame timer's: when the last submission it saw finish did
double g_latestGpuMs = -1.0;     // its time, not yet taken by FrameGpuTime
std::thread g_timer;
std::atomic<bool> g_timing{false};

// Under g_fenceLock.
void MarkDone(Submission& s, bool timed) {
    s.done = true;
    if (!timed) return;
    const double now = NowMs();
    s.gpuMs = now - std::max(s.submitted, g_lastFinished);
    g_lastFinished = now;
    g_latestGpuMs = s.gpuMs;
}

// Done submissions at the front dropped, their fences released. Under g_fenceLock.
void DropDone() {
    while (!g_submissions.empty() && g_submissions.front().done) {
        if (g_submissions.front().fence) SDL_ReleaseGPUFence(g_device, g_submissions.front().fence);
        g_doneBefore = g_submissions.front().serial + 1;
        g_submissions.pop_front();
    }
}

void PollFences(bool timed) {
    std::lock_guard<std::mutex> lock(g_fenceLock);
    // One queue: they finish in order.
    for (Submission& s : g_submissions) {
        if (s.done) continue;
        if (!SDL_QueryGPUFence(g_device, s.fence)) break;
        MarkDone(s, timed);
    }
}

void TimerThread() {
    while (g_timing.load()) {
        PollFences(true);
        SDL_DelayNS(200000);
    }
}

// Whether submission `serial` is done (asking its fence). The open command buffer's is not.
bool SubmissionDone(std::uint64_t serial) {
    if (serial >= g_serial) return false;
    if (serial < g_doneBefore) return true;
    std::lock_guard<std::mutex> lock(g_fenceLock);
    for (Submission& s : g_submissions) {
        if (s.serial != serial) continue;
        if (!s.done && SDL_QueryGPUFence(g_device, s.fence)) MarkDone(s, g_timing.load());
        return s.done;
    }
    return true;
}

// ---- The frame's commands -----------------------------------------------------------------------------------------

SDL_GPUCommandBuffer* g_commands = nullptr;
SDL_GPURenderPass* g_pass = nullptr;
SDL_Window* g_window = nullptr;
Texture g_swapchainTexture;
bool g_swapchainTaken = false;   // AcquireSwapchain was asked this command buffer

SDL_GPUCommandBuffer* Commands() {
    if (!g_commands && g_device) {
        g_commands = SDL_AcquireGPUCommandBuffer(g_device);
        if (!g_commands) Problem("no command buffer: %s", SDL_GetError());
    }
    return g_commands;
}

// ---- The upload ring and the copy pass ----------------------------------------------------------------------------

// Writes and copies wait here, each with the transfer buffer and offset its bytes went to, and are recorded in one copy
// pass before the next pass, download wait or submission. The ring is one transfer buffer, filled from its start each
// command buffer: mapped while writes go into it, unmapped when the waiting copies are recorded (SDL wants it so),
// mapped again without cycling to go on after them. A new command buffer, or a full ring, maps it cycling: SDL gives a
// free copy of it while the GPU still reads the old one. A write larger than kLargeWrite has a transfer buffer of its
// own.
constexpr std::uint32_t kRingBytes = 16u << 20;
constexpr std::uint32_t kLargeWrite = kRingBytes / 4;
// A command buffer that uploaded this much is submitted at the next write (a load's burst of textures), unless it has
// the swap chain's texture: the ring's copies stay few.
constexpr std::uint64_t kSubmitAfterBytes = 64ull << 20;
// A command buffer that cycled the ring this often (a burst) lets it go: SDL keeps every copy a cycle made until the
// transfer buffer is released.
constexpr int kMostRingCycles = 2;

struct Ring {
    SDL_GPUTransferBuffer* buffer = nullptr;
    std::uint8_t* mapped = nullptr;
    std::uint32_t used = 0;
    bool cycleNext = true;
    int cycles = 0;
};
Ring g_ring;
std::uint64_t g_uploaded = 0;   // bytes written in the open command buffer

struct CopyOp {
    enum Kind : std::uint8_t { UploadBuffer, UploadTexture, CopyTexture, DownloadTexture } kind;
    SDL_GPUTransferBuffer* transfer = nullptr;
    std::uint32_t offset = 0;
    bool releaseTransfer = false;   // a large write's own transfer buffer
    bool cycle = false;
    SDL_GPUBuffer* buffer = nullptr;
    std::uint32_t bufferOffset = 0, bytes = 0;
    SDL_GPUTexture* texture = nullptr;
    std::uint32_t level = 0, x = 0, y = 0, w = 0, h = 0;
    std::uint32_t pixelsPerRow = 0, rowsPerLayer = 0;
    SDL_GPUTexture* toTexture = nullptr;   // CopyTexture's
    std::uint32_t toLevel = 0, toX = 0, toY = 0;
};
std::vector<CopyOp> g_copies;

void Flush() {
    if (g_copies.empty()) return;
    if (g_ring.mapped) {
        SDL_UnmapGPUTransferBuffer(g_device, g_ring.buffer);
        g_ring.mapped = nullptr;
    }
    SDL_GPUCommandBuffer* commands = Commands();
    SDL_GPUCopyPass* pass = commands ? SDL_BeginGPUCopyPass(commands) : nullptr;
    if (!pass) {
        Problem("no copy pass: %s", SDL_GetError());
        for (const CopyOp& op : g_copies)
            if (op.releaseTransfer) SDL_ReleaseGPUTransferBuffer(g_device, op.transfer);
        g_copies.clear();
        return;
    }
    for (const CopyOp& op : g_copies) {
        switch (op.kind) {
        case CopyOp::UploadBuffer: {
            const SDL_GPUTransferBufferLocation from{op.transfer, op.offset};
            const SDL_GPUBufferRegion to{op.buffer, op.bufferOffset, op.bytes};
            SDL_UploadToGPUBuffer(pass, &from, &to, op.cycle);
            break;
        }
        case CopyOp::UploadTexture: {
            const SDL_GPUTextureTransferInfo from{op.transfer, op.offset, op.pixelsPerRow, op.rowsPerLayer};
            SDL_GPUTextureRegion to{};
            to.texture = op.texture;
            to.mip_level = op.level;
            to.x = op.x;
            to.y = op.y;
            to.w = op.w;
            to.h = op.h;
            to.d = 1;
            SDL_UploadToGPUTexture(pass, &from, &to, op.cycle);
            break;
        }
        case CopyOp::CopyTexture: {
            SDL_GPUTextureLocation from{};
            from.texture = op.texture;
            from.mip_level = op.level;
            from.x = op.x;
            from.y = op.y;
            SDL_GPUTextureLocation to{};
            to.texture = op.toTexture;
            to.mip_level = op.toLevel;
            to.x = op.toX;
            to.y = op.toY;
            SDL_CopyGPUTextureToTexture(pass, &from, &to, op.w, op.h, 1, false);
            break;
        }
        case CopyOp::DownloadTexture: {
            SDL_GPUTextureRegion from{};
            from.texture = op.texture;
            from.mip_level = op.level;
            from.x = op.x;
            from.y = op.y;
            from.w = op.w;
            from.h = op.h;
            from.d = 1;
            const SDL_GPUTextureTransferInfo to{op.transfer, op.offset, op.pixelsPerRow, op.rowsPerLayer};
            SDL_DownloadFromGPUTexture(pass, &from, &to);
            break;
        }
        }
        if (op.releaseTransfer) SDL_ReleaseGPUTransferBuffer(g_device, op.transfer);
    }
    SDL_EndGPUCopyPass(pass);
    g_copies.clear();
}

SDL_GPUTransferBuffer* MakeTransfer(SDL_GPUTransferBufferUsage usage, std::uint32_t bytes) {
    SDL_GPUTransferBufferCreateInfo ci{};
    ci.usage = usage;
    ci.size = bytes;
    SDL_GPUTransferBuffer* t = SDL_CreateGPUTransferBuffer(g_device, &ci);
    if (!t) Problem("no transfer buffer of %u bytes: %s", bytes, SDL_GetError());
    return t;
}

// Room for `bytes` at a multiple of `alignment`: the op's transfer buffer and offset set, the bytes' place returned
// (null on a failure).
std::uint8_t* Place(std::uint32_t bytes, std::uint32_t alignment, CopyOp* op) {
    g_uploaded += bytes;
    if (bytes > kLargeWrite) {
        op->transfer = MakeTransfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, bytes);
        if (!op->transfer) return nullptr;
        op->offset = 0;
        op->releaseTransfer = true;   // released once its copy is recorded; unmapped by PlaceDone
        auto* p = static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(g_device, op->transfer, false));
        if (!p) {
            Problem("a transfer buffer could not be mapped: %s", SDL_GetError());
            SDL_ReleaseGPUTransferBuffer(g_device, op->transfer);
            op->transfer = nullptr;
        }
        return p;
    }
    if (!g_ring.buffer) {
        g_ring.buffer = MakeTransfer(SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, kRingBytes);
        if (!g_ring.buffer) return nullptr;
        g_ring.used = 0;
        g_ring.cycleNext = true;
    }
    std::uint32_t at = Align(g_ring.used, alignment);
    bool cycle = g_ring.cycleNext;
    if (at + bytes > kRingBytes) {
        // Full: the copies waiting on this copy of the ring recorded, then a fresh copy.
        Flush();
        at = 0;
        cycle = true;
        ++g_ring.cycles;
    }
    if (!g_ring.mapped) {
        g_ring.mapped = static_cast<std::uint8_t*>(SDL_MapGPUTransferBuffer(g_device, g_ring.buffer, cycle));
        if (!g_ring.mapped) {
            Problem("the upload ring could not be mapped: %s", SDL_GetError());
            return nullptr;
        }
        g_ring.cycleNext = false;
    }
    op->transfer = g_ring.buffer;
    op->offset = at;
    g_ring.used = at + bytes;
    return g_ring.mapped + at;
}

// After the bytes went into Place's room: a large write's own transfer buffer unmapped, the op queued.
void PlaceDone(CopyOp& op) {
    if (op.releaseTransfer) SDL_UnmapGPUTransferBuffer(g_device, op.transfer);
    g_copies.push_back(op);
}

// A write starts: false (logged) inside a pass or without a device; a burst's command buffer submitted first.
bool WriteAllowed(const char* what) {
    if (!g_device) return false;
    if (g_pass) {
        Problem("%s inside a pass", what);
        return false;
    }
    if (g_uploaded >= kSubmitAfterBytes && !g_swapchainTaken) Submit();
    return true;
}

// ---- Downloads ----------------------------------------------------------------------------------------------------

struct DownloadEntry {
    SDL_GPUTransferBuffer* buffer = nullptr;
    std::uint32_t capacity = 0;
    std::uint64_t serial = 0;
    Texels texels;
    bool mapped = false;
};
std::unordered_map<std::uint32_t, DownloadEntry> g_downloads;
std::uint32_t g_nextTicket = 1;
// Download transfer buffers of released tickets, kept for the next downloads (the largest few).
struct Spare {
    SDL_GPUTransferBuffer* buffer;
    std::uint32_t capacity;
};
std::vector<Spare> g_spares;
constexpr std::size_t kMostSpares = 4;

SDL_GPUTransferBuffer* TakeDownloadBuffer(std::uint32_t bytes, std::uint32_t* capacity) {
    auto best = g_spares.end();
    for (auto it = g_spares.begin(); it != g_spares.end(); ++it)
        if (it->capacity >= bytes && (best == g_spares.end() || it->capacity < best->capacity)) best = it;
    if (best != g_spares.end()) {
        SDL_GPUTransferBuffer* b = best->buffer;
        *capacity = best->capacity;
        g_spares.erase(best);
        return b;
    }
    *capacity = Align(bytes, 64u << 10);
    return MakeTransfer(SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, *capacity);
}

void GiveBackDownloadBuffer(SDL_GPUTransferBuffer* buffer, std::uint32_t capacity) {
    g_spares.push_back({buffer, capacity});
    if (g_spares.size() <= kMostSpares) return;
    auto smallest = std::min_element(g_spares.begin(), g_spares.end(),
                                     [](const Spare& a, const Spare& b) { return a.capacity < b.capacity; });
    SDL_ReleaseGPUTransferBuffer(g_device, smallest->buffer);
    g_spares.erase(smallest);
}

// ---- Shaders, samplers and pipelines ------------------------------------------------------------------------------

std::vector<SDL_GPUShader*> g_vertexShaders, g_fragmentShaders;   // by program, made at their first use

SDL_GPUShader* ProgramShader(int program, bool vertex) {
    std::vector<SDL_GPUShader*>& shaders = vertex ? g_vertexShaders : g_fragmentShaders;
    shaders.resize(gpu_shaders::kProgramCount, nullptr);
    if (shaders[static_cast<std::size_t>(program)]) return shaders[static_cast<std::size_t>(program)];
    const gpu_shaders::Program& p = gpu_shaders::kPrograms[program];
    const gpu_shaders::Stage& s = vertex ? p.vertex : p.fragment;
    const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(g_device);
    SDL_GPUShaderCreateInfo ci{};
    const gpu_shaders::Code* code = &s.spirv;
    ci.format = SDL_GPU_SHADERFORMAT_SPIRV;
    if ((formats & SDL_GPU_SHADERFORMAT_DXIL) && s.dxil.size) {
        code = &s.dxil;
        ci.format = SDL_GPU_SHADERFORMAT_DXIL;
    } else if (!(formats & SDL_GPU_SHADERFORMAT_SPIRV) && (formats & SDL_GPU_SHADERFORMAT_MSL)) {
        code = &s.msl;
        ci.format = SDL_GPU_SHADERFORMAT_MSL;
    }
    ci.code = code->bytes;
    ci.code_size = code->size;
    ci.entrypoint = s.entry;
    ci.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
    ci.num_samplers = static_cast<Uint32>(s.samplers);
    ci.num_uniform_buffers = static_cast<Uint32>(s.uniformBlocks);
    SDL_GPUShader* shader = SDL_CreateGPUShader(g_device, &ci);
    if (!shader) Problem("the %s %s shader: %s", p.name, vertex ? "vertex" : "fragment", SDL_GetError());
    shaders[static_cast<std::size_t>(program)] = shader;
    return shader;
}

// FNV-1a over a description's fields.
struct Hasher {
    std::size_t h = 1469598103934665603ull;
    template <class T>
    void Add(T value) {
        unsigned char bytes[sizeof(T)];
        std::memcpy(bytes, &value, sizeof(T));
        for (unsigned char b : bytes) h = (h ^ b) * 1099511628211ull;
    }
};

struct SamplerHash {
    std::size_t operator()(const SamplerDesc& d) const {
        Hasher h;
        h.Add(d.min), h.Add(d.mag), h.Add(d.mip), h.Add(d.u), h.Add(d.v), h.Add(d.minLod), h.Add(d.maxLod);
        h.Add(d.anisotropy), h.Add(d.compare);
        return h.h;
    }
};
std::unordered_map<SamplerDesc, Sampler, SamplerHash> g_samplers;

struct PipelineHash {
    std::size_t operator()(const PipelineDesc& d) const {
        Hasher h;
        h.Add(d.program), h.Add(d.bufferCount);
        for (const VertexBuffer& b : d.buffers) h.Add(b.stride), h.Add(b.perInstance);
        h.Add(d.attributeCount);
        for (const VertexAttribute& a : d.attributes)
            h.Add(a.location), h.Add(a.buffer), h.Add(a.format), h.Add(a.offset);
        h.Add(d.primitive), h.Add(d.cull), h.Add(d.front), h.Add(d.colour), h.Add(d.colourMask);
        h.Add(d.blend.on), h.Add(d.blend.sourceColour), h.Add(d.blend.destColour), h.Add(d.blend.sourceAlpha);
        h.Add(d.blend.destAlpha), h.Add(d.blend.colourOp), h.Add(d.blend.alphaOp), h.Add(d.second), h.Add(d.secondMask);
        h.Add(d.depth), h.Add(d.depthCompare), h.Add(d.depthWrite), h.Add(d.depthBias), h.Add(d.depthBiasSlope);
        h.Add(d.depthBiasClamp), h.Add(d.samples);
        return h.h;
    }
};
std::unordered_map<PipelineDesc, Pipeline, PipelineHash> g_pipelines;

// A description's fault, or null.
const char* PipelineFault(const PipelineDesc& d) {
    if (d.program < 0 || d.program >= gpu_shaders::kProgramCount) return "no such program";
    if (d.bufferCount < 0 || d.bufferCount > kMostVertexBuffers) return "too many vertex buffers";
    if (d.attributeCount < 0 || d.attributeCount > kMostVertexAttributes) return "too many vertex attributes";
    if (d.colour == Format::None && d.depth == Format::None) return "no target";
    if (d.second != Format::None && d.colour == Format::None) return "a second colour target without a first";
    const gpu_shaders::Program& p = gpu_shaders::kPrograms[d.program];
    for (int i = 0; i < d.attributeCount; ++i)
        if (d.attributes[i].buffer >= d.bufferCount) return "an attribute reads a vertex buffer it does not have";
    for (int k = 0; k < p.inputCount; ++k) {
        bool given = false;
        for (int i = 0; i < d.attributeCount; ++i) given = given || d.attributes[i].location == p.inputs[k].location;
        if (!given) return "a vertex input of the program has no attribute";
    }
    return nullptr;
}

// ---- What the open pass has bound ---------------------------------------------------------------------------------

constexpr int kMostSamplers = 16;
constexpr int kMostBlocks = 4;
struct Bound {
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    BufferBinding vertex[kMostVertexBuffers];
    BufferBinding index;
    bool haveIndex = false;
    SDL_GPUTexture* textures[2][kMostSamplers] = {};
    SDL_GPUSampler* samplers[2][kMostSamplers] = {};
    std::vector<std::uint8_t> uniforms[2][kMostBlocks];   // the last pushed, kept as capacity between passes
    bool pushed[2][kMostBlocks] = {};
    void Reset() {
        pipeline = nullptr;
        for (BufferBinding& b : vertex) b = BufferBinding{};
        haveIndex = false;
        std::memset(textures, 0, sizeof(textures));
        std::memset(samplers, 0, sizeof(samplers));
        std::memset(pushed, 0, sizeof(pushed));
    }
};
Bound g_bound;

} // namespace

// ---- The device ---------------------------------------------------------------------------------------------------

void PrepareInBackground(const DeviceOptions& options) {
    if (g_device || g_prepared.thread.joinable()) return;
    g_prepared.driver = options.driver ? options.driver : "";
    g_prepared.debug = options.debug;
    g_prepared.thread = std::thread([] {
        g_prepared.device = CreateDevice(g_prepared.driver, g_prepared.debug, &g_prepared.error);
    });
}

bool Start(const DeviceOptions& options) {
    if (g_device) return true;
    const std::string driver = options.driver ? options.driver : "";
    if (g_prepared.thread.joinable()) {
        g_prepared.thread.join();
        if (g_prepared.driver == driver && g_prepared.debug == options.debug) {
            g_device = g_prepared.device;
            g_error = g_prepared.error;
        } else if (g_prepared.device) {
            SDL_DestroyGPUDevice(g_prepared.device);
        }
        g_prepared.device = nullptr;
    }
    if (!g_device) {
        if (!SDL_WasInit(SDL_INIT_VIDEO)) {
            if (SDL_InitSubSystem(SDL_INIT_VIDEO)) g_videoStarted = true;
        }
        g_device = CreateDevice(driver, options.debug, &g_error);
    }
    if (!g_device) {
        ei::Log("gpu: %s", g_error.c_str());
        return false;
    }
    g_error.clear();
    g_debug = options.debug;
    g_driverName = SDL_GetGPUDeviceDriver(g_device);
    g_wholeBlocks = g_driverName == "direct3d12";
    g_deviceName = SDL_GetStringProperty(SDL_GetGPUDeviceProperties(g_device), SDL_PROP_GPU_DEVICE_NAME_STRING, "?");
    ei::Log("gpu: %s device (%s)", g_driverName.c_str(), g_deviceName.c_str());
    return true;
}

void Stop() {
    if (g_prepared.thread.joinable()) {
        g_prepared.thread.join();
        if (g_prepared.device) SDL_DestroyGPUDevice(g_prepared.device);
        g_prepared.device = nullptr;
    }
    if (!g_device) return;
    SetFrameTiming(false);
    if (g_pass) EndPass();
    for (const CopyOp& op : g_copies)
        if (op.releaseTransfer) SDL_ReleaseGPUTransferBuffer(g_device, op.transfer);
    g_copies.clear();
    if (g_commands) {
        if (g_swapchainTaken) SDL_SubmitGPUCommandBuffer(g_commands);
        else SDL_CancelGPUCommandBuffer(g_commands);
        g_commands = nullptr;
    }
    SDL_WaitForGPUIdle(g_device);
    {
        std::lock_guard<std::mutex> lock(g_fenceLock);
        for (Submission& s : g_submissions)
            if (s.fence) SDL_ReleaseGPUFence(g_device, s.fence);
        g_submissions.clear();
    }
    g_doneBefore = g_serial;
    for (auto& [id, d] : g_downloads) {
        if (d.mapped) SDL_UnmapGPUTransferBuffer(g_device, d.buffer);
        SDL_ReleaseGPUTransferBuffer(g_device, d.buffer);
    }
    g_downloads.clear();
    for (const Spare& s : g_spares) SDL_ReleaseGPUTransferBuffer(g_device, s.buffer);
    g_spares.clear();
    if (g_ring.buffer) {
        if (g_ring.mapped) SDL_UnmapGPUTransferBuffer(g_device, g_ring.buffer);
        SDL_ReleaseGPUTransferBuffer(g_device, g_ring.buffer);
    }
    g_ring = Ring{};
    for (auto& [desc, p] : g_pipelines) SDL_ReleaseGPUGraphicsPipeline(g_device, p.pipeline);
    g_pipelines.clear();
    for (auto& [desc, s] : g_samplers) SDL_ReleaseGPUSampler(g_device, s.sampler);
    g_samplers.clear();
    for (std::vector<SDL_GPUShader*>* shaders : {&g_vertexShaders, &g_fragmentShaders}) {
        for (SDL_GPUShader* s : *shaders)
            if (s) SDL_ReleaseGPUShader(g_device, s);
        shaders->clear();
    }
    ReleaseWindow();
    SDL_DestroyGPUDevice(g_device);
    g_device = nullptr;
    g_bound.Reset();
    g_uploaded = 0;
    if (g_videoStarted) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        g_videoStarted = false;
    }
}

bool Started() { return g_device != nullptr; }
const char* Error() { return g_error.c_str(); }
const char* DriverName() { return g_driverName.c_str(); }
const char* DeviceName() { return g_deviceName.c_str(); }
int Problems() { return g_problems; }

// ---- Textures and samplers ----------------------------------------------------------------------------------------

int MaxSamples(Format colour, Format depth, int most) {
    if (!g_device) return 1;
    for (int samples = most; samples > 1; samples /= 2) {
        const SDL_GPUSampleCount count = SampleCount(samples);
        if (colour != Format::None && !SDL_GPUTextureSupportsSampleCount(g_device, ToSdl(colour), count)) continue;
        if (depth != Format::None && !SDL_GPUTextureSupportsSampleCount(g_device, ToSdl(depth), count)) continue;
        return samples;
    }
    return 1;
}

namespace {

SDL_GPUTextureUsageFlags Usage(std::uint8_t use) {
    SDL_GPUTextureUsageFlags flags = 0;
    if (use & kSampled) flags |= SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (use & kColourTarget) flags |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    if (use & kDepthTarget) flags |= SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    return flags;
}

} // namespace

bool FormatSupported(Format format, std::uint8_t use) {
    return g_device && format != Format::None &&
           SDL_GPUTextureSupportsFormat(g_device, ToSdl(format), SDL_GPU_TEXTURETYPE_2D, Usage(use));
}

Texture MakeTexture(const TextureDesc& desc) {
    if (!g_device) return {};
    if (Integer(desc.format) && (desc.use & kSampled)) {
        Problem("texture %s: an integer format is not sampled", desc.label ? desc.label : "");
        return {};
    }
    SDL_GPUTextureCreateInfo ci{};
    ci.type = SDL_GPU_TEXTURETYPE_2D;
    ci.format = ToSdl(desc.format);
    ci.usage = Usage(desc.use);
    ci.width = static_cast<Uint32>(desc.width);
    ci.height = static_cast<Uint32>(desc.height);
    ci.layer_count_or_depth = 1;
    ci.num_levels = static_cast<Uint32>(desc.levels);
    ci.sample_count = SampleCount(desc.samples);
    SDL_GPUTexture* t = SDL_CreateGPUTexture(g_device, &ci);
    if (!t) {
        Problem("texture %s (%dx%d, %d levels, %d samples): %s", desc.label ? desc.label : "", desc.width, desc.height,
                desc.levels, desc.samples, SDL_GetError());
        return {};
    }
    if (g_debug && desc.label) SDL_SetGPUTextureName(g_device, t, desc.label);
    Texture out;
    out.texture = t;
    out.width = desc.width;
    out.height = desc.height;
    out.levels = desc.levels;
    out.samples = desc.samples;
    out.format = desc.format;
    return out;
}

void Release(Texture& texture) {
    // Copies waiting for it recorded first: SDL keeps what recorded commands use.
    if (texture.texture && g_device && !texture.swapchain) {
        Flush();
        SDL_ReleaseGPUTexture(g_device, texture.texture);
    }
    texture = Texture{};
}

Sampler MakeSampler(const SamplerDesc& desc) {
    if (!g_device) return {};
    auto it = g_samplers.find(desc);
    if (it != g_samplers.end()) return it->second;
    SDL_GPUSamplerCreateInfo ci{};
    ci.min_filter = desc.min == Filter::Linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    ci.mag_filter = desc.mag == Filter::Linear ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
    ci.mipmap_mode = desc.mip == Filter::Linear ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    ci.address_mode_u = ToSdl(desc.u);
    ci.address_mode_v = ToSdl(desc.v);
    ci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    ci.min_lod = desc.minLod;
    ci.max_lod = desc.maxLod;
    ci.enable_anisotropy = desc.anisotropy > 1;
    ci.max_anisotropy = static_cast<float>(desc.anisotropy > 1 ? desc.anisotropy : 1);
    ci.enable_compare = desc.compare != Compare::Never;
    ci.compare_op = ToSdl(desc.compare);
    Sampler s{SDL_CreateGPUSampler(g_device, &ci)};
    if (!s.sampler) {
        Problem("sampler: %s", SDL_GetError());
        return {};
    }
    g_samplers.emplace(desc, s);
    return s;
}

// ---- Buffers, writes, copies and downloads ------------------------------------------------------------------------

Buffer MakeBuffer(BufferUse use, std::uint32_t bytes, const char* label) {
    if (!g_device || bytes == 0) return {};
    SDL_GPUBufferCreateInfo ci{};
    ci.usage = use == BufferUse::Index ? SDL_GPU_BUFFERUSAGE_INDEX : SDL_GPU_BUFFERUSAGE_VERTEX;
    ci.size = bytes;
    SDL_GPUBuffer* b = SDL_CreateGPUBuffer(g_device, &ci);
    if (!b) {
        Problem("buffer %s (%u bytes): %s", label ? label : "", bytes, SDL_GetError());
        return {};
    }
    if (g_debug && label) SDL_SetGPUBufferName(g_device, b, label);
    return Buffer{b, bytes};
}

void Release(Buffer& buffer) {
    if (buffer.buffer && g_device) {
        Flush();
        SDL_ReleaseGPUBuffer(g_device, buffer.buffer);
    }
    buffer = Buffer{};
}

bool Write(const Buffer& buffer, std::uint32_t offset, const void* data, std::uint32_t bytes, Contents contents) {
    if (bytes == 0) return true;
    if (!buffer || !WriteAllowed("a buffer write")) return false;
    if (offset > buffer.size || bytes > buffer.size - offset) {
        Problem("a write of %u bytes at %u into a buffer of %u", bytes, offset, buffer.size);
        return false;
    }
    CopyOp op{CopyOp::UploadBuffer};
    std::uint8_t* to = Place(bytes, 16, &op);
    if (!to) return false;
    std::memcpy(to, data, bytes);
    op.cycle = contents == Contents::Discard;
    op.buffer = buffer.buffer;
    op.bufferOffset = offset;
    op.bytes = bytes;
    PlaceDone(op);
    return true;
}

bool Write(const Texture& texture, int level, const Rect& region, const void* rows, std::size_t pitch,
           Contents contents) {
    if (!texture || texture.swapchain || !WriteAllowed("a texture write")) return false;
    const int w = region.right - region.left, h = region.bottom - region.top;
    const int levelWidth = std::max(1, texture.width >> level), levelHeight = std::max(1, texture.height >> level);
    if (w <= 0 || h <= 0 || level < 0 || level >= texture.levels || region.left < 0 || region.top < 0 ||
        region.right > levelWidth || region.bottom > levelHeight) {
        Problem("a texture write outside level %d", level);
        return false;
    }
    const std::uint32_t rowBytes = RowBytes(texture.format, w), rowCount = Rows(texture.format, h);
    const std::uint32_t padded = Align(rowBytes, kRowPitchAlignment);
    CopyOp op{CopyOp::UploadTexture};
    std::uint8_t* to = Place(padded * rowCount, kTexturePlacement, &op);
    if (!to) return false;
    const auto* from = static_cast<const std::uint8_t*>(rows);
    for (std::uint32_t y = 0; y < rowCount; ++y) std::memcpy(to + y * padded, from + y * pitch, rowBytes);
    op.cycle = contents == Contents::Discard;
    op.texture = texture.texture;
    op.level = static_cast<std::uint32_t>(level);
    op.x = static_cast<std::uint32_t>(region.left);
    op.y = static_cast<std::uint32_t>(region.top);
    op.w = CopyExtent(texture.format, w);
    op.h = CopyExtent(texture.format, h);
    op.pixelsPerRow = PixelsPerRow(texture.format, padded);
    op.rowsPerLayer = CopyExtent(texture.format, h);
    PlaceDone(op);
    return true;
}

bool Copy(const Texture& from, int fromLevel, const Rect& region, const Texture& to, int toLevel, int x, int y) {
    if (!from || !to || from.swapchain || to.swapchain || !WriteAllowed("a texture copy")) return false;
    const int w = region.right - region.left, h = region.bottom - region.top;
    if (w <= 0 || h <= 0 || from.format != to.format || from.samples != 1 || to.samples != 1) {
        Problem("a texture copy of another format, of samples or of nothing");
        return false;
    }
    CopyOp op{CopyOp::CopyTexture};
    op.texture = from.texture;
    op.level = static_cast<std::uint32_t>(fromLevel);
    op.x = static_cast<std::uint32_t>(region.left);
    op.y = static_cast<std::uint32_t>(region.top);
    op.w = static_cast<std::uint32_t>(w);
    op.h = static_cast<std::uint32_t>(h);
    op.toTexture = to.texture;
    op.toLevel = static_cast<std::uint32_t>(toLevel);
    op.toX = static_cast<std::uint32_t>(x);
    op.toY = static_cast<std::uint32_t>(y);
    g_copies.push_back(op);
    return true;
}

Ticket Download(const Texture& texture, int level, const Rect& region) {
    if (!texture || !g_device) return {};
    if (g_pass || texture.swapchain || texture.samples != 1) {
        Problem("a download %s", g_pass ? "inside a pass" : texture.swapchain ? "of the swap chain" : "of samples");
        return {};
    }
    const int w = region.right - region.left, h = region.bottom - region.top;
    const int levelWidth = std::max(1, texture.width >> level), levelHeight = std::max(1, texture.height >> level);
    if (w <= 0 || h <= 0 || level < 0 || level >= texture.levels || region.left < 0 || region.top < 0 ||
        region.right > levelWidth || region.bottom > levelHeight) {
        Problem("a download outside level %d", level);
        return {};
    }
    const std::uint32_t padded = Align(RowBytes(texture.format, w), kRowPitchAlignment);
    const std::uint32_t rows = Rows(texture.format, h);
    DownloadEntry d;
    d.buffer = TakeDownloadBuffer(padded * rows, &d.capacity);
    if (!d.buffer) return {};
    d.serial = g_serial;
    d.texels.pitch = padded;
    d.texels.width = w;
    d.texels.height = h;
    d.texels.format = texture.format;
    CopyOp op{CopyOp::DownloadTexture};
    op.transfer = d.buffer;
    op.texture = texture.texture;
    op.level = static_cast<std::uint32_t>(level);
    op.x = static_cast<std::uint32_t>(region.left);
    op.y = static_cast<std::uint32_t>(region.top);
    op.w = CopyExtent(texture.format, w);
    op.h = CopyExtent(texture.format, h);
    op.pixelsPerRow = PixelsPerRow(texture.format, padded);
    op.rowsPerLayer = CopyExtent(texture.format, h);
    g_copies.push_back(op);
    const Ticket ticket{g_nextTicket++};
    g_downloads.emplace(ticket.id, d);
    return ticket;
}

bool Done(Ticket ticket) {
    auto it = g_downloads.find(ticket.id);
    return it != g_downloads.end() && SubmissionDone(it->second.serial);
}

bool Wait(Ticket ticket) {
    auto it = g_downloads.find(ticket.id);
    if (it == g_downloads.end()) return false;
    const std::uint64_t serial = it->second.serial;
    if (serial >= g_serial && !Submit()) return false;
    if (SubmissionDone(serial)) return true;
    SDL_GPUFence* fence = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_fenceLock);
        for (const Submission& s : g_submissions)
            if (s.serial == serial) fence = s.fence;
    }
    // Outside the lock: the frame timer goes on polling; only this thread releases fences.
    if (!fence || !SDL_WaitForGPUFences(g_device, true, &fence, 1)) {
        Problem("a download's wait failed: %s", SDL_GetError());
        return false;
    }
    return SubmissionDone(serial);
}

bool Read(Ticket ticket, Texels* texels) {
    auto it = g_downloads.find(ticket.id);
    if (it == g_downloads.end() || !SubmissionDone(it->second.serial)) return false;
    DownloadEntry& d = it->second;
    if (!d.mapped) {
        d.texels.data = static_cast<const std::uint8_t*>(SDL_MapGPUTransferBuffer(g_device, d.buffer, false));
        if (!d.texels.data) {
            Problem("a download could not be mapped: %s", SDL_GetError());
            return false;
        }
        d.mapped = true;
    }
    *texels = d.texels;
    return true;
}

void Release(Ticket& ticket) {
    auto it = g_downloads.find(ticket.id);
    ticket = Ticket{};
    if (it == g_downloads.end()) return;
    DownloadEntry& d = it->second;
    if (g_device) {
        Flush();
        if (d.mapped) SDL_UnmapGPUTransferBuffer(g_device, d.buffer);
        // One the GPU may still write into goes (SDL keeps it until then); a done one is kept for the next download.
        if (SubmissionDone(d.serial)) GiveBackDownloadBuffer(d.buffer, d.capacity);
        else SDL_ReleaseGPUTransferBuffer(g_device, d.buffer);
    }
    g_downloads.erase(it);
}

// ---- Pipelines ----------------------------------------------------------------------------------------------------

Pipeline MakePipeline(const PipelineDesc& desc, const char* label) {
    if (!g_device) return {};
    auto it = g_pipelines.find(desc);
    if (it != g_pipelines.end()) return it->second;
    if (const char* fault = PipelineFault(desc)) {
        Problem("pipeline %s: %s", label ? label : "", fault);
        return {};
    }
    SDL_GPUShader* vs = ProgramShader(desc.program, true);
    SDL_GPUShader* fs = ProgramShader(desc.program, false);
    if (!vs || !fs) return {};
    SDL_GPUVertexBufferDescription buffers[kMostVertexBuffers]{};
    for (int i = 0; i < desc.bufferCount; ++i) {
        buffers[i].slot = static_cast<Uint32>(i);
        buffers[i].pitch = desc.buffers[i].stride;
        buffers[i].input_rate =
            desc.buffers[i].perInstance ? SDL_GPU_VERTEXINPUTRATE_INSTANCE : SDL_GPU_VERTEXINPUTRATE_VERTEX;
    }
    SDL_GPUVertexAttribute attributes[kMostVertexAttributes]{};
    for (int i = 0; i < desc.attributeCount; ++i) {
        const VertexAttribute& a = desc.attributes[i];
        attributes[i] = {a.location, a.buffer, ToSdl(a.format), a.offset};
    }
    SDL_GPUGraphicsPipelineCreateInfo ci{};
    ci.vertex_shader = vs;
    ci.fragment_shader = fs;
    ci.vertex_input_state = {buffers, static_cast<Uint32>(desc.bufferCount), attributes,
                             static_cast<Uint32>(desc.attributeCount)};
    ci.primitive_type = desc.primitive == Primitive::Lines ? SDL_GPU_PRIMITIVETYPE_LINELIST
                                                           : SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    ci.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    ci.rasterizer_state.cull_mode = desc.cull == Cull::Back    ? SDL_GPU_CULLMODE_BACK
                                    : desc.cull == Cull::Front ? SDL_GPU_CULLMODE_FRONT
                                                               : SDL_GPU_CULLMODE_NONE;
    ci.rasterizer_state.front_face =
        desc.front == Winding::Clockwise ? SDL_GPU_FRONTFACE_CLOCKWISE : SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    ci.rasterizer_state.enable_depth_clip = true;
    if (desc.depthBias != 0.0f || desc.depthBiasSlope != 0.0f) {
        ci.rasterizer_state.enable_depth_bias = true;
        ci.rasterizer_state.depth_bias_constant_factor = desc.depthBias;
        ci.rasterizer_state.depth_bias_slope_factor = desc.depthBiasSlope;
        ci.rasterizer_state.depth_bias_clamp = desc.depthBiasClamp;
    }
    ci.multisample_state.sample_count = SampleCount(desc.samples);
    SDL_GPUColorTargetDescription colours[2]{};
    if (desc.colour != Format::None) {
        SDL_GPUColorTargetDescription& colour = colours[0];
        colour.format = ToSdl(desc.colour);
        SDL_GPUColorTargetBlendState& b = colour.blend_state;
        b.enable_blend = desc.blend.on;
        b.src_color_blendfactor = ToSdl(desc.blend.sourceColour);
        b.dst_color_blendfactor = ToSdl(desc.blend.destColour);
        b.color_blend_op = ToSdl(desc.blend.colourOp);
        b.src_alpha_blendfactor = ToSdl(desc.blend.sourceAlpha);
        b.dst_alpha_blendfactor = ToSdl(desc.blend.destAlpha);
        b.alpha_blend_op = ToSdl(desc.blend.alphaOp);
        b.enable_color_write_mask = true;
        b.color_write_mask = static_cast<SDL_GPUColorComponentFlags>(desc.colourMask);
        ci.target_info.color_target_descriptions = colours;
        ci.target_info.num_color_targets = 1;
    }
    if (desc.second != Format::None) {
        colours[1].format = ToSdl(desc.second);
        colours[1].blend_state.enable_color_write_mask = true;
        colours[1].blend_state.color_write_mask = static_cast<SDL_GPUColorComponentFlags>(desc.secondMask);
        ci.target_info.num_color_targets = 2;
    }
    if (desc.depth != Format::None) {
        ci.target_info.has_depth_stencil_target = true;
        ci.target_info.depth_stencil_format = ToSdl(desc.depth);
        ci.depth_stencil_state.enable_depth_test = true;
        ci.depth_stencil_state.enable_depth_write = desc.depthWrite;
        ci.depth_stencil_state.compare_op = ToSdl(desc.depthCompare);
    }
    Pipeline p{SDL_CreateGPUGraphicsPipeline(g_device, &ci)};
    if (!p.pipeline) {
        Problem("pipeline %s (%s, %d samples): %s", label ? label : "", gpu_shaders::kPrograms[desc.program].name,
                desc.samples, SDL_GetError());
        return {};
    }
    g_pipelines.emplace(desc, p);
    return p;
}

int PipelineCount() { return static_cast<int>(g_pipelines.size()); }

// ---- Passes and draws ---------------------------------------------------------------------------------------------

namespace {

SDL_GPULoadOp ToSdl(Load l) {
    switch (l) {
    case Load::Keep: return SDL_GPU_LOADOP_LOAD;
    case Load::Clear: return SDL_GPU_LOADOP_CLEAR;
    case Load::DontCare: break;
    }
    return SDL_GPU_LOADOP_DONT_CARE;
}

} // namespace

bool BeginPass(const PassDesc& pass) {
    if (!g_device) return false;
    if (g_pass) {
        Problem("a pass begun inside another");
        EndPass();
    }
    if (!pass.colour.texture && !pass.depth.texture) {
        Problem("a pass without targets");
        return false;
    }
    if (pass.second.texture && !pass.colour.texture) {
        Problem("a pass with a second colour target but no first");
        return false;
    }
    Flush();
    SDL_GPUCommandBuffer* commands = Commands();
    if (!commands) return false;
    SDL_GPUColorTargetInfo colours[2]{};
    int colourCount = 0;
    for (const ColourTarget* target : {&pass.colour, &pass.second}) {
        const ColourTarget& c = *target;
        if (!c.texture) continue;
        SDL_GPUColorTargetInfo& colour = colours[colourCount++];
        colour.texture = c.texture.texture;
        colour.mip_level = static_cast<Uint32>(c.level);
        colour.clear_color = {c.clear[0], c.clear[1], c.clear[2], c.clear[3]};
        colour.load_op = ToSdl(c.load);
        if (c.resolve) {
            colour.store_op = c.store ? SDL_GPU_STOREOP_RESOLVE_AND_STORE : SDL_GPU_STOREOP_RESOLVE;
            colour.resolve_texture = c.resolve.texture;
            colour.resolve_mip_level = static_cast<Uint32>(c.resolveLevel);
        } else {
            colour.store_op = c.store ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
        }
        // Nothing of the old contents is read: a free copy of the texture may take the pass (a copy is the whole
        // texture, so not one with other levels).
        colour.cycle = c.load != Load::Keep && !c.texture.swapchain && c.texture.levels == 1;
    }
    SDL_GPUDepthStencilTargetInfo depth{};
    const DepthTarget& d = pass.depth;
    if (d.texture) {
        depth.texture = d.texture.texture;
        depth.mip_level = static_cast<Uint8>(d.level);
        depth.clear_depth = d.clear;
        depth.load_op = ToSdl(d.load);
        depth.store_op = d.store ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
        depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
        depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
        depth.cycle = d.load != Load::Keep && d.texture.levels == 1;
    }
    if (g_debug && pass.label) SDL_InsertGPUDebugLabel(commands, pass.label);
    g_pass = SDL_BeginGPURenderPass(commands, colourCount ? colours : nullptr, static_cast<Uint32>(colourCount),
                                    d.texture ? &depth : nullptr);
    if (!g_pass) {
        Problem("pass %s: %s", pass.label ? pass.label : "", SDL_GetError());
        return false;
    }
    g_bound.Reset();
    return true;
}

void EndPass() {
    if (!g_pass) {
        Problem("a pass ended outside one");
        return;
    }
    SDL_EndGPURenderPass(g_pass);
    g_pass = nullptr;
}

bool InPass() { return g_pass != nullptr; }

void SetViewport(const Rect& rect) {
    if (!g_pass) return Problem("a viewport outside a pass");
    const SDL_GPUViewport v{static_cast<float>(rect.left), static_cast<float>(rect.top),
                            static_cast<float>(rect.right - rect.left), static_cast<float>(rect.bottom - rect.top),
                            0.0f, 1.0f};
    SDL_SetGPUViewport(g_pass, &v);
}

void SetScissor(const Rect& rect) {
    if (!g_pass) return Problem("a scissor rectangle outside a pass");
    const SDL_Rect r{rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
    SDL_SetGPUScissor(g_pass, &r);
}

void ApplyPipeline(Pipeline pipeline) {
    if (!g_pass || !pipeline) return Problem("a pipeline %s", g_pass ? "that is empty" : "outside a pass");
    if (pipeline.pipeline == g_bound.pipeline) return;
    SDL_BindGPUGraphicsPipeline(g_pass, pipeline.pipeline);
    g_bound.pipeline = pipeline.pipeline;
}

void BindVertexBuffers(int first, std::span<const BufferBinding> buffers) {
    if (!g_pass || first < 0 || first + static_cast<int>(buffers.size()) > kMostVertexBuffers)
        return Problem("vertex buffers %s", g_pass ? "out of range" : "outside a pass");
    for (const BufferBinding& b : buffers)
        if (!b.buffer) return Problem("an empty vertex buffer bound");
    // The changed ones' range bound again.
    int low = -1, high = -1;
    for (int i = 0; i < static_cast<int>(buffers.size()); ++i) {
        BufferBinding& b = g_bound.vertex[first + i];
        if (b.buffer.buffer == buffers[i].buffer.buffer && b.offset == buffers[i].offset && b.buffer.buffer) continue;
        b = buffers[i];
        if (low < 0) low = i;
        high = i;
    }
    if (low < 0) return;
    SDL_GPUBufferBinding sdl[kMostVertexBuffers]{};
    for (int i = low; i <= high; ++i) sdl[i - low] = {buffers[i].buffer.buffer, buffers[i].offset};
    SDL_BindGPUVertexBuffers(g_pass, static_cast<Uint32>(first + low), sdl, static_cast<Uint32>(high - low + 1));
}

void BindIndexBuffer(const BufferBinding& binding) {
    if (!g_pass || !binding.buffer) return Problem("an index buffer %s", g_pass ? "that is empty" : "outside a pass");
    const BufferBinding& bound = g_bound.index;
    if (g_bound.haveIndex && bound.buffer.buffer == binding.buffer.buffer && bound.offset == binding.offset) return;
    g_bound.index = binding;
    g_bound.haveIndex = true;
    const SDL_GPUBufferBinding b{binding.buffer.buffer, binding.offset};
    SDL_BindGPUIndexBuffer(g_pass, &b, SDL_GPU_INDEXELEMENTSIZE_32BIT);
}

void BindTextures(Stage stage, int first, std::span<const TextureBinding> textures) {
    if (!g_pass || first < 0 || first + static_cast<int>(textures.size()) > kMostSamplers)
        return Problem("textures %s", g_pass ? "out of range" : "outside a pass");
    for (const TextureBinding& t : textures)
        if (!t.texture || !t.sampler) return Problem("an empty texture or sampler bound");
    const int s = stage == Stage::Vertex ? 0 : 1;
    int low = -1, high = -1;
    for (int i = 0; i < static_cast<int>(textures.size()); ++i) {
        SDL_GPUTexture*& t = g_bound.textures[s][first + i];
        SDL_GPUSampler*& m = g_bound.samplers[s][first + i];
        if (t == textures[i].texture.texture && m == textures[i].sampler.sampler && t) continue;
        t = textures[i].texture.texture;
        m = textures[i].sampler.sampler;
        if (low < 0) low = i;
        high = i;
    }
    if (low < 0) return;
    SDL_GPUTextureSamplerBinding sdl[kMostSamplers]{};
    for (int i = low; i <= high; ++i) sdl[i - low] = {textures[i].texture.texture, textures[i].sampler.sampler};
    const Uint32 slot = static_cast<Uint32>(first + low), count = static_cast<Uint32>(high - low + 1);
    if (stage == Stage::Vertex) SDL_BindGPUVertexSamplers(g_pass, slot, sdl, count);
    else SDL_BindGPUFragmentSamplers(g_pass, slot, sdl, count);
}

void PushUniforms(Stage stage, int slot, const void* data, std::uint32_t bytes) {
    if (!g_pass || slot < 0 || slot >= kMostBlocks)
        return Problem("uniforms %s", g_pass ? "out of range" : "outside a pass");
    const int s = stage == Stage::Vertex ? 0 : 1;
    std::vector<std::uint8_t>& last = g_bound.uniforms[s][slot];
    if (g_bound.pushed[s][slot] && last.size() == bytes && std::memcmp(last.data(), data, bytes) == 0) return;
    last.assign(static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + bytes);
    g_bound.pushed[s][slot] = true;
    if (stage == Stage::Vertex) SDL_PushGPUVertexUniformData(g_commands, static_cast<Uint32>(slot), data, bytes);
    else SDL_PushGPUFragmentUniformData(g_commands, static_cast<Uint32>(slot), data, bytes);
}

void Draw(std::uint32_t first, std::uint32_t count, std::uint32_t instances) {
    if (!g_pass || !g_bound.pipeline) return Problem("a draw %s", g_pass ? "without a pipeline" : "outside a pass");
    SDL_DrawGPUPrimitives(g_pass, count, instances, first, 0);
}

void DrawIndexed(std::uint32_t first, std::uint32_t count, std::uint32_t instances, std::uint32_t firstInstance) {
    if (!g_pass || !g_bound.pipeline || !g_bound.haveIndex)
        return Problem("an indexed draw %s",
                       !g_pass ? "outside a pass" : g_bound.pipeline ? "without indices" : "without a pipeline");
    SDL_DrawGPUIndexedPrimitives(g_pass, count, instances, first, 0, firstInstance);
}

// ---- The window ---------------------------------------------------------------------------------------------------

bool ClaimWindow(SDL_Window* window) {
    if (!g_device || !window) return false;
    if (g_window == window) return true;
    ReleaseWindow();
    if (!SDL_ClaimWindowForGPUDevice(g_device, window)) {
        g_error = std::string("the window's swap chain: ") + SDL_GetError();
        ei::Log("gpu: %s", g_error.c_str());
        return false;
    }
    g_window = window;
    return true;
}

void ReleaseWindow() {
    if (!g_window || !g_device) return;
    if (g_commands && g_swapchainTaken) Submit();
    SDL_WaitForGPUIdle(g_device);
    SDL_ReleaseWindowFromGPUDevice(g_device, g_window);
    g_window = nullptr;
}

namespace {

SDL_GPUPresentMode ToSdl(PresentMode m) {
    switch (m) {
    case PresentMode::Vsync: return SDL_GPU_PRESENTMODE_VSYNC;
    case PresentMode::Immediate: return SDL_GPU_PRESENTMODE_IMMEDIATE;
    case PresentMode::Mailbox: return SDL_GPU_PRESENTMODE_MAILBOX;
    }
    return SDL_GPU_PRESENTMODE_VSYNC;
}

} // namespace

bool PresentModeSupported(PresentMode mode) {
    return g_window && SDL_WindowSupportsGPUPresentMode(g_device, g_window, ToSdl(mode));
}

PresentMode SetPresentMode(PresentMode mode) {
    if (mode == PresentMode::Immediate && !PresentModeSupported(mode)) mode = PresentMode::Mailbox;
    if (mode == PresentMode::Mailbox && !PresentModeSupported(mode)) mode = PresentMode::Vsync;
    if (g_window && !SDL_SetGPUSwapchainParameters(g_device, g_window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, ToSdl(mode))) {
        Problem("present mode %d: %s", static_cast<int>(mode), SDL_GetError());
        return PresentMode::Vsync;
    }
    return mode;
}

Format SwapchainFormat() {
    if (!g_window) return Format::None;
    switch (SDL_GetGPUSwapchainTextureFormat(g_device, g_window)) {
    case SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM: return Format::Bgra8;
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM: return Format::Rgba8;
    default: return Format::None;
    }
}

bool AcquireSwapchain(Texture* target) {
    *target = Texture{};
    if (!g_window) {
        g_error = "no window claimed";
        return false;
    }
    if (g_pass) {
        Problem("the swap chain taken inside a pass");
        return false;
    }
    if (g_swapchainTaken) {
        *target = g_swapchainTexture;
        return true;
    }
    SDL_GPUCommandBuffer* commands = Commands();
    if (!commands) return false;
    SDL_GPUTexture* texture = nullptr;
    Uint32 w = 0, h = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, g_window, &texture, &w, &h)) {
        g_error = std::string("the swap chain's texture: ") + SDL_GetError();
        return false;
    }
    g_swapchainTaken = true;
    if (texture) {
        g_swapchainTexture.texture = texture;
        g_swapchainTexture.width = static_cast<int>(w);
        g_swapchainTexture.height = static_cast<int>(h);
        g_swapchainTexture.levels = 1;
        g_swapchainTexture.samples = 1;
        g_swapchainTexture.format = SwapchainFormat();
        g_swapchainTexture.swapchain = true;
    }
    *target = g_swapchainTexture;
    return true;
}

bool Submit() {
    if (!g_device) return false;
    if (g_pass) {
        Problem("a submission inside a pass");
        EndPass();
    }
    Flush();
    if (g_ring.mapped) {
        SDL_UnmapGPUTransferBuffer(g_device, g_ring.buffer);
        g_ring.mapped = nullptr;
    }
    SDL_GPUCommandBuffer* commands = Commands();
    if (!commands) return false;
    g_commands = nullptr;
    g_swapchainTaken = false;
    g_swapchainTexture = Texture{};
    const double submitted = NowMs();
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
    const std::uint64_t serial = g_serial++;
    g_uploaded = 0;
    g_ring.used = 0;
    g_ring.cycleNext = true;
    if (g_ring.cycles > kMostRingCycles && g_ring.buffer) {
        SDL_ReleaseGPUTransferBuffer(g_device, g_ring.buffer);
        g_ring.buffer = nullptr;
    }
    g_ring.cycles = 0;
    if (!fence) Problem("a submission failed: %s", SDL_GetError());
    std::lock_guard<std::mutex> lock(g_fenceLock);
    g_submissions.push_back({serial, fence, submitted, fence == nullptr});
    DropDone();
    return fence != nullptr;
}

std::uint64_t SubmitCount() { return g_serial - 1; }

void SetFrameTiming(bool on) {
    if (on == g_timing.load()) return;
    if (on && !g_device) return;
    g_timing.store(on);
    if (on) {
        {
            std::lock_guard<std::mutex> lock(g_fenceLock);
            g_lastFinished = 0.0;
            g_latestGpuMs = -1.0;
        }
        g_timer = std::thread(TimerThread);
    } else if (g_timer.joinable()) {
        g_timer.join();
    }
}

bool FrameGpuTime(double* milliseconds) {
    std::lock_guard<std::mutex> lock(g_fenceLock);
    DropDone();
    if (g_latestGpuMs < 0.0) return false;
    *milliseconds = g_latestGpuMs;
    g_latestGpuMs = -1.0;
    return true;
}

} // namespace render::gpu
