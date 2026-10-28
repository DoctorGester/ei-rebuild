// Drawing a lightning bolt (CLightning's renderer virtual; called by DrawAll) and its
// glow: the segments as textured strips through the render device, then a
// glow: the segments as textured strips through the render device, then a
// the world's frame counter. Plain float maths (rule 7). See docs/notes/objects.md ("Rewritten").
#include "lightning.h"
#include "lib/static_object.h"
#include "texture.h"

#include "core_maths.h"
#include "model.h"
#include "random.h"
#include "world.h"

#include "ei/x87.h"
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

// The glow's static arrays: the sprite records (zero filled) and the 16-byte points, each built on the first draw
// and destroyed by the exit function it registers.

struct SpriteRecord {   // 8 bytes: colour, texture rectangle
    std::uint32_t colour;
    const float* rect;
};
constinit lib::StaticObject<std::vector<SpriteRecord>> g_sprites = /*0079C1E0*/ {};
std::vector<SpriteRecord>& Sprites() { return *g_sprites; }
void DestroySprites() { g_sprites.Destroy(); }
constinit std::vector<LightningPoint> g_glowPoints = /*0079C1D0*/ {};
void FreeGlowPoints() { lib::FreeStorage(g_glowPoints); }
std::uint8_t g_glowGuards = /*0079C1F4*/ {};

void SizeSprites() { Sprites().resize(8); }
void SizePoints() { g_glowPoints.resize(8); }

// A random texture offset: 0, 0.25, 0.5 or 0.75.
float RandomOffset() {
    return static_cast<float>(static_cast<int>(g_random.Next() & 3)) * 0.25f /*0073C3B0*/;
}

} // namespace

// `position` the glow's centre, `size` its radius; the frame argument the caller computes is not
// used (the frame comes from the position and the world's counter).
void CLightning::DrawGlow(CSceneRenderer* device, const Vector3* position, int /*frame: not used*/, float size) {
    if (!(g_glowGuards & 1)) {
        g_glowGuards |= 1;
        g_sprites.Construct();
        std::atexit(DestroySprites);
    }
    if (!(g_glowGuards & 2)) {
        g_glowGuards |= 2;
        std::vector<LightningPoint>& p = g_glowPoints;
        std::construct_at(&p);
        std::atexit(FreeGlowPoints);
    }
    TextureHolderEnsureLoaded(&glowTexture_);
    if (!device->SetTexture(0, glowTexture_)) return;

    // The 15 frames of the 4 x 4 glow texture: (u0, v0, u1, v1).
    float frames[15][4];
    const float quarter = 0.25f /*0073C3B0*/;
    for (int i = 0; i < 15; ++i) {
        frames[i][0] = static_cast<float>(i & 3) * quarter;
        frames[i][1] = 0.75f /*0073C524*/ - static_cast<float>((i >> 2) & 3) * quarter;
        frames[i][2] = frames[i][0] + quarter;
        frames[i][3] = frames[i][1] + quarter;
    }
    SizeSprites();
    SizePoints();

    // Towards the camera, normalised unless zero.
    const Vector3& camera = TheCamera()->localPosition_;
    Vector3 towards = camera - *position;
    float length = LengthZYX(towards);
    EI_NAN_AUDIT(length);
    if (length != 0.0f) towards /= length;
    float scaled = position->x * 15.0f /*0073C518*/;
    int shift = x87::fistp(scaled);
    int frame = (ObjectWorld(this)->tick_ + shift) % 15;
    for (int j = 0; j < 8; ++j) {
        float t = static_cast<float>(j) * 0.125f /*0073C520*/;   // 1/8
        Vector3 along{towards.x * t * size, towards.y * t * size, towards.z * t * size};
        LightningPoint& p = g_glowPoints[j];
        p.x = position->x + along.x;
        p.y = position->y + along.y;
        p.z = position->z + along.z;
        p.w = static_cast<float>(std::sqrt(1.0f - t * t) * size);
        SpriteRecord& s = Sprites()[j];
        s.colour = 0xA0FFFFFFu;
        s.rect = frames[frame % 15];
        frame += 2;
    }
    device->DrawSprites(0, &g_glowPoints, &Sprites());
}

// 0 when the texture cannot be set; 1 otherwise (also when the bolt is not visible).
int CLightning::Draw(CSceneRenderer* device) {
    TextureHolderEnsureLoaded(&texture_);
    CSceneRenderer* renderer = device;
    if (!renderer->SetTexture(0, texture_)) return 0;
    int visibility = renderer->pipeline_->ClipTest(&bounds_);
    if (visibility == 2) return 1;
    const float zero = 0.0f /*0073B858*/;
    if (power_ >= zero) Animate();
    const float slant = 0.25f /*0073C3B0*/;
    EI_NAN_AUDIT(power_);
    if (power_ < zero) {   // negative: untextured lengthwise
        renderer->DrawStrip(&main_.points, main_.width, visibility, 0.0f, 0.0f, slant);
        for (LightningSegment& b : branches_) renderer->DrawStrip(&b.points, b.width, visibility, 0.0f, 0.0f, slant);
    } else {
        float offset = RandomOffset();
        float span = LengthZXY(main_.step) * static_cast<float>(std::ssize(main_.points)) * 0.6f /*0073C4FC*/;
        renderer->DrawStrip(&main_.points, main_.width, visibility, span, offset, slant);
        for (LightningSegment& b : branches_) {
            offset = RandomOffset();
            span = LengthZXY(b.step) * static_cast<float>(std::ssize(b.points)) * 0.6f /*0073C4FC*/;
            renderer->DrawStrip(&b.points, b.width, visibility, span, offset, slant);
        }
    }

    float count = static_cast<float>(std::ssize(main_.points));
    Vector3 start = main_.start;
    Vector3 run{count * main_.step.x, count * main_.step.y, count * main_.step.z};
    Vector3 end{start.x + run.x, start.y + run.y, start.z + run.z};
    int frameCounter = ObjectWorld(this)->tick_;
    float dx = start.x - end.x, dy = start.y - end.y, dz = start.z - end.z;
    float size = static_cast<float>(std::sqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)) * 0.11f /*0073C528*/);
    int phase = (frameCounter / 2) % 15;
    if (power_ > zero) {
        // Clamped to [0.5, 1.2] with selects (NaN keeps the size).
        float low = 0.5f, high = 1.2f;
        size = low >= size ? low : size;
        size = size >= high ? high : size;
    }
    if (power_ >= zero) DrawGlow(device, &end, phase, size);
    if (power_ == zero) DrawGlow(device, &start, phase, size);
    return 1;
}
