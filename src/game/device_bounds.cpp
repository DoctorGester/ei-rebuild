// The Direct3D device's screen bounds of a mesh draw (mesh draw flag 1): the smallest and largest x and y of
// its projected positions, rounded into the device's drawn rectangle (+0x15C), which the figures' and the 3D
// library's draws read back. See docs/notes/renderer.md (section 3, "Picking").
#include "device_buffers.h"  // ProjectedVertex
#include "model.h"           // MeshCorner
#include "texture.h"         // CTextureDevice

#include "ei/x87.h"
#include "lib/gameapi.h"

#include <cstdint>
#include <cfloat>
#include <vector>

namespace {

// The bounds so far: they start inside out, and a coordinate replaces a bound it is not inside of
// (a NaN never does).
struct ScreenBounds {
    float minX = FLT_MAX;
    float minY = FLT_MAX;
    float maxX = -FLT_MAX;
    float maxY = -FLT_MAX;

    void Include(float x, float y) {
        minX = x < minX ? x : minX;
        maxX = maxX < x ? x : maxX;
        minY = y < minY ? y : minY;
        maxY = maxY < y ? y : maxY;
    }

    // The bounds rounded to nearest into the device's rectangle; SetRect's result.
    int Store(CTextureDevice* device) const {
        const int bottom = x87::fistp(maxY);
        const int right = x87::fistp(maxX);
        const int top = x87::fistp(minY);
        const int left = x87::fistp(minX);
        return gameapi::SetRect(&device->drawnRect_, left, top, right, bottom);
    }
};

constexpr int kBlock = 4;   // projected positions come in blocks of four

using devicebuffers::ProjectedVertex;

// A clip-space point's distances inside the depth range's near plane (z = 0) and far plane (z = w): both at
// least 0 inside.
float NearDistance(const ProjectedVertex& p) { return p.z; }
float FarDistance(const ProjectedVertex& p) { return p.w - p.z; }
bool InDepth(const ProjectedVertex& p) { return NearDistance(p) >= 0.0f && FarDistance(p) >= 0.0f; }

// The point of the edge from `a` to `b` where a plane they lie on either side of (distances `da` and `db`) cuts it.
ProjectedVertex Cut(const ProjectedVertex& a, const ProjectedVertex& b, float da, float db) {
    const float t = da / (da - db);
    ProjectedVertex p;
    p.x = a.x + (b.x - a.x) * t;
    p.y = a.y + (b.y - a.y) * t;
    p.z = a.z + (b.z - a.z) * t;
    p.w = a.w + (b.w - a.w) * t;
    return p;
}

// The screen bounds of a crossing mesh's clip-space points inside the depth range, each mapped as the transform
// object's ToScreen maps them (x right and y down from the top left corner, the viewport's size the screen's).
class CrossingBounds {
public:
    explicit CrossingBounds(const RECT& screen)
        : width_(static_cast<float>(screen.right - screen.left)), height_(static_cast<float>(screen.bottom - screen.top)),
          halfWidth_(width_ * 0.5f), halfHeight_(height_ * 0.5f) {}

    void Include(const ProjectedVertex& p) {
        if (p.w <= 0.0f) return;
        const float rhw = 1.0f / p.w;
        bounds_.Include((p.x * rhw + 1.0f) * halfWidth_, (1.0f - p.y * rhw) * halfHeight_);
    }

    // The points the edge from `a` to `b` has inside the depth range where it crosses the near or far plane.
    void IncludeCuts(const ProjectedVertex& a, const ProjectedVertex& b) {
        const float nearA = NearDistance(a), nearB = NearDistance(b);
        if ((nearA < 0.0f) != (nearB < 0.0f)) {
            const ProjectedVertex p = Cut(a, b, nearA, nearB);
            if (FarDistance(p) >= 0.0f) Include(p);
        }
        const float farA = FarDistance(a), farB = FarDistance(b);
        if ((farA < 0.0f) != (farB < 0.0f)) {
            const ProjectedVertex p = Cut(a, b, farA, farB);
            if (NearDistance(p) >= 0.0f) Include(p);
        }
    }

    // Whether the bounds overlap the screen.
    bool OnScreen() const {
        return bounds_.minX < width_ && 0.0f < bounds_.maxX && bounds_.minY < height_ && 0.0f < bounds_.maxY;
    }

    // The bounds clamped to the screen, into the device's rectangle.
    void Store(CTextureDevice* device) const {
        ScreenBounds clamped = bounds_;
        clamped.minX = clamped.minX < 0.0f ? 0.0f : clamped.minX;
        clamped.minY = clamped.minY < 0.0f ? 0.0f : clamped.minY;
        clamped.maxX = width_ < clamped.maxX ? width_ : clamped.maxX;
        clamped.maxY = height_ < clamped.maxY ? height_ : clamped.maxY;
        clamped.Store(device);
    }

private:
    float width_;
    float height_;
    float halfWidth_;
    float halfHeight_;
    ScreenBounds bounds_;
};

} // namespace

// The bounds of screen positions (the projection's of a mesh on screen); with `outcodes` only of those whose
// outcode is 0. Whole blocks of four at a time (a size that is not a multiple of four reads to the end of its last
// block).
int CTextureDevice::BoundsOfProjected(const std::vector<ProjectedVertex>* positions, const std::uint8_t* outcodes) {
    ScreenBounds bounds;
    for (int i = 0; i < std::ssize(*positions); i += kBlock) {
        for (int k = i; k < i + kBlock; ++k) {
            if (outcodes && outcodes[k] != 0) continue;
            const ProjectedVertex& position = (*positions)[k];
            bounds.Include(position.x, position.y);
        }
    }
    return bounds.Store(this);
}

// The bounds of the positions inside the depth range and of the triangles' edges' cuts through its planes, clamped
// to the screen: the bounds of the triangles clipped against the view where the mesh crosses one edge of the
// screen, larger by the corner it misses where it passes a corner of the screen.
bool CTextureDevice::BoundsOfCrossing(const std::vector<ProjectedVertex>* positions, const MeshCorner* corners,
                                      const std::uint16_t* indices, int indexCount, bool keep) {
    CrossingBounds bounds(screen_);
    bool cut = false;
    for (const ProjectedVertex& position : *positions) {
        if (InDepth(position)) {
            bounds.Include(position);
        } else {
            cut = true;
        }
    }
    if (cut) {
        for (int i = 0; i + 2 < indexCount; i += 3) {
            const ProjectedVertex& a = (*positions)[corners[indices[i]].position];
            const ProjectedVertex& b = (*positions)[corners[indices[i + 1]].position];
            const ProjectedVertex& c = (*positions)[corners[indices[i + 2]].position];
            bounds.IncludeCuts(a, b);
            bounds.IncludeCuts(b, c);
            bounds.IncludeCuts(c, a);
        }
    }
    if (!bounds.OnScreen()) return false;
    if (keep) bounds.Store(this);
    return true;
}
