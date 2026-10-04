// Drawing a lightning bolt (00467AD0, CLightning's renderer virtual; called by DrawAll) and its
// glow (00467590): the segments as textured strips through the render device (slot 16), then a
// glow of eight sprites (slot 17) at one or both ends, sized by the bolt's length and animated by
// the world's frame counter. Plain float maths (rule 7). See docs/notes/objects.md ("Sixth round").
#include "lightning.h"

#include "core_maths.h"
#include "random.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "lib/crt.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

// The render device's slots used here: 11 SetTexture(stage, texture) (0: failed), 16 a strip
// along points (points, width, visibility, texture length, texture offset, 0.25), 17 sprites
// (0, points, sprite records).
constexpr int kSetTexture = 11;
constexpr int kDrawStrip = 16;
constexpr int kDrawSprites = 17;

// 00500970: makes sure the texture in `holder` is loaded.
__declspec(naked) void __fastcall EnsureTextureLoaded(void** /*holder*/) EI_ORIGINAL(0x00500970)
// 004F16A0: the visibility of a bounds record for the device's view (+8): 2 is outside.
__declspec(naked) int __fastcall ClipTest(void* /*view*/, void*, const void* /*bounds*/) EI_ORIGINAL(0x004F16A0)
// 00455950: the world an engine object belongs to (its frame counter at +0x64).
__declspec(naked) void* __fastcall WorldOf(const void*) EI_ORIGINAL(0x00455950)
// 00467590 (below), through its address.
__declspec(naked) void __fastcall DrawGlowAt(CLightning*, void*, void* /*device*/, const Vector3*, int, float) EI_ORIGINAL(0x00467590)
// The glow's static arrays: CArray constructor 00468D60, its exit-time destructor 00467A10, the
// points' exit-time free 004679F0; sprite record constructor 00468F90 [pointer, count]; aligned
// allocation of 16-byte points 00468F70; atexit 006E727B.
__declspec(naked) void __fastcall SpriteArrayConstruct(void*) EI_ORIGINAL(0x00468D60)
__declspec(naked) void __stdcall ConstructSprites(void*, int) EI_ORIGINAL(0x00468F90)
__declspec(naked) void* __fastcall AllocPoints16(int) EI_ORIGINAL(0x00468F70)
__declspec(naked) int __cdecl GameAtexit(void (__cdecl*)()) EI_ORIGINAL(0x006E727B)

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
struct SpriteArray {    // CArray (vtable 0073C584) at 0079C1E0
    std::uintptr_t vtable;
    SpriteRecord* data;
    int size;
    int max;
    int grow;
};
SpriteArray& Sprites() { return *reinterpret_cast<SpriteArray*>(0x0079C1E0); }
LightningPointArray& GlowPoints() { return *reinterpret_cast<LightningPointArray*>(0x0079C1D0); }
std::uint8_t& GlowGuards() { return *reinterpret_cast<std::uint8_t*>(0x0079C1F4); }

int GrowStep(int grow, int size) {
    if (grow != 0) return grow;
    int g = size / 8;
    return g < 4 ? 4 : g > 0x400 ? 0x400 : g;
}

// The two static arrays' SetSize(8), as inlined.
void SizeSprites() {
    SpriteArray& a = Sprites();
    if (!a.data) {
        a.data = static_cast<SpriteRecord*>(gamecrt::operator_new(0x40));
        ConstructSprites(a.data, 8);
        a.max = 8;
        a.size = 8;
    } else if (a.max >= 8) {
        if (a.size < 8) ConstructSprites(a.data + a.size, 8 - a.size);
        a.size = 8;
    } else {
        int capacity = a.max + GrowStep(a.grow, a.size);
        if (capacity <= 8) capacity = 8;
        auto* data = static_cast<SpriteRecord*>(gamecrt::operator_new(static_cast<std::size_t>(capacity) * 8));
        std::memcpy(data, a.data, static_cast<std::size_t>(a.size) * 8);
        ConstructSprites(data + a.size, 8 - a.size);
        gamecrt::operator_delete(a.data);
        a.data = data;
        a.size = 8;
        a.max = capacity;
    }
}
void SizePoints() {
    LightningPointArray& a = GlowPoints();
    if (!a.data) {
        a.data = static_cast<LightningPoint*>(AllocPoints16(8));
        a.capacity = 8;
        a.size = 8;
    } else if (a.capacity >= 8) {
        a.size = 8;
    } else {
        int capacity = a.capacity + GrowStep(a.growBy, a.size);
        if (capacity <= 8) capacity = 8;
        auto* data = static_cast<LightningPoint*>(AllocPoints16(capacity));
        std::memcpy(data, a.data, static_cast<std::size_t>(a.size) * 16);
        gamecrt::operator_delete(reinterpret_cast<void**>(a.data)[-1]);
        a.data = data;
        a.size = 8;
        a.capacity = capacity;
    }
}

// A segment step's length, summed in the original's order.
float LengthXZY(const Vector3& v) { return std::sqrt((v.x * v.x + v.z * v.z) + v.y * v.y); }

// A random texture offset: 0, 0.25, 0.5 or 0.75.
float RandomOffset() {
    return static_cast<float>(static_cast<int>(Rng()->Next() & 3)) * GameFloat(0x0073C3B0);
}

} // namespace

// 00467590. `position` the glow's centre, `size` its radius; the frame argument the caller computes is not
// used (the frame comes from the position and the world's counter).
void __fastcall DrawLightningGlow(CLightning* self, void*, void* device, const Vector3* position, int /*frame: not used*/, float size) {
    if (!(GlowGuards() & 1)) {
        GlowGuards() |= 1;
        SpriteArrayConstruct(&Sprites());
        GameAtexit(reinterpret_cast<void (__cdecl*)()>(0x00467A10));
    }
    if (!(GlowGuards() & 2)) {
        GlowGuards() |= 2;
        LightningPointArray& p = GlowPoints();
        p.data = nullptr;
        p.growBy = 0;
        p.capacity = 0;
        p.size = 0;
        GameAtexit(reinterpret_cast<void (__cdecl*)()>(0x004679F0));
    }
    auto** holder = reinterpret_cast<void**>(&self->glowTexture_);
    EnsureTextureLoaded(holder);
    if (!ei::VCall<int>(device, kSetTexture, 0, *holder)) return;

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
    Vector3 towards{camera.x - position->x, camera.y - position->y, camera.z - position->z};
    float length = std::sqrt(towards.z * towards.z + towards.y * towards.y + towards.x * towards.x);
    if (length > 0.0f || length < 0.0f) {
        towards.x /= length;
        towards.y /= length;
        towards.z /= length;
    }
    float scaled = position->x * GameFloat(0x0073C518);   // 15
    int shift = x87::fistp(scaled);
    int frame = (static_cast<int*>(WorldOf(self))[0x64 / 4] + shift) % 15;
    for (int j = 0; j < 8; ++j) {
        float t = static_cast<float>(j) * GameFloat(0x0073C520);   // 1/8
        Vector3 along{towards.x * t * size, towards.y * t * size, towards.z * t * size};
        LightningPoint& p = GlowPoints().data[j];
        p.x = position->x + along.x;
        p.y = position->y + along.y;
        p.z = position->z + along.z;
        p.w = std::sqrt(1.0f - t * t) * size;
        SpriteRecord& s = Sprites().data[j];
        s.colour = 0xA0FFFFFFu;
        s.rect = frames[frame % 15];
        frame += 2;
    }
    ei::VCall<int>(device, kDrawSprites, 0, &GlowPoints(), &Sprites());
}
EI_REPLACE(0x00467590, DrawLightningGlow);

// 00467AD0. 0 when the texture cannot be set; 1 otherwise (also when the bolt is not visible).
int __fastcall DrawLightning(CLightning* self, void*, void* device) {
    auto** holder = reinterpret_cast<void**>(&self->texture_);
    EnsureTextureLoaded(holder);
    if (!ei::VCall<int>(device, kSetTexture, 0, *holder)) return 0;
    int visibility = ClipTest(static_cast<void**>(device)[2], nullptr, &self->boundsKind_);
    if (visibility == 2) return 1;
    const float zero = GameFloat(0x0073B858);
    if (self->power_ >= zero) self->Animate();
    const float slant = GameFloat(0x0073C3B0);   // 0.25
    if (!(self->power_ >= zero)) {   // negative (or NaN): untextured lengthwise
        ei::VCall<int>(device, kDrawStrip, &self->main_.points, self->main_.width, visibility, 0.0f, 0.0f, slant);
        for (LightningSegment& b : self->branches_) ei::VCall<int>(device, kDrawStrip, &b.points, b.width, visibility, 0.0f, 0.0f, slant);
    } else {
        float offset = RandomOffset();
        float span = LengthXZY(self->main_.step) * static_cast<float>(self->main_.points.size) * GameFloat(0x0073C4FC);   // 0.6
        ei::VCall<int>(device, kDrawStrip, &self->main_.points, self->main_.width, visibility, span, offset, slant);
        for (LightningSegment& b : self->branches_) {
            offset = RandomOffset();
            span = LengthXZY(b.step) * static_cast<float>(b.points.size) * GameFloat(0x0073C4FC);
            ei::VCall<int>(device, kDrawStrip, &b.points, b.width, visibility, span, offset, slant);
        }
    }

    float count = static_cast<float>(self->main_.points.size);
    Vector3 start = self->main_.start;
    Vector3 run{count * self->main_.step.x, count * self->main_.step.y, count * self->main_.step.z};
    Vector3 end{start.x + run.x, start.y + run.y, start.z + run.z};
    int frameCounter = static_cast<int*>(WorldOf(self))[0x64 / 4];
    float dx = start.x - end.x, dy = start.y - end.y, dz = start.z - end.z;
    float size = std::sqrt(dz * dz + dy * dy + dx * dx) * GameFloat(0x0073C528);   // 0.11
    int phase = (frameCounter / 2) % 15;
    if (self->power_ > zero) {
        // Clamped to [0.5, 1.2] with the original's selects (NaN keeps the size).
        float low = 0.5f, high = 1.2f;
        size = !(low >= size) ? size : low;
        size = !(size >= high) ? size : high;
    }
    if (self->power_ >= zero) DrawGlowAt(self, nullptr, device, &end, phase, size);
    if (!(self->power_ < zero) && !(self->power_ > zero)) DrawGlowAt(self, nullptr, device, &start, phase, size);   // zero (or NaN)
    return 1;
}
EI_REPLACE(0x00467AD0, DrawLightning);
