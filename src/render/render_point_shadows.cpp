// Point-light shadows: a cube of six depth faces for each of the few strongest point lights in view. See
// docs/notes/renderer.md, "The renderer's source files".
#include "render.h"
#include "render_internal.h"

#include "../hook/log.h"

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
// The faces' near distance (world units): nearer geometry is drawn at it (shaders/shadow_caster.hlsl).
constexpr double kNear = 0.05;
// A light dimmer than this in every channel casts no shadow; casters weaker than kLeastStrength (units
// fading in) cast none, as for the sun.
constexpr float kLeastColour = 0.02f;
constexpr float kLeastStrength = 0.5f;
// An object holds a light (and casts nothing for it) when the light lies within this far of one of its
// meshes' spheres: a lamp's post, the campfire's spit.
constexpr double kHoldMargin = 0.5;

// The cube's faces: the axis a face looks along and the two across it (its clip x and y). The
// receivers pick the face and its coordinates the same way (shaders/point_shadow.hlsli, point_seen).
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
    double distance;   // squared, to the light being mapped
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
    float front[kMostPointShadows] = {};
};

// A light in view that casts (not an object's own).
struct Candidate {
    int scene;
    Vector3 at;
    double range, strength;
};

// A light given a map this frame, and the casters each of its faces draws.
struct MappedLight {
    Vector3 at;
    double range;
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

struct CasterVs {
    Matrix4 light;
    float uvOffset[4];
};
struct CasterFs {
    float alpha[4];
};

sg_shader g_shader{};
sg_pipeline g_pipelines[2]{};   // the frame's stream, the meshes kept on the GPU
sg_image g_atlas{};
sg_view g_atlasAttachment{}, g_atlasTexture{};

bool MakePipelines() {
    if (g_pipelines[0].id) return sg_query_pipeline_state(g_pipelines[1]) == SG_RESOURCESTATE_VALID;
    if (!g_shader.id) {
        sg_shader_desc s{};
        SetBytecode(s, kCasterShader);
        s.attrs[0].base_type = SG_SHADERATTRBASETYPE_FLOAT;
        s.attrs[0].hlsl_sem_name = "POSITION";
        s.attrs[1].base_type = SG_SHADERATTRBASETYPE_FLOAT;
        s.attrs[1].hlsl_sem_name = "TEXCOORD";
        s.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
        s.uniform_blocks[0].size = sizeof(CasterVs);
        s.uniform_blocks[0].hlsl_register_b_n = 0;
        s.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
        s.uniform_blocks[1].size = sizeof(CasterFs);
        s.uniform_blocks[1].hlsl_register_b_n = 0;
        s.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
        s.views[0].texture.image_type = SG_IMAGETYPE_2D;
        s.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
        s.views[0].texture.hlsl_register_t_n = 0;
        s.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
        s.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
        s.samplers[0].hlsl_register_s_n = 0;
        s.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
        s.texture_sampler_pairs[0].view_slot = 0;
        s.texture_sampler_pairs[0].sampler_slot = 0;
        s.label = "ei-point-shadow-caster";
        g_shader = sg_make_shader(&s);
    }
    bool ok = sg_query_shader_state(g_shader) == SG_RESOURCESTATE_VALID;
    for (int cached = 0; cached < 2 && ok; ++cached) {
        sg_pipeline_desc p{};
        p.shader = g_shader;
        MeshLayout(p, cached != 0, true);
        p.index_type = SG_INDEXTYPE_UINT32;
        p.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
        p.cull_mode = SG_CULLMODE_NONE;
        p.colors[0].pixel_format = SG_PIXELFORMAT_NONE;   // depth only
        p.depth.pixel_format = kDepthFormat;
        p.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        p.depth.write_enabled = true;
        p.depth.bias_slope_scale = 1.5f;
        p.sample_count = 1;
        p.label = "ei-point-shadow-caster";
        g_pipelines[cached] = sg_make_pipeline(&p);
        ok = sg_query_pipeline_state(g_pipelines[cached]) == SG_RESOURCESTATE_VALID;
    }
    if (!ok) ei::Log("render: the point shadows' pipeline could not be created (see the sokol messages above)");
    return ok;
}

// a x b, both row-major, in double.
Matrix4 Multiply(const Matrix4& a, const Matrix4& b) {
    Matrix4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            double s = 0.0;
            for (int k = 0; k < 4; ++k) s += static_cast<double>(a.m[i * 4 + k]) * b.m[k * 4 + j];
            r.m[i * 4 + j] = static_cast<float>(s);
        }
    return r;
}

double DistanceSquared(const Vector3& a, const Vector3& b) { return LengthSquaredUnrounded<Sum3::XYZ, double>(a - b); }

double Distance(const Vector3& a, const Vector3& b) { return std::sqrt(DistanceSquared(a, b)); }

// sqrt(squared) <= limit, decided as with the root, which is taken only where the two lie too close to tell.
bool NotFurther(double squared, double limit) {
    if (!(limit >= 0.0)) return std::sqrt(squared) <= limit;
    const double edge = limit * limit;
    if (squared < edge * (1.0 - 1e-9)) return true;
    if (squared > edge * (1.0 + 1e-9)) return false;
    return std::sqrt(squared) <= limit;
}

// A face's depth for a distance d along its axis is a + b / d: 0 at kNear, 1 at `range`.
void FaceDepth(double range, double* a, double* b) {
    *a = range / (range - kNear);
    *b = -range * kNear / (range - kNear);
}

// The landscape's space to a face's clip space (x across `right`, y along `up`, w along `forward`).
Matrix4 FaceMatrix(const CubeFace& face, const Vector3& at, double range) {
    double a = 0.0, b = 0.0;
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
    for (int j = 0; j < 4; ++j) m.m[8 + j] = static_cast<float>(a * m.m[12 + j] + (j == 3 ? b : 0.0));
    return m;
}

// The sphere (relative to the light) reaches into the face's view: in front of it and inside its four
// sides (planes at 45 degrees through the light).
bool InFace(const CubeFace& face, const Vector3& c, float radius) {
    const double f = vecmath::Dot<Sum3::XYZ>(face.forward, c), x = vecmath::Dot<Sum3::XYZ>(face.right, c),
                 y = vecmath::Dot<Sum3::XYZ>(face.up, c);
    const double r = radius * 1.41421356;
    return f + radius > kNear && f - x + r >= 0.0 && f + x + r >= 0.0 && f - y + r >= 0.0 && f + y + r >= 0.0;
}

// The sphere around a model-space box placed by `model`: its centre and the farthest corner's distance (the root
// of the largest square: the root keeps the order).
Sphere BoxSphere(const float box[6], const Matrix4& model) {
    const Vector3 centre = TransformColumn<Sum3::XYZ>(model, Vector3{(box[0] + box[3]) * 0.5f, (box[1] + box[4]) * 0.5f, (box[2] + box[5]) * 0.5f});
    double farthest = 0.0;
    for (int k = 0; k < 8; ++k) {
        const Vector3 corner{box[(k & 1) ? 3 : 0], box[(k & 2) ? 4 : 1], box[(k & 4) ? 5 : 2]};
        farthest = std::max(farthest, DistanceSquared(TransformColumn<Sum3::XYZ>(model, corner), centre));
    }
    return Sphere{centre, static_cast<float>(std::sqrt(farthest))};
}

// The sphere lies inside the camera's view (the clip matrix's six planes).
bool InView(const Matrix4& clip, const Vector3& c, double radius) {
    // Row k of the matrix: its normal (the first three) and its constant.
    auto row = [&](int k) { return Vector3{clip.m[k * 4], clip.m[k * 4 + 1], clip.m[k * 4 + 2]}; };
    auto constant = [&](int k) { return clip.m[k * 4 + 3]; };
    auto inside = [&](const Vector3& normal, float w) {
        const double length = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ, double>(normal));
        return length <= 1e-12 || (vecmath::Dot<Sum3::XYZ>(normal, c) + w) / length >= -radius;
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
        const double range = 1.0 / std::sqrt(static_cast<double>(at[3]));
        const Vector3 position{at[0], at[1], at[2]};
        if (range <= kNear * 4.0 || !InView(camera, position, range)) continue;
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
            g_casters.push_back(Caster{&b, object, sphereOf(b, model), model, 0.0});
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
            c.distance = DistanceSquared(c.sphere.centre, light.at);
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
        const int slot = g_mappedCount++;
        ps.sceneSlots[light.scene] = slot + 1;
        double a = 0.0, b = 0.0;
        FaceDepth(light.range, &a, &b);
        ps.params.lights[slot][0] = light.at.x;
        ps.params.lights[slot][1] = light.at.y;
        ps.params.lights[slot][2] = light.at.z;
        ps.params.lights[slot][3] = static_cast<float>(kNear);
        ps.params.depth[slot][0] = static_cast<float>(a);
        ps.params.depth[slot][1] = static_cast<float>(b);
    }
    if (g_mappedCount == 0) return;
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
            const double squared = DistanceSquared(u.sphere.centre, g_mapped[slot].at);
            if (o.front[slot] != kNoFront && !NotFurther(squared, o.front[slot] + static_cast<double>(u.sphere.radius))) continue;
            const float d = static_cast<float>(std::sqrt(squared) - u.sphere.radius);
            o.front[slot] = std::min(o.front[slot], d);
        }
    }
    for (const UnitMesh& u : g_units) {
        const FrameObject& o = g_objects[static_cast<std::size_t>(u.object)];
        if (!o.hasFront) continue;
        for (int slot = 0; slot < g_mappedCount; ++slot) u.batch->pointFront[slot] = o.front[slot];
    }
}

void DrawPointShadows(Frame& f) {
    if (g_mappedCount == 0) return;
    StatsTimer timer(&FrameStats::shadowMs);
    PointShadowFrame& ps = f.shadow.point;
    bool ok = MakePipelines() &&
              (g_atlas.id || MakeShadowDepth(kAtlasWidth, kAtlasHeight, "ei-point-shadows", &g_atlas, &g_atlasAttachment,
                                             &g_atlasTexture));
    if (!ok) {
        ps.params.on[0] = 0.0f;
        for (int& s : ps.sceneSlots) s = 0;
        g_mappedCount = 0;
        return;
    }
    sg_pass pass{};
    pass.action.depth.load_action = SG_LOADACTION_CLEAR;
    pass.action.depth.clear_value = 1.0f;
    pass.action.depth.store_action = SG_STOREACTION_STORE;
    pass.attachments.depth_stencil = g_atlasAttachment;
    pass.label = "ei-point-shadows";
    sg_begin_pass(&pass);
    ++g_stats.passes;
    for (int slot = 0; slot < g_mappedCount; ++slot) {
        const MappedLight& light = g_mapped[slot];
        for (int face = 0; face < kFaces; ++face) {
            if (light.faces[face].empty()) continue;
            sg_apply_viewport(face * kPointFaceTexels, slot * kPointFaceTexels, kPointFaceTexels,
                              kPointFaceTexels, true);
            const Matrix4 view = FaceMatrix(kCubeFaces[face], light.at, light.range);
            int lastLayout = -1;
            for (int index : light.faces[face]) {
                const Caster& c = g_casters[static_cast<std::size_t>(index)];
                const MeshBatch& batch = *c.batch;
                sg_bindings b{};
                int first = 0;
                if (!BindMeshBatch(batch, b, &first)) continue;
                const int layout = batch.cached ? 1 : 0;
                if (layout != lastLayout) {
                    sg_apply_pipeline(g_pipelines[layout]);
                    lastLayout = layout;
                }
                // Cut out as the sun's casters (render_shadows.cpp): an alpha-tested or blended pass by its
                // texture's alpha.
                const PassState& s = batch.state;
                const StageState& st = s.stages[0];
                CasterFs fs{};
                if ((s.alphaTest || s.blend) && st.hasTexture) {
                    fs.alpha[0] = 1.0f;
                    fs.alpha[1] = s.alphaTest ? (static_cast<float>(s.alphaReference) + 0.5f) / 255.0f : 0.5f;
                }
                b.views[0] = st.texture.id ? st.texture : SurfaceView(nullptr);
                b.samplers[0] = StageSampler(st);
                sg_apply_bindings(&b);
                CasterVs vs{Multiply(view, ps.models[static_cast<std::size_t>(c.model)]),
                            {batch.uOffset, batch.vOffset, 0.0f, 0.0f}};
                sg_range vr{&vs, sizeof(vs)};
                sg_apply_uniforms(0, &vr);
                sg_range fr{&fs, sizeof(fs)};
                sg_apply_uniforms(1, &fr);
                sg_draw(first, batch.indexCount, 1);
                ++g_stats.shadowDraws;
            }
        }
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

void DescribePointShadows(sg_shader_desc& s, int viewSlot, int samplerSlot) {
    s.views[viewSlot].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    s.views[viewSlot].texture.image_type = SG_IMAGETYPE_2D;
    s.views[viewSlot].texture.sample_type = SG_IMAGESAMPLETYPE_DEPTH;
    s.views[viewSlot].texture.hlsl_register_t_n = static_cast<std::uint8_t>(viewSlot);
    s.samplers[samplerSlot].stage = SG_SHADERSTAGE_FRAGMENT;
    s.samplers[samplerSlot].sampler_type = SG_SAMPLERTYPE_COMPARISON;
    s.samplers[samplerSlot].hlsl_register_s_n = static_cast<std::uint8_t>(samplerSlot);
    s.texture_sampler_pairs[viewSlot].stage = SG_SHADERSTAGE_FRAGMENT;
    s.texture_sampler_pairs[viewSlot].view_slot = static_cast<std::uint8_t>(viewSlot);
    s.texture_sampler_pairs[viewSlot].sampler_slot = static_cast<std::uint8_t>(samplerSlot);
}

void DestroyPointShadows() {
    if (g_atlasAttachment.id) sg_destroy_view(g_atlasAttachment);
    if (g_atlasTexture.id) sg_destroy_view(g_atlasTexture);
    if (g_atlas.id) sg_destroy_image(g_atlas);
    g_atlas = sg_image{};
    g_atlasAttachment = g_atlasTexture = sg_view{};
    for (sg_pipeline& p : g_pipelines) {
        if (p.id) sg_destroy_pipeline(p);
        p = sg_pipeline{};
    }
    if (g_shader.id) sg_destroy_shader(g_shader);
    g_shader = sg_shader{};
    g_casters.clear();
    g_units.clear();
    g_mappedCount = 0;
}

} // namespace detail

} // namespace render
