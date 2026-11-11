// The client world's light by the time of day (UpdateLighting) and the zone exit
// markers' colour (UpdateExitMarkers). See world.h and docs/notes/world.md.
#include "game/effects/effects.h"

#include "engine/models/figure_mesh.h"   // VertexPacket, MeshCorner
#include "game/app/singletons.h"
#include "game/terrain/ground_decal.h"
#include "engine/scene/light.h"
#include "engine/models/link_figure.h"
#include "game/objects/map_object.h"           // MapObject (ObjectWithId)
#include "game/objects/object3d.h"   // Object3D
#include "engine/scene/scene_renderer.h"
#include "engine/scene/pipeline.h"
#include "engine/core/named_floats.h"
#include "game/app/options.h"
#include "game/players/party.h"
#include "game/players/player.h"
#include "engine/scene/sky_object.h"
#include "game/spells/spell.h"
#include "game/terrain/terrain.h"
#include "game/world/world.h"
#include "lib/containers.h"
#include "lib/float_select.h"
#include "lib/maths.h"
#include "lib/vecmath.h"
#include "render/render.h"
#include "render/render_checks.h"
#include "render/render_state.h"

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
constexpr int kNoon = 12;                        // the daylight colours' hour (DaylightColours)
// The cloud shadows' and the distance fog's day: from 6 to 18; the fog's night before 2 and from 22.
constexpr float kDawnStart = 2.0f, kDayStart = 6.0f;
constexpr float kDayEnd = 18.0f, kNightStart = 22.0f;
// The distance fog (render::DistanceFog): the depth along the view it starts at and the depth where it hides half a
// colour, by day and (thicker) by night, linear between them at dawn and dusk; a cave has the day's.
struct FogRange {
    float start = 0.0f;
    float half = 0.0f;
};
constexpr FogRange kDayFog = {40.0f, 130.0f};
constexpr FogRange kNightFog = {25.0f, 90.0f};
constexpr float kHalfHidden = 0.83255461f;   // sqrt(ln 2): exp(-x^2) is 1/2 there
// The sun's elevation (degrees): 30 at hours 4 and 20, 90 at noon (a tent in between), and from
// 30 to 75 over the night (falling from hour 0 to 4, rising from 20 to 24).
constexpr float kSunrise = 4.0f, kSunset = 20.0f;
constexpr float kDayLength = 24.0f;
constexpr float kNightSpan = 45.0f, kBaseElevation = 30.0f;
constexpr float kPerDegree = 1.0f / 180.0f;
constexpr float kRadiansPerDegree = kPi * kPerDegree;
constexpr float kPerHour = 0.041666668f;   // 1/24 (the sun turns once a day)
// A cave's sun.
const Vector3 kCaveSun = {0.5f, 0.5f, -0.707106f};

// Terrain::SetSun takes the ambient colour as a Vector4 (and copies it).
Vector4 Ambient(const LightColour& c) { return {c.r, c.g, c.b, c.a}; }

bool IsBlack(const LightColour& c) { return IsZero(c.r) && IsZero(c.g) && IsZero(c.b); }

// (b - a) * t + a.
float Lerp(float from, float to, float t) { return (to - from) * t + from; }
LightColour Lerp(const LightColour& from, const LightColour& to, float t) {
    LightColour c;
    c.r = Lerp(from.r, to.r, t);
    c.g = Lerp(from.g, to.g, t);
    c.b = Lerp(from.b, to.b, t);
    c.a = 0.0f;
    return c;
}

FogRange FogAt(float hour) {
    float t;   // 0 night, 1 day
    if (hour >= kDayStart && hour <= kDayEnd) t = 1.0f;
    else if (hour > kDawnStart && hour < kDayStart) t = (hour - kDawnStart) / (kDayStart - kDawnStart);
    else if (hour > kDayEnd && hour < kNightStart) t = (kNightStart - hour) / (kNightStart - kDayEnd);
    else t = 0.0f;
    return {Lerp(kNightFog.start, kDayFog.start, t), Lerp(kNightFog.half, kDayFog.half, t)};
}

void SetDistanceFog(const FogRange& range) {
    render::DistanceFog& fog = render::g_renderState.distanceFog;
    fog.start = range.start;
    fog.density = kHalfHidden / (range.half - range.start);
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
// rain's or snow's strength as its layer fades it, which it is and which the layer is heading to, the wind and the
// world's time at the drawn frame.
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
    w.settled = layer.targetMode;
    w.windX = layer.wind.x;
    w.windY = layer.wind.y;
    w.time = static_cast<double>(world->tick) + static_cast<double>(world->tickFraction);
    render::CloudShadows(w);
}

} // namespace

float g_lastLitHour = /*0078FF38*/ -1.0f;

void WorldClient::UpdateLighting() {
    auto* display = &g_sceneRenderer;
    if (!display->running) return;
    UpdateExitMarkers();
    const float hour = HourOfDay(this);
    keptVisuals.RestoreUnits(objects);
    HandOverClouds(this, hour);
    if (g_lastLitHour > hour) g_lastLitHour = -1.0f;

    // The cave's light is the table's hour 0 and a fixed sun; outside it is the hour's colours between two rows.
    LightColour sun, ambientColour = {}, skyColour;
    bool blackAmbient;
    Vector3 direction;
    // The ground's edge fade (Terrain::EdgeFade), which the renderer widens far away.
    render::MapEdge& edge = render::g_renderState.mapEdge;
    edge = {};
    if (terrain && lib::Has(terrain->landscapeFlags, terrain::LandscapeFlag::EdgeFade))
        edge = {terrain->width, terrain->height, options::g_options.borderFogDistance};
    if (cave == 1) {
        SetDistanceFog(kDayFog);
        sun = sunlight[0];
        ambientColour = ambient[0];     // the table's hour 0
        blackAmbient = IsBlack(ambient[0]);
        skyColour = sky[0];
        direction = kCaveSun;
    } else {
        const int index = HourIndex(hour);
        SetDistanceFog(FogAt(hour));
        const float elevation = SunElevation(hour) * kRadiansPerDegree;
        const float turn = hour * kPerHour;
        const float azimuth = (turn + turn) * kPi;
        const float cosElevation = lib::Cos(elevation);
        direction = {lib::Cos(azimuth) * cosElevation, lib::Sin(azimuth) * cosElevation, -lib::Sin(elevation)};
        float whole;
        const float fraction = std::modf(hour, &whole);
        const int next = (index + 1) % kHours;

        sun = Lerp(sunlight[index], sunlight[next], fraction);
        blackAmbient = IsBlack(ambient[index]);
        if (!blackAmbient) ambientColour = Lerp(ambient[index], ambient[next], fraction);
        skyColour = Lerp(sky[index], sky[next], fraction);
    }

    frameSun.r = sun.r;
    frameSun.g = sun.g;
    frameSun.b = sun.b;
    if (blackAmbient) {
        frameAmbient.r = sun.r * kAmbientFromSun;
        frameAmbient.g = sun.g * kAmbientFromSun;
        frameAmbient.b = sun.b * kAmbientFromSun;
    } else {
        frameAmbient.r = ambientColour.r;
        frameAmbient.g = ambientColour.g;
        frameAmbient.b = ambientColour.b;
    }
    SkyColour domeAmbient = {0.0f, 0.0f, 0.0f, 1.0f}, domeSun = domeAmbient;   // a cave's sky dome is black
    if (cave != 1) {
        domeAmbient = {frameAmbient.r, frameAmbient.g, frameAmbient.b, frameAmbient.a};
        domeSun = {frameSun.r, frameSun.g, frameSun.b, frameSun.a};
    }
    Light* light = display->sun;
    light->color[0] = sun.r;
    light->color[1] = sun.g;
    light->color[2] = sun.b;
    light->direction = direction;
    display->SetSunAndAmbient(light, frameAmbient.r, frameAmbient.g, frameAmbient.b);
    display->SetFogColour(skyColour.r, skyColour.g, skyColour.b);
    if (skyDome) skyDome->ApplyLight(direction, domeAmbient, domeSun);
    if (hour - g_lastLitHour < 1.0f) return;
    g_lastLitHour = std::floor(hour);
    if (terrain) {
        const Vector4 terrainAmbient = Ambient(frameAmbient);
        terrain->SetSun(light, terrainAmbient);
    }
}

void WorldClient::DaylightColours(LightColour* sun, LightColour* ambientColour) const {
    const int hour = cave == 1 ? 0 : kNoon;
    *sun = sunlight[hour];
    if (IsBlack(ambient[hour])) {
        ambientColour->r = sun->r * kAmbientFromSun;
        ambientColour->g = sun->g * kAmbientFromSun;
        ambientColour->b = sun->b * kAmbientFromSun;
        ambientColour->a = 0.0f;
    } else {
        *ambientColour = ambient[hour];
    }
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
    figure->GetPartData(part->GetName(), &data.vertices, &data.corners, &data.indices, &data.blocks, &data.indexCount);
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
