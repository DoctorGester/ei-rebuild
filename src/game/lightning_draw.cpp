// Drawing a lightning bolt (00467AD0, CLightning's renderer virtual; called by DrawAll) and its
// glow (00467590): the segments as textured strips through the render device (slot 16), then a
// glow of eight sprites (slot 17) at one or both ends, sized by the bolt's length and animated by
// the world's frame counter. Plain float maths (rule 7). See docs/notes/objects.md ("Rewritten").
#include "lightning.h"
#include "texture.h"

#include "core_maths.h"
#include "model.h"
#include "random.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/mfc_array.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_render.h"

namespace {

// 00500970: makes sure the texture in `holder` is loaded.
void __fastcall EnsureTextureLoaded(void** a0) { TextureHolderEnsureLoaded(reinterpret_cast<CTexture**>(a0)); }
// 004F16A0: the visibility of a bounds record for the device's view (+8): 2 is outside.
// 00455950: the world an engine object belongs to (its frame counter at +0x64).
// The glow's static arrays: CArray constructor 00468D60, its exit-time destructor 00467A10, the
// points' exit-time free 004679F0; sprite record constructor 00468F90 [pointer, count]; aligned
// allocation of 16-byte points 00468F70; atexit 006E727B. The sprite records are zero filled (00468F90).
void __fastcall SpriteArrayConstruct(void* a0) { mfc::impl::ConstructHeader<mfc::impl::SpriteArrayClass>(a0); }

IsaacRandom* Rng() { return reinterpret_cast<IsaacRandom*>(0x007A9D40); }
float GameFloat(std::uintptr_t address) {
    float f;
    std::memcpy(&f, reinterpret_cast<const void*>(address), sizeof(f));
    return f;
}

struct SpriteRecord {   // 8 bytes: colour, texture rectangle
    std::uint32_t colour;
    const float* rect;
};
struct SpriteElements {
    static void Construct(SpriteRecord* records, int count) { mfc::PlainElements<SpriteRecord>::Construct(records, count); }
    static void Destruct(SpriteRecord*, int) {}
};
using SpriteArray = mfc::CArray<SpriteRecord, SpriteElements>;   // vtable 0073C584, at 0079C1E0
SpriteArray& Sprites() { return *reinterpret_cast<SpriteArray*>(0x0079C1E0); }
LightningPointArray& GlowPoints() { return *reinterpret_cast<LightningPointArray*>(0x0079C1D0); }
std::uint8_t& GlowGuards() { return *reinterpret_cast<std::uint8_t*>(0x0079C1F4); }

// The two static arrays' SetSize(8), as inlined (the sprites' new records constructed by 00468F90).
void SizeSprites() { Sprites().SetSize(8); }
void SizePoints() { GlowPoints().SetSize(8); }

// A random texture offset: 0, 0.25, 0.5 or 0.75.
float RandomOffset() {
    return static_cast<float>(static_cast<int>(Rng()->Next() & 3)) * GameFloat(0x0073C3B0);
}

} // namespace

// 00467590. `position` the glow's centre, `size` its radius; the frame argument the caller computes is not
// used (the frame comes from the position and the world's counter).
void CLightning::DrawGlow(void* device, const Vector3* position, int /*frame: not used*/, float size) {
    if (!(GlowGuards() & 1)) {
        GlowGuards() |= 1;
        SpriteArrayConstruct(&Sprites());
        GameAtExit(reinterpret_cast<void (__cdecl*)()>(0x00467A10));
    }
    if (!(GlowGuards() & 2)) {
        GlowGuards() |= 2;
        LightningPointArray& p = GlowPoints();
        p.data = nullptr;
        p.growBy = 0;
        p.capacity = 0;
        p.size = 0;
        GameAtExit(reinterpret_cast<void (__cdecl*)()>(0x004679F0));
    }
    auto** holder = reinterpret_cast<void**>(&glowTexture_);
    EnsureTextureLoaded(holder);
    if (!static_cast<CSceneRenderer*>(device)->SetTexture(0, *holder)) return;

    // The 15 frames of the 4 x 4 glow texture: (u0, v0, u1, v1).
    float frames[15][4];
    const float quarter = GameFloat(0x0073C3B0);
    for (int i = 0; i < 15; ++i) {
        frames[i][0] = static_cast<float>(i & 3) * quarter;
        frames[i][1] = GameFloat(0x0073C524) - static_cast<float>((i >> 2) & 3) * quarter;
        frames[i][2] = frames[i][0] + quarter;
        frames[i][3] = frames[i][1] + quarter;
    }
    SizeSprites();
    SizePoints();

    // Towards the camera (0079B2C4), normalised unless zero.
    const Vector3& camera = *reinterpret_cast<const Vector3*>(0x0079B2C4);
    Vector3 towards = camera - *position;
    float length = LengthZYX(towards);
    if (length > 0.0f || length < 0.0f) towards /= length;
    float scaled = position->x * GameFloat(0x0073C518);   // 15
    int shift = x87::fistp(scaled);
    int frame = (static_cast<CWorld*>(ObjectWorld(this))->tick_ + shift) % 15;
    for (int j = 0; j < 8; ++j) {
        float t = static_cast<float>(j) * GameFloat(0x0073C520);   // 1/8
        Vector3 along{towards.x * t * size, towards.y * t * size, towards.z * t * size};
        LightningPoint& p = GlowPoints().data[j];
        p.x = position->x + along.x;
        p.y = position->y + along.y;
        p.z = position->z + along.z;
        p.w = static_cast<float>(x87::fsqrt(1.0f - t * t) * size);
        SpriteRecord& s = Sprites().data[j];
        s.colour = 0xA0FFFFFFu;
        s.rect = frames[frame % 15];
        frame += 2;
    }
    static_cast<CSceneRenderer*>(device)->DrawSprites(0, &GlowPoints(), &Sprites());
}
EI_REPLACE_METHOD(0x00467590, CLightning::DrawGlow);

// 00467AD0. 0 when the texture cannot be set; 1 otherwise (also when the bolt is not visible).
int CLightning::DrawImpl(void* device) {
    auto** holder = reinterpret_cast<void**>(&texture_);
    EnsureTextureLoaded(holder);
    auto* renderer = static_cast<CSceneRenderer*>(device);
    if (!renderer->SetTexture(0, *holder)) return 0;
    int visibility = renderer->pipeline_->ClipTest(reinterpret_cast<const ClipBounds*>(&boundsKind_));
    if (visibility == 2) return 1;
    const float zero = GameFloat(0x0073B858);
    if (power_ >= zero) Animate();
    const float slant = GameFloat(0x0073C3B0);   // 0.25
    if (!(power_ >= zero)) {   // negative (or NaN): untextured lengthwise
        renderer->DrawStrip(&main_.points, main_.width, visibility, 0.0f, 0.0f, slant);
        for (LightningSegment& b : branches_) renderer->DrawStrip(&b.points, b.width, visibility, 0.0f, 0.0f, slant);
    } else {
        float offset = RandomOffset();
        float span = LengthZXY(main_.step) * static_cast<float>(main_.points.size) * GameFloat(0x0073C4FC);   // 0.6
        renderer->DrawStrip(&main_.points, main_.width, visibility, span, offset, slant);
        for (LightningSegment& b : branches_) {
            offset = RandomOffset();
            span = LengthZXY(b.step) * static_cast<float>(b.points.size) * GameFloat(0x0073C4FC);
            renderer->DrawStrip(&b.points, b.width, visibility, span, offset, slant);
        }
    }

    float count = static_cast<float>(main_.points.size);
    Vector3 start = main_.start;
    Vector3 run{count * main_.step.x, count * main_.step.y, count * main_.step.z};
    Vector3 end{start.x + run.x, start.y + run.y, start.z + run.z};
    int frameCounter = static_cast<int*>(ObjectWorld(this))[0x64 / 4];
    float dx = start.x - end.x, dy = start.y - end.y, dz = start.z - end.z;
    float size = static_cast<float>(x87::fsqrt(LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz)) * GameFloat(0x0073C528));   // 0.11
    int phase = (frameCounter / 2) % 15;
    if (power_ > zero) {
        // Clamped to [0.5, 1.2] with the original's selects (NaN keeps the size).
        float low = 0.5f, high = 1.2f;
        size = !(low >= size) ? size : low;
        size = !(size >= high) ? size : high;
    }
    if (power_ >= zero) DrawGlow(device, &end, phase, size);
    if (!(power_ < zero) && !(power_ > zero)) DrawGlow(device, &start, phase, size);   // zero (or NaN)
    return 1;
}
EI_REPLACE_METHOD(0x00467AD0, CLightning::DrawImpl);
