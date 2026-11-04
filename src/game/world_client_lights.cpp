// The client world's light by the time of day (UpdateLighting) and the zone exit
// markers' colour (UpdateExitMarkers). See world.h and docs/notes/world.md.
#include "effects.h"

#include "figure_mesh.h"   // VertexPacket, MeshCorner
#include "ground_decal.h"
#include "link_figure.h"
#include "map_object.h"           // MapObject (ObjectWithId)
#include "map_object_figures.h"   // Object3D
#include "model.h"
#include "object_tables.h"
#include "party.h"
#include "sky_object.h"
#include "spell.h"
#include "terrain.h"
#include "world.h"
#include "world_data.h"
#include "lib/containers.h"
#include "lib/maths.h"
#include "lib/vecmath.h"
#include "render/render.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr float kHoursPerDay = 24.0f;
constexpr float kAmbientFromSun = 0.8f;          // a black ambient entry takes 0.8 x the sunlight
constexpr int kHours = 24;
// The fog start: the day's value from 6 to 18, the night's before 2 and from 22, linear between.
constexpr float kDawnStart = 2.0f, kDayStart = 6.0f;
constexpr float kDayEnd = 18.0f, kNightStart = 22.0f;
constexpr float kDawnLength = 4.0f, kDuskLength = 4.0f;
// The sun's elevation (degrees): 30 at hours 4 and 20, 90 at noon (a tent in between), and from
// 30 to 75 over the night (falling from hour 0 to 4, rising from 20 to 24).
constexpr float kSunrise = 4.0f, kSunset = 20.0f;
constexpr float kDayLength = 24.0f;
constexpr float kNightSpan = 45.0f, kBaseElevation = 30.0f;
constexpr float kPerDegree = 1.0f / 180.0f;
constexpr float kRadiansPerDegree = kPi * kPerDegree;
constexpr float kPerHour = 0.041666668f;   // 1/24 (the sun turns once a day)
// A cave's sun: (0.5, 0.5, -0.70710677).
const Vector3 kCaveSun = {0.5f, 0.5f, -0.707106f};

// The hour (floored) the terrain's sun was last set for; -1 makes the next frame set it.
using worlddata::g_lastLitHour;

// Terrain::SetSun takes the ambient colour as a Vector4 (and copies it).
Vector4 Ambient(const LightColour& c) { return {c.r, c.g, c.b, c.a}; }

// The original's test counts a NaN as zero; the audit checks that none arrives.
bool IsZero(float value) {
    return value == 0.0f;
}
bool IsBlack(const LightColour& c) { return IsZero(c.r) && IsZero(c.g) && IsZero(c.b); }

// (b - a) * t + a, stored as a float.
float Lerp(float from, float to, float t) {
    float value = (to - from) * t + from;
    return value;
}
LightColour Lerp(const LightColour& from, const LightColour& to, float t) {
    LightColour c;
    c.r = Lerp(from.r, to.r, t);
    c.g = Lerp(from.g, to.g, t);
    c.b = Lerp(from.b, to.b, t);
    c.a = 0.0f;
    return c;
}
float Scaled(float value, float scale) {
    float result = value * scale;
    return result;
}

float FogStart(float hour) {
    const float day = options::kFogDayStartDistance, night = options::kFogNightStartDistance;
    float fog;
    if (hour >= kDayStart && hour <= kDayEnd) fog = day;
    else if (hour < kNightStart && hour > kDawnStart) {
        if (hour < kDayStart) fog = (hour - kDawnStart) / kDawnLength * (day - night) + night;
        else fog = day - (hour - kDayEnd) / kDuskLength * (day - night);
    } else fog = night;
    return fog;
}

// In degrees.
float SunElevation(float hour) {
    float night;
    if (hour < kSunrise) {
        if (hour <= kSunset) night = (kSunrise - hour) / kSunrise;
        else night = (hour - kSunset) / (kDayLength - kSunset);
    } else if (hour > kSunset) {
        night = (hour - kSunset) / (kDayLength - kSunset);
    } else {
        float offset = hour - (kSunset + kSunrise) * 0.5f;
        const float distance = std::fabs(offset);
        const float t = distance / (kSunset - kSunrise);
        return ((1.0f - (t + t)) + 1.0f) * kBaseElevation;
    }
    return night * kNightSpan + kBaseElevation;
}

// The time of day in hours (fmod of the game time by 24), at the drawn frame: the tick plus the part of the next tick
// the client has had (tickFraction, as the units' animation), so the sun turns a little every frame and the shadows
// glide instead of stepping at each tick.
float HourOfDay(const WorldClient* world) {
    const float hours = world->tick * world->secondsPerTick + world->tickFraction * world->secondsPerTick;
    return std::fmod(hours + world->timeBase, kHoursPerDay);
}

// The hour's row of the colour tables: the hour truncated, 0 for NaN.
int HourIndex(float hour) { return std::isnan(hour) ? 0 : static_cast<int>(hour); }

// The cloud shadows' and the surfaces' weather (render::CloudShadows), read from the client's state only: by day (from
// none at the sunrise to full at the day's start, back to none from the day's end to the sunset; none in a cave), the
// rain's or snow's strength as its layer fades it and which it is, the wind and the world's time at the drawn frame.
void HandOverClouds(const WorldClient* world, float hour) {
    render::CloudWeather w;
    if (!world->cave && !std::isnan(hour)) {
        if (hour <= kSunrise || hour >= kSunset) w.day = 0.0f;
        else if (hour < kDayStart) w.day = (hour - kSunrise) / (kDayStart - kSunrise);
        else if (hour > kDayEnd) w.day = (kSunset - hour) / (kSunset - kDayEnd);
        else w.day = 1.0f;
    }
    const RainSnow& layer = world->rainSnow;
    w.rain = layer.mode != 0 ? static_cast<float>(layer.density) / 64.0f : 0.0f;
    w.snow = layer.mode == 2;
    w.windX = layer.wind.x;
    w.windY = layer.wind.y;
    w.time = static_cast<double>(world->tick) + static_cast<double>(world->tickFraction);
    render::CloudShadows(w);
}

} // namespace

int WorldClient::UpdateLighting() {
    auto* display = &g_sceneRenderer;
    if (!(display->flags & 1)) return 1;
    UpdateExitMarkers();
    const float now = HourOfDay(this);
    HandOverClouds(this, now);
    TransformPipeline* pipeline = display->pipeline;

    if (cave == 1) {
        const float hour = now;
        if (g_lastLitHour > hour) g_lastLitHour = -1.0f;
        pipeline->SetDepthRange(options::kFogDayStartDistance, options::g_options.farClipDistance);
        const LightColour sun = sunlight[0];
        frameSun.r = sun.r;
        frameSun.g = sun.g;
        frameSun.b = sun.b;
        if (IsBlack(ambient[0])) {
            frameAmbient.r = Scaled(sun.r, kAmbientFromSun);
            frameAmbient.g = Scaled(sun.g, kAmbientFromSun);
            frameAmbient.b = Scaled(sun.b, kAmbientFromSun);
        } else {
            frameAmbient.r = ambient[0].r;     // the table's hour 0
            frameAmbient.g = ambient[0].g;
            frameAmbient.b = ambient[0].b;
        }
        Light* light = display->sun;
        light->color[0] = sun.r;
        light->color[1] = sun.g;
        light->color[2] = sun.b;
        light->direction = kCaveSun;
        display->SetSunAndAmbient(light, frameAmbient.r, frameAmbient.g, frameAmbient.b);
        const LightColour& skyColour = sky[0];
        display->SetFogColour(skyColour.r, skyColour.g, skyColour.b);
        if (skyDome) {
            const SkyColour black = {0.0f, 0.0f, 0.0f, 1.0f};
            skyDome->ApplyLight(kCaveSun, black, black);
        }
        if (hour - g_lastLitHour < 1.0f) return 0;
        g_lastLitHour = std::floor(hour);
        if (terrain) {
            const Vector4 ambientColour = Ambient(frameAmbient);
            terrain->SetSun(light, &ambientColour);
        }
        return 1;
    }

    const float hour = now;
    const int index = HourIndex(now);
    pipeline->SetDepthRange(FogStart(hour), options::g_options.farClipDistance);
    if (g_lastLitHour > hour) g_lastLitHour = -1.0f;

    const float elevation = SunElevation(hour) * kRadiansPerDegree;
    const float turn = hour * kPerHour;
    const float azimuth = (turn + turn) * kPi;
    const float cosElevation = lib::Cos(elevation);
    Vector3 direction;
    {
        direction = {lib::Cos(azimuth) * cosElevation, lib::Sin(azimuth) * cosElevation, -lib::Sin(elevation)};
    }
    float whole;
    const float fraction = std::modf(hour, &whole);
    const int next = (index + 1) % kHours;

    const LightColour sun = Lerp(sunlight[index], sunlight[next], fraction);
    frameSun.r = sun.r;
    frameSun.g = sun.g;
    frameSun.b = sun.b;
    if (IsBlack(ambient[index])) {
        frameAmbient.r = Scaled(sun.r, kAmbientFromSun);
        frameAmbient.g = Scaled(sun.g, kAmbientFromSun);
        frameAmbient.b = Scaled(sun.b, kAmbientFromSun);
    } else {
        const LightColour ambientColour = Lerp(ambient[index], ambient[next], fraction);
        frameAmbient.r = ambientColour.r;
        frameAmbient.g = ambientColour.g;
        frameAmbient.b = ambientColour.b;
    }
    Light* light = display->sun;
    light->color[0] = sun.r;
    light->color[1] = sun.g;
    light->color[2] = sun.b;
    light->direction = direction;
    display->SetSunAndAmbient(light, frameAmbient.r, frameAmbient.g, frameAmbient.b);
    const LightColour skyColour = Lerp(sky[index], sky[next], fraction);
    display->SetFogColour(skyColour.r, skyColour.g, skyColour.b);
    if (skyDome) {
        const SkyColour ambientColour = {frameAmbient.r, frameAmbient.g, frameAmbient.b, frameAmbient.a};
        const SkyColour sunColour = {frameSun.r, frameSun.g, frameSun.b, frameSun.a};
        skyDome->ApplyLight(direction, ambientColour, sunColour);
    }
    if (hour - g_lastLitHour < 1.0f) return 0;
    g_lastLitHour = std::floor(hour);
    if (terrain) {
        const Vector4 ambientColour = Ambient(frameAmbient);
        terrain->SetSun(light, &ambientColour);
    }
    return 1;
}

void WorldClient::UpdateExitMarkers() {
    if (exits.size() != exitMarkers.size()) return;
    NamedFloatTable* variables = &session->GetParty()->variables;
    for (int i = 0; i < std::ssize(exits); ++i) {
        ParticleSource* marker = exitMarkers[i];
        if (!marker) continue;
        std::string name("z.");
        name += exits[i].target;
        const float visited = variables->Get(name);
        marker->extent = {visited == 1.0f ? 1.0f : 0.0f, 0.0f, 0.0f};
    }
}

// --- World client decals -----------------------------------------------------------------------------

// The client world's decal projector (ground prints). The decal builder (ground_decal.h) is a
// function-local static of the original game, as is the work array of transformed part points; both are made
// on first use and freed at exit. See docs/notes/models.md ("Ground prints").

namespace {

DecalBuilder g_builder;
std::vector<Vector4> g_partPoints;   // a figure part's points, reused by every decal

// A scene node's matrix (an object's or a figure part's) from its rotation and position (the
// bottom row 0, 0, 0, 1).
void BuildNodeMatrix(const Quaternion& rotation, const Vector3& position, Matrix4& matrix) {
    rotation.ToMatrix(matrix.m);
    float* m = matrix.m;
    m[11] = position.z;
    m[3] = position.x;
    m[7] = position.y;
    m[14] = 0.0f;
    m[13] = 0.0f;
    m[12] = 0.0f;
    m[15] = 1.0f;
}

// Pushes `m` onto the pipeline's decal stack: the product goes through a library call whose terms sum
// in another order than PushMatrix's.
void PushDecalMatrix(TransformPipeline* pipeline, const float* m) {
    MatrixStack& stack = pipeline->stacks[TransformPipeline::kDecalStack];
    if (stack.count == 0) {
        stack.count = 1;
        std::memcpy(stack.matrices.data(), m, sizeof(Matrix4));
        return;
    }
    ++stack.count;
    stack.matrices[stack.count - 1].Multiply(stack.matrices[stack.count - 2].m, m);
}

// The part's triangles (its figure's part of the same name), transformed by the pipeline's
// stack-14 top, added to the decal. As in the original, GetPartData's result is not checked: a missing
// part leaves the previous part's data in use (ours starts from none).
struct PartData {
    VertexPacket* vertices = nullptr;
    MeshCorner* corners = nullptr;
    std::uint16_t* indices = nullptr;
    int blocks = 0;
    int indexCount = 0;
};

void AddPartTriangles(TransformPipeline* pipeline, LinkMesh* figure, FigureMesh* part, PartData& data) {
    const Matrix4* top = pipeline->Top(TransformPipeline::kDecalStack);
    {
        std::string name(part->GetName());
        figure->GetPartData(&name, &data.vertices, &data.corners, &data.indices, &data.blocks, &data.indexCount);
    }
    std::vector<Vector4>& points = g_partPoints;
    points.resize(data.blocks * 4);
    for (int i = 0; i < data.blocks * 4; ++i) {
        const VertexPacket& packet = data.vertices[i >> 2];
        points[i] = ToClip(*top, {packet.block[0][i & 3], packet.block[1][i & 3], packet.block[2][i & 3]});
    }
    DecalBuilder& builder = g_builder;
    builder.Reserve(data.indexCount * 6);
    for (unsigned i = 0; i < static_cast<unsigned>(data.indexCount); i += 3) {
        const Vector4& a = points[data.corners[data.indices[i]].position];
        const Vector4& b = points[data.corners[data.indices[i + 1]].position];
        const Vector4& c = points[data.corners[data.indices[i + 2]].position];
        const Vector3 va{a.x, a.y, a.z};
        const Vector3 vb{b.x, b.y, b.z};
        const Vector3 vc{c.x, c.y, c.z};
        builder.AddTriangle(va, vb, vc);
    }
}

} // namespace

void WorldClient::ProjectDecal(const Vector3& direction, const Vector3& point, float depth,
                                const VertexPacket& quad, std::vector<VertexPacket>* points,
                                std::vector<TexCoord>* texCoords, std::vector<std::uint16_t>* indices) {
    render::BeginDecalTime();
    DecalBuilder& builder = g_builder;
    builder.Begin(points, texCoords, indices, quad, direction, point, depth);
    if (!platforms.Contains(point.x, point.y)) terrain->AddDecalTriangles(&builder, quad);

    TransformPipeline* pipeline = g_sceneRenderer.pipeline;
    PartData data;
    for (const Platform& face : platforms.list) {
        Object3D* object = ObjectWithId(face.id);
        if (!object) continue;
        // The object's bounding sphere about its origin, then about its centre.
        if (!builder.Touches(object->localPosition, Length(object->bounds.center) + object->bounds.radius))
            continue;
        BuildNodeMatrix(object->localRotation, object->localPosition, object->worldMatrix);
        Vector3 centre = TransformColumnNormal(object->worldMatrix, object->bounds.center) +
                         object->localPosition;
        if (!builder.Touches(centre, object->bounds.radius)) continue;
        PushDecalMatrix(pipeline, object->worldMatrix.m);
        LinkMesh* figure = object->figure;
        if (figure) {
            for (int i = 0; i < std::ssize(object->parts); ++i) {
                FigureMesh* part = object->parts[i];
                BuildNodeMatrix(part->localRotation, part->localPosition, part->worldMatrix);
                PushDecalMatrix(pipeline, part->worldMatrix.m);
                AddPartTriangles(pipeline, figure, part, data);
                pipeline->PopMatrix(TransformPipeline::kDecalStack);
            }
        }
        pipeline->PopMatrix(TransformPipeline::kDecalStack);
    }
    builder.Finish();
    render::EndDecalTime(lib::Count(*points) * 4);
}
