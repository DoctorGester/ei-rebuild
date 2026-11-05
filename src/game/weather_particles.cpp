// The rain's and the snow's draw (the world's rain / snow layer; effects_weather.cpp calls it in the effects pass):
// the field of weather_field.h placed around the view, each particle it shows projected with the pipeline's clip
// stack and built on the screen, so that it has the same shape in pixels at any window's aspect. A raindrop is a
// streak from where it was a moment ago to where it is (its motion over kStreakTicks, as a camera's shutter sees
// it), a snowflake a soft round dot; both are fans of vertex colours over the white texture, opaque along the middle
// and clear at the rim, at least a pixel or so wide at any resolution (thinner ones fainter instead). Their colour is
// the scene's light. Drawn as pre-transformed triangles with the effects pass's alpha blend, depth tested.
// docs/notes/renderer.md, section 12, "Weather".
#include "camera.h"
#include "clip.h"
#include "effects.h"
#include "model.h"
#include "render/render.h"
#include "texture.h"
#include "weather_field.h"
#include "world.h"

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
// Snow: the flake's alpha at its middle, the least radius and the most in 1080p pixels.
constexpr float kSnowAlpha = 0.95f;
constexpr float kLeastFlakeRadius = 1.6f;
constexpr float kMostFlakeRadius = 10.0f;
// The flake's shape: an inner ring of kFlakeRim corners at kFlakeCore of the radius, nearly as opaque as the middle
// (kCoreAlpha), and the clear rim: a disc with a soft edge.
constexpr int kFlakeRim = 12;
constexpr float kFlakeCore = 0.55f;
constexpr float kCoreAlpha = 0.85f;

// The scene's light on a particle: the ambient light and part of the sun's.
constexpr float kSunShare = 0.6f;
// The least light (a night's particles are still seen a little).
constexpr float kLeastLight = 0.12f;

// A batch's vertices at most (16-bit indices).
constexpr int kMostVertices = 0xFFFF;

std::uint32_t Colour(float alpha, float r, float g, float b) {
    auto byte = [](float v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
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

// The flake's rim: the corners' directions.
constexpr float kRimCos[kFlakeRim] = {1.0f,  0.8660254f,  0.5f,  0.0f, -0.5f, -0.8660254f,
                                      -1.0f, -0.8660254f, -0.5f, 0.0f, 0.5f,  0.8660254f};
constexpr float kRimSin[kFlakeRim] = {0.0f, 0.5f,  0.8660254f,  1.0f,  0.8660254f,  0.5f,
                                      0.0f, -0.5f, -0.8660254f, -1.0f, -0.8660254f, -0.5f};

TLVertex Corner(float x, float y, const Vector4& screen, std::uint32_t colour) {
    TLVertex v;
    v.x = x;
    v.y = y;
    v.z = screen.z;
    v.rhw = screen.w;
    v.color = colour;
    v.specular = 0xFF000000u;   // not fogged until FogVertices lowers it
    return v;
}

} // namespace

void RainSnow::Flush(SceneRenderer* renderer) {
    if (vertices.empty()) return;
    if (render::WeatherParticlesDrawn())
        renderer->device->DrawTlTriangles(vertices.data(), lib::Count(vertices), indices.data(), lib::Count(indices));
    vertices.clear();
    indices.clear();
}

void RainSnow::DrawParticles(SceneRenderer* renderer, weather::Kind kind) {
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

    vertices.clear();
    indices.clear();
    const int count = weather::Count(kind);
    const int perParticle = kind == weather::Kind::Rain ? 6 : 2 * kFlakeRim + 1;
    for (int i = 0; i < count; ++i) {
        const weather::Particle p = weather::Place(kind, i, view);
        if (p.alpha < kLeastAlpha) continue;
        if (lib::Count(vertices) + perParticle > kMostVertices) Flush(renderer);
        const auto base = static_cast<std::uint16_t>(vertices.size());
        if (kind == weather::Kind::Rain) {
            const Vector3 tail = p.position - p.velocity * kStreakTicks;
            const Vector4 headClip = projector.Clip(p.position), tailClip = projector.Clip(tail);
            if (!Projector::Ahead(headClip) || !Projector::Ahead(tailClip)) continue;
            if (!Projector::Inside(headClip) && !Projector::Inside(tailClip)) continue;
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
            const Vector4 centre = projector.Screen(clipped);
            const float radius = p.size * pixelsPerUnit / clipped.w;
            const float least = kLeastFlakeRadius * pixel;
            const float drawn = std::min(std::max(radius, least), kMostFlakeRadius * pixel);
            const float cover = std::min(1.0f, radius / least);
            const float alpha = p.alpha * kSnowAlpha * cover * cover;
            if (alpha < kLeastAlpha) continue;
            const float r = lightR * p.shade, g = lightG * p.shade, b = lightB * p.shade;
            vertices.push_back(Corner(centre.x, centre.y, centre, Colour(alpha, r, g, b)));
            const std::uint32_t core = Colour(alpha * kCoreAlpha, r, g, b), clear = Colour(0.0f, r, g, b);
            const float inner = drawn * kFlakeCore;
            for (int k = 0; k < kFlakeRim; ++k)
                vertices.push_back(Corner(centre.x + inner * kRimCos[k], centre.y + inner * kRimSin[k], centre, core));
            for (int k = 0; k < kFlakeRim; ++k)
                vertices.push_back(Corner(centre.x + drawn * kRimCos[k], centre.y + drawn * kRimSin[k], centre, clear));
            // The middle's fan, then the band from the inner ring to the rim, clockwise as the rain's.
            for (int k = 0; k < kFlakeRim; ++k) {
                const int next = (k + 1) % kFlakeRim;
                const auto in = [&](int corner) { return static_cast<std::uint16_t>(base + 1 + corner); };
                const auto out = [&](int corner) { return static_cast<std::uint16_t>(base + 1 + kFlakeRim + corner); };
                for (const std::uint16_t index :
                     {base, in(next), in(k), in(k), in(next), out(next), in(k), out(next), out(k)})
                    indices.push_back(index);
            }
        }
    }
    Flush(renderer);
}
