// Meshes kept on the GPU, built again only where the game says they changed. See docs/notes/renderer.md,
// "The renderer's source files".
#include "../platform/log.h"
#include "render.h"
#include "render_internal.h"

#include <cstring>
#include <deque>
#include <unordered_map>

namespace render {

// A corner's geometry; its colours (colour, specular) are a stream of their own.
struct MeshChanges {
    std::uint32_t positions = 0, colours = 0, rebuilt = 0;
};
static std::unordered_map<const void*, MeshChanges> g_changes;   // never erased: an entry keeps its place
static std::uint64_t g_changesMade = 0;                          // entries made in g_changes

// A g_changes entry as a kept mesh last found it: one that was not there is looked for again only after an
// entry was made.
struct ChangesRef {
    const void* key = nullptr;
    const MeshChanges* at = nullptr;
    std::uint64_t made = 0;
};

static MeshChanges ChangesOf(const void* object, ChangesRef& ref) {
    if (!object) return MeshChanges{};
    if (ref.key != object || (!ref.at && ref.made != g_changesMade)) {
        auto it = g_changes.find(object);
        ref = {object, it == g_changes.end() ? nullptr : &it->second, g_changesMade};
    }
    return ref.at ? *ref.at : MeshChanges{};
}

// What a kept mesh was built from: when an array or count differs, it is built again.
struct MeshFrom {
    const LinkMesh* shared = nullptr;
    const VertexPacket* positions = nullptr;   // null for moved positions (a work array all moved meshes share)
    int positionCount = 0;
    const std::uint32_t* colours = nullptr;
    const std::uint32_t* speculars = nullptr;
    const TexCoord* uvs = nullptr;
    int uvCount = 0;                    // a caster's (KeepCasterMesh); 0 for drawn meshes
    const MeshCorner* corners = nullptr;
    int cornerCount = 0;
    const std::uint16_t* indices = nullptr;
    int indexCount = 0;
    const NormalPacket* normals = nullptr;
    const std::uint32_t* words = nullptr;
    int wordCount = 0;
    MeshChanges own, link;   // the figure mesh's and the link figure's changes when built
    std::uint32_t colourWrites = 0;   // the colour array's (MeshChanged on the array's data)

    bool SameArrays(const MeshFrom& o) const {
        return shared == o.shared && positions == o.positions && positionCount == o.positionCount && colours == o.colours &&
               speculars == o.speculars && uvs == o.uvs && uvCount == o.uvCount && corners == o.corners &&
               cornerCount == o.cornerCount &&
               indices == o.indices && indexCount == o.indexCount && normals == o.normals && words == o.words &&
               wordCount == o.wordCount && own.rebuilt == o.own.rebuilt &&
               link.rebuilt == o.link.rebuilt;
    }
    bool SamePositions(const MeshFrom& o) const {
        return own.positions == o.own.positions && link.positions == o.link.positions;
    }
    bool SameColours(const MeshFrom& o) const {
        return own.colours == o.own.colours && colourWrites == o.colourWrites;
    }
};

// The draw being handed over (SetMeshSource .. SetMeshSource(null)).
static MeshSource g_source{};
static bool g_haveSource = false;
static bool g_interface = false;   // SetInterfaceDraw

// A figure mesh's corners as kept, and their place in the pool.
struct CachedMesh : KeptRanges {
    MeshFrom from;
    bool valid = false;                  // built (a corner out of range: not)
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> colours;
    std::vector<std::uint32_t> indices;
    bool drawn = false;
    bool relightNext = false;                        // a draw not kept wrote the colours (NoteMeshDraw)
    std::uint32_t version = 0;                       // CachedMeshVersion
    bool boxKept = false;                            // CachedMeshBox's box taken since the positions changed
    float box[6] = {};
    ChangesRef own, link, colourWrites;              // MeshFrom's changes as last found
};

// The kept meshes by the slot their figure mesh holds (MeshSlots: 1 + the index). A dropped mesh's slot is used
// again; a deque keeps the meshes' places (the frame's batches and g_pending point at them).
class MeshSlotPool {
public:
    // `owner`'s mesh in slot `handle`, null when the slot is not the owner's (none, dropped, used again).
    CachedMesh* Of(const FigureMesh* owner, std::uint32_t handle) {
        if (handle == 0 || handle > slots.size()) return nullptr;
        Slot& s = slots[handle - 1];
        return s.owner == owner ? &s.mesh : nullptr;
    }
    // `owner`'s mesh, a new one in a free slot (`handle` set, *made true) when it has none.
    CachedMesh& Take(const FigureMesh* owner, std::uint32_t& handle, bool* made) {
        *made = false;
        if (CachedMesh* kept = Of(owner, handle)) return *kept;
        std::uint32_t index = 0;
        if (!unused.empty()) {
            index = unused.back();
            unused.pop_back();
        } else {
            index = static_cast<std::uint32_t>(slots.size());
            slots.emplace_back();
        }
        Slot& s = slots[index];
        s.owner = owner;
        s.mesh = CachedMesh{};
        ++live;
        handle = index + 1;
        *made = true;
        return s.mesh;
    }
    // Lets go of every kept mesh `drop(mesh)` says to.
    template <class Drop>
    void DropIf(Drop drop) {
        for (std::size_t i = 0; i < slots.size(); ++i) {
            Slot& s = slots[i];
            if (!s.owner || !drop(s.mesh)) continue;
            s.owner = nullptr;
            s.mesh = CachedMesh{};
            unused.push_back(static_cast<std::uint32_t>(i));
            --live;
        }
    }
    template <class Each>
    void ForEach(Each each) {
        for (Slot& s : slots)
            if (s.owner) each(s.mesh);
    }
    std::size_t Live() const { return live; }

private:
    struct Slot {
        const FigureMesh* owner = nullptr;   // null: free
        CachedMesh mesh;
    };
    std::deque<Slot> slots;
    std::vector<std::uint32_t> unused;   // free slots
    std::size_t live = 0;
};

static MeshSlotPool g_meshes;
static MeshSlotPool g_casterMeshes;   // KeepCasterMesh's, by part (no colours)
// The casters read in frame g_frameCastersAt (UploadCachedMeshes).
static std::vector<CachedMesh*> g_frameCasters;
static std::uint32_t g_frameCastersAt = 0;
static std::uint32_t g_lastVersion = 0;                              // the last CachedMesh::version given
static std::vector<CachedMesh*> g_pending;
static GpuPool g_pool(sizeof(MeshVertex), 1u << 16, 3u << 16, "ei-mesh");
// A mesh not drawn for this many frames is dropped (its pool room comes back at the next Restart).
constexpr std::uint32_t kMeshLife = 600;

static bool g_cacheOn = true;   // SetMeshCache
static std::uint64_t g_checks = 0, g_stale = 0;
#if defined(EI_RENDER_VALIDATION)
// Every this many frames each kept mesh drawn is compared with a fresh build.
constexpr std::uint32_t kCheckEvery = 16;
static CachedMesh g_fresh;   // the fresh build
#endif

static MeshFrom FromOf(const MeshDraw& m, CachedMesh& e) {
    MeshFrom f;
    f.shared = g_source.shared;
    f.positions = g_source.moved ? nullptr : m.positions;
    f.positionCount = m.positionCount;
    f.colours = m.colours;
    f.speculars = m.speculars;
    f.uvs = m.uvs;
    f.corners = m.corners;
    f.cornerCount = m.cornerCount;
    f.indices = m.indices;
    f.indexCount = m.indexCount;
    f.normals = m.normals;
    f.words = m.words;
    f.wordCount = m.wordCount;
    f.own = ChangesOf(g_source.owner, e.own);
    f.colourWrites = ChangesOf(m.colours, e.colourWrites).colours;
    f.link = ChangesOf(g_source.shared, e.link);
    return f;
}

static MeshFrom FromOf(const ShadowMesh& m, CachedMesh& e) {
    MeshFrom f;
    f.shared = m.shared;
    f.positions = m.moved ? nullptr : m.positions;
    f.positionCount = m.positionCount;
    f.uvs = m.uvs;
    f.uvCount = m.uvCount;
    f.corners = m.corners;
    f.cornerCount = m.cornerCount;
    f.indices = m.indices;
    f.indexCount = m.indexCount;
    f.own = ChangesOf(m.owner, e.own);
    f.link = ChangesOf(m.shared, e.link);
    return f;
}

// Corner `i`'s position, texture coordinates and normal.
static MeshVertex CornerVertex(const MeshDraw& m, int i) {
    const MeshCorner& c = m.corners[i];
    return MeshVertex{PacketVertex(m.positions, c.position), m.uvs[c.texCoord].u, m.uvs[c.texCoord].v,
                      CornerNormal(m, c.normal)};
}

// A caster's corner `i` (KeepCasterMesh): its position and texture coordinates (none: 0, 0), no normal.
static MeshVertex CasterCorner(const ShadowMesh& m, int i) {
    const MeshCorner& c = m.corners[i];
    MeshVertex v{PacketVertex(m.positions, c.position), 0.0f, 0.0f, {}};
    if (m.uvs && c.texCoord < m.uvCount) {
        v.u = m.uvs[c.texCoord].u;
        v.v = m.uvs[c.texCoord].v;
    }
    return v;
}

// The mesh's corners and triangles as the frame's stream builds them (render_mesh.cpp), in `out`;
// false when a corner's position is out of range (nothing drawn).
static bool Build(const MeshDraw& m, CachedMesh& out) {
    out.vertices.clear();
    out.colours.clear();
    for (int i = 0; i < m.cornerCount; ++i) {
        const MeshCorner& c = m.corners[i];
        if (c.position >= m.positionCount) return false;
        out.vertices.push_back(CornerVertex(m, i));
        out.colours.push_back(m.colours[c.normal]);
        out.colours.push_back(m.speculars[c.normal]);
    }
    out.indices.clear();
    AppendTriangles(m.indices, m.indexCount, m.cornerCount, 0, out.indices);
    return true;
}

// A caster's corners and triangles (no colours), as Build.
static bool Build(const ShadowMesh& m, CachedMesh& out) {
    out.vertices.clear();
    out.colours.clear();
    for (int i = 0; i < m.cornerCount; ++i) {
        if (m.corners[i].position >= m.positionCount) return false;
        out.vertices.push_back(CasterCorner(m, i));
    }
    out.indices.clear();
    AppendTriangles(m.indices, m.indexCount, m.cornerCount, 0, out.indices);
    return true;
}

// Reads every corner again (`corner(i)`: CornerVertex or CasterCorner); the changed ones are marked for upload.
template <class Corner>
static bool ReadPositions(int cornerCount, CachedMesh& e, Corner corner) {
    bool changed = false;
    for (int i = 0; i < cornerCount; ++i) {
        MeshVertex now = corner(i);
        MeshVertex& kept = e.vertices[static_cast<std::size_t>(i)];
        if (std::memcmp(&kept, &now, sizeof(MeshVertex)) == 0) continue;
        kept = now;
        e.MarkVertex(static_cast<std::uint32_t>(i));
        changed = true;
    }
    return changed;
}

static bool ReadColours(const MeshDraw& m, CachedMesh& e) {
    bool changed = false;
    for (int i = 0; i < m.cornerCount; ++i) {
        int index = m.corners[i].normal;
        std::uint32_t* kept = &e.colours[2 * static_cast<std::size_t>(i)];
        if (kept[0] == m.colours[index] && kept[1] == m.speculars[index]) continue;
        kept[0] = m.colours[index];
        kept[1] = m.speculars[index];
        e.MarkColour(static_cast<std::uint32_t>(i));
        changed = true;
    }
    return changed;
}

#if defined(EI_RENDER_VALIDATION)
// The kept mesh against a fresh build of the draw; a stale one takes the fresh build (all of it uploaded).
static void Check(const MeshDraw& m, CachedMesh& e) {
    ++g_checks;
    if (!Build(m, g_fresh)) return;
    if (g_fresh.vertices.size() == e.vertices.size() && g_fresh.indices == e.indices && g_fresh.colours == e.colours &&
        std::memcmp(g_fresh.vertices.data(), e.vertices.data(), e.vertices.size() * sizeof(MeshVertex)) == 0)
        return;
    bool geometry = g_fresh.vertices.size() != e.vertices.size() || g_fresh.indices != e.indices ||
                    std::memcmp(g_fresh.vertices.data(), e.vertices.data(), e.vertices.size() * sizeof(MeshVertex)) != 0;
    if (++g_stale <= 20) {
        ei::Log("render: a mesh kept on the GPU was stale (%s): figure mesh %p, link figure %p, %d corners%s",
                geometry ? "positions" : "colours", static_cast<const void*>(g_source.owner),
                static_cast<const void*>(g_source.shared), m.cornerCount,
                g_source.moved ? ", moved positions" : "");
        for (std::size_t i = 0; i < e.vertices.size() && i < g_fresh.vertices.size(); ++i) {
            const MeshVertex& a = e.vertices[i];
            const MeshVertex& b = g_fresh.vertices[i];
            if (std::memcmp(&a, &b, sizeof(a)) != 0) {
                ei::Log("render:   corner %u kept %g %g %g / %g %g, now %g %g %g / %g %g (positions %p uvs %p)",
                        static_cast<unsigned>(i), a.position.x, a.position.y, a.position.z, a.u, a.v, b.position.x,
                        b.position.y, b.position.z, b.u, b.v, m.positions, m.uvs);
                break;
            }
        }
        for (std::size_t i = 0; i < e.colours.size() && i < g_fresh.colours.size(); ++i)
            if (e.colours[i] != g_fresh.colours[i]) {
                ei::Log("render:   colour %u kept %08X now %08X (colours %p, frame %u, last %u)", static_cast<unsigned>(i),
                        e.colours[i], g_fresh.colours[i], m.colours, FrameNumber(), e.lastUsed);
                break;
            }
    }
    e.vertices.swap(g_fresh.vertices);
    e.colours.swap(g_fresh.colours);
    e.indices.swap(g_fresh.indices);
    e.dirty = true;
    if (geometry) {
        e.version = ++g_lastVersion;
        e.boxKept = false;
    }
}

// The kept box against the positions' box now.
static void CheckBox(const MeshDraw& m, CachedMesh& e) {
    float fresh[6];
    if (!e.boxKept || !PositionBox(m, fresh) || std::memcmp(fresh, e.box, sizeof(fresh)) == 0) return;
    LogFirst(g_stale, 20, "render: a kept mesh's box was stale: figure mesh %p, low %g %g %g, now %g %g %g",
             static_cast<const void*>(g_source.owner), e.box[0], e.box[1], e.box[2], fresh[0], fresh[1], fresh[2]);
    e.boxKept = false;
}

// A kept caster against a fresh build of what the walk handed over.
static void Check(const ShadowMesh& m, CachedMesh& e) {
    ++g_checks;
    if (!Build(m, g_fresh)) return;
    if (g_fresh.vertices.size() == e.vertices.size() && g_fresh.indices == e.indices &&
        std::memcmp(g_fresh.vertices.data(), e.vertices.data(), e.vertices.size() * sizeof(MeshVertex)) == 0)
        return;
    LogFirst(g_stale, 20, "render: a caster kept on the GPU was stale: figure mesh %p, link figure %p, %d corners%s",
             static_cast<const void*>(m.owner), static_cast<const void*>(m.shared), m.cornerCount,
             m.moved ? ", moved positions" : "");
    e.vertices.swap(g_fresh.vertices);
    e.indices.swap(g_fresh.indices);
    e.dirty = true;
    e.version = ++g_lastVersion;
}
#endif

CachedMesh* KeepMesh(const MeshDraw& m) {
    if (g_haveSource && g_source.moved) {
        ++g_stats.counts[FrameStats::Count::MorphMeshes];
        g_stats.counts[FrameStats::Count::MorphCorners] += static_cast<std::uint64_t>(m.cornerCount > 0 ? m.cornerCount : 0);
    }
    if (!g_haveSource || !g_source.owner || !g_source.slots || !m.corners || g_interface) return nullptr;
    if (!g_cacheOn) return nullptr;
    bool made = false;
    CachedMesh& e = g_meshes.Take(g_source.owner, g_source.slots->drawn, &made);
    MeshFrom now = FromOf(m, e);
    bool build = made || !e.valid || !e.from.SameArrays(now);
    bool move = !build && !e.from.SamePositions(now);
    bool relight = !build && (e.relightNext || !e.from.SameColours(now));
    bool check = false;
#if defined(EI_RENDER_VALIDATION)
    check = !build && FrameNumber() % kCheckEvery == 0;
#endif
    // Drawn already this frame with the data kept (the GPU reads it at the frame's end): a draw with
    // other data goes into the frame's stream.
    // (What changed is read at the next draw: e.from and relightNext stay as they are.)
    if (e.drawn && e.lastUsed == FrameNumber() && (build || move || relight || check)) return nullptr;
    e.relightNext = false;

    FrameStats::Reuse reuse = FrameStats::Reuse::Same;
    if (build || move) e.boxKept = false;
    if (build) {
        e.valid = Build(m, e);
        e.version = ++g_lastVersion;
        if (!e.valid) return nullptr;
        e.WriteAll();
        reuse = FrameStats::Reuse::New;
    } else {
        if (move && ReadPositions(m.cornerCount, e, [&](int i) { return CornerVertex(m, i); })) {
            reuse = FrameStats::Reuse::Moved;
            e.version = ++g_lastVersion;
        }
        if (relight && ReadColours(m, e) && reuse == FrameStats::Reuse::Same) reuse = FrameStats::Reuse::Relit;
#if defined(EI_RENDER_VALIDATION)
        if (check) {
            Check(m, e);
            CheckBox(m, e);
        }
#endif
    }
    e.from = now;
    e.drawn = true;
    e.lastUsed = FrameNumber();
    if (!g_pool.Placed(e.place)) e.dirty = true;
    if (e.HasWrites()) AddPending(g_pending, e);
    ++g_stats.meshReuse[reuse];
    return &e;
}

CachedMesh* KeepCasterMesh(const ShadowMesh& m) {
    if (!m.owner || !m.slots || !m.positions || !m.corners || !m.indices || m.cornerCount <= 0) return nullptr;
    bool made = false;
    CachedMesh& e = g_casterMeshes.Take(m.owner, m.slots->caster, &made);
    MeshFrom now = FromOf(m, e);
    const bool usable = e.valid && !e.indices.empty();
    if (!made && e.lastUsed == FrameNumber()) return usable ? &e : nullptr;   // read once a frame
    bool build = made || !e.valid || !e.from.SameArrays(now);
    build = build || !g_cacheOn;   // off: built every frame
    if (build) {
        e.valid = Build(m, e);
        e.version = ++g_lastVersion;
        e.WriteAll();
    } else {
        if (!e.from.SamePositions(now) && ReadPositions(m.cornerCount, e, [&](int i) { return CasterCorner(m, i); }))
            e.version = ++g_lastVersion;
#if defined(EI_RENDER_VALIDATION)
        if (FrameNumber() % kCheckEvery == 0) Check(m, e);
#endif
    }
    e.from = now;
    e.drawn = true;
    e.lastUsed = FrameNumber();
    if (g_frameCastersAt != e.lastUsed) {
        g_frameCasters.clear();
        g_frameCastersAt = e.lastUsed;
    }
    g_frameCasters.push_back(&e);
    if (!e.valid || e.indices.empty()) return nullptr;
    if (!g_pool.Placed(e.place)) e.dirty = true;
    if (e.HasWrites()) AddPending(g_pending, e);
    return &e;
}

bool PositionBox(const MeshDraw& m, float box[6]) {
    if (!m.positions || m.positionCount <= 0) return false;
    for (int k = 0; k < 3; ++k) {
        box[k] = 1e30f;
        box[3 + k] = -1e30f;
    }
    for (int i = 0; i < m.positionCount; ++i) {
        const Vector3 p = PacketVertex(m.positions, i);
        const float v[3] = {p.x, p.y, p.z};
        for (int k = 0; k < 3; ++k) {
            box[k] = v[k] < box[k] ? v[k] : box[k];
            box[3 + k] = v[k] > box[3 + k] ? v[k] : box[3 + k];
        }
    }
    return true;
}

void CachedMeshBox(CachedMesh& kept, const MeshDraw& m, float box[6]) {
    if (!kept.boxKept) kept.boxKept = PositionBox(m, kept.box);
    std::memcpy(box, kept.box, sizeof(kept.box));
}

void NoteMeshDraw(const MeshDraw& m) {
    if (!g_haveSource || !g_source.owner || !(g_source.coloursWritten || m.coloursWritten)) return;
    // The colours were written whether or not the draw reaches KeepMesh (a draw outside a frame, without
    // passes, streamed): the kept mesh reads them at its next draw.
    if (!g_source.slots) return;
    if (CachedMesh* kept = g_meshes.Of(g_source.owner, g_source.slots->drawn)) kept->relightNext = true;
}

const FigureMesh* MeshSourceOwner() { return g_haveSource ? g_source.owner : nullptr; }

const MeshLighting* MeshSourceLighting() { return g_haveSource ? g_source.lighting : nullptr; }

Vector3 CornerNormal(const MeshDraw& m, int index) {
    if (!m.normals || !m.words || index >= m.wordCount) return Vector3{};
    const int normal = static_cast<int>(m.words[index] >> 16);
    if (normal >= m.normalCount) return Vector3{};
    return PacketVertex(m.normals, normal);
}

int CachedIndexCount(const CachedMesh& mesh) { return static_cast<int>(mesh.indices.size()); }

int CachedVertexCount(const CachedMesh& mesh) { return static_cast<int>(mesh.vertices.size()); }

Vector3 CachedCorner(const CachedMesh& mesh, int k) { return mesh.vertices[mesh.indices[static_cast<std::size_t>(k)]].position; }

std::uint32_t CachedMeshVersion(const CachedMesh& mesh) { return mesh.version; }

void UploadCachedMeshes(const Frame& f) {
    // The frame's meshes: those its batches draw and the casters read this frame.
    auto frameMeshes = [&](auto add) {
        for (const MeshBatch& b : f.meshBatches)
            if (b.cached) add(*b.cached);
        if (g_frameCastersAt == FrameNumber())
            for (CachedMesh* caster : g_frameCasters)
                if (caster->valid) add(*caster);
    };
    UploadKept(g_pool, g_pending, frameMeshes, FrameStats::Count::MeshVertexBytes, FrameStats::Count::MeshIndexBytes);
}

bool BindCachedMesh(const CachedMesh& mesh, Geometry& g, int* firstIndex) {
    if (!g_pool.Placed(mesh.place)) return false;
    g_pool.Bind(g);
    *firstIndex = static_cast<int>(mesh.place.firstIndex);
    return true;
}

void MeshCacheFrameDone() {
    const std::uint32_t now = FrameNumber();
    if (now % 64 != 0) return;
    for (MeshSlotPool* meshes : {&g_meshes, &g_casterMeshes})
        meshes->DropIf([now](const CachedMesh& mesh) { return !mesh.pending && now - mesh.lastUsed > kMeshLife; });
}

void SetMeshSource(const MeshSource* source) {
    g_haveSource = source != nullptr;
    g_source = source ? *source : MeshSource{};
}

void MeshChanged(const void* object, MeshChange change) {
    if (!object) return;
    auto [entry, made] = g_changes.try_emplace(object);
    if (made) ++g_changesMade;
    MeshChanges& c = entry->second;
    switch (change) {
    case MeshChange::Positions:
        ++c.positions;
        break;
    case MeshChange::Colours:
        ++c.colours;
        break;
    default:
        ++c.rebuilt;
        break;
    }
}

void SetInterfaceDraw(bool on) { g_interface = on; }

void SetMeshCache(bool on) {
    if (on != g_cacheOn) ei::Log("render: figure meshes %s", on ? "kept on the GPU" : "built and uploaded every frame");
    // What changed while off was not read: every kept mesh is built again at its next draw.
    if (on && !g_cacheOn)
        for (MeshSlotPool* meshes : {&g_meshes, &g_casterMeshes})
            meshes->ForEach([](CachedMesh& mesh) { mesh.valid = false; });
    g_cacheOn = on;
}

MeshCacheInfo MeshCacheState() {
    MeshCacheInfo info;
    info.on = g_cacheOn;
    info.meshes = g_meshes.Live();
    info.checks = g_checks;
    info.stale = g_stale;
    return info;
}

} // namespace render
