// Drawing a lightning bolt (Lightning's renderer virtual; called by DrawAll) and its
// glow: the segments as textured strips through the render device, then a
// glow: the segments as textured strips through the render device, then a
// the world's frame counter. Plain float maths (rule 7). See docs/notes/objects.md ("Rewritten").
#include "lightning.h"
#include "texture.h"

#include "core_maths.h"
#include "model.h"
#include "random.h"
#include "world.h"

#include "lib/crt.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#include "lib/nan_audit.h"
#include "camera.h"

namespace {

// The glow's work arrays: the sprite records (zero filled) and the 16-byte points.

struct SpriteRecord {   // 8 bytes: colour, texture rectangle
    std::uint32_t colour;
    const float* rect;
};
std::vector<SpriteRecord> g_sprites;
std::vector<LightningPoint> g_glowPoints;

void SizeSprites() { g_sprites.resize(8); }
void SizePoints() { g_glowPoints.resize(8); }

// A random texture offset: 0, 0.25, 0.5 or 0.75.
float RandomOffset() {
    return static_cast<float>(static_cast<int>(g_clientRandom.Next() & 3)) * 0.25f;
}

} // namespace

// `position` the glow's centre, `size` its radius; the frame argument the caller computes is not
// used (the frame comes from the position and the world's counter).
void Lightning::DrawGlow(SceneRenderer* device, const Vector3& position, float size) {
    EnsureTextureLoaded(glowTexture);
    if (!device->SetTexture(0, glowTexture)) return;

    // The 15 frames of the 4 x 4 glow texture: (u0, v0, u1, v1).
    float frames[15][4];
    const float quarter = 0.25f;
    for (int i = 0; i < 15; ++i) {
        frames[i][0] = static_cast<float>(i & 3) * quarter;
        frames[i][1] = 0.75f - static_cast<float>((i >> 2) & 3) * quarter;
        frames[i][2] = frames[i][0] + quarter;
        frames[i][3] = frames[i][1] + quarter;
    }
    SizeSprites();
    SizePoints();

    // Towards the camera, normalised unless zero.
    const Vector3& eye = camera::FrameView().position;
    Vector3 towards = eye - position;
    float length = LengthZYX(towards);
    EI_NAN_AUDIT(length);
    if (length != 0.0f) towards /= length;
    float scaled = position.x * 15.0f;
    int shift = std::lround(scaled);
    int frame = (World()->tick + shift) % 15;
    for (int j = 0; j < 8; ++j) {
        float t = static_cast<float>(j) * 0.125f;   // 1/8
        Vector3 along{towards.x * t * size, towards.y * t * size, towards.z * t * size};
        LightningPoint& p = g_glowPoints[j];
        p.x = position.x + along.x;
        p.y = position.y + along.y;
        p.z = position.z + along.z;
        p.w = static_cast<float>(std::sqrt(1.0f - t * t) * size);
        SpriteRecord& s = g_sprites[j];
        s.colour = 0xA0FFFFFFu;
        s.rect = frames[frame % 15];
        frame += 2;
    }
    device->DrawSprites(0, &g_glowPoints, &g_sprites);
}

// 0 when the texture cannot be set; 1 otherwise (also when the bolt is not visible).
int Lightning::Draw(SceneRenderer* device) {
    EnsureTextureLoaded(texture);
    SceneRenderer* renderer = device;
    if (!renderer->SetTexture(0, texture)) return 0;
    int visibility = renderer->pipeline->ClipTest(&bounds);
    if (visibility == 2) return 1;
    const float zero = 0.0f;
    if (power >= zero) Animate();
    const float slant = 0.25f;
    EI_NAN_AUDIT(power);
    if (power < zero) {    // negative: untextured lengthwise
        renderer->DrawStrip(&main.points, main.width, visibility, 0.0f, 0.0f, slant);
        for (LightningSegment& b : branches) renderer->DrawStrip(&b.points, b.width, visibility, 0.0f, 0.0f, slant);
    } else {
        float offset = RandomOffset();
        float span = LengthZXY(main.step) * static_cast<float>(std::ssize(main.points)) * 0.6f;
        renderer->DrawStrip(&main.points, main.width, visibility, span, offset, slant);
        for (LightningSegment& b : branches) {
            offset = RandomOffset();
            span = LengthZXY(b.step) * static_cast<float>(std::ssize(b.points)) * 0.6f;
            renderer->DrawStrip(&b.points, b.width, visibility, span, offset, slant);
        }
    }

    float count = static_cast<float>(std::ssize(main.points));
    Vector3 start = main.start;
    Vector3 run{count * main.step.x, count * main.step.y, count * main.step.z};
    Vector3 end{start.x + run.x, start.y + run.y, start.z + run.z};
    float dx = start.x - end.x, dy = start.y - end.y, dz = start.z - end.z;
    float size = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)) * 0.11f);
    if (power > zero) {
        // Clamped to [0.5, 1.2] with selects (NaN keeps the size).
        float low = 0.5f, high = 1.2f;
        size = low >= size ? low : size;
        size = size >= high ? high : size;
    }
    if (power >= zero) DrawGlow(device, end, size);
    if (power == zero) DrawGlow(device, start, size);
    return 1;
}
