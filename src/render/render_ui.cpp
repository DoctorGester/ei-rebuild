// The interface drawn into the frame (render_ui.h). See docs/notes/ui-native.md, "Source file overviews".
#include "render_ui.h"
#include "render_internal.h"
#include "render_text.h"

#include "../platform/log.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace render {

namespace {

using namespace detail;

constexpr int kMostPicture = 8192;

// 0x00BBGGRR (a COLORREF) as the vertex colour's bytes R, G, B, A (opaque).
std::uint32_t VertexColour(std::uint32_t colour) { return (colour & 0xFFFFFF) | 0xFF000000u; }

gpu::Sampler Nearest() {
    return gpu::MakeSampler({gpu::Filter::Nearest, gpu::Filter::Nearest, gpu::Filter::Nearest, gpu::Wrap::Clamp, gpu::Wrap::Clamp});
}

// One texture with the nearest sampler at slot 0 of the fragment stage.
void BindTexture(const gpu::Texture& texture) {
    const gpu::TextureBinding b{texture, Nearest()};
    gpu::BindTextures(gpu::Stage::Fragment, 0, {&b, 1});
}

// ---- Quads into the frame ------------------------------------------------------------------------------

struct AtlasTexture {
    unsigned generation = 0;
    gpu::Texture image;
};
std::unordered_map<const text::GlyphAtlas*, AtlasTexture> g_atlases;
gpu::Texture g_noAtlas;   // bound for batches of solids only

StreamBuffer g_vertices(false, "ei-ui-quads");
std::size_t g_uploaded = 0;   // g_frame.uiVertices on the GPU from g_offset
int g_offset = -1;

// The quads' pipeline for the pass's sample count: alpha-blended over the frame, the depth ignored and kept.
gpu::PipelineDesc QuadDesc(int samples) {
    gpu::PipelineDesc p;
    p.program = gpu_shaders::ui_quad::kIndex;
    p.bufferCount = 1;
    p.buffers[0].stride = sizeof(UiVertex);
    p.attributeCount = 2;
    p.attributes[0] = {gpu_shaders::ui_quad::kAttrPosition, 0, gpu::VertexFormat::Float4, 0};
    p.attributes[1] = {gpu_shaders::ui_quad::kAttrColour, 0, gpu::VertexFormat::UByte4Norm, 4 * sizeof(float)};
    p.depth = kDepthFormat;
    p.colour = kColourFormat;
    p.colourMask = gpu::kWriteRed | gpu::kWriteGreen | gpu::kWriteBlue;
    p.blend = {true, gpu::BlendFactor::SourceAlpha, gpu::BlendFactor::OneMinusSourceAlpha, gpu::BlendFactor::One,
               gpu::BlendFactor::Zero, gpu::BlendOp::Add, gpu::BlendOp::Add};
    p.samples = samples;
    AddDepthCopy(p);
    return p;
}

// The atlas as an R8 texture, made again when it changed.
gpu::Texture AtlasImage(const text::GlyphAtlas* atlas) {
    if (!atlas) {
        if (!g_noAtlas) {
            g_noAtlas = gpu::MakeTexture({1, 1, 1, 1, gpu::Format::R8, gpu::kSampled, "ei-ui-no-atlas"});
            const std::uint8_t zero = 0;
            if (g_noAtlas) gpu::Write(g_noAtlas, 0, {0, 0, 1, 1}, &zero, 1);
        }
        return g_noAtlas;
    }
    AtlasTexture& t = g_atlases[atlas];
    if (t.image && t.generation == atlas->Generation()) return t.image;
    gpu::Release(t.image);
    t = AtlasTexture{};
    if (atlas->Width() <= 0 || atlas->Height() <= 0) return gpu::Texture{};
    t.image = gpu::MakeTexture({atlas->Width(), atlas->Height(), 1, 1, gpu::Format::R8, gpu::kSampled, "ei-ui-atlas"});
    if (t.image)
        gpu::Write(t.image, 0, {0, 0, atlas->Width(), atlas->Height()}, atlas->Pixels(), static_cast<std::size_t>(atlas->Width()));
    t.generation = atlas->Generation();
    return t.image;
}

// The batch to append quads of `atlas` clipped to `scissor` to: the frame's last command when it is such a batch
// (solids join any batch: their texture coordinates read no atlas), else a new one.
UiBatch& BatchFor(const text::GlyphAtlas* atlas, const Rect& scissor) {
    Frame& f = g_frame;
    if (!f.commands.empty() && f.commands.back().kind == Command::Ui) {
        UiBatch& last = f.uiBatches[static_cast<std::size_t>(f.commands.back().index)];
        if (last.scissor == scissor && (!atlas || !last.atlas || last.atlas == atlas)) {
            if (atlas) last.atlas = atlas;
            return last;
        }
    }
    f.uiBatches.push_back(UiBatch{static_cast<std::uint32_t>(f.uiVertices.size()), 0, atlas, scissor});
    f.commands.push_back(Command{Command::Ui, static_cast<int>(f.uiBatches.size()) - 1});
    return f.uiBatches.back();
}

// A quad of `batch` over [x, x + w) x [y, y + h), its atlas texels from (u, v) (u < 0: full coverage).
void AddQuad(UiBatch& batch, int x, int y, int w, int h, float u, float v, std::uint32_t colour) {
    if (w <= 0 || h <= 0) return;
    const float x0 = static_cast<float>(x), y0 = static_cast<float>(y);
    const float x1 = static_cast<float>(x + w), y1 = static_cast<float>(y + h);
    const float u1 = u < 0.0f ? u : u + static_cast<float>(w), v1 = u < 0.0f ? v : v + static_cast<float>(h);
    const UiVertex corners[6] = {{x0, y0, u, v, colour},   {x1, y0, u1, v, colour}, {x1, y1, u1, v1, colour},
                                 {x0, y0, u, v, colour},   {x1, y1, u1, v1, colour}, {x0, y1, u, v1, colour}};
    g_frame.uiVertices.insert(g_frame.uiVertices.end(), std::begin(corners), std::end(corners));
    batch.count += 6;
}

// ---- Pictures ---------------------------------------------------------------------------------------------

struct UiPicture {
    gpu::Texture image;
    int width = 0, height = 0;
};
std::unordered_map<const void*, UiPicture> g_uiPictures;
// Pictures given up: released once the frames that drew them are done.
std::vector<gpu::Texture> g_retired;

struct UiCopy {
    gpu::Texture image;
    int width, height;
    Rect source;
};
std::vector<UiCopy> g_copies;   // frame copies waiting for the next DrawPending (or a read)

static_assert(gpu_shaders::ui_picture::kBlockUiParams == 0 && gpu_shaders::ui_copy::kBlockUiParams == 0);
void ApplyFs(const UiParams& fs) { gpu::PushUniforms(gpu::Stage::Fragment, 0, fs); }

gpu::PipelineDesc TargetDesc(int program, gpu::Format format) {
    gpu::PipelineDesc p;
    p.program = program;
    p.colour = format;
    return p;
}

gpu::PipelineDesc PictureDesc(int samples) {
    gpu::PipelineDesc p;
    p.program = gpu_shaders::ui_picture::kIndex;
    p.depth = kDepthFormat;
    p.colour = kColourFormat;
    p.colourMask = gpu::kWriteRed | gpu::kWriteGreen | gpu::kWriteBlue;
    p.samples = samples;
    AddDepthCopy(p);
    return p;
}

bool BeginTargetPass(const gpu::Texture& target, bool clear, const char* label) {
    gpu::PassDesc pass;
    pass.colour.texture = target;
    pass.colour.load = clear ? gpu::Load::Clear : gpu::Load::Keep;
    pass.label = label;
    return gpu::BeginPass(pass);
}

// The copies waiting drawn from the frame target as it is now.
void RunCopies() {
    if (g_copies.empty()) return;
    const gpu::Texture& frame = FrameTexture();
    int fw = 0, fh = 0;
    const bool haveFrame = frame && FrameSize(&fw, &fh);
    const gpu::Pipeline pipeline = gpu::MakePipeline(TargetDesc(gpu_shaders::ui_copy::kIndex, gpu::Format::Rgba8), "ei-ui-copy");
    for (const UiCopy& copy : g_copies) {
        if (!BeginTargetPass(copy.image, true, "ei-ui-copy")) continue;
        const Rect r = haveFrame ? copy.source.Intersection(Rect{0, 0, fw, fh}) : Rect{};
        if (!r.IsEmpty() && pipeline) {
            gpu::ApplyPipeline(pipeline);
            BindTexture(frame);
            ApplyRect(Rect{0, 0, copy.width, copy.height}, copy.width, copy.height);
            UiParams fs{};
            fs.source[0] = r.left;
            fs.source[1] = r.top;
            fs.source[2] = r.Width();
            fs.source[3] = r.Height();
            fs.dest[0] = copy.width;
            fs.dest[1] = copy.height;
            ApplyFs(fs);
            gpu::Draw(0, 6);
        }
        gpu::EndPass();
    }
    g_copies.clear();
}

// `region` of `texture` read back now (a stall), into `texels` until `ticket` is released.
bool ReadBack(const gpu::Texture& texture, const Rect& region, gpu::Ticket* ticket, gpu::Texels* texels) {
    *ticket = gpu::Download(texture, 0, region);
    return gpu::Wait(*ticket) && gpu::Read(*ticket, texels);
}

const UiPicture* UiPictureOf(const void* key) {
    RunCanvasesNow();
    auto it = g_uiPictures.find(key);
    return it != g_uiPictures.end() && it->second.image ? &it->second : nullptr;
}

} // namespace

// ---- The public calls --------------------------------------------------------------------------------------

namespace ui {

void Solid(const Rect& rect, std::uint32_t colour, const Rect& clip) {
    const Rect r = rect.Intersection(clip);
    if (r.IsEmpty()) return;
    AddQuad(BatchFor(nullptr, r), r.left, r.top, r.Width(), r.Height(), -1.0f, -1.0f, VertexColour(colour));
}

void Glyphs(text::TextFont& font, const text::TextLayout& layout, Point origin, std::uint32_t colour, const Rect& clip) {
    if (layout.quads.empty() && layout.underlines.empty()) return;
    Rect scissor = clip;
    if (layout.clipped) scissor = scissor.Intersection(layout.clip.Offsetted(origin));
    if (scissor.IsEmpty()) return;
    UiBatch& batch = BatchFor(layout.quads.empty() ? nullptr : &font.Atlas(), scissor);
    const std::uint32_t c = VertexColour(colour);
    for (const text::GlyphQuad& q : layout.quads)
        AddQuad(batch, origin.x + q.x, origin.y + q.y, q.width, q.height, static_cast<float>(q.atlasX), static_cast<float>(q.atlasY), c);
    for (const Rect& u : layout.underlines) AddQuad(batch, origin.x + u.left, origin.y + u.top, u.Width(), u.Height(), -1.0f, -1.0f, c);
}

std::vector<Rect> LineRects(Point from, Point to, int width) {
    std::vector<Rect> out;
    width = std::max(width, 1);
    if (from == to) {
        if (width == 2) out.push_back(Rect::Sized(from, {1, 1}));
        return out;
    }
    const auto [x0, y0] = from;
    const auto [x1, y1] = to;
    const int before = width > 1 ? width / 2 : 0;
    const int left = std::min(x0, x1), right = std::max(x0, x1), top = std::min(y0, y1), bottom = std::max(y0, y1);
    if (width == 2 && (x0 == x1 || y0 == y1)) {
        if (x0 == x1) {
            out.push_back(Rect{x0 - 1, top, x0 + 1, bottom + 1});
        } else {
            out.push_back(Rect{left, y0, right + 1, y0 + 1});
            out.push_back(x1 > x0 ? Rect{x0 + 1, y0 - 1, x1 + 1, y0} : Rect{x1, y0 - 1, x0, y0});
        }
        return out;
    }
    if (y0 == y1) {
        out.push_back(width > 1 ? Rect{left - before, y0 - before, right + before + 1, y0 - before + width}
                                : Rect{x0 < x1 ? x0 : x1 + 1, y0, x0 < x1 ? x1 : x0 + 1, y0 + 1});
        return out;
    }
    if (x0 == x1) {
        out.push_back(width > 1 ? Rect{x0 - before, top - before, x0 - before + width, bottom + before + 1}
                                : Rect{x0, y0 < y1 ? y0 : y1 + 1, x0 + 1, y0 < y1 ? y1 : y0 + 1});
        return out;
    }
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int error = dx + dy, x = x0, y = y0;
    while (x != x1 || y != y1) {
        out.push_back(Rect{x - before, y - before, x - before + width, y - before + width});
        int e2 = 2 * error;
        if (e2 >= dy) {
            error += dy;
            x += sx;
        }
        if (e2 <= dx) {
            error += dx;
            y += sy;
        }
    }
    return out;
}

std::vector<Rect> FrameRects(const Rect& rect) {
    if (rect.IsEmpty()) return {};
    const Rect sides = rect.Inset(0, 1);
    return {rect.TopStrip(1), rect.BottomStrip(1), sides.LeftStrip(1), sides.RightStrip(1)};
}

void CapturePicture(const void* key, const Rect& source, int width, int height) {
    if (!key || width < 1 || height < 1 || width > kMostPicture || height > kMostPicture || source.IsEmpty()) return;
    UiPicture& p = g_uiPictures[key];
    if (!p.image || p.width != width || p.height != height) {
        // A picture the frame still draws is released after it.
        if (p.image) g_retired.push_back(p.image);
        p.image = gpu::MakeTexture({width, height, 1, 1, gpu::Format::Rgba8, gpu::kColourTarget | gpu::kSampled, "ei-ui-picture"});
        p.width = width;
        p.height = height;
        if (!p.image) {
            ei::Log("render: cannot create a %dx%d interface picture", width, height);
            return;
        }
    } else if (std::any_of(g_frame.pictureBatches.begin(), g_frame.pictureBatches.end(),
                           [&](const PictureBatch& b) { return b.texture.texture == p.image.texture; })) {
        // Drawn in this frame and not replaced under that draw: a new image.
        g_retired.push_back(p.image);
        p.image = gpu::MakeTexture({width, height, 1, 1, gpu::Format::Rgba8, gpu::kColourTarget | gpu::kSampled, "ei-ui-picture"});
        if (!p.image) return;
    }
    // The frame as drawn up to now: its commands recorded so far drawn before the copy is.
    SplitFrame();
    g_copies.push_back(UiCopy{p.image, width, height, source});
}

bool HasPicture(const void* key) { return g_uiPictures.count(key) != 0; }

void DrawPicture(const void* key, const Rect& destination, bool grey) {
    auto it = g_uiPictures.find(key);
    if (it == g_uiPictures.end() || !it->second.image || destination.IsEmpty()) return;
    Frame& f = g_frame;
    f.pictureBatches.push_back(PictureBatch{it->second.image, it->second.width, it->second.height, destination, grey});
    f.commands.push_back(Command{Command::Picture, static_cast<int>(f.pictureBatches.size()) - 1});
}

void DropPicture(const void* key) {
    auto it = g_uiPictures.find(key);
    if (it == g_uiPictures.end()) return;
    g_retired.push_back(it->second.image);
    g_uiPictures.erase(it);
}

bool ReadPicture(const void* key, int* width, int* height, std::vector<std::uint8_t>* rgba) {
    const UiPicture* p = UiPictureOf(key);
    if (!p) return false;
    gpu::Ticket ticket;
    gpu::Texels texels;
    const bool read = ReadBack(p->image, Rect{0, 0, p->width, p->height}, &ticket, &texels);
    if (read) {
        const std::size_t row = static_cast<std::size_t>(p->width) * 4;
        rgba->resize(row * static_cast<std::size_t>(p->height));
        for (int y = 0; y < p->height; ++y) std::memcpy(rgba->data() + row * static_cast<std::size_t>(y), texels.Row(y), row);
        *width = p->width;
        *height = p->height;
    }
    gpu::Release(ticket);
    return read;
}

} // namespace ui

// ---- render.cpp's side ------------------------------------------------------------------------------------

namespace detail {

void ListUiPipelines(std::vector<gpu::PipelineDesc>& out, int samples) {
    out.push_back(QuadDesc(samples));
    out.push_back(PictureDesc(samples));
    out.push_back(TargetDesc(gpu_shaders::ui_copy::kIndex, gpu::Format::Rgba8));
}

void RunCanvasCommands() { RunCopies(); }

void UploadUi(const Frame& f) {
    if (f.uiVertices.empty() || f.uiVertices.size() == g_uploaded) return;
    g_offset = g_vertices.Append(f.uiVertices.data(), f.uiVertices.size() * sizeof(UiVertex));
    g_uploaded = f.uiVertices.size();
    // The atlases as the frame's glyphs need them (an atlas grows as text is laid out; what it held stays put).
    for (const UiBatch& b : f.uiBatches) AtlasImage(b.atlas);
}

void UiFrameDone() {
    g_uploaded = 0;
    g_offset = -1;
    for (gpu::Texture& t : g_retired) gpu::Release(t);
    g_retired.clear();
}

void DrawUiBatch(const UiBatch& batch, int width, int height) {
    if (batch.count == 0 || g_offset < 0) return;
    const gpu::Texture atlas = AtlasImage(batch.atlas);
    const gpu::Pipeline pipeline = gpu::MakePipeline(QuadDesc(PassSampleCount()), "ei-ui-quads");
    if (!atlas || !pipeline) return;
    gpu::ApplyPipeline(pipeline);
    const gpu::BufferBinding vertices{g_vertices.Buffer(), static_cast<std::uint32_t>(g_offset)};
    gpu::BindVertexBuffers(0, {&vertices, 1});
    BindTexture(atlas);
    RectParams vs{{}, {static_cast<float>(width), static_cast<float>(height), 0.0f, 0.0f}};
    gpu::PushUniforms(gpu::Stage::Vertex, 0, vs);
    gpu::SetScissor(batch.scissor.Intersection(Rect{0, 0, width, height}));
    gpu::Draw(batch.first, batch.count);
    gpu::SetScissor(Rect{0, 0, width, height});
}

void DrawPictureBatch(const PictureBatch& batch, int width, int height) {
    const int dw = batch.destination.Width(), dh = batch.destination.Height();
    if (!batch.texture || dw <= 0 || dh <= 0) return;
    const gpu::Pipeline pipeline = gpu::MakePipeline(PictureDesc(PassSampleCount()), "ei-ui-picture");
    if (!pipeline) return;
    gpu::ApplyPipeline(pipeline);
    BindTexture(batch.texture);
    ApplyRect(batch.destination, width, height);
    UiParams fs{};
    fs.source[2] = batch.width;
    fs.source[3] = batch.height;
    fs.dest[0] = batch.destination.left;
    fs.dest[1] = batch.destination.top;
    fs.scale[0] = static_cast<float>(batch.width) / static_cast<float>(dw);
    fs.scale[1] = static_cast<float>(batch.height) / static_cast<float>(dh);
    fs.key[3] = batch.grey ? 1 : 0;
    ApplyFs(fs);
    gpu::Draw(0, 6);
}

} // namespace detail

} // namespace render
