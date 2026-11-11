// WorldClient per-frame view code: filing visible objects into the frame's draw lists, sorting
// those lists, and picking the ground point under the cursor. See world.h and docs/notes/world.md.
#include "game/world/frame_lists.h"
#include "game/app/singletons.h"
#include "game/scene/light.h"
#include "game/scene/scene_renderer.h"
#include "game/scene/pipeline.h"
#include "game/data/options.h"
#include "game/world/world.h"

#include "game/core/core_maths.h"
#include "game/scene/drawn_motion.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "game/objects/map_object_shadow.h"
#include "game/data/text_manager.h"
#include "game/players/player.h"
#include "lib/containers.h"
#include "game/terrain/terrain.h"
#include "render/render.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>
#include "lib/float_select.h"
#include "lib/vecmath.h"
#if defined(_MSC_VER) && defined(_M_X64)
#include <xmmintrin.h>   // _mm_prefetch
#endif

namespace {


// The scene's transform pipeline: the picking stack, the clip stack an object's matrix is pushed on to classify it,
// and the view stack.
float* StackTop(TransformPipeline* device, int index) {
    MatrixStack& stack = device->stacks[index];
    return stack.matrices[stack.count - 1].m;
}


void PushBack(std::vector<MapObject*>& v, MapObject* const& value) {
    v.push_back(value);
}

const int& g_multiplayerView = options::g_options.drawVisibleOnly;

// Pushes `matrix` on the pipeline's stack `index` (multiplied onto its top).
void PushStackMatrix(TransformPipeline* device, int index, const float* matrix) {
    MatrixStack& stack = device->stacks[index];
    if (stack.count == 0) {
        stack.count = 1;
        std::memcpy(stack.matrices[0].m, matrix, 0x40);
    } else {
        int depth = ++stack.count;
        stack.matrices[depth - 1].Multiply(stack.matrices[depth - 2].m, matrix);
    }
}

Object3D* AsObject3D(MapObject* object) { return object; }
Object3DClientSpecific* ModelOf(MapObject* object) {
    return AsObject3D(object)->ClientSpecific();
}

// Clamp used by the picking: negative or NaN becomes +0 (a branch-free select).
inline float NonNegative(float v) { return MaxKeepB(v, 0.0f); }

// The two draws share everything but the height function.
template <typename Height>
Vector3 PickPoint(WorldClient* world, int screenX, int screenY, Height height) {
    TransformPipeline* device = g_sceneRenderer.pipeline;
    if (device->stacks[TransformPipeline::kViewStack].count == 0) {
        return Vector3{0.0f, 0.0f, 0.0f};
    }
    PushStackMatrix(device, TransformPipeline::kPickStack, StackTop(device, TransformPipeline::kViewStack));
    const float* m = StackTop(g_sceneRenderer.pipeline, TransformPipeline::kPickStack);

    // The screen point: ints, exact as floats.
    float sx = static_cast<float>(screenX);
    float sy = static_cast<float>(screenY);
    float outX = 0.0f, outY = 0.0f, pz = 0.0f;
    float det = ((m[4] * m[13] - m[5] * m[12]) * sx + (m[1] * m[12] - m[0] * m[13]) * sy) + (m[0] * m[5] - m[4] * m[1]);
    // Skipped when zero.
    if (det != 0.0f) {
        // The screen ray as x = a z + b, y = c z + d.
        float a =
            (((m[13] * m[2] - m[1] * m[14]) * sy + (m[14] * m[5] - m[13] * m[6]) * sx) + (m[1] * m[6] - m[5] * m[2])) / det;
        float b =
            (((m[5] * m[15] - m[13] * m[7]) * sx + (m[3] * m[13] - m[1] * m[15]) * sy) + (m[1] * m[7] - m[3] * m[5])) / det;
        float c =
            -((((m[12] * m[2] - m[0] * m[14]) * sy + (m[4] * m[14] - m[6] * m[12]) * sx) + (m[0] * m[6] - m[4] * m[2])) / det);
        float d =
            -((((m[4] * m[15] - m[12] * m[7]) * sx + (m[3] * m[12] - m[0] * m[15]) * sy) + (m[0] * m[7] - m[3] * m[4])) / det);

        // Start halfway up the terrain's height range.
        float half = world->terrain->heightRange * 0.5f;
        float x = NonNegative(a * half + b);
        float y = NonNegative(c * half + d);
        pz = (height(x, y) + half) * 0.5f;

        // Fixed-point iteration on the height (up to 100 steps)...
        float previous;
        int steps = 0;
        bool converged = false;
        for (;;) {
            previous = pz;
            outX = NonNegative(a * previous + b);
            outY = NonNegative(c * previous + d);
            pz = static_cast<float>(height(outX, outY));
            float diff = std::fabs(pz - previous);
            ++steps;
            if (diff <= 0.01f) {
                converged = steps < 100;
                break;
            }
            if (steps >= 100) break;
        }
        // ...and when it did not settle, bisection between the last two heights.
        if (!converged) {
            steps = 0;
            do {
                previous = (pz + previous) * 0.5f;
                outX = NonNegative(a * previous + b);
                outY = NonNegative(c * previous + d);
                pz = static_cast<float>(height(outX, outY));
                float diff = std::fabs(pz - previous);
                ++steps;
                if (diff <= 0.01f) break;
            } while (steps < 100);
        }
    }
    device->PopMatrix(TransformPipeline::kPickStack);
    return {outX, outY, pz};
}

} // namespace

// ---- Visibility ----------------------------------------------------------------------------------

namespace {

// How far past a plane a conservative verdict below must be: 2^-12 of a bound on every term of the sums, against
// rounding below 2^-19 of it in the verdict or in the exact test it stands for.
constexpr float kDecideMargin = 0x1p-12f;

// A world box (low and high corners) and a bound on the magnitudes of the terms that made it.
struct WorldBox {
    Vector3 low, high, size;
};

// Not NaN or infinite, by the exponent's bits (std::isfinite is a library call).
bool Finite(float v) { return (std::bit_cast<std::uint32_t>(v) & 0x7F800000u) != 0x7F800000u; }
bool Finite(const Vector3& v) { return Finite(v.x) && Finite(v.y) && Finite(v.z); }
bool Finite(const WorldBox& b) { return Finite(b.low) && Finite(b.high) && Finite(b.size); }

// The view's six clip planes over world points (ClipCodeOf's x + w, w - x, y + w, w - y, z and w - z of the clip
// stack's base matrix times (p, 1)), each positive inside: verdicts on world boxes without corner codes, decided
// only where a box clears a plane by the margin.
class ViewPlanes {
public:
    explicit ViewPlanes(const Matrix4& m) {
        const float* r = m.m;
        const Vector3 x{r[0], r[1], r[2]}, y{r[4], r[5], r[6]}, z{r[8], r[9], r[10]}, w{r[12], r[13], r[14]};
        const Vector3 normals[6] = {x + w, w - x, y + w, w - y, z, w - z};
        const float offsets[6] = {r[3] + r[15], r[15] - r[3], r[7] + r[15], r[15] - r[7], r[11], r[15] - r[11]};
        for (int i = 0; i < 6; ++i) {
            normal[i] = normals[i];
            reach[i] = Abs(normals[i]);
            offset[i] = offsets[i];
        }
        scale = Abs(x) + Abs(y) + Abs(z) + Abs(w);
        scaleW = std::fabs(r[3]) + std::fabs(r[7]) + std::fabs(r[11]) + std::fabs(r[15]);
    }

    // 2 when the whole box is outside one plane, 0 when it is inside all six, else -1 (also for NaN).
    int Classify(const WorldBox& box) const {
        const Vector3 centre = (box.low + box.high) * 0.5f, half = (box.high - box.low) * 0.5f;
        const float margin = (Dot(scale, box.size) + scaleW) * kDecideMargin;
        bool inside = true;
        for (int i = 0; i < 6; ++i) {
            const float value = Dot(normal[i], centre) + offset[i], extent = Dot(reach[i], half);
            if (value + extent < -margin) return 2;
            if (!(value - extent > margin)) inside = false;
        }
        return inside ? 0 : -1;
    }

private:
    Vector3 normal[6], reach[6];
    float offset[6] = {};
    Vector3 scale;        // |x| + |y| + |z| + |w|: bounds every plane's coefficients
    float scaleW = 0.0f;  // the same of the translation column
};

// The world box of the volume ClipTest decides `bounds` by first (the cube when there is one, else the box) under
// the placement `w`; false without either or when not finite.
bool ClipVolume(const ClipBounds& bounds, const Matrix4& w, WorldBox* out) {
    Vector3 centre, half;
    if (bounds.flags & static_cast<std::uint32_t>(BoundsFlag::Sphere)) {
        centre = bounds.center;
        half = {bounds.radius, bounds.radius, bounds.radius};
    } else if (bounds.flags & static_cast<std::uint32_t>(BoundsFlag::Box)) {
        centre = bounds.center + (bounds.boxMin + bounds.boxMax) * 0.5f;
        half = Abs(bounds.boxMax - bounds.boxMin) * 0.5f;
    } else {
        return false;
    }
    const float* r = w.m;
    const Vector3 rows[3] = {{r[0], r[1], r[2]}, {r[4], r[5], r[6]}, {r[8], r[9], r[10]}};
    const Vector3 at{r[3], r[7], r[11]};
    const Vector3 reach = Abs(centre) + half;
    const Vector3 placed = Vector3{Dot(rows[0], centre), Dot(rows[1], centre), Dot(rows[2], centre)} + at;
    const Vector3 extent{Dot(Abs(rows[0]), half), Dot(Abs(rows[1]), half), Dot(Abs(rows[2]), half)};
    out->low = placed - extent;
    out->high = placed + extent;
    out->size = Vector3{Dot(Abs(rows[0]), reach), Dot(Abs(rows[1]), reach), Dot(Abs(rows[2]), reach)} + Abs(at);
    return Finite(*out);
}

// The same for a shadow's sphere moved to the object `at` (ShadowInView: no rotation).
bool ShadowVolume(const ClipBounds& sphere, const Vector3& at, WorldBox* out) {
    const Vector3 centre = sphere.center + at;
    Vector3 low, high;
    if (sphere.flags & static_cast<std::uint32_t>(BoundsFlag::Sphere)) {
        const Vector3 r{sphere.radius, sphere.radius, sphere.radius};
        low = centre - r;
        high = centre + r;
    } else if (sphere.flags & static_cast<std::uint32_t>(BoundsFlag::Box)) {
        low = centre + sphere.boxMin;
        high = centre + sphere.boxMax;
    } else {
        return false;
    }
    *out = {Min(low, high), Max(low, high), Max(Abs(low), Abs(high)) + Abs(centre)};
    return Finite(*out);
}

// Boxes gathered into one.
struct BoxUnion {
    WorldBox box{};
    bool any = false;
    void Add(const WorldBox& b) {
        box = any ? WorldBox{Min(box.low, b.low), Max(box.high, b.high), Max(box.size, b.size)} : b;
        any = true;
    }
};

// The object placed at the pose the draw shows (drawn::Show) in its worldMatrix, its own light carried there; the pose.
drawn::Pose Place(MapObject* object) {
    Object3D* o3d = AsObject3D(object);
    drawn::Observe(o3d);
    const drawn::Pose shown = drawn::Show(o3d);
    // An object's own light follows its drawn place (drawn::Carry; MapObjectClientSpecific::RunTick puts the
    // game's there at each tick).
    if (EngineObject* side = object->companion; side && side->GetClassId() == ClassId::MapObjectClientSpecific)
        if (Light* light = object->ClientCompanion()->light)
            drawn::Carry(light, shown.position + Vector3{0.0f, 0.0f, MapObjectClientSpecific::kLightHeight});
    float* matrix = o3d->worldMatrix.m;
    shown.rotation.ToMatrix(matrix);
    matrix[3] = shown.position.x;
    matrix[7] = shown.position.y;
    matrix[11] = shown.position.z;
    matrix[14] = 0.0f;
    matrix[13] = 0.0f;
    matrix[12] = 0.0f;
    matrix[15] = 1.0f;
    return shown;
}

// The placed object's clip test: not outside the view (at any distance: the projection has no far plane).
bool InView(Object3D* o3d) {
    TransformPipeline* device = g_sceneRenderer.pipeline;
    PushStackMatrix(device, TransformPipeline::kClipStack, o3d->worldMatrix.m);
    const bool inView = device->ClipTest(&o3d->bounds) != TransformPipeline::ClipResult::Outside;
    device->PopMatrix(TransformPipeline::kClipStack);
    return inView;
}

// Files the placed object in the frame's lists. Out of view, `shadowVerdict` (ViewPlanes::Classify of its shadow's
// volume) stands for ShadowInView's clip test when it is 2 or 0.
void File(MapObject* object, ClassId kind, bool inView, int shadowVerdict, const Matrix4* view) {
    Object3D* o3d = AsObject3D(object);
    MapObject* const& entry = object;
    if (inView) {
        object->flags |= MapObjectFlag::Shown;
        // View-space position.
        o3d->worldPosition = TransformColumnNormal(*view, o3d->localPosition);
        switch (kind) {
        case ClassId::Unit:
            PushBack(FrameObjects(FrameList::VisibleUnits), entry);
            PushBack(FrameObjects(FrameList::ShadowUnits), entry);
            break;
        case ClassId::Tree:
            PushBack(FrameObjects(FrameList::VisibleTrees), entry);
            PushBack(FrameObjects(FrameList::ShadowTrees), entry);
            break;
        case ClassId::Building:
            PushBack(FrameObjects(FrameList::VisibleBuildings), entry);
            PushBack(FrameObjects(FrameList::ShadowBuildings), entry);
            break;
        case ClassId::Lever:
            PushBack(FrameObjects(FrameList::VisibleLevers), entry);
            PushBack(FrameObjects(FrameList::ShadowLevers), entry);
            break;
        default:
            PushBack(FrameObjects(FrameList::VisibleOther), entry);
            PushBack(FrameObjects(FrameList::ShadowOther), entry);
            break;
        }
        return;
    }
    object->flags &= ~MapObjectFlag::Shown;
    // Out of view: still listed for shadows when its shadow volume is in view (ShadowInView).
    Object3DClientSpecific* model = ModelOf(object);
    bool shadow;
    if (!options::g_options.shadows) {
        shadow = false;
    } else if (lib::Has(model->flags, ClientHalfFlag::Shown)) {
        shadow = true;
    } else {
        shadow = shadowVerdict == 0 || (shadowVerdict != 2 && model->ShadowInView());
    }
    if (shadow) {
        if (o3d->animation) o3d->animation->PlaceForDraw();
        switch (kind) {
        case ClassId::Unit: PushBack(FrameObjects(FrameList::ShadowUnits), entry); break;
        case ClassId::Tree: PushBack(FrameObjects(FrameList::ShadowTrees), entry); break;
        case ClassId::Building: PushBack(FrameObjects(FrameList::ShadowBuildings), entry); break;
        case ClassId::Lever: PushBack(FrameObjects(FrameList::ShadowLevers), entry); break;
        default: PushBack(FrameObjects(FrameList::ShadowOther), entry); break;
        }
    }
}

float LargestScale(const Vector3& s) {
    return (std::max)((std::max)(std::fabs(s.x), std::fabs(s.y)), (std::max)(std::fabs(s.z), 1.0f));
}

// What an object's boxes are taken from, compared bit for bit: the drawn pose, its own position (where its shadow's
// sphere stands), its bounds, its shadow's sphere and its extra scale.
struct BoxInputs {
    drawn::Pose shown;
    Vector3 at;
    ClipBounds bounds, sphere;
    Vector3 scale;
};
static_assert(sizeof(BoxInputs) == 140, "no padding: compared with memcmp");

// An object's boxes as last taken (Take), kept by cell and place among the cell's objects but units: an object that
// stands still takes them again only when its inputs changed.
struct KeptBoxes {
    BoxInputs from;
    bool clip = false, shadow = false;   // ClipVolume, ShadowVolume taken
    bool caster = false;                 // the casters' walk's sphere: valid bounds
    bool casterFinite = true;
    WorldBox clipBox, shadowBox;
    Vector3 casterCentre;
    float casterRadius = 0.0f;

    void Take(const BoxInputs& inputs, const Matrix4& world) {
        from = inputs;
        clip = ClipVolume(inputs.bounds, world, &clipBox);
        shadow = ShadowVolume(inputs.sphere, inputs.at, &shadowBox);
        // The casters' walk's world sphere (world_client_shadow_casters.cpp, WorldSphere) when its bounds are valid
        // (the walk passes over the others).
        constexpr std::uint32_t kValid = static_cast<std::uint32_t>(BoundsFlag::Sphere) | static_cast<std::uint32_t>(BoundsFlag::Box);
        caster = (inputs.bounds.flags & kValid) == kValid;
        if (!caster) return;
        const Vector3& s = inputs.scale;
        const Vector3& c = inputs.bounds.center;
        casterCentre = TransformColumn(world, Vector3{c.x * s.x, c.y * s.y, c.z * s.z});
        casterRadius = inputs.bounds.radius * LargestScale(s);
        casterFinite = Finite(casterCentre) && Finite(casterRadius);
    }
};

// A cell's kept boxes and their unions (taken again when an object's boxes or the cell's objects changed).
struct KeptCell {
    std::vector<KeptBoxes> objects;
    BoxUnion clip, shadow, casterCentres;   // clip: of the objects but levers
    float casterRadius = 0.0f;
    bool casterFinite = true;
};
std::vector<KeptCell> g_keptCells;   // row by row

// An object of the cell CollectCells walks, between placing it and filing it.
struct CellObject {
    MapObject* object = nullptr;
    ClassId kind{};
    const KeptBoxes* boxes = nullptr;
};
std::vector<CellObject> g_cellObjects;
std::vector<MapObject*> g_rangeObjects;   // CollectCells's objects in walk order
std::vector<std::size_t> g_cellEnds;      // ... and each cell's end

// The walk is bound by memory: an object's lines are asked for this many objects ahead, its halves' half as far
// (once the object's line holding their pointers has come).
constexpr std::size_t kReadAhead = 8;

void Fetch(const void* at) {
#if defined(_MSC_VER) && defined(_M_X64)
    _mm_prefetch(static_cast<const char*>(at), _MM_HINT_T0);
#elif defined(__GNUC__)
    __builtin_prefetch(at);
#endif
}

void FetchObject(const MapObject* object) {
    if (!object) return;
    Fetch(object);
    Fetch(&object->attached);
    Fetch(&object->companion);
    Fetch(&object->localPosition);
    Fetch(&object->bounds);
    Fetch(&object->drawnStep);
    Fetch(&object->worldMatrix);
}

void FetchHalves(const MapObject* object) {
    if (!object) return;
    if (const Object3DSpecific* half = object->attached.Get()) {
        const auto* at = reinterpret_cast<const char*>(half);
        for (std::size_t k = 0; k < sizeof(Object3DClientSpecific); k += 64) Fetch(at + k);
    }
    if (const MapObjectCompanion* side = object->companion.Get()) Fetch(side);
}

std::uint32_t g_viewPass = 0;    // CollectCells's count (CasterCell::pass)
std::uint32_t g_viewFrame = 0;   // render::CasterFrame() at the last CollectCells
std::uint32_t g_viewWidth = 0;   // the object grid's width then

} // namespace

std::vector<CasterCell> g_casterCells;

const CasterCell* ViewCasterCell(std::uint32_t x, std::uint32_t y) {
    if (g_viewFrame != render::CasterFrame() || x >= g_viewWidth) return nullptr;
    const std::size_t at = static_cast<std::size_t>(y) * g_viewWidth + x;
    if (at >= g_casterCells.size() || g_casterCells[at].pass != g_viewPass) return nullptr;
    return &g_casterCells[at];
}

void WorldClient::CollectCells(const CellRange& range, const Matrix4* view) {
    static const Matrix4 kIdentity = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    const MatrixStack& clipStack = g_sceneRenderer.pipeline->stacks[TransformPipeline::kClipStack];
    const ViewPlanes planes(clipStack.count ? clipStack.matrices[clipStack.count - 1] : kIdentity);
    const bool shadows = options::g_options.shadows != 0;
    constexpr std::uint32_t kBounds = static_cast<std::uint32_t>(BoundsFlag::Sphere) | static_cast<std::uint32_t>(BoundsFlag::Box);
    const std::size_t cells = static_cast<std::size_t>(objectGrid.width) * objectGrid.height;
    ++g_viewPass;
    g_viewFrame = render::CasterFrame();
    g_viewWidth = objectGrid.width;
    g_casterCells.resize(cells);
    g_keptCells.resize(cells);
    // The range's objects in walk order, each cell's end after its last: the walk reads ahead across cells.
    g_rangeObjects.clear();
    g_cellEnds.clear();
    for (std::uint32_t y = range.y0; y <= range.y1; ++y)
        for (std::uint32_t x = range.x0; x <= range.x1; ++x) {
            for (MapObject* object : objectGrid.rows[y][x]) g_rangeObjects.push_back(object);
            g_cellEnds.push_back(g_rangeObjects.size());
        }
    for (std::size_t k = 0; k < kReadAhead && k < g_rangeObjects.size(); ++k) FetchObject(g_rangeObjects[k]);
    std::size_t next = 0, cellIndex = 0;
    for (std::uint32_t y = range.y0; y <= range.y1; ++y)
        for (std::uint32_t x = range.x0; x <= range.x1; ++x) {
            // Every object placed and its boxes taken (or kept) first; then the cell's boxes decide for all of their
            // objects where they clear a plane, else each object's own box, else its exact test.
            const std::size_t at = static_cast<std::size_t>(y) * objectGrid.width + x;
            KeptCell& kept = g_keptCells[at];
            g_cellObjects.clear();
            bool changed = false, units = false;
            std::size_t place = 0;
            for (const std::size_t end = g_cellEnds[cellIndex++]; next < end; ++next) {
                if (next + kReadAhead < g_rangeObjects.size()) FetchObject(g_rangeObjects[next + kReadAhead]);
                if (next + kReadAhead / 2 < g_rangeObjects.size()) FetchHalves(g_rangeObjects[next + kReadAhead / 2]);
                MapObject* object = g_rangeObjects[next];
                if (!object || !AsObject3D(object)->attached) continue;
                const ClassId kind = object->GetClassId();
                if (kind == ClassId::Unit) {
                    // Units wait on the deferred list (which holds a reference); heroes with a text-key id come later.
                    if (!(object->MapType() == MapObjectType::Hero && IsTextKey(object->id))) g_deferredUnits.emplace_back(object);
                    units = true;
                    continue;
                }
                Object3D* o3d = AsObject3D(object);
                Object3DClientSpecific* model = ModelOf(object);
                const BoxInputs inputs{Place(object), o3d->localPosition, o3d->bounds, model->sphere, model->figureScale};
                if (place == kept.objects.size()) {
                    kept.objects.emplace_back();
                    kept.objects.back().Take(inputs, o3d->worldMatrix);
                    changed = true;
                } else if (std::memcmp(&kept.objects[place].from, &inputs, sizeof(inputs)) != 0) {
                    kept.objects[place].Take(inputs, o3d->worldMatrix);
                    changed = true;
                }
                g_cellObjects.push_back(CellObject{object, kind, nullptr});
                ++place;
            }
            if (place != kept.objects.size()) {
                kept.objects.resize(place);
                changed = true;
            }
            for (std::size_t k = 0; k < place; ++k) g_cellObjects[k].boxes = &kept.objects[k];
            if (changed) {
                kept.clip = kept.shadow = kept.casterCentres = BoxUnion{};
                kept.casterRadius = 0.0f;
                kept.casterFinite = true;
                for (const CellObject& e : g_cellObjects) {
                    const KeptBoxes& b = *e.boxes;
                    if (b.clip) kept.clip.Add(b.clipBox);
                    if (b.shadow) kept.shadow.Add(b.shadowBox);
                    if (!b.caster) continue;
                    if (!b.casterFinite) {
                        kept.casterFinite = false;
                        continue;
                    }
                    const Vector3 r{b.casterRadius, b.casterRadius, b.casterRadius};
                    kept.casterCentres.Add(WorldBox{b.casterCentre, b.casterCentre, Abs(b.casterCentre) + r});
                    kept.casterRadius = (std::max)(kept.casterRadius, b.casterRadius);
                }
            }
            CasterCell& caster = g_casterCells[at];
            caster.pass = g_viewPass;
            caster.complete = !units && kept.casterFinite;
            caster.any = kept.casterCentres.any;
            caster.low = kept.casterCentres.box.low;
            caster.high = kept.casterCentres.box.high;
            caster.size = kept.casterCentres.box.size;
            caster.radius = kept.casterRadius;
            if (g_cellObjects.empty()) continue;
            const int clipVerdict = kept.clip.any ? planes.Classify(kept.clip.box) : -1;
            const int shadowVerdict = shadows && kept.shadow.any ? planes.Classify(kept.shadow.box) : -1;
            for (const CellObject& e : g_cellObjects) {
                Object3D* o3d = AsObject3D(e.object);
                const KeptBoxes& b = *e.boxes;
                bool inView;
                if (b.clip) {
                    const int verdict = clipVerdict >= 0 ? clipVerdict : planes.Classify(b.clipBox);
                    inView = verdict == 0 || (verdict != 2 && InView(o3d));
                } else if (o3d->bounds.flags & kBounds) {
                    inView = InView(o3d);
                } else {
                    inView = true;   // ClipTest calls a volume without bounds crossing the view
                }
                int shadowVolume = -1;
                if (!inView && shadows && b.shadow) shadowVolume = shadowVerdict >= 0 ? shadowVerdict : planes.Classify(b.shadowBox);
                File(e.object, e.kind, inView, shadowVolume, view);
            }
        }
}

void WorldClient::CollectVisible(MapObject* object, const Matrix4* view, float animTime, char animateUnits, char withScripted) {
    if (!object || !AsObject3D(object)->attached) return;
    // Only a multiplayer spectator view (session kind 1) leaves units it cannot see out.
    bool drawAllUnits = true;
    if (g_multiplayerView != 0 && session->sessionKind == 1) drawAllUnits = false;

    const ClassId kind = object->GetClassId();
    if (kind == ClassId::Unit) {
        if (object->MapType() == MapObjectType::Hero && IsTextKey(object->id) && !withScripted) return;
        if (!animateUnits) {
            // First pass: units wait on the deferred list (which holds a reference).
            g_deferredUnits.emplace_back(object);
            return;
        }
        drawn::Observe(object);
        const drawn::Pose before = drawn::OwnPose(object);
        object->AdvanceTime(animTime);
        drawn::NotePlaced(object, before);
        if (!drawAllUnits && !(object->flags & MapObjectFlag::InParty)) return;
    }
    Place(object);
    File(object, kind, InView(AsObject3D(object)), -1, view);
}

// ---- Cursor picking ------------------------------------------------------------------------------

Vector3 WorldClient::PickGround(int x, int y) {
    return PickPoint(this, x, y, [this](float px, float py) {
        return terrain->HeightAt(px, py, terrain::Layer::Ground);
    });
}

Vector3 WorldClient::PickSurface(int x, int y) {
    return PickPoint(this, x, y, [this](float px, float py) {
        return GroundHeightAt(px, py);
    });
}

// ---- The frame's draw lists (frame_lists.h) ------------------------------------------------------

// Plain globals (constructed and destroyed by the C++ runtime).
std::vector<MapObject*> g_frameLists[static_cast<int>(FrameList::Count)];
std::vector<HeldRef<MapObject>> g_deferredUnits;
