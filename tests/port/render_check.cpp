// Exercise the same GPU glyph oracle as the Windows canvas checker on Metal.
#include "game/game_singletons.h"
#include "game/model.h"
#include "game/texture.h"
#include "render/render_internal.h"
#include "render/render_text.h"
#include "render/render_ui.h"
#include "standalone/static_init.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
namespace {
using render::Rect;
namespace text = render::text;
namespace ui = render::ui;
bool g_verbose = true;
#include "../../tools/ui_check/ui_canvas_glyphs.h"

void CheckShaderPrograms() {
    using namespace render::detail;
    for (int code = 1; code <= static_cast<int>(ShaderProgram::UiBlit); ++code) {
        const sg_shader_desc desc = ShaderDescription(static_cast<ShaderProgram>(code));
        const sg_shader shader = sg_make_shader(desc);
        Check(sg_query_shader_state(shader) == SG_RESOURCESTATE_VALID,
              "compile shader program " + std::to_string(code));
        sg_destroy_shader(shader);
    }
}

void CheckClouds() {
    const render::CloudFrame from{{{0xFFFFFFFFu, 0x80000000u, 123456789u},
                                   {17u, 0xDEADBEEFu, 9001u},
                                   {12345u, 67890u, 0xABCDEF01u},
                                   {0x87654321u, 42u, 0xFEDCBA98u}}};
    const render::CloudFrame to{{{0xC001CAFEu, 31u, 0xAAAAAAAAu},
                                 {0x55555555u, 7654321u, 0x80000001u},
                                 {0xFEEDFACEu, 77u, 123u},
                                 {345u, 0x90000000u, 0xFFFFFFFFu}}};
    std::uint8_t ramp[256];
    for (int i = 0; i < 256; ++i) ramp[i] = static_cast<std::uint8_t>(i * 73 + 19);
    // Pixel hashes from the HLSL reference shaders.
    const std::uint64_t expected[] = {0x4818A49DC6BCF8DFull, 0xE69938CD567DD113ull, 0xB8BA0C7EC3AFD74Eull};
    const int blends[] = {0, 73, 256};
    int holder = 0;
    Check(render::BlankTexture(&holder, 256, 256, render::BlankAlpha::Full, false), "create cloud canvas");
    for (int k = 0; k < 3; ++k) {
        render::DrawClouds(&holder, from, to, blends[k], ramp);
        const sg_view view = render::detail::SurfaceView(&holder);
        const sg_image image = sg_query_view_image(view);
        std::vector<std::uint8_t> pixels;
        std::size_t pitch = 0;
        const bool read = platform::graphics::ReadImage(image, 0, 0, 256, 256, &pixels, &pitch);
        std::uint64_t hash = 0xCBF29CE484222325ull;
        if (read) {
            for (int y = 0; y < 256; ++y)
                for (int x = 0; x < 256 * 4; ++x) hash = (hash ^ pixels[y * pitch + x]) * 0x100000001B3ull;
        }
        std::printf("cloud blend %d: %016llX\n", blends[k], static_cast<unsigned long long>(hash));
        Check(read && hash == expected[k], "cloud pixels match HLSL at blend " + std::to_string(blends[k]));
    }
    render::HolderGone(&holder);
}

void CheckCanvasPixels() {
    constexpr int width = 37, height = 29;
    std::vector<std::uint32_t> source(width * height);
    for (std::size_t i = 0; i < source.size(); ++i) source[i] = static_cast<std::uint32_t>(i * 1234567u) & 0xFFFFFF;
    for (const auto& format : kFormats) {
        int holder = 0;
        ui::Canvas(&holder, width, height, format.format);
        ui::Image(&holder, Rect{0, 0, width, height}, source.data(), width);
        std::vector<std::uint32_t> pixels;
        bool same = ui::ReadPixels(&holder, &pixels) && pixels.size() == source.size();
        for (std::size_t i = 0; i < pixels.size() && same; ++i)
            same = pixels[i] == RefQuantise(source[i], format.format);
        Check(same, std::string(format.name) + " canvas image matches CPU quantisation");
        constexpr int outWidth = 17, outHeight = 11;
        std::uint16_t packed[outWidth * outHeight]{};
        same = ui::ReadRgb565(&holder, outWidth, outHeight, packed) && pixels.size() == source.size();
        for (int y = 0; y < outHeight && same; ++y) {
            for (int x = 0; x < outWidth; ++x) {
                const int sx = (2 * x + 1) * width / (2 * outWidth), sy = (2 * y + 1) * height / (2 * outHeight);
                const auto rgb = pixels[sy * width + sx];
                const auto want = ((rgb >> 19) << 11) | (((rgb >> 10) & 63) << 5) | ((rgb >> 3) & 31);
                same = same && packed[y * outWidth + x] == want;
            }
        }
        Check(same, std::string(format.name) + " save picture matches CPU centre sampling and RGB565 packing");
        std::vector<std::uint32_t> grey;
        for (auto rgb : pixels) {
            const auto packedPixel = ui::Pack(rgb, format.format);
            const auto green = format.format.greenMask;
            std::uint32_t value = 0;
            if (green == 0x7E0) {
                value = (((packedPixel >> 4) & 0x7E) + ((packedPixel >> 10) & 0x3E) + (packedPixel & 31)) >> 4;
                value = ((value << 5 | value) << 6) | value;
            } else if (green == 0x3E0) {
                value = (((packedPixel >> 3) & 0x7C) + ((packedPixel >> 9) & 0x3E) + (packedPixel & 31)) >> 4;
                value = ((value << 5 | value) << 5) | value;
            } else {
                value = (((packedPixel >> 6) & 0x3FC) + ((packedPixel >> 15) & 0x1FE) + (packedPixel & 255)) >> 4;
                value = value * 0x010101;
            }
            grey.push_back(value);
        }
        ui::Greyscale(&holder);
        Check(!grey.empty() && ReadPacked(&holder, format.format) == grey,
              std::string(format.name) + " greyscale matches the CPU packed-pixel formula");
        ui::DropCanvas(&holder);
    }

    g_sceneRenderer.device.Assign(new TextureDevice(nullptr, false, width, height, 32));
    std::vector<std::uint8_t> rgba(source.size() * 4);
    for (std::size_t i = 0; i < source.size(); ++i) {
        rgba[i * 4] = source[i] >> 16;
        rgba[i * 4 + 1] = source[i] >> 8;
        rgba[i * 4 + 2] = source[i];
        rgba[i * 4 + 3] = 255;
    }
    int movie = 0;
    render::MovieFrame(&movie, rgba.data(), width, height);
    render::MovieBlit(&movie, Rect{0, 0, width, height});
    for (const auto& format : kFormats) {
        int holder = 0;
        ui::Canvas(&holder, 17, 11, format.format);
        ui::CopyFrame(&holder, Rect{3, 2, 31, 26});
        std::vector<std::uint32_t> pixels;
        bool same = ui::ReadPixels(&holder, &pixels) && pixels.size() == 17 * 11;
        for (int y = 0; y < 11 && same; ++y) {
            for (int x = 0; x < 17; ++x)
                same = same && pixels[y * 17 + x] ==
                                   RefQuantise(source[(2 + y * 24 / 11) * width + 3 + x * 28 / 17], format.format);
        }
        Check(same, std::string(format.name) + " frame copy matches CPU nearest sampling");
        ui::DropCanvas(&holder);
    }
    render::Present();
    g_sceneRenderer.device.Reset();
}

void CheckMultisampling() {
    using namespace render::detail;
    constexpr int side = 4;
    std::uint8_t pixels[side * side * 4];
    for (int i = 0; i < side * side * 4; ++i) pixels[i] = static_cast<std::uint8_t>(i * 17);
    sg_image_desc sourceDesc{};
    sourceDesc.width = sourceDesc.height = side;
    sourceDesc.pixel_format = SG_PIXELFORMAT_RGBA8;
    sourceDesc.data.mip_levels[0] = sg_range{pixels, sizeof(pixels)};
    sg_image source = sg_make_image(sourceDesc);
    sg_view_desc view{};
    view.texture.image = source;
    sg_view sourceView = sg_make_view(view);
    struct Target {
        sg_image image;
        sg_view attachment, texture;
    };
    auto make = [&](sg_pixel_format format, int samples) {
        sg_image_desc desc{};
        desc.width = desc.height = side;
        desc.sample_count = samples;
        desc.pixel_format = format;
        desc.usage.color_attachment = format == SG_PIXELFORMAT_RGBA8;
        desc.usage.depth_stencil_attachment = format == SG_PIXELFORMAT_DEPTH;
        desc.usage.resolve_attachment = format == SG_PIXELFORMAT_RGBA8 && samples == 1;
        Target target{sg_make_image(desc)};
        sg_view_desc v{};
        if (format == SG_PIXELFORMAT_DEPTH) v.depth_stencil_attachment.image = target.image;
        else v.color_attachment.image = target.image;
        target.attachment = sg_make_view(v);
        v = {};
        v.texture.image = target.image;
        target.texture = sg_make_view(v);
        return target;
    };
    auto drop = [](const Target& target) {
        sg_destroy_view(target.texture);
        sg_destroy_view(target.attachment);
        sg_destroy_image(target.image);
    };
    for (int samples : {2, 4, 8}) {
        if (samples > platform::graphics::SampleCount()) continue;
        const std::string label = "MSAA " + std::to_string(samples);
        Check(PrepareSmoothCopies(samples), label + " copy pipelines");
        const auto depth = make(SG_PIXELFORMAT_DEPTH, 1);
        const auto multiColour = make(SG_PIXELFORMAT_RGBA8, samples);
        const auto multiDepth = make(SG_PIXELFORMAT_DEPTH, samples);
        const auto resolved = make(SG_PIXELFORMAT_RGBA8, 1);
        const auto resultDepth = make(SG_PIXELFORMAT_DEPTH, 1);
        sg_view_desc resolveDesc{};
        resolveDesc.resolve_attachment.image = resolved.image;
        const sg_view resolve = sg_make_view(resolveDesc);
        sg_pass pass{};
        pass.attachments.depth_stencil = depth.attachment;
        pass.action.depth.load_action = SG_LOADACTION_CLEAR;
        pass.action.depth.clear_value = 0.375f;
        pass.action.depth.store_action = SG_STOREACTION_STORE;
        sg_begin_pass(pass);
        sg_end_pass();
        pass = {};
        pass.attachments.colors[0] = multiColour.attachment;
        pass.attachments.depth_stencil = multiDepth.attachment;
        pass.attachments.resolves[0] = resolve;
        pass.action.depth.store_action = SG_STOREACTION_STORE;
        sg_begin_pass(pass);
        CopyIntoMultisampled(sourceView, depth.texture);
        sg_end_pass();
        pass = {};
        pass.attachments.colors[0] = resolved.attachment;
        pass.attachments.depth_stencil = resultDepth.attachment;
        pass.action.colors[0].load_action = SG_LOADACTION_LOAD;
        pass.action.depth.store_action = SG_STOREACTION_STORE;
        sg_begin_pass(pass);
        CopyDepthFromMultisampled(multiDepth.texture);
        sg_end_pass();
        CommitGpu();
        std::vector<std::uint8_t> read;
        std::size_t pitch = 0;
        bool same = platform::graphics::ReadImage(resolved.image, 0, 0, side, side, &read, &pitch);
        for (int y = 0; y < side && same; ++y)
            same = std::memcmp(read.data() + y * pitch, pixels + y * side * 4, side * 4) == 0;
        Check(same, label + " colour copy and resolve preserve every texel");
        same = platform::graphics::ReadImage(resultDepth.image, 0, 0, side, side, &read, &pitch);
        for (int y = 0; y < side && same; ++y) {
            for (int x = 0; x < side; ++x) {
                float z = 0;
                std::memcpy(&z, read.data() + y * pitch + x * sizeof(float), sizeof(z));
                same = same && std::abs(z - 0.375f) < 1e-6f;
            }
        }
        Check(same, label + " depth copy preserves sample zero");
        sg_destroy_view(resolve);
        for (const auto& target : {depth, multiColour, multiDepth, resolved, resultDepth}) drop(target);
    }
    DestroySmooth();
    sg_destroy_view(sourceView);
    sg_destroy_image(source);
}

void CheckTransfers() {
    namespace graphics = platform::graphics;
    std::vector<std::uint8_t> pixels(8 * 8 * 4);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            auto* pixel = pixels.data() + (y * 8 + x) * 4;
            pixel[0] = x * 16;
            pixel[1] = y * 16;
            pixel[2] = 32;
            pixel[3] = 255;
        }
    }
    sg_image_desc desc{};
    desc.width = desc.height = 8;
    desc.num_mipmaps = 4;
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    desc.usage.color_attachment = true;
    const sg_image source = graphics::MakeMipmappedImage(desc);
    Check(sg_query_image_state(source) == SG_RESOURCESTATE_VALID, "make a mipmapped render target");
    graphics::WriteImage(source, {0, 0, 8, 8}, pixels.data(), 8 * 4);

    std::vector<std::uint8_t> patch(4 * 4 * 4);
    for (std::size_t i = 0; i < patch.size(); i += 4) {
        patch[i] = 192;
        patch[i + 1] = 64;
        patch[i + 2] = 128;
        patch[i + 3] = 255;
    }
    graphics::WriteImage(source, {2, 2, 6, 6}, patch.data(), 4 * 4);
    for (int y = 0; y < 4; ++y) std::memcpy(pixels.data() + ((y + 2) * 8 + 2) * 4, patch.data() + y * 4 * 4, 4 * 4);

    std::vector<std::uint8_t> read;
    std::size_t pitch = 0;
    bool same = graphics::ReadImage(source, 0, 0, 8, 8, &read, &pitch);
    if (same) {
        for (int y = 0; y < 8; ++y)
            same = same && std::memcmp(read.data() + y * pitch, pixels.data() + y * 8 * 4, 8 * 4) == 0;
    }
    Check(same, "partial image upload preserves every pixel outside the region");

    desc.num_mipmaps = 1;
    const sg_image target = sg_make_image(desc);
    graphics::CopyImage(source, target, {2, 2, 6, 6}, 1, 1);
    same = graphics::ReadImage(target, 1, 1, 4, 4, &read, &pitch);
    if (same) {
        for (int y = 0; y < 4; ++y)
            same = same && std::memcmp(read.data() + y * pitch, patch.data() + y * 4 * 4, 4 * 4) == 0;
    }
    Check(same, "image copy and readback use their source and destination offsets");
    Check(!graphics::ReadImage(target, 7, 7, 2, 2, &read, &pitch), "reject readback outside the image");
    graphics::GenerateMipmaps(source);
#if defined(EI_RENDER_VALIDATION)
    const bool mipRead = render::detail::ReadImageLevel(source, 3, 1, 1, &read);
    const int expected[] = {90, 58, 56, 255};
    same = mipRead && read.size() == 4;
    if (same) {
        for (int c = 0; c < 4; ++c) same = same && std::abs(static_cast<int>(read[c]) - expected[c]) <= 1;
    }
    Check(same, "generated last mip matches the CPU average");
#endif
    sg_destroy_image(target);
    sg_destroy_image(source);

    desc.usage = {};
    desc.usage.dynamic_update = true;
    const sg_image dynamic = sg_make_image(desc);
    same = sg_query_image_state(dynamic) == SG_RESOURCESTATE_VALID;
    for (int frame = 0; frame < 4 && same; ++frame) {
        std::fill(pixels.begin(), pixels.end(), static_cast<std::uint8_t>(40 + frame));
        sg_image_data data{};
        data.mip_levels[0] = sg_range{pixels.data(), pixels.size()};
        sg_update_image(dynamic, data);
        same = graphics::ReadImage(dynamic, 0, 0, 8, 8, &read, &pitch);
        for (int y = 0; y < 8 && same; ++y)
            same = std::memcmp(read.data() + y * pitch, pixels.data() + y * 8 * 4, 8 * 4) == 0;
        render::detail::CommitGpu();
    }
    Check(same, "readback follows every dynamic image update across commits");
    sg_destroy_image(dynamic);
    graphics::ReleaseReadbacks();
}

#if defined(EI_RENDER_VALIDATION)
void CheckRecovery() {
    int holder = 0;
    ui::Canvas(&holder, 4, 4, kFormats[2].format);
    ui::Fill(&holder, Rect{0, 0, 4, 4}, Colourref(40, 80, 120));
    std::vector<std::uint32_t> before, after;
    const bool saved = ui::ReadPixels(&holder, &before);
    g_sceneRenderer.device.Assign(new TextureDevice(nullptr, false, 8, 8, 32));
    render::Present();
    const auto previous = render::WindowPresents();
    render::LoseDevice();
    render::Present();
    const auto recovered = render::WindowPresents();
    Check(recovered.deviceLosses == previous.deviceLosses + 1 &&
              recovered.deviceRecoveries == previous.deviceRecoveries + 1 && recovered.lastLossSimulated,
          "device loss rebuilds the shared renderer");
    Check(saved && ui::ReadPixels(&holder, &after) && after == before, "canvas history survives device recreation");
    ui::DropCanvas(&holder);
    g_sceneRenderer.device.Reset();
    CheckGlyphs();
}
#endif
}
int main() {
    ei::static_init::RunStaticInitialisers();
    CheckGlyphs();
    CheckShaderPrograms();
    CheckClouds();
    CheckCanvasPixels();
    CheckMultisampling();
    CheckTransfers();
#if defined(EI_RENDER_VALIDATION)
    CheckRecovery();
#endif
    std::printf("Native renderer checks: %d, failures: %d\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
