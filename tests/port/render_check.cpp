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
#include "../ui_canvas_glyphs.h"

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
    CheckTransfers();
#if defined(EI_RENDER_VALIDATION)
    CheckRecovery();
#endif
    std::printf("Native renderer checks: %d, failures: %d\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
