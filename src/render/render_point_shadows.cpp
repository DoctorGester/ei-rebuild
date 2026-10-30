// Point-light shadows: a cube of six depth faces for each of the few strongest point lights in view. See
// docs/notes/renderer.md, "The renderer's source files".
#include "../hook/log.h"
#include "render.h"
#include "render_internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace render {

namespace {

using namespace detail;

constexpr int kFaces = 6;
constexpr int kAtlasWidth = kFaces * kPointFaceTexels;
constexpr int kAtlasHeight = kMostPointShadows * kPointFaceTexels;
// The faces' near distance (world units): nearer geometry is drawn at it (shaders/shadow_caster.glsl).
constexpr float kNear = 0.05f;
// A light dimmer than this in every channel casts no shadow; casters weaker than kLeastStrength (units
// fading in) cast none, as for the sun.
constexpr float kLeastColour = 0.02f;
constexpr float kLeastStrength = 0.5f;
// An object holds a light (and casts nothing for it) when the light lies within this far of one of its
// meshes' spheres: a lamp's post, the campfire's spit.
constexpr float kHoldMargin = 0.5f;

// The cube's faces: the axis a face looks along and the two across it (its clip x and y). The
// receivers pick the face and its coordinates the same way (shaders/point_shadow.glsl, point_seen).
struct CubeFace {
    Vector3 forward, right, up;
};
const CubeFace kCubeFaces[kFaces] = {
    {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    {{-1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    {{0.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    {{0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    {{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
};

struct Sphere {
    Vector3 centre;
    float radius;
};

// A caster's mesh this frame: its batch, its object and its bounding sphere in the landscape's space.
struct Caster {
    const MeshBatch* batch;
    int object;   // into g_objects
    Sphere sphere;
    int model;   // PointShadowFrame::models
    float distance;   // squared, to the light being mapped
};

// A unit's mesh this frame, its model matrix (-1: none under the camera) and its sphere once made.
struct UnitMesh {
    MeshBatch* batch;
    int object;   // into g_objects
    int model;    // PointShadowFrame::models
    bool haveSphere;
    Sphere sphere;
};

// An object with casting or unit meshes this frame: its strength (render::CastsShadow; kNoStrength: not
// handed over), whether it holds the light being mapped, and a unit's nearest distance to each light mapped.
constexpr float kNoStrength = -1.0f;
struct FrameObject {
    float strength;
    bool holds = false;
    bool hasFront = false;
    float front[kMostPointShadows] = {};   // per g_mapped entry
};

// A light in view that casts (not an object's own).
struct Candidate {
    int scene;
    Vector3 at;
    float range, strength;
};

// A light given a map this frame: its scene light, its row of the atlas, and the casters each of its faces draws.
struct MappedLight {
    int scene;
    int row;
    Vector3 at;
    float range;
    std::vector<int> faces[kFaces];   // indices into g_casters
};

constexpr int kModelUnknown = -2;

// The frame's working lists, kept across frames for their room.
std::vector<Candidate> g_candidates;
std::unordered_map<const void*, int> g_objectIndex;   // object -> g_objects
std::vector<FrameObject> g_objects;
std::vector<int> g_modelOf;   // per frame matrix its PointShadowFrame::models (-1 none), kModelUnknown: not yet
std::vector<Caster> g_casters;
std::vector<UnitMesh> g_units;
MappedLight g_mapped[kMostPointShadows];
int g_mappedCount = 0;

// A caster as drawn into a face: what its depth there depends on besides the face's light.
struct FaceCaster {
    const CachedMesh* mesh;   // null: the frame's stream (never taken as unchanged)
    std::uint32_t version;    // CachedMeshVersion
    int firstIndex, indexCount;
    Matrix4 model;
    std::uint32_t texture, sampler;
    float alpha[2];   // CasterInstance's
    float uv[2];
};

// A model matrix into the landscape's space is the camera's inverse times the game's matrix under the camera
// (UnderCamera): an object that stays where it is still moves by the rounding as the camera does, up to about
// 0.0015 units measured in the town. Within these a kept face is not drawn again: its place differs by
// less than a tenth of a face texel at a range of 5 units (a texel there is 0.02 units), well under the
// receivers' one-texel depth bias.
constexpr float kKeptPlace = 0.002f;   // the translation, units
constexpr float kKeptTurn = 1e-4f;     // the other entries, relative

bool CloseModel(const Matrix4& a, const Matrix4& b) {
    for (int k = 0; k < 16; ++k) {
        const float x = a.m[k], y = b.m[k];
        const bool place = k == 3 || k == 7 || k == 11;
        if (std::fabs(x - y) > (place ? kKeptPlace : kKeptTurn * (1.0f + std::fabs(x)))) return false;
    }
    return true;
}

bool SameCaster(const FaceCaster& a, const FaceCaster& b) {
    return a.mesh && a.mesh == b.mesh && a.version == b.version && a.firstIndex == b.firstIndex &&
           a.indexCount == b.indexCount && a.texture == b.texture && a.sampler == b.sampler && CloseModel(a.model, b.model) &&
           std::memcmp(a.alpha, b.alpha, sizeof(a.alpha)) == 0 && std::memcmp(a.uv, b.uv, sizeof(a.uv)) == 0;
}

// The atlas's rows are kept across frames: a row keeps the light it was last given and each face what was
// drawn into it, so a face is drawn again only when its light or its casters changed. A face known and
// without casters holds the clear depth, whatever the light; a face not known holds anything.
struct AtlasFace {
    bool known = false;
    std::vector<FaceCaster> casters;
};
struct AtlasRow {
    bool given = false;
    Vector3 at{};
    float range = 0.0f;
    AtlasFace faces[kFaces];
};
AtlasRow g_rows[kMostPointShadows];
bool g_atlasKnown = false;   // the faces hold what g_rows says (the atlas cleared once since it was made)
// Per g_casters entry its draw, made when a face compares or draws it (g_drawMade).
std::vector<FaceCaster> g_draws;
std::vector<char> g_drawMade;

// Each light mapped takes a row: the one it had (the same place and range), else one no light mapped this frame
// has, first one never given. A row given another light keeps only its empty faces.
void GiveRows() {
    bool taken[kMostPointShadows] = {};
    for (int i = 0; i < g_mappedCount; ++i) {
        MappedLight& m = g_mapped[i];
        m.row = -1;
        for (int r = 0; r < kMostPointShadows && m.row < 0; ++r) {
            const AtlasRow& row = g_rows[r];
            if (!taken[r] && row.given && row.at.x == m.at.x && row.at.y == m.at.y && row.at.z == m.at.z &&
                row.range == m.range) {
                m.row = r;
                taken[r] = true;
            }
        }
    }
    for (int i = 0; i < g_mappedCount; ++i) {
        MappedLight& m = g_mapped[i];
        if (m.row >= 0) continue;
        for (int r = 0; r < kMostPointShadows && m.row < 0; ++r)
            if (!taken[r] && !g_rows[r].given) m.row = r;
        for (int r = 0; r < kMostPointShadows && m.row < 0; ++r)
            if (!taken[r]) m.row = r;
        taken[m.row] = true;
        AtlasRow& row = g_rows[m.row];
        row.given = true;
        row.at = m.at;
        row.range = m.range;
        for (AtlasFace& face : row.faces)
            if (!face.casters.empty()) face.known = false;
    }
}

// The draw of g_casters[index] into the point shadows.
const FaceCaster& DrawOf(const PointShadowFrame& ps, int index) {
    const auto k = static_cast<std::size_t>(index);
    if (g_drawMade[k]) return g_draws[k];
    const Caster& c = g_casters[k];
    const MeshBatch& batch = *c.batch;
    // Cut out as the sun's casters (render_shadows.cpp): an alpha-tested or blended pass by its texture's alpha.
    const PassState& s = batch.state;
    const StageState& st = s.stages[0];
    FaceCaster& d = g_draws[k];
    d.mesh = batch.cached;
    d.version = batch.cached ? CachedMeshVersion(*batch.cached) : 0;
    d.firstIndex = batch.firstIndex;
    d.indexCount = batch.indexCount;
    d.model = ps.models[static_cast<std::size_t>(c.model)];
    d.texture = st.texture.id;
    d.sampler = StageSampler(st).id;
    d.alpha[0] = d.alpha[1] = 0.0f;
    if ((s.alphaTest || s.blend) && st.hasTexture) {
        d.alpha[0] = 1.0f;
        d.alpha[1] = s.alphaTest ? (static_cast<float>(s.alphaReference) + 0.5f) / 255.0f : 0.5f;
    }
    d.uv[0] = batch.uOffset;
    d.uv[1] = batch.vOffset;
    g_drawMade[k] = 1;
    return d;
}

// The face holds what it would be drawn with.
bool Unchanged(const PointShadowFrame& ps, const AtlasFace& face, const std::vector<int>& casters) {
    if (!face.known || face.casters.size() != casters.size()) return false;
    for (std::size_t k = 0; k < casters.size(); ++k)
        if (!SameCaster(face.casters[k], DrawOf(ps, casters[k]))) return false;
    return true;
}

// The casters drawn into the faces (with the sun map's pipelines), a face's draws one range of them; the faces
// drawn again this frame: each one's place in the atlas, whether it is cleared first, and its range.
CasterDraws g_faceDraws("ei-point-shadow-casters");
struct RedrawnFace {
    AtlasFace* face;
    int x, y;
    bool clear;
    std::size_t from, to;
};
std::vector<RedrawnFace> g_redrawn;   // kept across frames for its room
// A face cleared alone (shaders/solid.glsl: a rectangle at a depth, written whatever is there).
using ClearVs = solid_params_t;
sg_shader g_clearShader{};
sg_pipeline g_clearPipeline{};
sg_image g_atlas{};
sg_view g_atlasAttachment{}, g_atlasTexture{};

bool MakePipelines() {
    if (g_clearPipeline.id) return sg_query_pipeline_state(g_clearPipeline) == SG_RESOURCESTATE_VALID;
    bool ok = true;
    {
        sg_shader_desc s = ShaderDescription(kSolidShader);
        s.label = "ei-point-shadow-clear";
        g_clearShader = sg_make_shader(&s);
        sg_pipeline_desc p{};
        p.shader = g_clearShader;
        p.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
        p.colors[0].pixel_format = SG_PIXELFORMAT_NONE;
        p.depth.pixel_format = kDepthFormat;
        p.depth.compare = SG_COMPAREFUNC_ALWAYS;
        p.depth.write_enabled = true;
        p.sample_count = 1;
        p.label = "ei-point-shadow-clear";
        g_clearPipeline = sg_make_pipeline(&p);
        ok = sg_query_pipeline_state(g_clearPipeline) == SG_RESOURCESTATE_VALID;
    }
    if (!ok) ei::Log("render: the point shadows' pipeline could not be created (see the sokol messages above)");
    return ok;
}

// sqrt(squared) <= limit, decided as with the root, which is taken only where the two lie too close to tell.
bool NotFurther(float squared, float limit) {
    if (!(limit >= 0.0f)) return std::sqrt(squared) <= limit;
    const float edge = limit * limit;
    if (squared < edge * (1.0f - 1e-6f)) return true;
    if (squared > edge * (1.0f + 1e-6f)) return false;
    return std::sqrt(squared) <= limit;
}

// A face's depth for a distance d along its axis is a + b / d: 0 at kNear, 1 at `range`.
void FaceDepth(float range, float* a, float* b) {
    *a = range / (range - kNear);
    *b = -range * kNear / (range - kNear);
}

// The landscape's space to a face's clip space (x across `right`, y along `up`, w along `forward`).
Matrix4 FaceMatrix(const CubeFace& face, const Vector3& at, float range) {
    float a = 0.0f, b = 0.0f;
    FaceDepth(range, &a, &b);
    const Vector3* rows[3] = {&face.right, &face.up, &face.forward};
    Matrix4 m{};
    for (int k = 0; k < 3; ++k) {
        const Vector3& v = *rows[k];
        const int row = k == 2 ? 3 : k;
        m.m[row * 4] = v.x;
        m.m[row * 4 + 1] = v.y;
        m.m[row * 4 + 2] = v.z;
        m.m[row * 4 + 3] = -vecmath::Dot<Sum3::XYZ>(v, at);
    }
    for (int j = 0; j < 4; ++j) m.m[8 + j] = a * m.m[12 + j] + (j == 3 ? b : 0.0f);
    return m;
}

// The sphere (relative to the light) reaches into the face's view: in front of it and inside its four
// sides (planes at 45 degrees through the light).
bool InFace(const CubeFace& face, const Vector3& c, float radius) {
    const float f = vecmath::Dot<Sum3::XYZ>(face.forward, c), x = vecmath::Dot<Sum3::XYZ>(face.right, c),
                y = vecmath::Dot<Sum3::XYZ>(face.up, c);
    const float r = radius * 1.41421356f;
    return f + radius > kNear && f - x + r >= 0.0f && f + x + r >= 0.0f && f - y + r >= 0.0f && f + y + r >= 0.0f;
}

// The sphere around a model-space box placed by `model`: its centre and the farthest corner's distance (the root
// of the largest square: the root keeps the order).
Sphere BoxSphere(const float box[6], const Matrix4& model) {
    const Vector3 centre = TransformColumn<Sum3::XYZ>(model, Vector3{(box[0] + box[3]) * 0.5f, (box[1] + box[4]) * 0.5f, (box[2] + box[5]) * 0.5f});
    float farthest = 0.0f;
    for (int k = 0; k < 8; ++k) {
        const Vector3 corner{box[(k & 1) ? 3 : 0], box[(k & 2) ? 4 : 1], box[(k & 4) ? 5 : 2]};
        farthest = std::max(farthest, DistanceSquared<Sum3::XYZ>(TransformColumn<Sum3::XYZ>(model, corner), centre));
    }
    return Sphere{centre, std::sqrt(farthest)};
}

// The sphere lies inside the camera's view (the clip matrix's six planes).
bool InView(const Matrix4& clip, const Vector3& c, float radius) {
    // Row k of the matrix: its normal (the first three) and its constant.
    auto row = [&](int k) { return Vector3{clip.m[k * 4], clip.m[k * 4 + 1], clip.m[k * 4 + 2]}; };
    auto constant = [&](int k) { return clip.m[k * 4 + 3]; };
    auto inside = [&](const Vector3& normal, float w) {
        const float length = Length<Sum3::XYZ>(normal);
        return length <= 1e-12f || (vecmath::Dot<Sum3::XYZ>(normal, c) + w) / length >= -radius;
    };
    // -w <= x, y <= w and 0 <= z <= w.
    for (int k = 0; k < 2; ++k)
        if (!inside(row(3) + row(k), constant(3) + constant(k)) || !inside(row(3) - row(k), constant(3) - constant(k)))
            return false;
    return inside(row(2), constant(2)) && inside(row(3) - row(2), constant(3) - constant(2));
}

} // namespace

namespace detail {

void PreparePointShadows(Frame& f) {
    PointShadowFrame& ps = f.shadow.point;
    ps = PointShadowFrame{};
    g_casters.clear();
    g_units.clear();
    g_mappedCount = 0;
    int sceneCount = 0;
    const float(*scene)[2][4] = SceneLightPoints(&sceneCount);
    Matrix4 camera;
    Vector3 eye;
    if (sceneCount == 0 || f.worldRanges.empty() || !WorldCamera(f, &camera, &eye)) return;
    StatsTimer timer(&FrameStats::shadowMs);

    // The lights in view that cast (not an object's own), strongest first (colour times reach; equals in
    // the scene's order).
    g_candidates.clear();
    for (int k = 0; k < sceneCount; ++k) {
        const float* at = scene[k][0];
        const float* colour = scene[k][1];
        const float brightest = std::max({colour[0], colour[1], colour[2]});
        if (!SceneLightCastsShadows(k) || !(at[3] > 0.0f) || brightest < kLeastColour) continue;
        const float range = 1.0f / std::sqrt(at[3]);
        const Vector3 position{at[0], at[1], at[2]};
        if (range <= kNear * 4.0f || !InView(camera, position, range)) continue;
        g_candidates.push_back(Candidate{k, position, range, brightest * range});
    }
    if (g_candidates.empty()) return;
    std::sort(g_candidates.begin(), g_candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.strength > b.strength || (a.strength == b.strength && a.scene < b.scene);
    });

    // The objects handed over as casters (a later entry of the same object wins), then the others met.
    g_objectIndex.clear();
    g_objects.clear();
    for (const auto& c : f.shadow.casters) {
        auto [it, added] = g_objectIndex.try_emplace(c.first, static_cast<int>(g_objects.size()));
        if (added) g_objects.push_back(FrameObject{c.second});
        else g_objects[static_cast<std::size_t>(it->second)].strength = c.second;
    }
    const void* lastObject = nullptr;
    int lastObjectIndex = -1;
    auto objectOf = [&](const void* object) {
        if (object == lastObject) return lastObjectIndex;
        auto [it, added] = g_objectIndex.try_emplace(object, static_cast<int>(g_objects.size()));
        if (added) g_objects.push_back(FrameObject{kNoStrength});
        lastObject = object;
        lastObjectIndex = it->second;
        return lastObjectIndex;
    };

    // The world's meshes after the ground: the GPU-lit ones get their model matrix into the landscape's
    // space; the casters' meshes (the objects the game shadows this frame) and the units' their spheres, one
    // for all passes of a mesh.
    g_modelOf.assign(f.matrices.size(), kModelUnknown);
    auto modelOf = [&](const MeshBatch& b) {
        int& index = g_modelOf[static_cast<std::size_t>(b.matrix)];
        if (index != kModelUnknown) return index;
        Matrix4 model;
        index = -1;
        if (UnderCamera(camera, f.matrices[static_cast<std::size_t>(b.matrix)], &model)) {
            ps.models.push_back(model);
            index = static_cast<int>(ps.models.size()) - 1;
        }
        return index;
    };
    const MeshBatch* lastSphereOf = nullptr;
    Sphere lastSphere{};
    auto sphereOf = [&](const MeshBatch& b, int model) {
        if (!lastSphereOf || lastSphereOf->matrix != b.matrix || std::memcmp(lastSphereOf->box, b.box, sizeof(b.box)) != 0) {
            lastSphere = BoxSphere(b.box, ps.models[static_cast<std::size_t>(model)]);
            lastSphereOf = &b;
        }
        return lastSphere;
    };
    const CachedMesh* lastMesh = nullptr;
    int lastFirst = -1;
    bool started = false;
    for (const Frame::Range& r : f.worldRanges) {
        const std::size_t end = std::min(r.end, f.commands.size());
        for (std::size_t k = r.first; k < end; ++k) {
            const Command& c = f.commands[k];
            if (c.kind == Command::Terrain) started = started || !f.terrainBatches[static_cast<std::size_t>(c.index)].water;
            if (!started || c.kind != Command::Mesh) continue;
            MeshBatch& b = f.meshBatches[static_cast<std::size_t>(c.index)];
            if (!b.model || b.flat || b.indexCount <= 0) continue;
            if (b.lighting >= 0) b.landscape = modelOf(b);
            if (!b.caster || !b.boxed) continue;
            const int object = objectOf(b.caster);
            if (b.figure) g_units.push_back(UnitMesh{&b, object, modelOf(b), false, Sphere{}});
            if (b.state.blend && b.state.destBlend == 2) continue;
            if (g_objects[static_cast<std::size_t>(object)].strength < kLeastStrength) continue;
            if (b.cached == lastMesh && b.firstIndex == lastFirst) continue;   // a later pass of the same mesh
            lastMesh = b.cached;
            lastFirst = b.firstIndex;
            const int model = modelOf(b);
            if (model < 0) continue;
            g_casters.push_back(Caster{&b, object, sphereOf(b, model), model, 0.0f});
            if (b.figure) {
                g_units.back().haveSphere = true;
                g_units.back().sphere = g_casters.back().sphere;
            }
        }
    }
    if (g_casters.empty()) return;

    // Each light, strongest first, while there is room: the casters in its reach, less the objects
    // holding it; a light that nothing shadows gets no map.
    for (const Candidate& light : g_candidates) {
        if (g_mappedCount == kMostPointShadows) break;
        for (Caster& c : g_casters) {
            c.distance = DistanceSquared<Sum3::XYZ>(c.sphere.centre, light.at);
            g_objects[static_cast<std::size_t>(c.object)].holds = false;
        }
        for (const Caster& c : g_casters)
            if (NotFurther(c.distance, c.sphere.radius + kHoldMargin)) g_objects[static_cast<std::size_t>(c.object)].holds = true;
        MappedLight& m = g_mapped[g_mappedCount];
        m.at = light.at;
        m.range = light.range;
        for (std::vector<int>& face : m.faces) face.clear();
        bool any = false;
        for (std::size_t i = 0; i < g_casters.size(); ++i) {
            const Caster& c = g_casters[i];
            if (!NotFurther(c.distance, light.range + c.sphere.radius) || g_objects[static_cast<std::size_t>(c.object)].holds)
                continue;
            const Vector3 relative = c.sphere.centre - light.at;
            for (int face = 0; face < kFaces; ++face)
                if (InFace(kCubeFaces[face], relative, c.sphere.radius)) {
                    m.faces[face].push_back(static_cast<int>(i));
                    any = true;
                }
        }
        if (!any) continue;
        m.scene = light.scene;
        ++g_mappedCount;
    }
    if (g_mappedCount == 0) return;
    GiveRows();
    for (int i = 0; i < g_mappedCount; ++i) {
        const MappedLight& m = g_mapped[i];
        ps.sceneSlots[m.scene] = m.row + 1;
        float a = 0.0f, b = 0.0f;
        FaceDepth(m.range, &a, &b);
        ps.params.lights[m.row][0] = m.at.x;
        ps.params.lights[m.row][1] = m.at.y;
        ps.params.lights[m.row][2] = m.at.z;
        ps.params.lights[m.row][3] = kNear;
        ps.params.depth[m.row][0] = a;
        ps.params.depth[m.row][1] = b;
    }
    ps.params.on[0] = static_cast<float>(g_mappedCount);
    ps.params.on[1] = static_cast<float>(kPointFaceTexels);
    ps.params.eye[0] = eye.x;
    ps.params.eye[1] = eye.y;
    ps.params.eye[2] = eye.z;

    // A unit's meshes compare from its nearest distance to each light (its meshes' spheres).
    for (UnitMesh& u : g_units) {
        if (u.model < 0) continue;
        if (!u.haveSphere) {
            u.sphere = sphereOf(*u.batch, u.model);
            u.haveSphere = true;
        }
        FrameObject& o = g_objects[static_cast<std::size_t>(u.object)];
        if (!o.hasFront) {
            o.hasFront = true;
            for (float& d : o.front) d = kNoFront;
        }
        for (int slot = 0; slot < g_mappedCount; ++slot) {
            const float squared = DistanceSquared<Sum3::XYZ>(u.sphere.centre, g_mapped[slot].at);
            if (o.front[slot] != kNoFront && !NotFurther(squared, o.front[slot] + u.sphere.radius)) continue;
            const float d = std::sqrt(squared) - u.sphere.radius;
            o.front[slot] = std::min(o.front[slot], d);
        }
    }
    for (const UnitMesh& u : g_units) {
        const FrameObject& o = g_objects[static_cast<std::size_t>(u.object)];
        if (!o.hasFront) continue;
        for (int slot = 0; slot < g_mappedCount; ++slot) u.batch->pointFront[g_mapped[slot].row] = o.front[slot];
    }
}

void DrawPointShadows(Frame& f) {
    if (g_mappedCount == 0) return;
    StatsTimer timer(&FrameStats::shadowMs);
    PointShadowFrame& ps = f.shadow.point;
    if (!g_atlas.id) g_atlasKnown = false;
    bool ok = MakePipelines() &&
              (g_atlas.id || MakeShadowDepth(kAtlasWidth, kAtlasHeight, "ei-point-shadows", &g_atlas, &g_atlasAttachment,
                                             &g_atlasTexture));
    if (!ok) {
        ps.params.on[0] = 0.0f;
        for (int& s : ps.sceneSlots) s = 0;
        g_mappedCount = 0;
        return;
    }
    // A new atlas is cleared whole, its faces then known empty.
    const bool fresh = !g_atlasKnown;
    if (fresh) {
        for (AtlasRow& row : g_rows)
            for (AtlasFace& face : row.faces) {
                face.known = true;
                face.casters.clear();
            }
        g_atlasKnown = true;
    }
    g_draws.resize(g_casters.size());
    g_drawMade.assign(g_casters.size(), 0);
    std::vector<RedrawnFace>& redrawn = g_redrawn;
    redrawn.clear();
    g_faceDraws.Clear();
    for (int i = 0; i < g_mappedCount; ++i) {
        const MappedLight& light = g_mapped[i];
        AtlasRow& row = g_rows[light.row];
        for (int face = 0; face < kFaces; ++face) {
            const std::vector<int>& casters = light.faces[face];
            AtlasFace& kept = row.faces[face];
            if (Unchanged(ps, kept, casters)) continue;
            RedrawnFace r{&kept, face * kPointFaceTexels, light.row * kPointFaceTexels,
                          !(kept.known && kept.casters.empty()), g_faceDraws.Count(), 0};
            kept.known = true;
            kept.casters.clear();
            const Matrix4 view = FaceMatrix(kCubeFaces[face], light.at, light.range);
            for (int index : casters) {
                const FaceCaster& d = DrawOf(ps, index);
                const MeshBatch& batch = *g_casters[static_cast<std::size_t>(index)].batch;
                sg_bindings b{};
                int first = 0;
                if (!BindMeshBatch(batch, b, &first)) {
                    kept.known = false;   // drawn again next frame
                    continue;
                }
                const StageState& st = batch.state.stages[0];
                g_faceDraws.Add(b, batch.cached != nullptr, st.texture.id ? st.texture : SurfaceView(nullptr),
                                sg_sampler{d.sampler}, first, batch.indexCount,
                                CasterInstance{Product(view, d.model), {d.uv[0], d.uv[1]}, {d.alpha[0], d.alpha[1]}});
                ++g_stats.shadowDraws;
                kept.casters.push_back(d);
            }
            r.to = g_faceDraws.Count();
            g_faceDraws.Sort(r.from, r.to);
            redrawn.push_back(r);
        }
    }
    if (redrawn.empty()) return;
    const bool haveDraws = g_faceDraws.Upload();
    if (!haveDraws)
        for (const RedrawnFace& r : redrawn)
            if (r.from != r.to) r.face->known = false;   // cleared only: drawn again next frame
    sg_pass pass{};
    pass.action.depth.load_action = fresh ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD;
    pass.action.depth.clear_value = 1.0f;
    pass.action.depth.store_action = SG_STOREACTION_STORE;
    pass.attachments.depth_stencil = g_atlasAttachment;
    pass.label = "ei-point-shadows";
    sg_begin_pass(&pass);
    ++g_stats.passes;
    // The faces' clears first (the faces do not overlap), then their casters.
    bool clearApplied = false;
    for (const RedrawnFace& r : redrawn) {
        if (!r.clear) continue;
        if (!clearApplied) {
            sg_apply_pipeline(g_clearPipeline);
            clearApplied = true;
        }
        sg_apply_viewport(r.x, r.y, kPointFaceTexels, kPointFaceTexels, true);
        const auto side = static_cast<float>(kPointFaceTexels);
        ClearVs vs{{0.0f, 0.0f, side, side}, {side, side, 1.0f, 0.0f}, {}};
        sg_range vr{&vs, sizeof(vs)};
        sg_apply_uniforms(UB_solid_params, &vr);
        sg_draw(0, 6, 1);
    }
    if (haveDraws)
        for (const RedrawnFace& r : redrawn) {
            if (r.from == r.to) continue;
            sg_apply_viewport(r.x, r.y, kPointFaceTexels, kPointFaceTexels, true);
            g_faceDraws.Draw(r.from, r.to);
        }
    sg_end_pass();
}

void PointShadowUniforms(const Frame& f, PointShadowParams* params) { *params = f.shadow.point.params; }

int PointShadowSlot(const Frame& f, int sceneLight) {
    if (sceneLight < 0 || sceneLight >= kMostSceneLights) return -1;
    return f.shadow.point.sceneSlots[sceneLight] - 1;
}

void BindPointShadows(sg_bindings& b, int viewSlot, int samplerSlot) {
    const bool on = g_atlasTexture.id && g_frame.shadow.point.params.on[0] > 0.5f;
    b.views[viewSlot] = on ? g_atlasTexture : NoShadowTexture();
    b.samplers[samplerSlot] = ShadowCompareSampler();
}

void DestroyPointShadows() {
    if (g_atlasAttachment.id) sg_destroy_view(g_atlasAttachment);
    if (g_atlasTexture.id) sg_destroy_view(g_atlasTexture);
    if (g_atlas.id) sg_destroy_image(g_atlas);
    g_atlas = sg_image{};
    g_atlasAttachment = g_atlasTexture = sg_view{};
    g_faceDraws.Destroy();
    if (g_clearPipeline.id) sg_destroy_pipeline(g_clearPipeline);
    if (g_clearShader.id) sg_destroy_shader(g_clearShader);
    g_clearPipeline = sg_pipeline{};
    g_clearShader = sg_shader{};
    for (AtlasRow& row : g_rows) row = AtlasRow{};
    g_atlasKnown = false;
    g_casters.clear();
    g_units.clear();
    g_mappedCount = 0;
}

} // namespace detail

} // namespace render
