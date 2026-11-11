// The flying hit points (ui_flying_hit_points.h): the list and its drawing. docs/notes/ui.md, section 3.
#include "game/ui/ui_flying_hit_points.h"

#include <algorithm>

#include "game/app/singletons.h"
#include "engine/data/options.h"
#include "game/world/world.h"

#include "engine/scene/clip.h"
#include "engine/core/core_maths.h"
#include "engine/core/engine_object.h"
#include "engine/models/link_figure.h"
#include "game/objects/object3d.h"
#include "engine/scene/device.h"
#include "engine/scene/scene_renderer.h"
#include "game/ui/screen_layout.h"
#include "game/ui/ui.h"
#include "game/units/unit.h"

#include "lib/containers.h"
#include "platform/platform.h"
#include "lib/crt.h"
#include <cstdint>
#include <string>
#include "lib/str.h"

#include <iterator>
#include <memory>

namespace {
constexpr std::uint32_t kDelayed = 8;
constexpr std::uint32_t kDelay = 500;   // ms

constexpr int kLifetime = 3000;                        // ms
constexpr float kPerMillisecond = 0x1.5d867cp-12f;     // 1/3000
constexpr std::uint32_t kLabelA = 1, kLabelB = 2, kLabelC = kDelayed, kMarker = 0x80000000u;
constexpr std::uint32_t kGreen = 0x40FF40, kRed = 0xFF4040, kCyan = 0x40FFFF, kMarkerColour = 0xFFFFFF00u;
constexpr std::uint32_t kSpecular = 0xFF000000u;
constexpr int kMaxQuads = 16;   // the stack buffers hold 64 vertices and 96 indices

// The texture: digits in 8 x 12 pixel cells (four to a row, rows 0.15625 high from the top),
// label rows 64 x 10 pixels, the marker at u 0.5..1, v 0.84375..1. Drawn at these sizes up to a
// 1600x1200 screen and scaled up with the window above it (Draw's scale).
constexpr float kDigitWidth = 8.0f, kDigitHeight = 12.0f, kDigitU = 0.125f, kDigitV = 0.15625f;
constexpr float kLabelWidth = 64.0f, kLabelHeight = 10.0f;
constexpr float kMarkerWidth = 32.0f, kMarkerHeight = 10.0f;
// The texture coordinates are pulled inwards by this much (u up, v down).
constexpr float kNumberInset = 1.0f / 512, kMarkerInset = 1.0f / 256;

struct LabelRow {
    std::uint32_t flag = 0;
    float top = 0.0f, bottom = 0.0f;  // texture v
};
constexpr LabelRow kLabelRows[3] = {
    {kLabelA, 0.5390625f, 0.390625f}, {kLabelB, 0.390625f, 0.2421875f}, {kLabelC, 0.2421875f, 0.09375f}};

struct QuadBuffer {
    TLVertex vertices[kMaxQuads * 4];
    std::uint16_t indices[kMaxQuads * 6] = {};
    int vertexCount = 0;
    int indexCount = 0;

    // A screen rectangle with the texture rectangle (u0, v0)-(u1, v1): corners top-left,
    // top-right, bottom-left, bottom-right; triangles 0 2 1 and 1 2 3.
    void Add(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, std::uint32_t colour) {
        TLVertex* v = vertices + vertexCount;
        v[0] = TLVertex{x0, y0, 1.0f, 1.0f, colour, kSpecular, u0, v0};
        v[1] = TLVertex{x1, y0, 1.0f, 1.0f, colour, kSpecular, u1, v0};
        v[2] = TLVertex{x0, y1, 1.0f, 1.0f, colour, kSpecular, u0, v1};
        v[3] = TLVertex{x1, y1, 1.0f, 1.0f, colour, kSpecular, u1, v1};
        constexpr std::uint16_t kOrder[6] = {0, 2, 1, 1, 2, 3};
        for (int i = 0; i < 6; ++i) indices[indexCount + i] = static_cast<std::uint16_t>(vertexCount + kOrder[i]);
        vertexCount += 4;
        indexCount += 6;
    }
    void Inset(float d) {
        for (int i = 0; i < vertexCount; ++i) {
            vertices[i].u += d;
            vertices[i].v -= d;
        }
    }
    void Draw(SceneRenderer* renderer) {
        renderer->device->DrawTlTriangles(vertices, vertexCount, indices, indexCount);
    }
};

// Where the unit was drawn: the screen rectangle of its first "hd", "bd" or "hp" part's own mesh
// (part flag 0x40), else the unit's; null when it has none of those parts.
const Rect* UnitScreenRect(Unit* unit) {
    Object3D* object = unit->AsObject3D();
    FigureMesh* part = nullptr;
    for (int i = 0; i < 3 && !part; ++i) part = object->FindPartByPrefix(kHeroFigureParts[i]);
    if (!part) return nullptr;
    if (part->flags & FigureMeshFlag::Drawn) return &part->drawer->screen;
    return &object->dirtyRect;
}

} // namespace

void FlyingHitPoints::Add(int target, int amount, std::uint32_t flags) {
    if (!target || amount < 0) return;
    std::uint32_t time = (flags & kDelayed) ? platform::FrameClock() + kDelay : platform::FrameClock();
    points.push_back(FlyingHitPoint{target, amount, flags, time});
}

// --- drawing -------------------------------------------------------------------------------------

void FlyingHitPoints::Draw(SceneRenderer* renderer) {
    std::string text;
    std::uint32_t now = platform::FrameClock();
    const Rect& screen = renderer->device->screen;
    if (!texture) return;
    renderer->SetTexture(0, texture);

    std::erase_if(points, [now](const FlyingHitPoint& point) { return static_cast<int>(now - point.time) > kLifetime; });
    if (!options::g_options.drawFlyingHp) return;

    const float width = static_cast<float>(screen.right), height = static_cast<float>(screen.bottom);
    // Sizes, offsets and the drift are the original's pixels, kept up to a 1200-high 4:3 area (1600x1200; the
    // user found 800x600's scale twice too large at 4K) and grown with the 4:3 part's height above it (1.8 at 4K).
    const float scale = std::max(1.0f, screen_layout::Get().height43 / 1200.0f);
    const float digitWidth = kDigitWidth * scale, digitHeight = kDigitHeight * scale;
    const float labelWidth = kLabelWidth * scale, labelHeight = kLabelHeight * scale;
    for (const FlyingHitPoint& point : points) {
        // The age as a fraction of the lifetime, stored as float.
        float age = static_cast<int>(now - point.time) * kPerMillisecond;
        if (age > 1.0f || age < 0.0f) continue;   // integer-sourced: never NaN
        auto* unit = g_clientWorld.FindUnit(point.target);
        if (!unit) continue;
        if (!(unit->flags & MapObjectFlag::Shown) || (unit->flags & MapObjectFlag::NotDrawn) || !(unit->flags & MapObjectFlag::InParty)) continue;
        const Rect* rect = UnitScreenRect(unit);
        if (!rect) continue;
        int middleX = (rect->left + rect->right) / 2;
        int middleY = (rect->top + rect->bottom) / 2;

        if (point.flags & kMarker) {
            // 32 x 10 centred 20 above the middle.
            float x0 = middleX - kMarkerWidth * scale * 0.5f;
            float y0 = (middleY - 20.0f * scale) - kMarkerHeight * scale * 0.5f;
            if (x0 < 0.0f) continue;   // integer-sourced, as below: never NaN
            float x1 = x0 + kMarkerWidth * scale;
            if (width < x1) continue;
            if (y0 < 0.0f) continue;
            float y1 = y0 + kMarkerHeight * scale;
            if (y1 > height) continue;
            QuadBuffer quads;
            quads.Add(x0, y0, x1, y1, 0.5f, 1.0f, 1.0f, 0.84375f, kMarkerColour);
            quads.Inset(kMarkerInset);
            quads.Draw(renderer);
            continue;
        }

        // The number drifts 120 right and rises 100 over its life (truncated, as _ftol).
        middleX += static_cast<int>(age * 120.0f * scale);
        middleY += static_cast<int>(age * -100.0f * scale);
        text = str::Format("%d", point.amount);
        int length = lib::Count(text);
        // The box: the digits, at least a label row wide when there are labels; a digit high,
        // a label row more for each label.
        const float digitsWidth = length * digitWidth;
        float boxWidth = digitsWidth;
        float boxHeight = digitHeight;
        for (const LabelRow& row : kLabelRows) {
            if (!(point.flags & row.flag)) continue;
            if (boxWidth <= labelWidth) boxWidth = labelWidth;
            boxHeight += labelHeight;
        }
        float x0 = middleX - boxWidth * 0.5f;
        float y0 = middleY - boxHeight * 0.5f;
        if (x0 < 0.0f) continue;   // integer-sourced: never NaN
        if (width < boxWidth + x0) continue;
        if (y0 < 0.0f) continue;
        if (height < boxHeight + y0) continue;

        // Fading out over the life: green, red for a player's unit, cyan with flag 8.
        auto alpha = static_cast<std::uint32_t>(static_cast<int>((1.0f - age) * 255.0f)) & 0xFF;
        std::uint32_t colour = (point.flags & kLabelC) ? kCyan : unit->player ? kRed : kGreen;
        colour |= alpha << 24;

        QuadBuffer quads;
        float x = (boxWidth - digitsWidth) * 0.5f + x0;
        float digitsBottom = y0 + digitHeight;
        for (int i = 0; i < length; ++i) {
            int value = static_cast<signed char>(text[i]) - '0';
            float u0 = (value & 3) * kDigitU;
            float top = 1.0f - (value >> 2) * kDigitV;
            float next = x + digitWidth;
            quads.Add(x, y0, next, digitsBottom, u0, top, u0 + kDigitU, top - kDigitV, colour);
            x = next;
        }
        // The label rows under the digits, centred in the box.
        float y = digitsBottom;
        for (const LabelRow& row : kLabelRows) {
            if (!(point.flags & row.flag)) continue;
            float left = (boxWidth - labelWidth) * 0.5f + x0;
            float bottom = y + labelHeight;
            quads.Add(left, y, left + labelWidth, bottom, 0.0f, row.top, 1.0f, row.bottom, colour);
            y = bottom;
        }
        quads.Inset(kNumberInset);
        quads.Draw(renderer);
    }

    // Markers are shown once.
    std::erase_if(points, [](const FlyingHitPoint& point) { return (point.flags & kMarker) != 0; });
}
