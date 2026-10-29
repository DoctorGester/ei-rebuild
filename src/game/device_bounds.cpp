// The Direct3D device's screen rectangle of a mesh draw (mesh draw flag 1): the bounds of its projected positions
// clamped to the screen, rounded into the device's drawn rectangle (+0x15C), which the figures' and the 3D library's
// draws read back for picking. See docs/notes/renderer.md (section 3, "Picking").
#include "model.h"     // CTransformPipeline
#include "texture.h"   // CTextureDevice

#include "lib/gameapi.h"

#include "ei/x87.h"

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

    // Clamped to the screen (0..width, 0..height); bounds that hold no point, or none on screen, come out inside out
    // (an empty rectangle).
    void Clamp(float width, float height) {
        minX = minX < 0.0f ? 0.0f : minX;
        minY = minY < 0.0f ? 0.0f : minY;
        maxX = width < maxX ? width : maxX;
        maxY = height < maxY ? height : maxY;
    }

    // The bounds rounded to nearest into the device's rectangle.
    void Store(CTextureDevice* device) const {
        const int bottom = x87::fistp(maxY);
        const int right = x87::fistp(maxX);
        const int top = x87::fistp(minY);
        const int left = x87::fistp(minX);
        gameapi::SetRect(&device->drawnRect, left, top, right, bottom);
    }
};

// A clip-space point inside the depth range: in front of the near plane (z = 0) and not past the far plane (z = w).
bool InDepth(const Vector4& p) { return p.z >= 0.0f && p.w - p.z >= 0.0f; }

} // namespace

// The projection leaves screen positions when every one is on screen, clip-space ones when the mesh crosses the view's
// edges: those count when inside the depth range (nothing behind the camera), mapped as the pipeline's ToScreen maps
// them (x right and y down from the top left corner, the viewport the screen's size). Past a corner of the screen
// this is larger than the original's rectangle (the bounds of the clipped triangles), and across one edge it can be
// wider along that edge: an accepted difference (renderer.md section 3).
void CTextureDevice::BoundsOfProjected(const std::vector<Vector4>* positions, bool clipSpace) {
    const float width = static_cast<float>(screen.right - screen.left);
    const float height = static_cast<float>(screen.bottom - screen.top);
    ScreenBounds bounds;
    for (const Vector4& p : *positions) {
        if (!clipSpace) {
            bounds.Include(p.x, p.y);
        } else if (InDepth(p) && p.w > 0.0f) {
            const Vector4 newScreen = pipeline->ToScreen(p);
            bounds.Include(newScreen.x, newScreen.y);
        }
    }
    bounds.Clamp(width, height);
    bounds.Store(this);
}
