// The game's cursor as the system's (render::Cursor, docs/notes/renderer.md section 9): at each present the frame's
// cursor picture is shown as a platform cursor shape, made from the picture's frame at the window's scale and through
// the gamma ramp, one shape per picture frame kept until the scale or the ramp changes; a frame without one hides it.
#include "engine/scene/surface.h"
#include "../platform/log.h"
#include "../platform/platform.h"
#include "render.h"
#include "render_internal.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace render {

// A shape made: `from` of the picture `holder` with `mask`, at `scale` (the frame's placed size) through ramp
// `ramp` (GammaRampChanges).
struct Made {
    const Surface* holder = nullptr;
    const void* mask = nullptr;
    Rect from{0, 0, 0, 0};
    int placedWidth = 0, placedHeight = 0;
    std::uint32_t ramp = 0;
    int width = 0, height = 0, hotX = 0, hotY = 0;
    platform::CursorShape shape = nullptr;
};
static std::vector<Made> g_made;
static Made g_shown{};                              // the shape shown (one of g_made's); none: hidden
static int g_placedWidth = 0, g_placedHeight = 0;   // the scale the last shape was looked up at
static FrameCursor g_frameCursor{};
static bool g_wanted = false;   // the frame being built called Cursor
static CursorInfo g_info;

// Floor division by a positive `d`.
static int FloorDiv(long long n, long long d) { return static_cast<int>(n >= 0 ? n / d : -((-n + d - 1) / d)); }

// `from` of the picture with its mask as alpha (255 where the mask's pixel, `maskBytes`, is zero: the mask is all ones
// where the picture is black) scaled to `width` x `height` as the present's linear sampler stretched the frame
// (texel centres; the filter on texels weighted by their alpha), then through the ramp: R, G, B, A rows from the top.
static std::vector<std::uint8_t> Pixels(const Picture& picture, const std::uint8_t* mask, int maskBytes,
                                        const Rect& from, int width, int height, const std::uint8_t* ramp) {
    const int sw = from.Width(), sh = from.Height();
    auto texel = [&](int x, int y, std::uint32_t* rgb, std::uint32_t* alpha) {
        const std::size_t i = static_cast<std::size_t>(from.top + y) * picture.width + (from.left + x);
        std::uint32_t bits = 0;
        std::memcpy(&bits, mask + i * maskBytes, static_cast<std::size_t>(maskBytes));
        *alpha = bits == 0 ? 255 : 0;
        for (int c = 0; c < 3; ++c) rgb[c] = *alpha ? picture.rgba[i * 4 + c] : 0;
    };
    std::vector<std::uint8_t> out(static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        // The source position in 1/256 texels: ((y + 0.5) * sh / height - 0.5) * 256.
        const int v = FloorDiv((2LL * y + 1) * sh * 128 - 128LL * height, height);
        const int y0 = FloorDiv(v, 256), fy = v - y0 * 256;
        const int rows[2] = {std::clamp(y0, 0, sh - 1), std::clamp(y0 + 1, 0, sh - 1)};
        for (int x = 0; x < width; ++x) {
            const int u = FloorDiv((2LL * x + 1) * sw * 128 - 128LL * width, width);
            const int x0 = FloorDiv(u, 256), fx = u - x0 * 256;
            const int columns[2] = {std::clamp(x0, 0, sw - 1), std::clamp(x0 + 1, 0, sw - 1)};
            const std::uint32_t weights[4] = {static_cast<std::uint32_t>((256 - fx) * (256 - fy)),
                                              static_cast<std::uint32_t>(fx * (256 - fy)),
                                              static_cast<std::uint32_t>((256 - fx) * fy), static_cast<std::uint32_t>(fx * fy)};
            std::uint32_t sum[3] = {0, 0, 0}, alphaSum = 0;
            for (int k = 0; k < 4; ++k) {
                std::uint32_t rgb[3], alpha;
                texel(columns[k & 1], rows[k >> 1], rgb, &alpha);
                alphaSum += weights[k] * alpha;
                for (int c = 0; c < 3; ++c) sum[c] += weights[k] * rgb[c];
            }
            std::uint8_t* p = &out[(static_cast<std::size_t>(y) * width + x) * 4];
            const std::uint32_t alpha = (alphaSum + 32768) >> 16;
            for (int c = 0; c < 3; ++c) {
                // Opaque texels' colours only (black where none is): the weights of the transparent ones left out.
                const std::uint32_t colour = alphaSum ? (sum[c] * 255 + alphaSum / 2) / alphaSum : 0;
                p[c] = ramp[std::min<std::uint32_t>(colour, 255) * 4 + c];
            }
            p[3] = static_cast<std::uint8_t>(alpha);
        }
    }
    return out;
}

// The shape for the frame's cursor at the window's scale and the current ramp, made at its first use; null when there
// is no picture or no window.
static const Made* ShapeFor(const FrameCursor& cursor) {
    const std::optional<FramePlacement> shown = FrameInWindow();
    if (!shown || shown->frame.w <= 0 || shown->frame.h <= 0) return nullptr;
    const int frameWidth = shown->frame.w, frameHeight = shown->frame.h;
    g_placedWidth = shown->placed.Width();
    g_placedHeight = shown->placed.Height();
    const std::uint32_t ramp = GammaRampChanges();
    for (const Made& m : g_made)
        if (m.holder == cursor.imageHolder && m.mask == cursor.mask && m.from == cursor.from &&
            m.placedWidth == g_placedWidth && m.placedHeight == g_placedHeight && m.ramp == ramp)
            return &m;
    // The picture loaded from its file (render::PictureFromMmp); the mask, the holder's width in its pixel size.
    const Surface* holder = cursor.imageHolder;
    const int maskBytes = holder->bitsPerPixel / 8;
    const Picture* picture = PictureOf(cursor.imageHolder);
    const Rect& from = cursor.from;
    if (!picture || picture->width != holder->width || (maskBytes != 2 && maskBytes != 4) || from.IsEmpty() ||
        from.Intersection(Rect{0, 0, picture->width, picture->height}) != from)
        return nullptr;
    const int sw = from.Width(), sh = from.Height();
    Made m{cursor.imageHolder, cursor.mask, from, g_placedWidth, g_placedHeight, ramp};
    m.width = std::max(1, (sw * g_placedWidth + frameWidth / 2) / frameWidth);
    m.height = std::max(1, (sh * g_placedHeight + frameHeight / 2) / frameHeight);
    m.hotX = std::clamp((cursor.hot.x * m.width + sw / 2) / sw, 0, m.width - 1);
    m.hotY = std::clamp((cursor.hot.y * m.height + sh / 2) / sh, 0, m.height - 1);
    const std::vector<std::uint8_t> rgba = Pixels(*picture, static_cast<const std::uint8_t*>(cursor.mask), maskBytes,
                                                  from, m.width, m.height, GammaRampTable());
    m.shape = platform::MakeCursor(rgba.data(), m.width, m.height, m.hotX, m.hotY);
    if (!m.shape) {
        LogFirst(g_info.failures, 3, "render: the system cannot make a %dx%d cursor", m.width, m.height);
        return nullptr;
    }
    LogFirst(g_info.made, 32,
             "render: cursor made: picture %dx%d, its part %d,%d-%d,%d, %dx%d on the window (hot spot %d, %d)",
             picture->width, picture->height, from.left, from.top, from.right, from.bottom, m.width, m.height, m.hotX,
             m.hotY);
    g_made.push_back(m);
    return &g_made.back();
}

// The shapes made for another scale or ramp let go, unless shown.
static void DropStale() {
    const std::uint32_t ramp = GammaRampChanges();
    std::erase_if(g_made, [&](const Made& m) {
        if (m.shape == g_shown.shape || (m.placedWidth == g_placedWidth && m.placedHeight == g_placedHeight && m.ramp == ramp))
            return false;
        platform::FreeCursor(m.shape);
        return true;
    });
}

// `made` (null: none) shown in place of the shape shown.
static void Show(const Made* made) {
    const Made next = made ? *made : Made{};
    if (next.shape == g_shown.shape) return;
    if (next.shape && g_shown.shape && next.holder == g_shown.holder && next.mask == g_shown.mask) ++g_info.frameChanges;
    ++g_info.shapeChanges;
    platform::SetCursor(next.shape);
    g_shown = next;
}

void Cursor(const FrameCursor& cursor) {
    if (!cursor.imageHolder || !cursor.mask) return;
    g_frameCursor = cursor;
    g_wanted = true;
}

void ShowFrameCursor() {
    const Made* made = g_wanted ? ShapeFor(g_frameCursor) : nullptr;
    Show(made);
    DropStale();
    const bool held = made && g_frameCursor.held;
    if (held) platform::HoldCursor(g_frameCursor.heldAt);
    else platform::ReleaseCursor();
    if (held) ++g_info.presentsHeld;
    else if (made) ++g_info.presentsShown;
    else ++g_info.presentsHidden;
    g_wanted = false;
}

void ForgetCursor(const Surface* holder) {
    if (g_shown.shape && g_shown.holder == holder) Show(nullptr);
    std::erase_if(g_made, [&](const Made& m) {
        if (m.holder != holder) return false;
        platform::FreeCursor(m.shape);
        return true;
    });
}

CursorInfo CursorState() {
    CursorInfo info = g_info;
    info.shown = g_shown.shape != nullptr;
    info.width = g_shown.width;
    info.height = g_shown.height;
    info.hotX = g_shown.hotX;
    info.hotY = g_shown.hotY;
    return info;
}

} // namespace render
