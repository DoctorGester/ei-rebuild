// The OpenGL backend of the renderer's device and window (graphics.h; docs/notes/platform.md, "Graphics"), and
// sokol_gfx's implementation on it: an SDL GL 4.5 core context. Its clip control is Direct3D's (y from the top, depth
// 0..1), so every target keeps the rows, the depth and the facing the shaders were written for; the window's
// drawable is a framebuffer of ours, copied upside down into the window's at the present.

#include "platform/graphics.h"

#include "hook/log.h"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdio>
#include <cstring>

// sokol_gfx (third_party/sokol, unmodified; SOKOL_GLCORE from cmake/native.cmake): its implementation, after its
// declarations came through graphics.h. It includes the system's GL headers with their prototypes.
#define SOKOL_IMPL
#if defined(EI_RENDER_VALIDATION)
#define SOKOL_DEBUG
#define SOKOL_VALIDATE_NON_FATAL   // a failed check skips the call and is logged; the game goes on
#endif
#include "sokol_gfx.h"

namespace platform::graphics {

// Readback's copy: the texels read into memory of ours. Release, as Readback's: no destructor.
class Staging {
public:
    bool Map(GLuint texture, sg_pixel_format format, const Rect& region);
    const std::uint8_t* Row(int y) const { return texels.data() + static_cast<std::size_t>(y) * pitch; }
    void Unmap() {}
    void Release() { texels = {}; }

private:
    std::vector<std::uint8_t> texels;
    std::size_t pitch = 0;
};

namespace {

// ---- The device ---------------------------------------------------------------------------------------------------

SDL_Window* g_contextWindow = nullptr;   // hidden: the context's until the game's window draws
SDL_GLContext g_context = nullptr;
char g_error[160] = "";

bool Failed(const char* what) {
    std::snprintf(g_error, sizeof(g_error), "%s", what);
    return false;
}

// ---- The window ---------------------------------------------------------------------------------------------------

SDL_Window* g_window = nullptr;   // the window the context draws on
GLuint g_framebuffer = 0;         // the drawable: g_colour, upside down to the window's
GLuint g_colour = 0;
int g_width = 0, g_height = 0;
bool g_drawing = false;   // from BeginDrawable to EndDrawable

void DropDrawable() {
    if (g_framebuffer) glDeleteFramebuffers(1, &g_framebuffer);
    if (g_colour) glDeleteTextures(1, &g_colour);
    g_framebuffer = g_colour = 0;
    g_width = g_height = 0;
}

bool MakeDrawable(int width, int height) {
    if (g_framebuffer && width == g_width && height == g_height) return true;
    DropDrawable();
    glCreateTextures(GL_TEXTURE_2D, 1, &g_colour);
    glTextureStorage2D(g_colour, 1, GL_RGBA8, width, height);
    glCreateFramebuffers(1, &g_framebuffer);
    glNamedFramebufferTexture(g_framebuffer, GL_COLOR_ATTACHMENT0, g_colour, 0);
    if (glCheckNamedFramebufferStatus(g_framebuffer, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        DropDrawable();
        return Failed("the window's drawable framebuffer is incomplete");
    }
    g_width = width;
    g_height = height;
    return true;
}

// ---- Readback -----------------------------------------------------------------------------------------------------

struct TexelFormat {
    GLenum format, type;
};

bool TexelFormatOf(sg_pixel_format format, TexelFormat* texel) {
    switch (format) {
    case SG_PIXELFORMAT_RGBA8: *texel = {GL_RGBA, GL_UNSIGNED_BYTE}; return true;
    case SG_PIXELFORMAT_BGRA8: *texel = {GL_BGRA, GL_UNSIGNED_BYTE}; return true;
    case SG_PIXELFORMAT_R8: *texel = {GL_RED, GL_UNSIGNED_BYTE}; return true;
    case SG_PIXELFORMAT_RG8: *texel = {GL_RG, GL_UNSIGNED_BYTE}; return true;
    case SG_PIXELFORMAT_R16F: *texel = {GL_RED, GL_HALF_FLOAT}; return true;
    case SG_PIXELFORMAT_RGBA16F: *texel = {GL_RGBA, GL_HALF_FLOAT}; return true;
    case SG_PIXELFORMAT_R32F: *texel = {GL_RED, GL_FLOAT}; return true;
    case SG_PIXELFORMAT_RGBA32F: *texel = {GL_RGBA, GL_FLOAT}; return true;
    case SG_PIXELFORMAT_DEPTH: *texel = {GL_DEPTH_COMPONENT, GL_FLOAT}; return true;
    case SG_PIXELFORMAT_DEPTH_STENCIL: *texel = {GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8}; return true;
    default: return false;
    }
}

} // namespace

// ---- graphics.h ---------------------------------------------------------------------------------------------------

void PrepareInBackground() {}

// The context on a hidden window of its own, so that the device is there before the game's window (and without one,
// for the checks); the game's window, made with SDL's GL flag and the same attributes, takes it at its first frame.
bool Start(sg_environment* environment) {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return Failed(SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    g_contextWindow = SDL_CreateWindow("ei-gl", 1, 1, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (g_contextWindow) g_context = SDL_GL_CreateContext(g_contextWindow);
    if (!g_context) {
        Failed(SDL_GetError());
        ei::Log("render: no OpenGL 4.5 core context (%s)", g_error);
        Stop();
        return false;
    }
    glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    *environment = sg_environment{};
    ei::Log("render: OpenGL device ready (%s, %s)", reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
            reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    return true;
}

void Stop() {
    if (g_context) {
        DropDrawable();
        SDL_GL_DestroyContext(g_context);
    }
    if (g_contextWindow) SDL_DestroyWindow(g_contextWindow);
    g_context = nullptr;
    g_contextWindow = nullptr;
    g_window = nullptr;
}

const char* Error() { return g_error; }

int MaxSamples(int most) {
    GLint colour = 1, depth = 1;
    glGetIntegerv(GL_MAX_COLOR_TEXTURE_SAMPLES, &colour);
    glGetIntegerv(GL_MAX_DEPTH_TEXTURE_SAMPLES, &depth);
    int samples = most;
    while (samples > 1 && (samples > colour || samples > depth)) samples /= 2;
    return samples;
}

// No lost context is looked for (it would need a robust context).
std::optional<std::uint32_t> Lost() { return std::nullopt; }

void ApplyViewport(int x, int y, int width, int height) { sg_apply_viewport(x, y, width, height, false); }

void ApplyScissorRect(int x, int y, int width, int height) { sg_apply_scissor_rect(x, y, width, height, false); }

bool BeginDrawable(NativeWindow window, int width, int height, const PresentMode& mode, sg_swapchain* drawable) {
    auto* sdl = static_cast<SDL_Window*>(window);
    if (sdl != g_window) {
        if (!SDL_GL_MakeCurrent(sdl, g_context)) return Failed(SDL_GetError());
        g_window = sdl;
    }
    if (!MakeDrawable(width, height)) return false;
    SDL_GL_SetSwapInterval(mode.vsync ? 1 : 0);
    *drawable = sg_swapchain{};
    drawable->width = width;
    drawable->height = height;
    drawable->sample_count = 1;
    drawable->color_format = SG_PIXELFORMAT_BGRA8;   // the renderer's present pipelines' (read back B, G, R, A)
    drawable->depth_format = SG_PIXELFORMAT_NONE;
    drawable->gl.framebuffer = g_framebuffer;
    g_drawing = true;
    return true;
}

bool EndDrawable() {
    if (!g_drawing) return false;
    g_drawing = false;
    glBlitNamedFramebuffer(g_framebuffer, 0, 0, 0, g_width, g_height, 0, g_height, g_width, 0, GL_COLOR_BUFFER_BIT,
                           GL_NEAREST);
    if (!SDL_GL_SwapWindow(g_window)) {
        ei::Log("render: the window's present failed (%s)", SDL_GetError());
        return false;
    }
    return true;
}

// sokol writes its GL buffers with glBufferSubData too: the draws before this one read the bytes it replaces.
void UpdateBuffer(sg_buffer buffer, std::size_t offset, const void* data, std::size_t bytes) {
    const sg_gl_buffer_info info = sg_gl_query_buffer_info(buffer);
    if (const GLuint name = info.buf[info.active_slot])
        glNamedBufferSubData(name, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(bytes), data);
}

// The context is the main thread's: the cursor is not presented between frames.
bool KeepPresent(const PresentParts&) { return false; }

void DropPresent() {}

bool PresentAgain(std::span<const sg_range>, PresentReads*) { return false; }

Readback::Readback() = default;
Readback::~Readback() = default;

bool Readback::Read(sg_image image, const Rect& region) {
    if (!staging) staging = std::make_unique<Staging>();
    const sg_gl_image_info info = sg_gl_query_image_info(image);
    if (info.tex_target != GL_TEXTURE_2D) return false;
    return staging->Map(info.tex[info.active_slot], sg_query_image_pixelformat(image), region);
}

bool Readback::ReadDrawable(const Rect& region) {
    if (!g_drawing) return false;
    if (!staging) staging = std::make_unique<Staging>();
    return staging->Map(g_colour, SG_PIXELFORMAT_BGRA8, region);
}

const std::uint8_t* Readback::Row(int y) const { return staging->Row(y); }

void Readback::Unmap() {
    if (staging) staging->Unmap();
}

void Readback::Release() {
    if (staging) staging->Release();
    staging.reset();
}

struct GpuTimer::Queries {
    GLuint stamps[kMostStamps] = {};
    int count = 0;
    bool pending = false;   // ended, not read yet
};

GpuTimer::GpuTimer() = default;
GpuTimer::~GpuTimer() = default;

bool GpuTimer::Begin() {
    if (!queries) {
        queries = std::make_unique<Queries>();
        glGenQueries(kMostStamps, queries->stamps);
    }
    if (queries->pending) return false;
    queries->count = 0;
    return true;
}

void GpuTimer::Stamp() {
    if (queries && queries->count < kMostStamps) glQueryCounter(queries->stamps[queries->count++], GL_TIMESTAMP);
}

void GpuTimer::End() {
    if (queries) queries->pending = true;
}

// GL's timestamps are nanoseconds on a steady clock.
bool GpuTimer::Read(std::vector<double>* milliseconds) {
    if (!queries || !queries->pending) return false;
    GLuint64 nanoseconds[kMostStamps] = {};
    for (int k = 0; k < queries->count; ++k) {
        GLint available = 0;
        glGetQueryObjectiv(queries->stamps[k], GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available) return false;
        glGetQueryObjectui64v(queries->stamps[k], GL_QUERY_RESULT, &nanoseconds[k]);
    }
    queries->pending = false;
    milliseconds->clear();
    for (int k = 0; k < queries->count; ++k)
        milliseconds->push_back(static_cast<double>(static_cast<std::int64_t>(nanoseconds[k] - nanoseconds[0])) / 1e6);
    return true;
}

void GpuTimer::Release() {
    if (queries && g_context) glDeleteQueries(kMostStamps, queries->stamps);
    queries.reset();
}

bool Staging::Map(GLuint texture, sg_pixel_format format, const Rect& region) {
    TexelFormat texel{};
    if (!texture || !TexelFormatOf(format, &texel) || region.left < 0 || region.top < 0 || region.right <= region.left ||
        region.bottom <= region.top)
        return false;
    const int width = region.right - region.left, height = region.bottom - region.top;
    pitch = static_cast<std::size_t>(sg_query_row_pitch(format, width, 1));
    texels.resize(pitch * static_cast<std::size_t>(height));
    while (glGetError() != GL_NO_ERROR) {}
    glGetTextureSubImage(texture, 0, region.left, region.top, 0, width, height, 1, texel.format, texel.type,
                         static_cast<GLsizei>(texels.size()), texels.data());
    return glGetError() == GL_NO_ERROR;
}

} // namespace platform::graphics
