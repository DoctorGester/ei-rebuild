// Textures the game builds from images, drawn on the GPU (render::ComposeTexture, DrawIntoTexture): the images
// decoded from their files (32-bit premultiplied, "PNT3" packed, 5:6:5) and drawn into a render target, the levels
// below made by the GPU; and the global map's clouds (render::DrawClouds), their noise frames built by the GPU too.
// render_textures.cpp keeps the result by holder and the recipe to make it again on a new device. See
// docs/notes/renderer.md, section 7 ("Textures composed on the GPU").
#include "render_internal.h"

#include "../hook/log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>

#include <cctype>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace render::detail {

namespace {

struct FsParams {
    std::int32_t offset[4];   // the image texel at target texel (0, 0); z: fill with `colour`; w: the alpha kept
    float colour[4];
};
// texture_clouds.hlsl's fragment parameters.
struct CloudParams {
    std::uint32_t words[12];   // ps_frame: the layers' words
    std::uint32_t blend[4];    // ps_clouds: x
    std::uint32_t ramp[256];   // ps_clouds: one alpha a word
};
static_assert(sizeof(FsParams) == 32 && sizeof(CloudParams) == 1088);

bool g_tried = false, g_made = false;
sg_shader g_shader{}, g_cloudFrameShader{}, g_cloudsShader{};
sg_pipeline g_over{}, g_replace{}, g_cloudFrame{}, g_clouds{};
sg_sampler g_nearest{};
int g_failures = 0;

constexpr int kCloudSize = 256;   // a clouds frame's width and height
// The clouds' frames the GPU built, by their words: the two a draw blends and the next ones (a few).
struct CloudFrameTexture {
    CloudFrame frame{};
    sg_image image{};
    sg_view texture{};
    std::uint32_t used = 0;   // the composition that last used it
};
std::vector<CloudFrameTexture> g_cloudFrames;
std::uint32_t g_compositions = 0;
constexpr std::size_t kCloudFramesKept = 4;

// What was decoded of each image file, shared by every texture drawn from it while one holds it.
std::unordered_map<std::string, std::weak_ptr<const ComposeImage>> g_images;

sg_pipeline MakePipeline(bool over, const char* label) {
    sg_pipeline_desc p{};
    p.shader = g_shader;
    p.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    p.depth.pixel_format = SG_PIXELFORMAT_NONE;
    p.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
    if (over) {
        // Premultiplied: the image plus what is there times one less the image's alpha, alpha too.
        p.colors[0].blend.enabled = true;
        p.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_ONE;
        p.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        p.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
        p.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    }
    p.sample_count = 1;
    p.label = label;
    return sg_make_pipeline(&p);
}

// A shader of texture_compose.hlsl or texture_clouds.hlsl: the rectangle's vertex parameters, `fsBytes` of fragment
// parameters and `textures` texture views (t0, t1) of `sampleType` loaded directly (a non-filtering sampler paired
// with each, as sokol wants).
sg_shader MakeShader(const ShaderBytecode& code, std::size_t fsBytes, int textures, sg_image_sample_type sampleType,
                     const char* label) {
    sg_shader_desc s{};
    SetBytecode(s, code);
    s.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    s.uniform_blocks[0].size = sizeof(RectParams);
    s.uniform_blocks[0].hlsl_register_b_n = 0;
    s.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    s.uniform_blocks[1].size = static_cast<std::uint32_t>(fsBytes);
    s.uniform_blocks[1].hlsl_register_b_n = 0;
    for (int t = 0; t < textures; ++t) {
        s.views[t].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        s.views[t].texture.image_type = SG_IMAGETYPE_2D;
        s.views[t].texture.sample_type = sampleType;
        s.views[t].texture.hlsl_register_t_n = static_cast<std::uint8_t>(t);
        s.texture_sampler_pairs[t].stage = SG_SHADERSTAGE_FRAGMENT;
        s.texture_sampler_pairs[t].view_slot = static_cast<std::uint8_t>(t);
        s.texture_sampler_pairs[t].sampler_slot = 0;
    }
    if (textures > 0) {
        s.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
        s.samplers[0].sampler_type = SG_SAMPLERTYPE_NONFILTERING;
        s.samplers[0].hlsl_register_s_n = 0;
    }
    s.label = label;
    return sg_make_shader(&s);
}

// A pipeline drawing `shader`'s rectangle into a target of `format`, no blending.
sg_pipeline MakePlainPipeline(sg_shader shader, sg_pixel_format format, const char* label) {
    sg_pipeline_desc p{};
    p.shader = shader;
    p.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    p.depth.pixel_format = SG_PIXELFORMAT_NONE;
    p.colors[0].pixel_format = format;
    p.sample_count = 1;
    p.label = label;
    return sg_make_pipeline(&p);
}

bool EnsurePipelines() {
    if (g_tried) return g_made;
    g_tried = true;
    g_shader = MakeShader(kTextureComposeShader, sizeof(FsParams), 1, SG_IMAGESAMPLETYPE_UNFILTERABLE_FLOAT,
                          "ei-texture-compose");
    g_over = MakePipeline(true, "ei-texture-compose-over");
    g_replace = MakePipeline(false, "ei-texture-compose-replace");
    g_cloudFrameShader = MakeShader(kCloudFrameShader, sizeof(CloudParams), 0, SG_IMAGESAMPLETYPE_UINT, "ei-cloud-frame");
    g_cloudsShader = MakeShader(kCloudsShader, sizeof(CloudParams), 2, SG_IMAGESAMPLETYPE_UINT, "ei-clouds");
    g_cloudFrame = MakePlainPipeline(g_cloudFrameShader, SG_PIXELFORMAT_R8UI, "ei-cloud-frame");
    g_clouds = MakePlainPipeline(g_cloudsShader, SG_PIXELFORMAT_RGBA8, "ei-clouds");
    sg_sampler_desc sd{};
    sd.min_filter = SG_FILTER_NEAREST;
    sd.mag_filter = SG_FILTER_NEAREST;
    sd.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    sd.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    sd.label = "ei-texture-compose";
    g_nearest = sg_make_sampler(&sd);
    g_made = true;
    for (sg_pipeline p : {g_over, g_replace, g_cloudFrame, g_clouds})
        g_made = g_made && sg_query_pipeline_state(p) == SG_RESOURCESTATE_VALID;
    if (!g_made) ei::Log("render: the texture composition pipelines could not be made (see the sokol messages above)");
    return g_made;
}

// One 0xAARRGGBB pixel (bytes B, G, R, A) as R, G, B, A.
void PutPixel(std::uint8_t* out, const std::uint8_t* in) {
    out[0] = in[2];
    out[1] = in[1];
    out[2] = in[0];
    out[3] = in[3];
}

// The image's pixels as RGBA8 rows from the top; texels the packed data leaves out (or the bytes do not reach) are
// transparent black (what drawing them over anything leaves as it was).
void Decode(const TextureImage& image, ComposeImage& out) {
    out.width = image.width;
    out.height = image.height;
    const std::size_t count = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
    out.rgba.assign(count * 4, 0);
    const auto* p = static_cast<const std::uint8_t*>(image.pixels);
    std::size_t left = image.bytes;
    if (image.layout == ImageLayout::Rgb565) {
        // Each channel's bits repeated into the low ones (5 bits 0x1F is 255).
        for (std::size_t i = 0; i < count && left >= 2; ++i, p += 2, left -= 2) {
            const unsigned c = p[0] | (p[1] << 8);
            const unsigned r = c >> 11, g = (c >> 5) & 63, b = c & 31;
            std::uint8_t* o = &out.rgba[i * 4];
            o[0] = static_cast<std::uint8_t>((r << 3) | (r >> 2));
            o[1] = static_cast<std::uint8_t>((g << 2) | (g >> 4));
            o[2] = static_cast<std::uint8_t>((b << 3) | (b >> 2));
            o[3] = 255;
        }
        return;
    }
    if (image.layout == ImageLayout::Argb) {
        for (std::size_t i = 0; i < count && left >= 4; ++i, p += 4, left -= 4) PutPixel(&out.rgba[i * 4], p);
        return;
    }
    // Groups of four pixels; a group whose first pixel has alpha 0 is instead a word counting the target's bytes
    // to leave (unless the word is 0: a group whose first pixel is transparent black).
    std::size_t at = 0;
    while (at < count && left >= 4) {
        const std::uint32_t word = p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
        if (p[3] == 0 && word != 0) {
            at += word / 4;
            p += 4;
            left -= 4;
            continue;
        }
        for (int k = 0; k < 4 && at < count && left >= 4; ++k, ++at, p += 4, left -= 4) PutPixel(&out.rgba[at * 4], p);
    }
}

std::string Key(const char* name) {
    std::string key = name ? name : "";
    for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return key;
}

// The clouds frame of `frame`'s words as a texture (R8UI): one the GPU built for an earlier composition, else built
// now (a pass of its own, so not inside another). None when it cannot be made.
sg_view CloudFrameView(const CloudFrame& frame) {
    for (CloudFrameTexture& f : g_cloudFrames) {
        if (std::memcmp(&f.frame, &frame, sizeof(frame)) != 0) continue;
        f.used = g_compositions;
        return f.texture;
    }
    if (g_cloudFrames.size() >= kCloudFramesKept) {
        // The one unused longest goes.
        auto oldest = g_cloudFrames.begin();
        for (auto it = g_cloudFrames.begin(); it != g_cloudFrames.end(); ++it)
            if (it->used < oldest->used) oldest = it;
        sg_destroy_view(oldest->texture);
        sg_destroy_image(oldest->image);
        g_cloudFrames.erase(oldest);
    }
    CloudFrameTexture f;
    f.frame = frame;
    f.used = g_compositions;
    sg_image_desc d{};
    d.usage.color_attachment = true;
    d.width = kCloudSize;
    d.height = kCloudSize;
    d.pixel_format = SG_PIXELFORMAT_R8UI;
    d.label = "ei-cloud-frame";
    f.image = sg_make_image(&d);
    sg_view_desc a{};
    a.color_attachment.image = f.image;
    sg_view attachment = sg_make_view(&a);
    sg_view_desc v{};
    v.texture.image = f.image;
    f.texture = sg_make_view(&v);
    if (sg_query_view_state(attachment) != SG_RESOURCESTATE_VALID ||
        sg_query_view_state(f.texture) != SG_RESOURCESTATE_VALID) {
        if (g_failures++ < 10) ei::Log("render: a clouds frame could not be made");
        sg_destroy_view(attachment);
        sg_destroy_view(f.texture);
        sg_destroy_image(f.image);
        return sg_view{};
    }
    sg_pass pass{};
    pass.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
    pass.action.colors[0].store_action = SG_STOREACTION_STORE;
    pass.attachments.colors[0] = attachment;
    pass.label = "ei-cloud-frame";
    sg_begin_pass(&pass);
    sg_apply_pipeline(g_cloudFrame);
    ApplyRect(Rect{0, 0, kCloudSize, kCloudSize}, kCloudSize, kCloudSize);
    CloudParams params{};
    std::memcpy(params.words, frame.words, sizeof(params.words));
    sg_range range{&params, sizeof(params)};
    sg_apply_uniforms(1, &range);
    sg_draw(0, 6, 1);
    sg_end_pass();
    sg_destroy_view(attachment);
    CountImage(ImageUse::Texture, true);
    g_cloudFrames.push_back(f);
    return f.texture;
}

// A texture of the image's pixels for one composition.
struct StepTexture {
    sg_image image{};
    sg_view view{};
};
StepTexture MakeStepTexture(const ComposeImage& image) {
    StepTexture t;
    if (image.width <= 0 || image.height <= 0) return t;
    sg_image_desc d{};
    d.width = image.width;
    d.height = image.height;
    d.pixel_format = SG_PIXELFORMAT_RGBA8;
    d.data.mip_levels[0] = sg_range{image.rgba.data(), image.rgba.size()};
    d.label = "ei-texture-compose-image";
    t.image = sg_make_image(&d);
    sg_view_desc v{};
    v.texture.image = t.image;
    t.view = sg_make_view(&v);
    CountUpload(FrameStats::Mmp, image.rgba.size());
    return t;
}

void DrawSteps(const ComposeRecipe& recipe, sg_image target, const ComposeStep* steps, std::size_t count, bool clear) {
    sg_view_desc a{};
    a.color_attachment.image = target;
    a.color_attachment.mip_level = 0;
    sg_view attachment = sg_make_view(&a);
    if (sg_query_view_state(attachment) != SG_RESOURCESTATE_VALID) {
        sg_destroy_view(attachment);
        return;
    }
    // What the steps draw from, made before the pass (a clouds frame is built in a pass of its own).
    ++g_compositions;
    std::vector<StepTexture> textures;
    textures.reserve(count);
    std::vector<std::pair<sg_view, sg_view>> cloudFrames(count);
    for (std::size_t k = 0; k < count; ++k) {
        textures.push_back(steps[k].image ? MakeStepTexture(*steps[k].image) : StepTexture{});
        if (steps[k].clouds)
            cloudFrames[k] = {CloudFrameView(steps[k].clouds->from), CloudFrameView(steps[k].clouds->to)};
    }
    sg_pass pass{};
    pass.action.colors[0].load_action = clear ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD;
    pass.action.colors[0].store_action = SG_STOREACTION_STORE;
    pass.action.colors[0].clear_value = sg_color{0.0f, 0.0f, 0.0f, recipe.alpha == BlankAlpha::None ? 1.0f : 0.0f};
    pass.attachments.colors[0] = attachment;
    pass.label = "ei-texture-compose";
    sg_begin_pass(&pass);
    for (std::size_t k = 0; k < count; ++k) {
        const ComposeStep& step = steps[k];
        if (step.clouds) {
            if (!cloudFrames[k].first.id || !cloudFrames[k].second.id) continue;
            sg_apply_pipeline(g_clouds);
            sg_bindings b{};
            b.views[0] = cloudFrames[k].first;
            b.views[1] = cloudFrames[k].second;
            b.samplers[0] = g_nearest;
            sg_apply_bindings(&b);
            ApplyRect(step.to, recipe.width, recipe.height);
            CloudParams params{};
            params.blend[0] = static_cast<std::uint32_t>(step.clouds->blend);
            for (int g = 0; g < 256; ++g) params.ramp[g] = step.clouds->ramp[g];
            sg_range range{&params, sizeof(params)};
            sg_apply_uniforms(1, &range);
            sg_draw(0, 6, 1);
            continue;
        }
        const bool fill = !step.image;
        if (!fill && !textures[k].view.id) continue;
        sg_apply_pipeline(step.over && !fill ? g_over : g_replace);
        sg_bindings b{};
        b.views[0] = fill ? WhiteView() : textures[k].view;
        b.samplers[0] = g_nearest;
        sg_apply_bindings(&b);
        ApplyRect(step.to, recipe.width, recipe.height);
        FsParams fs{{step.fromX - step.to.left, step.fromY - step.to.top, fill ? 1 : 0, static_cast<int>(recipe.alpha)},
                    {1.0f, 1.0f, 1.0f, 1.0f}};
        sg_range range{&fs, sizeof(fs)};
        sg_apply_uniforms(1, &range);
        sg_draw(0, 6, 1);
    }
    sg_end_pass();
    sg_destroy_view(attachment);
    for (StepTexture& t : textures) {
        if (t.view.id) sg_destroy_view(t.view);
        if (t.image.id) sg_destroy_image(t.image);
    }
    if (recipe.levels > 1) {
        // The levels below the first, from it (a box filter).
        ID3D11Texture2D* texture = D3DTexture(target);
        ID3D11ShaderResourceView* all = nullptr;
        if (texture && SUCCEEDED(D3DDevice()->CreateShaderResourceView(texture, nullptr, &all))) {
            D3DContext()->GenerateMips(all);
            all->Release();
        }
    }
}

} // namespace

std::shared_ptr<const ComposeImage> DecodeComposeImage(const TextureImage& image) {
    if (!image.pixels || image.width <= 0 || image.height <= 0 || image.width > 4096 || image.height > 4096) return nullptr;
    if (!image.name) {
        auto decoded = std::make_shared<ComposeImage>();
        Decode(image, *decoded);
        return decoded;
    }
    const std::string key = Key(image.name);
    auto known = g_images.find(key);
    if (known != g_images.end()) {
        std::shared_ptr<const ComposeImage> kept = known->second.lock();
        if (kept && kept->width == image.width && kept->height == image.height) return kept;
    }
    auto decoded = std::make_shared<ComposeImage>();
    Decode(image, *decoded);
    g_images[key] = decoded;
    // Entries whose images nothing holds any more go now and then.
    if (g_images.size() > 256) {
        for (auto it = g_images.begin(); it != g_images.end();) it = it->second.expired() ? g_images.erase(it) : std::next(it);
    }
    return decoded;
}

bool MakeComposed(const ComposeRecipe& recipe, sg_image* image, sg_view* view) {
    *image = sg_image{};
    *view = sg_view{};
    if (!EnsurePipelines() || recipe.width <= 0 || recipe.height <= 0 || recipe.levels < 1 ||
        recipe.firstLevel >= recipe.levels)
        return false;
    ID3D11Device* device = D3DDevice();
    if (!device) return false;
    D3D11_TEXTURE2D_DESC d{};
    d.Width = static_cast<UINT>(recipe.width);
    d.Height = static_cast<UINT>(recipe.height);
    d.MipLevels = static_cast<UINT>(recipe.levels);
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    d.MiscFlags = recipe.levels > 1 ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
    ID3D11Texture2D* texture = nullptr;
    if (FAILED(device->CreateTexture2D(&d, nullptr, &texture))) {
        if (g_failures++ < 10) ei::Log("render: a %dx%d composed texture could not be made", recipe.width, recipe.height);
        return false;
    }
    sg_image_desc desc{};
    desc.usage.color_attachment = true;
    desc.width = recipe.width;
    desc.height = recipe.height;
    desc.num_mipmaps = recipe.levels;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.label = "ei-composed-texture";
    desc.d3d11_texture = texture;
    *image = sg_make_image(&desc);
    texture->Release();   // sokol holds its own reference
    if (sg_query_image_state(*image) != SG_RESOURCESTATE_VALID) {
        sg_destroy_image(*image);
        *image = sg_image{};
        return false;
    }
    CountImage(ImageUse::Texture, true);
    DrawSteps(recipe, *image, recipe.steps.data(), recipe.steps.size(), true);
    sg_view_desc v{};
    v.texture.image = *image;
    v.texture.mip_levels.base = recipe.firstLevel;
    v.texture.mip_levels.count = recipe.levels - recipe.firstLevel;
    *view = sg_make_view(&v);
    if (sg_query_view_state(*view) != SG_RESOURCESTATE_VALID) {
        sg_destroy_view(*view);
        sg_destroy_image(*image);
        *image = sg_image{};
        *view = sg_view{};
        return false;
    }
    return true;
}

void DrawComposed(const ComposeRecipe& recipe, sg_image image, const ComposeStep* steps, std::size_t count) {
    if (!image.id || !count || !EnsurePipelines()) return;
    DrawSteps(recipe, image, steps, count, false);
}

#if defined(EI_RENDER_VALIDATION)
bool ReadImageLevel(sg_image image, int level, int width, int height, std::vector<std::uint8_t>* rgba) {
    if (!image.id || width <= 0 || height <= 0 || !EnsurePipelines()) return false;
    sg_view_desc v{};
    v.texture.image = image;
    v.texture.mip_levels.base = level;
    v.texture.mip_levels.count = 1;
    sg_view source = sg_make_view(&v);
    sg_image_desc d{};
    d.usage.color_attachment = true;
    d.width = width;
    d.height = height;
    d.pixel_format = SG_PIXELFORMAT_RGBA8;
    d.label = "ei-texture-level-read";
    sg_image target = sg_make_image(&d);
    sg_view_desc a{};
    a.color_attachment.image = target;
    sg_view attachment = sg_make_view(&a);
    bool read = false;
    if (sg_query_view_state(source) == SG_RESOURCESTATE_VALID && sg_query_view_state(attachment) == SG_RESOURCESTATE_VALID) {
        sg_pass pass{};
        pass.action.colors[0].load_action = SG_LOADACTION_DONTCARE;
        pass.action.colors[0].store_action = SG_STOREACTION_STORE;
        pass.attachments.colors[0] = attachment;
        pass.label = "ei-texture-level-read";
        sg_begin_pass(&pass);
        sg_apply_pipeline(g_replace);
        sg_bindings b{};
        b.views[0] = source;
        b.samplers[0] = g_nearest;
        sg_apply_bindings(&b);
        ApplyRect(Rect{0, 0, width, height}, width, height);
        FsParams fs{{0, 0, 0, static_cast<int>(BlankAlpha::Full)}, {1.0f, 1.0f, 1.0f, 1.0f}};
        sg_range range{&fs, sizeof(fs)};
        sg_apply_uniforms(1, &range);
        sg_draw(0, 6, 1);
        sg_end_pass();
        StagingCopy copy;
        if (copy.Map(D3DContext(), D3DTexture(target))) {
            rgba->resize(static_cast<std::size_t>(width) * height * 4);
            for (int y = 0; y < height; ++y)
                std::memcpy(rgba->data() + static_cast<std::size_t>(y) * width * 4, copy.Row(static_cast<unsigned>(y)),
                            static_cast<std::size_t>(width) * 4);
            read = true;
        }
        copy.Release();
    }
    sg_destroy_view(attachment);
    sg_destroy_image(target);
    sg_destroy_view(source);
    return read;
}
#endif

void DestroyCompose() {
    for (CloudFrameTexture& f : g_cloudFrames) {
        sg_destroy_view(f.texture);
        sg_destroy_image(f.image);
    }
    g_cloudFrames.clear();
    for (sg_pipeline* p : {&g_over, &g_replace, &g_cloudFrame, &g_clouds}) {
        if (p->id) sg_destroy_pipeline(*p);
        *p = sg_pipeline{};
    }
    for (sg_shader* s : {&g_shader, &g_cloudFrameShader, &g_cloudsShader}) {
        if (s->id) sg_destroy_shader(*s);
        *s = sg_shader{};
    }
    if (g_nearest.id) sg_destroy_sampler(g_nearest);
    g_nearest = sg_sampler{};
    g_tried = g_made = false;
}

} // namespace render::detail
