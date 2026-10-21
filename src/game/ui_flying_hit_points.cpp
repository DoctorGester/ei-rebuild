// The flying hit points (ui_flying_hit_points.h): the list and its drawing. docs/notes/ui.md, section 3.
#include "ui_flying_hit_points.h"
#include "world_glue.h"

#include "clip.h"
#include "core_maths.h"
#include "engine_object.h"
#include "game_strings.h"
#include "link_figure.h"
#include "map_object_figures.h"
#include "model.h"
#include "texture.h"
#include "ui.h"
#include "unit.h"
#include "world_data.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/crt.h"
#include "originals_engine.h"
#include "originals_ui.h"

namespace {
using Node = stlp::list<FlyingHitPoint>::Node;
constexpr std::uint32_t kDelayed = 8;
constexpr std::uint32_t kDelay = 500;   // ms
} // namespace

CFlyingHitPoints* CFlyingHitPoints::Construct() {
    auto* sentinel = static_cast<Node*>(stlp::allocate(sizeof(Node)));
    points_.head = sentinel;
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
    object_ = nullptr;
    return this;
}

void CFlyingHitPoints::Destruct() {
    if (object_) object_->Release();
    points_.ClearWith([](FlyingHitPoint*) {});
    stlp::deallocate(points_.head, sizeof(Node));
}
EI_REWRITTEN(0x005E17E0, CFlyingHitPoints::Destruct);

void CFlyingHitPoints::Add(int target, int amount, std::uint32_t flags) {
    if (!target || amount < 0) return;
    std::uint32_t time = (flags & kDelayed) ? gameapi::timeGetTime() + kDelay : gameapi::timeGetTime();
    // list::insert(end(), value) (005E24E0, the node made by 005E2520).
    auto* node = static_cast<Node*>(stlp::allocate(sizeof(Node)));
    node->data = FlyingHitPoint{target, amount, flags, time};
    Node* head = points_.head;
    node->next = head;
    node->prev = head->prev;
    head->prev->next = node;
    head->prev = node;
}

// --- 005E18E0: drawing ------------------------------------------------------------------------------

namespace {

constexpr int kLifetime = 3000;                        // ms
constexpr float kPerMillisecond = 0x1.5d867cp-12f;     // 1/3000 (0073D61C)
constexpr std::uint32_t kLabelA = 1, kLabelB = 2, kLabelC = kDelayed, kMarker = 0x80000000u;
// The unit is drawn this frame (its screen rectangle is valid), not hidden, and can be picked.
constexpr std::uint32_t kUnitDrawn = 0x20, kUnitHidden = 0x40, kUnitPickable = 0x400000;
constexpr std::uint32_t kPartOwnMesh = 0x40;
constexpr std::uint32_t kGreen = 0x40FF40, kRed = 0xFF4040, kCyan = 0x40FFFF, kMarkerColour = 0xFFFFFF00u;
constexpr std::uint32_t kSpecular = 0xFF000000u;
constexpr int kMaxQuads = 16;   // the original's stack buffers hold 64 vertices and 96 indices

// The texture: digits in 8 x 12 pixel cells (four to a row, rows 0.15625 high from the top),
// label rows 64 x 10 pixels, the marker at u 0.5..1, v 0.84375..1.
constexpr float kDigitWidth = 8.0f, kDigitHeight = 12.0f, kDigitU = 0.125f, kDigitV = 0.15625f;
constexpr float kLabelWidth = 64.0f, kLabelHeight = 10.0f;
constexpr float kMarkerWidth = 32.0f, kMarkerHeight = 10.0f;
// The texture coordinates are pulled inwards by this much (u up, v down).
constexpr float kNumberInset = 1.0f / 512, kMarkerInset = 1.0f / 256;

struct LabelRow {
    std::uint32_t flag;
    float top, bottom;   // texture v
};
constexpr LabelRow kLabelRows[3] = {
    {kLabelA, 0.5390625f, 0.390625f}, {kLabelB, 0.390625f, 0.2421875f}, {kLabelC, 0.2421875f, 0.09375f}};

struct QuadBuffer {
    TLVertex vertices[kMaxQuads * 4];
    std::uint16_t indices[kMaxQuads * 6];
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
    void Draw(CSceneRenderer* renderer) {
        renderer->DrawTriangles(vertices, vertexCount, indices, static_cast<unsigned>(indexCount));
    }
};

// list::erase (005E25B0): the node unlinked and back to the pool. Returns the next one.
Node* Erase(Node* node) {
    Node* next = node->next;
    node->prev->next = next;
    next->prev = node->prev;
    stlp::deallocate(node, sizeof(Node));
    return next;
}

// Where the unit was drawn: the screen rectangle of its first "hd", "bd" or "hp" part's own mesh
// (part flag 0x40), else the unit's; null when it has none of those parts.
const RECT* UnitScreenRect(CUnit* unit) {
    CObject3D* object = unit->Object3D();
    CFigureMesh* part = nullptr;
    for (int i = 0; i < 3 && !part; ++i) part = object->FindPartByPrefix(worlddata::HeroPartText(i));
    if (!part) return nullptr;
    if (part->flags_ & kPartOwnMesh) return &part->drawer_->screen_;
    return reinterpret_cast<const RECT*>(object->dirtyRect);
}

} // namespace

int CFlyingHitPoints::Draw(CSceneRenderer* renderer) {
    mfc::CString text;
    DWORD now = gameapi::timeGetTime();
    // The render target's size (the original leaves both unset without a primary surface).
    std::uint32_t targetWidth = 0, targetHeight = 0;
    if (renderer->device_->primary_) {
        targetWidth = static_cast<std::uint32_t>(renderer->device_->renderTarget_->width);
        targetHeight = static_cast<std::uint32_t>(renderer->device_->renderTarget_->height);
    }
    if (!object_) return 0;
    if (!renderer->SetTexture(0, object_)) return game::FatalError(game::FlyingHitPointsTextureText());

    Node* head = points_.head;
    for (Node* node = head->next; node != head;) {
        if (static_cast<int>(now - node->data.time) > kLifetime) node = Erase(node);
        else node = node->next;
    }
    if (!ShowHitNumbersOption()) return 1;

    const float width = static_cast<float>(targetWidth), height = static_cast<float>(targetHeight);
    for (Node* node = head->next; node != head; node = node->next) {
        const FlyingHitPoint& point = node->data;
        // The age as a fraction of the lifetime, stored as float.
        float age = static_cast<float>(static_cast<float>(static_cast<int>(now - point.time)) * kPerMillisecond);
        if (age > 1.0f || age < 0.0f) continue;   // integer-sourced: never NaN
        auto* unit = GameFindClientUnit(point.target);
        if (!unit) continue;
        if (!(unit->flags_ & kUnitDrawn) || (unit->flags_ & kUnitHidden) || !(unit->flags_ & kUnitPickable)) continue;
        const RECT* rect = UnitScreenRect(unit);
        if (!rect) continue;
        int middleX = (rect->left + rect->right) / 2;
        int middleY = (rect->top + rect->bottom) / 2;

        if (point.flags & kMarker) {
            // 32 x 10 pixels centred 20 pixels above the middle.
            float x0 = static_cast<float>(static_cast<float>(middleX) - kMarkerWidth * 0.5f);
            float y0 = static_cast<float>(static_cast<float>(middleY - 20) - kMarkerHeight * 0.5f);
            if (x0 < 0.0f) continue;   // integer-sourced, as below: never NaN
            float x1 = static_cast<float>(x0 + kMarkerWidth);
            if (width < x1) continue;
            if (y0 < 0.0f) continue;
            float y1 = y0 + kMarkerHeight;
            if (y1 > height) continue;
            QuadBuffer quads;
            quads.Add(x0, y0, x1, y1, 0.5f, 1.0f, 1.0f, 0.84375f, kMarkerColour);
            quads.Inset(kMarkerInset);
            quads.Draw(renderer);
            continue;
        }

        // The number drifts 120 pixels right and rises 100 over its life (truncated, as _ftol).
        middleX += static_cast<int>(age * 120.0f);
        middleY += static_cast<int>(age * -100.0f);
        text.Format(game::DecimalFormat(), point.amount);
        int length = text.GetLength();
        // The box: the digits, at least a label row wide when there are labels; 12 pixels high
        // (22 with the first label row), 10 more for each other one.
        int boxWidth = length * static_cast<int>(kDigitWidth);
        int boxHeight = 12;
        for (const LabelRow& row : kLabelRows) {
            if (!(point.flags & row.flag)) continue;
            if (boxWidth <= 64) boxWidth = 64;
            boxHeight = row.flag == kLabelA ? 22 : boxHeight + 10;
        }
        float x0 = static_cast<float>(static_cast<float>(middleX) - static_cast<float>(boxWidth) * 0.5f);
        float boxHeightF = static_cast<float>(boxHeight);
        float y0 = static_cast<float>(static_cast<float>(middleY) - boxHeightF * 0.5f);
        if (x0 < 0.0f) continue;   // integer-sourced: never NaN
        if (width < static_cast<float>(boxWidth) + x0) continue;
        if (y0 < 0.0f) continue;
        if (height < boxHeightF + y0) continue;

        // Fading out over the life: green, red for a player's unit, cyan with flag 8.
        auto alpha = static_cast<std::uint32_t>(static_cast<int>((1.0f - age) * 255.0f)) & 0xFF;
        std::uint32_t colour = (point.flags & kLabelC) ? kCyan : unit->player ? kRed : kGreen;
        colour |= alpha << 24;

        QuadBuffer quads;
        float x = static_cast<float>(boxWidth - length * static_cast<int>(kDigitWidth)) * 0.5f + x0;
        float digitsBottom = static_cast<float>(y0 + kDigitHeight);
        for (int i = 0; i < length; ++i) {
            int value = static_cast<signed char>(static_cast<const char*>(text)[i]) - '0';
            float u0 = static_cast<float>(value & 3) * kDigitU;
            float top = static_cast<float>(1.0f - static_cast<float>(value >> 2) * kDigitV);
            float next = static_cast<float>(x + kDigitWidth);
            quads.Add(x, y0, next, digitsBottom, u0, top, static_cast<float>(u0 + kDigitU), top - kDigitV, colour);
            x = next;
        }
        // The label rows under the digits, centred in the box.
        float y = y0 + kDigitHeight;
        for (const LabelRow& row : kLabelRows) {
            if (!(point.flags & row.flag)) continue;
            float left = static_cast<float>(boxWidth - static_cast<int>(kLabelWidth)) * 0.5f + x0;
            float bottom = y + kLabelHeight;
            quads.Add(left, y, static_cast<float>(left + kLabelWidth), bottom, 0.0f, row.top, 1.0f, row.bottom, colour);
            y = bottom;
        }
        quads.Inset(kNumberInset);
        quads.Draw(renderer);
    }

    // Markers are shown once.
    for (Node* node = head->next; node != head;) {
        if (node->data.flags & kMarker) node = Erase(node);
        else node = node->next;
    }
    return 1;
}
EI_REWRITTEN(0x005E18E0, CFlyingHitPoints::Draw);
