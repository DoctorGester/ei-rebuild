// Point-light shadows: a cube of six depth faces for each of the few strongest point lights in view. See
// docs/notes/renderer.md, "The renderer's source files".
#include "../platform/log.h"
#include "render.h"
#include "render_internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace render {

constexpr int kFaces = 6;
constexpr int kAtlasWidth = kFaces * kPointFaceTexels;
constexpr int kAtlasHeight = kMostPointShadows * kPointFaceTexels;
// The faces' near distance (world units): nearer geometry is drawn at it (shaders/shadow_caster.hlsl).
constexpr float kNear = 0.05f;
// A light dimmer than this in every channel casts no shadow; casters weaker than kLeastStrength cast none, as for the sun.
constexpr float kLeastColour = 0.02f;
// An object holds a light (and casts nothing for it) when the light lies within this far of one of its
// meshes' spheres: a lamp's post, the campfire's spit.
constexpr float kHoldMargin = 0.5f;

// The cube's faces: the axis a face looks along and the two across it (its clip x and y). The
// receivers pick the face and its coordinates the same way (shaders/point_shadow.hlsli, point_seen).
struct CubeFace {
    Vector3 forward, right, up;
};
static const CubeFace kCubeFaces[kFaces] = {
    {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    {{-1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    {{0.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    {{0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    {{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
};

struct Sphere {
    Vector3 centre;
    float radius = 0.0f;
};

// A caster's mesh this frame: its batch, its object and its bounding sphere in the landscape's space.
struct Caster {
    const MeshBatch* batch = nullptr;
    int object = 0;  // into g_objects
    Sphere sphere;
    int model = 0;  // Frame::models
    float distance = 0.0f;  // squared, to the light being mapped
};

// A unit's mesh this frame, its model matrix and its sphere once made.
struct UnitMesh {
    MeshBatch* batch = nullptr;
    int object = 0;  // into g_objects
    bool haveSphere = false;
    Sphere sphere;
};

// An object of ShadowFrame::casters (by its place there): its strength (render::CastsShadow; kNoStrength: not
// handed over), whether it holds the light being mapped, and a unit's nearest distance to each light mapped.
struct FrameObject {
    float strength = 0.0f;
    bool holds = false;
    bool hasFront = false;
    float front[kMostPointShadows] = {};   // per g_mapped entry
};

// A light in view that casts (not an object's own).
struct Candidate {
    int scene = 0;
    Vector3 at;
    float range = 0.0f, strength = 0.0f;
    float reach = 0.0f;   // NearCandidate's: the range, at least kHoldMargin
    float size = 0.0f;    // ... a bound on the magnitudes of the place and the reach
};
// Around every candidate's place and reach (NearCandidate), and the largest of their sizes.
struct CandidatesReach {
    Box box;
    float size = 0.0f;
};

// A light given a map this frame: its scene light, its row of the atlas, and the casters each of its faces draws.
struct MappedLight {
    int scene = 0;
    int row = 0;
    Vector3 at;
    float range = 0.0f;
    std::vector<int> faces[kFaces];   // indices into g_casters
};

// The frame's working lists, kept across frames for their room.
static std::vector<Candidate> g_candidates;
static CandidatesReach g_candidatesReach;
static std::vector<FrameObject> g_objects;   // by place in ShadowFrame::casters
static std::vector<Caster> g_casters;
static std::vector<UnitMesh> g_units;
static MappedLight g_mapped[kMostPointShadows];   // PointShadowFrame::mapped of them

// A caster as drawn into a face: what its depth there depends on besides the face's light.
struct FaceCaster {
    const CachedMesh* mesh = nullptr;  // null: the frame's stream (never taken as unchanged)
    std::uint32_t version = 0;  // CachedMeshVersion
    int firstIndex = 0, indexCount = 0;
    Matrix4 model;
    gpu::Texture texture;
    gpu::Sampler sampler;
    float alpha[2] = {};  // CasterInstance's
    float uv[2] = {};
};

static bool SameCaster(const FaceCaster& a, const FaceCaster& b) {
    return a.mesh && a.mesh == b.mesh && a.version == b.version && a.firstIndex == b.firstIndex &&
           a.indexCount == b.indexCount && a.texture.texture == b.texture.texture && a.sampler.sampler == b.sampler.sampler &&
           std::memcmp(&a.model, &b.model, sizeof(Matrix4)) == 0 &&
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
static AtlasRow g_rows[kMostPointShadows];
static bool g_atlasKnown = false;   // the faces hold what g_rows says (the atlas cleared once since it was made)
// Per g_casters entry its draw, made when a face compares or draws it (g_drawMade).
static std::vector<FaceCaster> g_draws;
static std::vector<char> g_drawMade;

// Each light mapped takes a row: the one it had (the same place and range), else one no light mapped this frame
// has, first one never given. A row given another light keeps only its empty faces.
static void GiveRows(int mapped) {
    bool taken[kMostPointShadows] = {};
    for (int i = 0; i < mapped; ++i) {
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
    for (int i = 0; i < mapped; ++i) {
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
static const FaceCaster& DrawOf(const Frame& f, int index) {
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
    d.model = f.models[static_cast<std::size_t>(c.model)];
    d.texture = st.texture ? st.texture : SurfaceView(nullptr);
    d.sampler = StageSampler(st);
    CasterAlpha(s, d.alpha);
    d.uv[0] = batch.uOffset;
    d.uv[1] = batch.vOffset;
    g_drawMade[k] = 1;
    return d;
}

// The face holds what it would be drawn with.
static bool Unchanged(const Frame& f, const AtlasFace& face, const std::vector<int>& casters) {
    if (!face.known || face.casters.size() != casters.size()) return false;
    for (std::size_t k = 0; k < casters.size(); ++k)
        if (!SameCaster(face.casters[k], DrawOf(f, casters[k]))) return false;
    return true;
}

// The casters drawn into the faces (with the sun map's pipelines), a face's draws one range of them; the faces
// drawn again this frame: each one's place in the atlas, whether it is cleared first, and its range.
static CasterDraws g_faceDraws("ei-point-shadow-casters");
struct RedrawnFace {
    AtlasFace* face = nullptr;
    int x = 0, y = 0;
    bool clear = false;
    std::size_t from = 0, to = 0;
};
static std::vector<RedrawnFace> g_redrawn;   // kept across frames for its room
// A face cleared alone (shaders/solid.hlsl: a rectangle at a depth, written whatever is there).
static gpu::Pipeline g_clearPipeline;
static bool g_clearTried = false;
static gpu::Texture g_atlas;

static bool MakePipelines() {
    if (!g_clearTried) {
        g_clearTried = true;
        g_clearPipeline = gpu::MakePipeline(PointShadowClearDesc(), "ei-point-shadow-clear");
        if (!g_clearPipeline) ei::Log("render: the point shadows' pipeline could not be created (see the gpu messages above)");
    }
    return static_cast<bool>(g_clearPipeline);
}

// sqrt(squared) <= limit, decided as with the root, which is taken only where the two lie too close to tell.
static bool NotFurther(float squared, float limit) {
    if (!(limit >= 0.0f)) return std::sqrt(squared) <= limit;
    const float edge = limit * limit;
    if (squared < edge * (1.0f - 1e-6f)) return true;
    if (squared > edge * (1.0f + 1e-6f)) return false;
    return std::sqrt(squared) <= limit;
}

// A face's depth for a distance d along its axis is a + b / d: 0 at kNear, 1 at `range`.
static void FaceDepth(float range, float* a, float* b) {
    *a = range / (range - kNear);
    *b = -range * kNear / (range - kNear);
}

// The landscape's space to a face's clip space (x across `right`, y along `up`, w along `forward`).
static Matrix4 FaceMatrix(const CubeFace& face, const Vector3& at, float range) {
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
        m.m[row * 4 + 3] = -Dot(v, at);
    }
    for (int j = 0; j < 4; ++j) m.m[8 + j] = a * m.m[12 + j] + (j == 3 ? b : 0.0f);
    return m;
}

// The sphere (relative to the light) reaches into the face's view: in front of it and inside its four
// sides (planes at 45 degrees through the light).
static bool InFace(const CubeFace& face, const Vector3& c, float radius) {
    const float f = Dot(face.forward, c), x = Dot(face.right, c),
                y = Dot(face.up, c);
    const float r = radius * 1.41421356f;
    return f + radius > kNear && f - x + r >= 0.0f && f + x + r >= 0.0f && f - y + r >= 0.0f && f + y + r >= 0.0f;
}

// The sphere around a model-space box placed by `model`: its centre and the farthest corner's distance (the root
// of the largest square: the root keeps the order).
static Sphere BoxSphere(const float box[6], const Matrix4& model) {
    const Vector3 centre = TransformColumn(model, Vector3{(box[0] + box[3]) * 0.5f, (box[1] + box[4]) * 0.5f, (box[2] + box[5]) * 0.5f});
    float farthest = 0.0f;
    for (int k = 0; k < 8; ++k) {
        const Vector3 corner{box[(k & 1) ? 3 : 0], box[(k & 2) ? 4 : 1], box[(k & 4) ? 5 : 2]};
        farthest = std::max(farthest, DistanceSquared(TransformColumn(model, corner), centre));
    }
    return Sphere{centre, std::sqrt(farthest)};
}

// The sphere lies inside the camera's view (the clip matrix's six planes).
static bool InView(const Matrix4& clip, const Vector3& c, float radius) {
    // Row k of the matrix: its normal (the first three) and its constant.
    auto row = [&](int k) { return Vector3{clip.m[k * 4], clip.m[k * 4 + 1], clip.m[k * 4 + 2]}; };
    auto constant = [&](int k) { return clip.m[k * 4 + 3]; };
    auto inside = [&](const Vector3& normal, float w) {
        const float length = Length(normal);
        return length <= 1e-12f || (Dot(normal, c) + w) / length >= -radius;
    };
    // -w <= x, y <= w and 0 <= z <= w.
    for (int k = 0; k < 2; ++k)
        if (!inside(row(3) + row(k), constant(3) + constant(k)) || !inside(row(3) - row(k), constant(3) - constant(k)))
            return false;
    return inside(row(2), constant(2)) && inside(row(3) - row(2), constant(3) - constant(2));
}

gpu::PipelineDesc PointShadowClearDesc() {
    gpu::PipelineDesc p;
    p.program = gpu_shaders::solid::kIndex;
    p.colour = gpu::Format::None;
    p.depth = kDepthFormat;
    p.depthCompare = gpu::Compare::Always;
    p.depthWrite = true;
    return p;
}

// The lights in view that cast (not an object's own), strongest first (colour times reach; equals in the scene's
// order), into g_candidates; false when there are none.
static bool ListCandidates(const KeptScene& scene, const Matrix4& camera) {
    g_candidates.clear();
    for (int k = 0; k < scene.count; ++k) {
        const KeptLight& l = scene.lights[k];
        const float brightest = std::max({l.colour[0], l.colour[1], l.colour[2]});
        if (!l.shadows || !(l.inverseRangeSquared > 0.0f) || brightest < kLeastColour) continue;
        const float range = 1.0f / std::sqrt(l.inverseRangeSquared);
        if (range <= kNear * 4.0f || !InView(camera, l.at, range)) continue;
        const float reach = std::max(range, kHoldMargin);
        const float size = std::fabs(l.at.x) + std::fabs(l.at.y) + std::fabs(l.at.z) + reach;
        g_candidates.push_back(Candidate{k, l.at, range, brightest * range, reach, size});
    }
    if (g_candidates.empty()) return false;
    CandidatesReach& all = g_candidatesReach;
    all = CandidatesReach{};
    for (const Candidate& c : g_candidates) {
        all.box.Extend(c.at - Vector3{c.reach, c.reach, c.reach});
        all.box.Extend(c.at + Vector3{c.reach, c.reach, c.reach});
        all.size = std::max(all.size, c.size);
    }
    std::sort(g_candidates.begin(), g_candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.strength > b.strength || (a.strength == b.strength && a.scene < b.scene);
    });
    return true;
}

// A caster's mesh can matter to a candidate light (MapLights' tests on its sphere): its box placed by `model` lies
// within the light's range of it, or within kHoldMargin (it may hold the light), by its centre and a bound on its
// sphere's radius, with a margin far over the rounding of either side (2^-12 of the magnitudes); first against the box
// around all the candidates' reaches.
static bool NearCandidate(const float box[6], const Matrix4& model) {
    const PlacedBox placed = PlaceBox(box, model);
    const float radius = Length(placed.half);
    const Vector3& c = placed.centre;
    const float size = std::fabs(c.x) + std::fabs(c.y) + std::fabs(c.z) + radius;
    const Box& all = g_candidatesReach.box;
    const float widest = radius + (size + g_candidatesReach.size) * 0x1p-12f;
    if (c.x < all.low.x - widest || c.x > all.high.x + widest || c.y < all.low.y - widest ||
        c.y > all.high.y + widest || c.z < all.low.z - widest || c.z > all.high.z + widest)
        return false;
    for (const Candidate& light : g_candidates) {
        const float reach = light.reach + radius + (size + light.size) * 0x1p-12f;
        if (DistanceSquared(c, light.at) <= reach * reach) return true;
    }
    return false;
}

// BoxSphere of a batch's box under its model, made again only when the batch differs from the last one asked in its
// model or its box (a mesh's passes in a row).
struct LastSphere {
    const MeshBatch* batch = nullptr;
    Sphere sphere;
    const Sphere& Of(const Frame& f, const MeshBatch& b) {
        if (!batch || batch->placement != b.placement || std::memcmp(batch->box, b.box, sizeof(b.box)) != 0) {
            sphere = BoxSphere(b.box, f.models[static_cast<std::size_t>(b.placement)]);
            batch = &b;
        }
        return sphere;
    }
};

// The world's meshes after the ground into g_casters and g_units: the GPU-lit ones take their model matrix into the
// landscape's space; the casters' meshes (the objects the game shadows this frame) that may matter to a candidate
// light and the units' their spheres, one for all passes of a mesh.
static void ListCasters(Frame& f, LastSphere& spheres) {
    g_casters.clear();
    g_units.clear();
    g_objects.clear();
    for (const FrameCaster& c : f.shadow.casters) g_objects.push_back(FrameObject{c.strength});
    for (const WorldMesh& w : f.worldMeshes) {
        MeshBatch& batch = f.meshBatches[static_cast<std::size_t>(w.batch)];
        if (batch.flat) continue;
        if (batch.lighting >= 0) batch.landscape = batch.placement;
        if (!batch.caster || !batch.boxed || batch.casterSlot < 0 || batch.placement < 0) continue;
        const int object = batch.casterSlot;
        if (batch.figure) g_units.push_back(UnitMesh{&batch, object, false, Sphere{}});
        if (w.additive) continue;
        if (g_objects[static_cast<std::size_t>(object)].strength < kLeastStrength) continue;
        if (w.repeat) continue;   // a later pass of the same mesh
        if (!NearCandidate(batch.box, f.models[static_cast<std::size_t>(batch.placement)])) continue;
        g_casters.push_back(Caster{&batch, object, spheres.Of(f, batch), batch.placement, 0.0f});
        if (batch.figure) {
            g_units.back().haveSphere = true;
            g_units.back().sphere = g_casters.back().sphere;
        }
    }
}

// Each light, strongest first, while there is room, into g_mapped: the casters in its reach, less the objects
// holding it; a light that nothing shadows gets no map. The count mapped.
static int MapLights() {
    int mapped = 0;
    for (const Candidate& light : g_candidates) {
        if (mapped == kMostPointShadows) break;
        for (Caster& c : g_casters) {
            c.distance = DistanceSquared(c.sphere.centre, light.at);
            g_objects[static_cast<std::size_t>(c.object)].holds = false;
        }
        for (const Caster& c : g_casters)
            if (NotFurther(c.distance, c.sphere.radius + kHoldMargin)) g_objects[static_cast<std::size_t>(c.object)].holds = true;
        MappedLight& map = g_mapped[mapped];
        map.at = light.at;
        map.range = light.range;
        for (std::vector<int>& face : map.faces) face.clear();
        bool any = false;
        for (std::size_t i = 0; i < g_casters.size(); ++i) {
            const Caster& c = g_casters[i];
            if (!NotFurther(c.distance, light.range + c.sphere.radius) || g_objects[static_cast<std::size_t>(c.object)].holds)
                continue;
            const Vector3 relative = c.sphere.centre - light.at;
            for (int face = 0; face < kFaces; ++face)
                if (InFace(kCubeFaces[face], relative, c.sphere.radius)) {
                    map.faces[face].push_back(static_cast<int>(i));
                    any = true;
                }
        }
        if (!any) continue;
        map.scene = light.scene;
        ++mapped;
    }
    return mapped;
}

// The receivers' uniforms: each mapped light's place and its row's depth, the scene lights' rows, the eye.
static void WriteParams(PointShadowFrame& ps, const Vector3& eye) {
    for (int i = 0; i < ps.mapped; ++i) {
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
    ps.params.count = ps.mapped;
    ps.params.texels = static_cast<float>(kPointFaceTexels);
    ps.params.eye[0] = eye.x;
    ps.params.eye[1] = eye.y;
    ps.params.eye[2] = eye.z;
}

// A unit's meshes compare from its nearest distance to each light (its meshes' spheres): MeshBatch::pointFront.
static void UnitFronts(const Frame& f, const PointShadowFrame& ps, LastSphere& spheres) {
    for (UnitMesh& u : g_units) {
        if (!u.haveSphere) {
            u.sphere = spheres.Of(f, *u.batch);
            u.haveSphere = true;
        }
        FrameObject& o = g_objects[static_cast<std::size_t>(u.object)];
        if (!o.hasFront) {
            o.hasFront = true;
            for (float& d : o.front) d = kNoFront;
        }
        for (int slot = 0; slot < ps.mapped; ++slot) {
            const float squared = DistanceSquared(u.sphere.centre, g_mapped[slot].at);
            if (o.front[slot] != kNoFront && !NotFurther(squared, o.front[slot] + u.sphere.radius)) continue;
            const float d = std::sqrt(squared) - u.sphere.radius;
            o.front[slot] = std::min(o.front[slot], d);
        }
    }
    for (const UnitMesh& u : g_units) {
        const FrameObject& o = g_objects[static_cast<std::size_t>(u.object)];
        if (!o.hasFront) continue;
        for (int slot = 0; slot < ps.mapped; ++slot) u.batch->pointFront[g_mapped[slot].row] = o.front[slot];
    }
}

void PreparePointShadows(Frame& f) {
    PointShadowFrame& ps = f.shadow.point;
    const KeptScene& scene = KeptSceneLight();
    if (scene.count == 0 || f.worldRanges.empty()) return;
    const std::optional<WorldView> view = WorldCamera(f);
    if (!view) return;
    Timer timer(FrameStats::Time::ShadowMs);
    if (!ListCandidates(scene, view->clip)) return;
    LastSphere spheres;
    ListCasters(f, spheres);
    if (g_casters.empty()) return;
    ps.mapped = MapLights();
    if (ps.mapped == 0) return;
    GiveRows(ps.mapped);
    WriteParams(ps, view->eye);
    UnitFronts(f, ps, spheres);
}

// The faces to draw again (whose light or casters changed; all of them in a fresh atlas) into g_redrawn, their
// casters into g_faceDraws.
static void ListRedrawnFaces(const Frame& f, const PointShadowFrame& ps) {
    g_draws.resize(g_casters.size());
    g_drawMade.assign(g_casters.size(), 0);
    g_redrawn.clear();
    g_faceDraws.Clear();
    for (int i = 0; i < ps.mapped; ++i) {
        const MappedLight& light = g_mapped[i];
        AtlasRow& row = g_rows[light.row];
        for (int face = 0; face < kFaces; ++face) {
            const std::vector<int>& casters = light.faces[face];
            AtlasFace& kept = row.faces[face];
            if (Unchanged(f, kept, casters)) continue;
            RedrawnFace r{&kept, face * kPointFaceTexels, light.row * kPointFaceTexels,
                          !(kept.known && kept.casters.empty()), g_faceDraws.Count(), 0};
            kept.known = true;
            kept.casters.clear();
            const Matrix4 view = FaceMatrix(kCubeFaces[face], light.at, light.range);
            for (int index : casters) {
                const FaceCaster& d = DrawOf(f, index);
                const MeshBatch& batch = *g_casters[static_cast<std::size_t>(index)].batch;
                Geometry b;
                int first = 0;
                if (!BindMeshBatch(batch, b, &first)) {
                    kept.known = false;   // drawn again next frame
                    continue;
                }
                g_faceDraws.Add(b, batch.cached != nullptr, d.texture, d.sampler, first, batch.indexCount,
                                CasterInstance{Product(view, d.model), {d.uv[0], d.uv[1]}, {d.alpha[0], d.alpha[1]}});
                ++g_stats.counts[FrameStats::Count::ShadowDraws];
                kept.casters.push_back(d);
            }
            r.to = g_faceDraws.Count();
            g_faceDraws.Sort(r.from, r.to);
            g_redrawn.push_back(r);
        }
    }
}

// One pass on the atlas (cleared whole when `fresh`): the redrawn faces' clears first (the faces do not overlap),
// then their casters.
static void DrawRedrawnFaces(bool fresh) {
    const bool haveDraws = g_faceDraws.Upload();
    if (!haveDraws)
        for (const RedrawnFace& r : g_redrawn)
            if (r.from != r.to) r.face->known = false;   // cleared only: drawn again next frame
    gpu::PassDesc pass;
    pass.depth = {g_atlas, 0, fresh ? gpu::Load::Clear : gpu::Load::Keep, 1.0f, true};
    pass.label = "ei-point-shadows";
    if (!gpu::BeginPass(pass)) Fatal("Can't draw the frame: the point shadows' pass could not begin (%s).", gpu::Error());
    ++g_stats.counts[FrameStats::Count::Passes];
    bool clearApplied = false;
    for (const RedrawnFace& r : g_redrawn) {
        if (!r.clear) continue;
        if (!clearApplied) {
            gpu::ApplyPipeline(g_clearPipeline);
            clearApplied = true;
        }
        gpu::SetViewport({r.x, r.y, r.x + kPointFaceTexels, r.y + kPointFaceTexels});
        const auto side = static_cast<float>(kPointFaceTexels);
        SolidParams vs{{0.0f, 0.0f, side, side}, {}, {side, side}, 1.0f};
        gpu::PushUniforms(gpu::Stage::Vertex, gpu_shaders::solid::kBlockSolidParams, vs);
        gpu::Draw(0, 6);
    }
    if (haveDraws)
        for (const RedrawnFace& r : g_redrawn) {
            if (r.from == r.to) continue;
            gpu::SetViewport({r.x, r.y, r.x + kPointFaceTexels, r.y + kPointFaceTexels});
            g_faceDraws.Draw(r.from, r.to);
        }
    g_stats.counts[FrameStats::Count::ShadowCalls] += g_faceDraws.Calls();
    gpu::EndPass();
}

void DrawPointShadows(Frame& f) {
    PointShadowFrame& ps = f.shadow.point;
    if (ps.mapped == 0) return;
    Timer timer(FrameStats::Time::ShadowMs);
    if (!g_atlas) {
        g_atlasKnown = false;
        g_atlas = MakeShadowDepth(kAtlasWidth, kAtlasHeight, "ei-point-shadows");
    }
    if (!MakePipelines() || !g_atlas) {
        ps.mapped = 0;
        ps.params.count = 0;
        for (int& s : ps.sceneSlots) s = 0;
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
    ListRedrawnFaces(f, ps);
    if (!g_redrawn.empty()) DrawRedrawnFaces(fresh);
}

int PointShadowSlot(const Frame& f, int sceneLight) {
    if (sceneLight < 0 || sceneLight >= kMostSceneLights) return -1;
    return f.shadow.point.sceneSlots[sceneLight] - 1;
}

gpu::TextureBinding PointShadowBinding(const Frame& f) {
    const bool on = g_atlas && f.shadow.point.mapped > 0;
    return {on ? g_atlas : NoShadowTexture(), ShadowCompareSampler()};
}

void BindPointShadows(const Frame& f, int slot) {
    const gpu::TextureBinding b = PointShadowBinding(f);
    gpu::BindTextures(gpu::Stage::Fragment, slot, {&b, 1});
}

} // namespace render
