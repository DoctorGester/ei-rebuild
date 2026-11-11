// The rain's and the snow's draw (the world's rain / snow layer; effects_weather.cpp calls it in the effects pass):
// the field of weather_field.h placed around the view, each particle it shows projected with the pipeline's clip
// stack and built on the screen, so that it has the same shape in pixels at any window's aspect. The field's bins out
// of the view are passed over whole. A raindrop is a streak from where it was a moment ago to where it is (its
// motion over kStreakTicks, as a camera's shutter sees it), a fan of vertex colours over the white texture, opaque
// along the middle and clear at the rim; a snowflake a square of one of the "RainSnow" texture's four flake pictures
// (its snow row), turned by the flake's spin. Both at least a pixel or so wide at any resolution (thinner ones fainter
// instead), their colour the scene's light. Drawn as pre-transformed triangles with the effects pass's alpha blend,
// depth tested. docs/notes/renderer.md, section 12, "Weather".
#include "game/world/camera.h"
#include "engine/scene/clip.h"
#include "engine/scene/device.h"
#include "game/effects/effects.h"
#include "engine/scene/scene_renderer.h"
#include "engine/scene/pipeline.h"
#include "render/render.h"
#include "render/render_checks.h"
#include "game/effects/weather_field.h"
#include "game/world/world.h"

#include "lib/containers.h"
#include "lib/maths.h"
#include "lib/scales.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

// The field's box: ahead of the camera's focus along the view (most of what is seen lies beyond it), its floor
// kBelowFocus under the focus (ground lower than that is far below the view).
constexpr float kAhead = 10.0f;
constexpr float kBelowFocus = 8.0f;
// A world frame later than this many ticks after the last one starts the wind's carry afresh (a load, a long pause).
constexpr double kCarryGap = 100.0;

// A particle whose alpha is below this is not drawn.
constexpr float kLeastAlpha = lib::kByteScale;
// The nearest a particle is drawn at (clip w, about the distance along the view).
constexpr float kNearestW = 0.3f;
// How far past the screen's edges (a share of the half size) a particle's point may lie and still be drawn.
constexpr float kEdgeMargin = 1.1f;

// Rain: the streak's length in ticks of the drop's motion, its alpha at the head and the tail, its colour in the
// scene's light, the least and the most half width in 1080p pixels (clear at the edges, it shows about that wide).
constexpr float kStreakTicks = 1.6f;
constexpr float kRainAlpha = 0.45f;
constexpr float kTailAlpha = 0.1f;
constexpr float kRainRed = 0.78f, kRainGreen = 0.84f, kRainBlue = 0.95f;
constexpr float kLeastStreakWidth = 1.2f;
constexpr float kMostStreakWidth = 5.0f;
// The most a drop falls in a tick (the field's fastest, 0.575, and some): with the wind, how far a bin's streaks
// reach out of it.
constexpr float kMostRainFall = 0.6f;
// Snow: the flake's alpha, the least and the most half side of its square in 1080p pixels, and the half side against
// the flake's radius (the pictures fill about 0.6 of their squares).
constexpr float kSnowAlpha = 1.0f;
constexpr float kLeastFlakeHalf = 4.5f;
constexpr float kMostFlakeHalf = 16.0f;
constexpr float kFlakeHalf = 1.7f;
// The height over the ground a flake fades out over as it lands.
constexpr float kLanding = 0.5f;
// The ground's height taken without a landscape: far under every particle.
constexpr float kNoGround = 1e6f;
// The ground's heights kept (RainSnow::ground): a square of this many whole units a side (a power of two), more than
// the field's box spans.
constexpr int kGroundSide = 128;
// The texture's snow row: four frames a quarter of its width wide from kSnowTop, each a square, kept half a texel of
// the 128-texel picture inside (the rows above and below hold other pictures).
constexpr float kFrameWidth = 0.25f;
constexpr float kSnowTop = 0.5f;
constexpr float kTexelInset = 0.5f / 128.0f;

// The scene's light on a particle: the ambient light and part of the sun's.
constexpr float kSunShare = 0.6f;
// The least light (a night's particles are still seen a little).
constexpr float kLeastLight = 0.12f;

// A batch's vertices at most (16-bit indices).
constexpr int kMostVertices = 0xFFFF;

std::uint32_t Colour(float alpha, float r, float g, float b) {
    auto byte = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return byte(alpha) << 24 | byte(r) << 16 | byte(g) << 8 | byte(b);
}

// The pipeline's projection of world points to the screen.
struct Projector {
    const TransformPipeline& pipeline;
    const Matrix4& clip;

    // Whether a clip position is far enough from the eye.
    static bool Ahead(const Vector4& clipped) { return clipped.w > kNearestW; }
    // Whether it is on the screen (with the margin) and in the depth range.
    static bool Inside(const Vector4& clipped) {
        const float reach = clipped.w * kEdgeMargin;
        return clipped.x >= -reach && clipped.x <= reach && clipped.y >= -reach && clipped.y <= reach &&
               clipped.z >= 0.0f && clipped.z <= clipped.w;
    }
    Vector4 Clip(const Vector3& point) const { return ToClip(clip, point); }
    Vector4 Screen(const Vector4& clipped) const { return pipeline.ToScreen(clipped); }
};

// Ahead and Inside as seven planes in the world: each test is a half-space of the clip position, which is affine in
// the world point, so each is `offset` + Dot(normal, point - origin) >= 0 (taken from the projection at the origin
// and a unit step along each axis from it). A box or a sphere outside one plane has no point that passes.
class Frustum {
public:
    Frustum(const Projector& projector, const Vector3& origin) : origin(origin) {
        const Vector4 at = projector.Clip(origin);
        const Vector4 step[3] = {projector.Clip(origin + Vector3{1.0f, 0.0f, 0.0f}),
                                 projector.Clip(origin + Vector3{0.0f, 1.0f, 0.0f}),
                                 projector.Clip(origin + Vector3{0.0f, 0.0f, 1.0f})};
        // Each test's weights of the clip position's x, y, z, w, and its constant.
        struct Test {
            Vector3 xyz;
            float w = 0.0f;
            float constant = 0.0f;
        };
        const Test tests[kPlanes] = {{{1.0f, 0.0f, 0.0f}, kEdgeMargin}, {{-1.0f, 0.0f, 0.0f}, kEdgeMargin},
                                     {{0.0f, 1.0f, 0.0f}, kEdgeMargin}, {{0.0f, -1.0f, 0.0f}, kEdgeMargin},
                                     {{0.0f, 0.0f, 1.0f}, 0.0f},        {{0.0f, 0.0f, -1.0f}, 1.0f},
                                     {{0.0f, 0.0f, 0.0f}, 1.0f, -kNearestW}};
        for (int k = 0; k < kPlanes; ++k) {
            const Test& t = tests[k];
            auto weigh = [&t](const Vector4& c) { return Dot(t.xyz, Vector3{c.x, c.y, c.z}) + t.w * c.w; };
            offset[k] = weigh(at) + t.constant;
            normal[k] = {weigh(step[0]) - weigh(at), weigh(step[1]) - weigh(at), weigh(step[2]) - weigh(at)};
            length[k] = Length(normal[k]);
        }
    }
    // Whether a point within `radius` of `point` may pass.
    bool MaySee(const Vector3& point, float radius) const {
        const Vector3 d = point - origin;
        for (int k = 0; k < kPlanes; ++k)
            if (offset[k] + Dot(normal[k], d) < -radius * length[k]) return false;
        return true;
    }
    // Whether a point of `box` may pass: its corner furthest along each plane's normal does.
    bool MaySee(const weather::Bounds& box) const {
        const Vector3 low = box.low - origin, high = box.high - origin;
        for (int k = 0; k < kPlanes; ++k) {
            const Vector3& n = normal[k];
            const Vector3 corner = {n.x > 0.0f ? high.x : low.x, n.y > 0.0f ? high.y : low.y,
                                    n.z > 0.0f ? high.z : low.z};
            if (offset[k] + Dot(n, corner) < 0.0f) return false;
        }
        return true;
    }

private:
    static constexpr int kPlanes = 7;
    Vector3 origin;
    Vector3 normal[kPlanes];
    float offset[kPlanes] = {};
    float length[kPlanes] = {};
};

TLVertex Corner(float x, float y, const Vector4& screen, std::uint32_t colour, float u = 0.0f, float v = 0.0f) {
    TLVertex vertex;
    vertex.x = x;
    vertex.y = y;
    vertex.z = screen.z;
    vertex.rhw = screen.w;
    vertex.color = colour;
    vertex.specular = 0xFF000000u;   // not fogged until FogVertices lowers it
    vertex.u = u;
    vertex.v = v;
    return vertex;
}

} // namespace

float RainSnow::GroundAt(float x, float y) {
    if (groundOf != world->terrain.get()) {
        ground.assign(kGroundSide * kGroundSide, GroundPoint{});
        groundOf = world->terrain.get();
    }
    const float floorX = std::floor(x), floorY = std::floor(y);
    const int unitX = static_cast<int>(floorX), unitY = static_cast<int>(floorY);
    auto at = [&](int px, int py) {
        const int slot = (py & (kGroundSide - 1)) * kGroundSide + (px & (kGroundSide - 1));
        GroundPoint& point = ground[static_cast<std::size_t>(slot)];
        if (point.x != px || point.y != py)
            point = {px, py, world->RawSurfaceHeightAt(static_cast<float>(px), static_cast<float>(py))};
        return point.height;
    };
    const float fx = x - floorX, fy = y - floorY;
    const float front = Lerp(at(unitX, unitY), at(unitX + 1, unitY), fx);
    const float back = Lerp(at(unitX, unitY + 1), at(unitX + 1, unitY + 1), fx);
    return Lerp(front, back, fy);
}

void RainSnow::Flush(SceneRenderer* renderer) {
    if (vertices.empty()) return;
    flushed += lib::Count(vertices);
    if (render::WeatherParticlesDrawn())
        renderer->device->DrawTlTriangles(vertices.data(), lib::Count(vertices), indices.data(), lib::Count(indices));
    vertices.clear();
    indices.clear();
}

void RainSnow::DrawParticles(SceneRenderer* renderer, weather::Kind kind) {
    render::BeginWeatherTime();
    flushed = 0;
    const WorldClient* client = world;
    const double time = client->tick + static_cast<double>(client->tickFraction);
    if (carrying && time >= carriedTo && time - carriedTo <= kCarryGap)
        carry = weather::Carry(carry, wind, time - carriedTo);
    carrying = true;
    carriedTo = time;

    const camera::ViewPose& pose = camera::FrameView();
    weather::FieldView view;
    view.time = time;
    view.eye = pose.position;
    view.carry = carry;
    view.wind = wind;
    view.density = density / 64.0f;
    Vector3 ahead = {pose.focus.x - pose.position.x, pose.focus.y - pose.position.y, 0.0f};
    const float aheadLength = Length(ahead);
    ahead = aheadLength > 1e-3f ? ahead * (kAhead / aheadLength) : Vector3{};
    view.centre = {pose.focus.x + ahead.x, pose.focus.y + ahead.y,
                   pose.focus.z - kBelowFocus + 0.5f * weather::kBoxHeight};

    const TransformPipeline& pipeline = *renderer->pipeline;
    const Projector projector{pipeline, *pipeline.Top(TransformPipeline::kClipStack)};
    // The pixels a unit across the view covers at clip w 1, from a unit step sideways at the focus.
    float pixelsPerUnit = 0.0f;
    {
        const Vector3 along = pose.focus - pose.position;
        const Vector3 side =
            Length(along) > 1e-3f ? Normalised(Vector3{-along.y, along.x, 0.0f}) : Vector3{1.0f, 0.0f, 0.0f};
        const Vector4 a = projector.Clip(pose.focus), b = projector.Clip(pose.focus + side);
        if (Projector::Ahead(a) && Projector::Ahead(b)) {
            const Vector4 sa = projector.Screen(a), sb = projector.Screen(b);
            pixelsPerUnit = lib::Hypot(sb.x - sa.x, sb.y - sa.y) * a.w;
        }
    }
    const float pixel = pipeline.halfHeight / 540.0f;   // a 1080p pixel

    const LightColour& ambient = client->frameAmbient;
    const LightColour& sun = client->frameSun;
    auto light = [&](float a, float s) { return std::clamp(a + kSunShare * s, kLeastLight, 1.0f); };
    const float lightR = light(ambient.r, sun.r), lightG = light(ambient.g, sun.g), lightB = light(ambient.b, sun.b);

    const bool rain = kind == weather::Kind::Rain;
    const bool landscape = world->terrain != nullptr;
    // How far a particle's drawing reaches out of its bin: the rain's streak behind it. How far from its anchor the
    // point it is drawn by can be: the streak's reach or the snow's drift.
    const float margin = rain ? kStreakTicks * (Length(wind) + kMostRainFall) : 0.0f;
    const float reach = rain ? margin : weather::kMostDrift;
    const Frustum frustum(projector, view.centre);
    const weather::Field field(kind, view);
    const int perParticle = rain ? 6 : 4;
    vertices.clear();
    indices.clear();
    for (int bin = 0; bin < weather::kBins; ++bin) {
        const weather::BinRange range = field.Bin(bin);
        if (range.first == range.end) continue;
        weather::Bounds boxes[8];
        const int boxCount = field.BinBounds(bin, margin, boxes);
        if (std::none_of(boxes, boxes + boxCount, [&](const weather::Bounds& box) { return frustum.MaySee(box); }))
            continue;
        for (int i = range.first; i < range.end; ++i) {
            Vector3 anchor;
            if (!field.Anchor(i, &anchor) || !frustum.MaySee(anchor, reach)) continue;
            weather::Particle p = field.Place(i);
            if (p.alpha < kLeastAlpha) continue;
            if (lib::Count(vertices) + perParticle > kMostVertices) Flush(renderer);
            const auto base = static_cast<std::uint16_t>(vertices.size());
            if (rain) {
                const Vector3 tail = p.position - p.velocity * kStreakTicks;
                const Vector4 headClip = projector.Clip(p.position), tailClip = projector.Clip(tail);
                if (!Projector::Ahead(headClip) || !Projector::Ahead(tailClip)) continue;
                if (!Projector::Inside(headClip) && !Projector::Inside(tailClip)) continue;
                // The ground or water under it (the terrain's, not the roofs'): a drop whose streak is all under it
                // is not drawn.
                const float surface = landscape ? GroundAt(p.position.x, p.position.y) : -kNoGround;
                if (p.position.z - surface + p.velocity.z * -kStreakTicks < 0.0f) continue;
                const Vector4 head = projector.Screen(headClip), from = projector.Screen(tailClip);
                float dx = head.x - from.x, dy = head.y - from.y;
                const float length = lib::Hypot(dx, dy);
                if (length > 1e-3f) {
                    dx /= length;
                    dy /= length;
                } else {
                    dx = 0.0f;
                    dy = 1.0f;
                }
                const float width = p.size * pixelsPerUnit / headClip.w;
                const float least = kLeastStreakWidth * pixel;
                const float half = std::min(std::max(width, least), kMostStreakWidth * pixel);
                const float alpha = p.alpha * kRainAlpha * std::min(1.0f, width / least);
                if (alpha < kLeastAlpha) continue;
                const float r = lightR * kRainRed * p.shade, g = lightG * kRainGreen * p.shade,
                            b = lightB * kRainBlue * p.shade;
                const std::uint32_t clear = Colour(0.0f, r, g, b);
                const float nx = -dy * half, ny = dx * half;
                vertices.push_back(Corner(from.x + nx, from.y + ny, from, clear));
                vertices.push_back(Corner(from.x, from.y, from, Colour(alpha * kTailAlpha, r, g, b)));
                vertices.push_back(Corner(from.x - nx, from.y - ny, from, clear));
                vertices.push_back(Corner(head.x + nx, head.y + ny, head, clear));
                vertices.push_back(Corner(head.x, head.y, head, Colour(alpha, r, g, b)));
                vertices.push_back(Corner(head.x - nx, head.y - ny, head, clear));
                // Clockwise on the screen (the effects pass culls the other way).
                for (const int corner : {0, 4, 1, 0, 3, 4, 1, 5, 2, 1, 4, 5})
                    indices.push_back(static_cast<std::uint16_t>(base + corner));
            } else {
                const Vector4 clipped = projector.Clip(p.position);
                if (!Projector::Ahead(clipped) || !Projector::Inside(clipped)) continue;
                const float half = kFlakeHalf * p.size * pixelsPerUnit / clipped.w;
                const float least = kLeastFlakeHalf * pixel;
                const float drawn = std::min(std::max(half, least), kMostFlakeHalf * pixel);
                const float cover = std::min(1.0f, half / least);
                float alpha = p.alpha * kSnowAlpha * cover * cover;
                if (alpha < kLeastAlpha) continue;
                // The ground or water under it: a flake fades out over its last kLanding units as it lands (the
                // depth test would cut it in half).
                if (landscape) {
                    const float above = p.position.z - GroundAt(p.position.x, p.position.y);
                    alpha *= std::clamp(above / kLanding, 0.0f, 1.0f);
                    if (alpha < kLeastAlpha) continue;
                }
                const Vector4 centre = projector.Screen(clipped);
                const std::uint32_t colour = Colour(alpha, lightR * p.shade, lightG * p.shade, lightB * p.shade);
                // The square's half axes on the screen, turned by the spin.
                const float ax = drawn * p.spinCos, ay = drawn * p.spinSin;
                const float left = p.frame * kFrameWidth + kTexelInset, right = left + kFrameWidth - 2.0f * kTexelInset;
                const float top = kSnowTop + kTexelInset, bottom = top + kFrameWidth - 2.0f * kTexelInset;
                vertices.push_back(Corner(centre.x - ax + ay, centre.y - ay - ax, centre, colour, left, top));
                vertices.push_back(Corner(centre.x + ax + ay, centre.y + ay - ax, centre, colour, right, top));
                vertices.push_back(Corner(centre.x + ax - ay, centre.y + ay + ax, centre, colour, right, bottom));
                vertices.push_back(Corner(centre.x - ax - ay, centre.y - ay + ax, centre, colour, left, bottom));
                // Turned the same way as the rain's on the screen.
                for (const int corner : {0, 2, 1, 0, 3, 2}) indices.push_back(static_cast<std::uint16_t>(base + corner));
            }
        }
    }
    Flush(renderer);
    render::EndWeatherTime(flushed);
}
