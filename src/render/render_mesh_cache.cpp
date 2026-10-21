// Meshes kept on the GPU (R16d; docs/notes/renderer.md section 13, item 4). The figure part draws
// (005A7A70, 005A7640, src/game/figure_part_draw*.cpp) name the figure mesh a mesh draw draws
// (render::SetMeshSource); the mesh's corners are kept for that figure mesh: their positions and texture
// coordinates in model space, their colours and the triangles, in a pool on the GPU (GpuPool: the
// geometry and the colours in streams of their own). A frame hands over the mesh as kept; what is
// built and uploaded again is what the game says changed:
// - the colours, when the draw wrote the colour arrays (lit, flat colours, the device's colour) or the
//   game lit the mesh outside its draw (MeshChanged Colours: the sky, CSkyObject::Light; a link
//   figure's LightPart / FlatColours on the colour array, called by the original code too) or a draw
//   that wrote them was not kept (NoteMeshDraw): read and compared with the kept ones, the changed
//   range uploaded;
// - the positions, when the game moved them (render::MeshChanged Positions: the animation's per-vertex
//   offsets, CFigureMesh::SetOffsets; a link figure's part morphed again, CLinkMesh::SetPartVertices):
//   read and compared, the changed range uploaded;
// - all of it, when the mesh's buffers changed (MeshChanged Rebuilt: loaded, cleared, mirrored, parts
//   added or removed, destroyed) or the draw hands over other arrays or counts than it was built from.
// Interface sprites (render::SetInterfaceDraw: the interface rewrites their quads in place) and a mesh
// drawn again in the same frame with other data go into the frame's stream as before.
// The texture offsets (an animated mesh's frame of its sheet) are the draw's, added in the shader. The
// harness build compares every kept mesh with a fresh build every 16th frame (a change the game did not
// tell of: `meshcache check`); `meshcache off` builds every mesh into the frame's stream as before.
#include "render.h"
#include "render_internal.h"

#include "../hook/log.h"

#include <cstring>
#include <unordered_map>

namespace render {

namespace {

using namespace detail;

// A corner's geometry; its colours (colour, specular) are a stream of their own.
struct MeshVertex {
    Vector3 position;
    float u, v;
};
static_assert(sizeof(MeshVertex) == kCachedVertexBytes);

// How often the game changed an object (a figure mesh or a link figure) in place (MeshChanged).
struct MeshChanges {
    std::uint32_t positions = 0, colours = 0, rebuilt = 0;
};
std::unordered_map<const void*, MeshChanges> g_changes;

MeshChanges ChangesOf(const void* object) {
    if (!object) return MeshChanges{};
    auto it = g_changes.find(object);
    return it == g_changes.end() ? MeshChanges{} : it->second;
}

// What a kept mesh was built from: when an array or count differs, it is built again.
struct MeshFrom {
    const void* shared = nullptr;
    const float* positions = nullptr;   // null for moved positions (a work array all moved meshes share)
    int positionCount = 0;
    const std::uint32_t* colours = nullptr;
    const std::uint32_t* speculars = nullptr;
    const float* uvs = nullptr;
    const std::uint16_t* corners = nullptr;
    int cornerCount = 0;
    const std::uint16_t* indices = nullptr;
    int indexCount = 0;
    MeshChanges own, link;   // the figure mesh's and the link figure's changes when built
    std::uint32_t colourWrites = 0;   // the colour array's (MeshChanged on the array's data)

    bool SameArrays(const MeshFrom& o) const {
        return shared == o.shared && positions == o.positions && positionCount == o.positionCount && colours == o.colours &&
               speculars == o.speculars && uvs == o.uvs && corners == o.corners && cornerCount == o.cornerCount &&
               indices == o.indices && indexCount == o.indexCount && own.rebuilt == o.own.rebuilt &&
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
MeshSource g_source{};
bool g_haveSource = false;
bool g_interface = false;   // SetInterfaceDraw

} // namespace

namespace detail {

// A figure mesh's corners as kept, and their place in the pool.
struct CachedMesh {
    MeshFrom from;
    bool valid = false;                  // built (a corner out of range: not)
    std::vector<MeshVertex> vertices;    // per corner
    std::vector<std::uint32_t> colours;  // colour, specular per corner
    std::vector<std::uint32_t> indices;  // the valid triangles, relative to the first corner
    PoolPlace place;
    bool dirty = true;                               // all of it to upload
    std::uint32_t vertexFrom = 0, vertexTo = 0;      // corners whose positions to upload
    std::uint32_t colourFrom = 0, colourTo = 0;      // corners whose colours to upload
    bool pending = false;                            // in g_pending
    bool drawn = false;                              // handed over in a frame yet
    bool relightNext = false;                        // a draw not kept wrote the colours (NoteMeshDraw)
    std::uint32_t lastUsed = 0;                      // FrameNumber() of its last hand-over
    std::uint32_t mark = 0;                          // Restart's
};

} // namespace detail

namespace {

std::unordered_map<const void*, CachedMesh> g_meshes;   // by figure mesh
std::vector<CachedMesh*> g_pending;                     // meshes with something to upload
GpuPool g_pool(sizeof(MeshVertex), 1u << 16, 3u << 16, "ei-mesh");
// A mesh not drawn for this many frames is dropped (its pool room comes back at the next Restart).
constexpr std::uint32_t kMeshLife = 600;

#if defined(EI_RENDER_VALIDATION)
bool g_cacheOn = true;
std::uint64_t g_checks = 0, g_stale = 0;
// Every this many frames each kept mesh drawn is compared with a fresh build.
constexpr std::uint32_t kCheckEvery = 16;
CachedMesh g_fresh;   // the fresh build
#endif

MeshFrom FromOf(const MeshDraw& m) {
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
    f.own = ChangesOf(g_source.owner);
    f.colourWrites = ChangesOf(m.colours).colours;
    f.link = ChangesOf(g_source.shared);
    return f;
}

// Corner `i`'s position (blocks of four vertices: x[4], y[4], z[4], 4 unused) and texture coordinates.
MeshVertex CornerVertex(const MeshDraw& m, int i) {
    const std::uint16_t* c = m.corners + i * 3;
    const float* block = m.positions + (c[0] / 4) * 16;
    int lane = c[0] % 4;
    return MeshVertex{{block[lane], block[4 + lane], block[8 + lane]}, m.uvs[c[2] * 2], m.uvs[c[2] * 2 + 1]};
}

// The mesh's corners and triangles as the frame's stream builds them (render_mesh.cpp), in `out`;
// false when a corner's position is out of range (nothing drawn).
bool Build(const MeshDraw& m, CachedMesh& out) {
    out.vertices.clear();
    out.colours.clear();
    out.indices.clear();
    for (int i = 0; i < m.cornerCount; ++i) {
        const std::uint16_t* c = m.corners + i * 3;
        if (c[0] >= m.positionCount) return false;
        out.vertices.push_back(CornerVertex(m, i));
        out.colours.push_back(m.colours[c[1]]);
        out.colours.push_back(m.speculars[c[1]]);
    }
    for (int k = 0; k + 2 < m.indexCount; k += 3) {
        if (m.indices[k] >= m.cornerCount || m.indices[k + 1] >= m.cornerCount || m.indices[k + 2] >= m.cornerCount) continue;
        for (int j = 0; j < 3; ++j) out.indices.push_back(m.indices[k + j]);
    }
    return true;
}

// Corner `i` into the range [from, to) of corners to upload.
void Mark(std::uint32_t i, std::uint32_t& from, std::uint32_t& to) {
    if (to <= from) {
        from = i;
        to = i + 1;
    } else {
        if (i < from) from = i;
        if (i + 1 > to) to = i + 1;
    }
}

// The corners' positions (and texture coordinates) read again; the changed ones marked. True when any.
bool ReadPositions(const MeshDraw& m, CachedMesh& e) {
    bool changed = false;
    for (int i = 0; i < m.cornerCount; ++i) {
        MeshVertex now = CornerVertex(m, i);
        MeshVertex& kept = e.vertices[static_cast<std::size_t>(i)];
        if (std::memcmp(&kept, &now, sizeof(MeshVertex)) == 0) continue;
        kept = now;
        Mark(static_cast<std::uint32_t>(i), e.vertexFrom, e.vertexTo);
        changed = true;
    }
    return changed;
}

// The corners' colours read again; the changed ones marked. True when any.
bool ReadColours(const MeshDraw& m, CachedMesh& e) {
    bool changed = false;
    for (int i = 0; i < m.cornerCount; ++i) {
        int index = m.corners[i * 3 + 1];
        std::uint32_t* kept = &e.colours[2 * static_cast<std::size_t>(i)];
        if (kept[0] == m.colours[index] && kept[1] == m.speculars[index]) continue;
        kept[0] = m.colours[index];
        kept[1] = m.speculars[index];
        Mark(static_cast<std::uint32_t>(i), e.colourFrom, e.colourTo);
        changed = true;
    }
    return changed;
}

#if defined(EI_RENDER_VALIDATION)
// The kept mesh against a fresh build of the draw; a stale one takes the fresh build (all of it uploaded).
void Check(const MeshDraw& m, CachedMesh& e) {
    ++g_checks;
    if (!Build(m, g_fresh)) return;
    if (g_fresh.vertices.size() == e.vertices.size() && g_fresh.indices == e.indices && g_fresh.colours == e.colours &&
        std::memcmp(g_fresh.vertices.data(), e.vertices.data(), e.vertices.size() * sizeof(MeshVertex)) == 0)
        return;
    bool geometry = g_fresh.vertices.size() != e.vertices.size() || g_fresh.indices != e.indices ||
                    std::memcmp(g_fresh.vertices.data(), e.vertices.data(), e.vertices.size() * sizeof(MeshVertex)) != 0;
    if (++g_stale <= 20) {
        ei::Log("render: a mesh kept on the GPU was stale (%s): figure mesh %p, link figure %p, %d corners%s",
                geometry ? "positions" : "colours", g_source.owner, g_source.shared, m.cornerCount,
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
}
#endif

// Room for `e` in the pool (all of it to upload when placed anew). False when the pool is full.
bool Place(CachedMesh& e) {
    bool anew = false;
    if (!g_pool.Place(e.place, static_cast<std::uint32_t>(e.vertices.size()), static_cast<std::uint32_t>(e.indices.size()),
                      &anew))
        return false;
    if (anew) e.dirty = true;
    return true;
}

// The pool starts again with the meshes of frame `f` (and the pending ones), placed again from its start.
void Restart(const Frame& f) {
    static std::uint32_t stamp = 0;
    ++stamp;
    std::uint64_t vertices = 0, indices = 0;
    std::vector<CachedMesh*> meshes;
    auto take = [&](CachedMesh* e) {
        if (e->mark == stamp) return;
        e->mark = stamp;
        vertices += e->vertices.size();
        indices += e->indices.size();
        meshes.push_back(e);
    };
    for (const MeshBatch& b : f.meshBatches)
        if (b.cached) take(b.cached);
    for (CachedMesh* e : g_pending) take(e);
    g_pool.Restart(vertices, indices);
    for (CachedMesh* e : meshes) {
        if (!e->pending) {
            e->pending = true;
            g_pending.push_back(e);
        }
    }
}

// The mesh's changes into its room in the pool: all of it, or the corners that changed.
void Write(CachedMesh& e) {
    FrameStats& stats = Stats();
    if (e.dirty) {
        auto count = static_cast<std::uint32_t>(e.vertices.size());
        g_pool.WriteGeometry(e.place, 0, count, e.vertices.data());
        g_pool.WriteColours(e.place, 0, count, e.colours.data());
        g_pool.WriteIndices(e.place, e.indices.data(), e.indices.size());
        stats.meshVertexBytes += e.vertices.size() * (sizeof(MeshVertex) + kColourPairBytes);
        stats.meshIndexBytes += e.indices.size() * sizeof(std::uint32_t);
    } else {
        if (e.vertexTo > e.vertexFrom) {
            g_pool.WriteGeometry(e.place, e.vertexFrom, e.vertexTo - e.vertexFrom, &e.vertices[e.vertexFrom]);
            stats.meshVertexBytes += (e.vertexTo - e.vertexFrom) * sizeof(MeshVertex);
        }
        if (e.colourTo > e.colourFrom) {
            g_pool.WriteColours(e.place, e.colourFrom, e.colourTo - e.colourFrom,
                                &e.colours[2 * static_cast<std::size_t>(e.colourFrom)]);
            stats.meshVertexBytes += (e.colourTo - e.colourFrom) * kColourPairBytes;
        }
    }
    e.dirty = false;
    e.vertexFrom = e.vertexTo = 0;
    e.colourFrom = e.colourTo = 0;
}

} // namespace

namespace detail {

CachedMesh* KeepMesh(const MeshDraw& m) {
    if (g_haveSource && g_source.moved) {
        ++Stats().morphMeshes;
        Stats().morphCorners += static_cast<std::uint64_t>(m.cornerCount > 0 ? m.cornerCount : 0);
    }
    if (!g_haveSource || !g_source.owner || !m.corners || g_interface) return nullptr;
#if defined(EI_RENDER_VALIDATION)
    if (!g_cacheOn) return nullptr;
#endif
    MeshFrom now = FromOf(m);
    auto [found, made] = g_meshes.try_emplace(g_source.owner);
    CachedMesh& e = found->second;
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

    FrameStats::Reuse reuse = FrameStats::Same;
    if (build) {
        e.valid = Build(m, e);
        if (!e.valid) return nullptr;
        e.dirty = true;
        e.vertexFrom = e.vertexTo = e.colourFrom = e.colourTo = 0;
        reuse = FrameStats::New;
    } else {
        if (move && ReadPositions(m, e)) reuse = FrameStats::Moved;
        if (relight && ReadColours(m, e) && reuse == FrameStats::Same) reuse = FrameStats::Relit;
#if defined(EI_RENDER_VALIDATION)
        if (check) Check(m, e);
#endif
    }
    e.from = now;
    e.drawn = true;
    e.lastUsed = FrameNumber();
    if (!g_pool.Placed(e.place)) e.dirty = true;
    if ((e.dirty || e.vertexTo > e.vertexFrom || e.colourTo > e.colourFrom) && !e.pending) {
        e.pending = true;
        g_pending.push_back(&e);
    }
    ++Stats().meshReuse[reuse];
    return &e;
}

void NoteMeshDraw(const MeshDraw& m) {
    if (!g_haveSource || !g_source.owner || !(g_source.coloursWritten || m.coloursWritten)) return;
    // The colours were written whether or not the draw reaches KeepMesh (a draw outside a frame, without
    // passes, streamed): the kept mesh reads them at its next draw.
    auto it = g_meshes.find(g_source.owner);
    if (it != g_meshes.end()) it->second.relightNext = true;
}

int CachedIndexCount(const CachedMesh& mesh) { return static_cast<int>(mesh.indices.size()); }

void UploadCachedMeshes(const Frame& f) {
    if (g_pending.empty()) return;
    bool placed = true;
    for (CachedMesh* e : g_pending) {
        if (!Place(*e)) {
            placed = false;
            break;
        }
    }
    if (!placed) {
        Restart(f);
        for (CachedMesh* e : g_pending) Place(*e);   // fits: the pool holds twice the frame's meshes
    }
    for (CachedMesh* e : g_pending) {
        if (g_pool.Placed(e->place)) Write(*e);
        e->pending = false;
    }
    g_pending.clear();
}

bool BindCachedMesh(const CachedMesh& mesh, sg_bindings& b, int* firstIndex) {
    if (!g_pool.Placed(mesh.place)) return false;
    g_pool.Bind(b);
    *firstIndex = static_cast<int>(mesh.place.firstIndex);
    return true;
}

void MeshCacheFrameDone() {
    std::uint32_t now = FrameNumber();
    if (now % 64 != 0) return;
    for (auto it = g_meshes.begin(); it != g_meshes.end();) {
        if (!it->second.pending && now - it->second.lastUsed > kMeshLife) it = g_meshes.erase(it);
        else ++it;
    }
}

// The entries keep what they were built from; they are placed in a new pool at their next draw. The
// pool's buffers are destroyed now (the device goes: they may not outlive sokol).
void DropMeshCache() {
    g_pool.Drop();
    RetireBuffers();
}

} // namespace detail

void SetMeshSource(const MeshSource* source) {
    g_haveSource = source != nullptr;
    g_source = source ? *source : MeshSource{};
}

void MeshChanged(const void* object, MeshChange change) {
    if (!object) return;
    MeshChanges& c = g_changes[object];
    ++(change == MeshChange::Positions ? c.positions : change == MeshChange::Colours ? c.colours : c.rebuilt);
}

void SetInterfaceDraw(bool on) { g_interface = on; }

#if defined(EI_RENDER_VALIDATION)
void SetMeshCache(bool on) {
    if (on != g_cacheOn) ei::Log("render: figure meshes %s", on ? "kept on the GPU" : "built and uploaded every frame");
    // What changed while off was not read: every kept mesh is built again at its next draw.
    if (on && !g_cacheOn)
        for (auto& m : g_meshes) m.second.valid = false;
    g_cacheOn = on;
}

MeshCacheInfo MeshCacheState() {
    MeshCacheInfo info;
    info.on = g_cacheOn;
    info.meshes = g_meshes.size();
    info.checks = g_checks;
    info.stale = g_stale;
    return info;
}
#endif

} // namespace render
