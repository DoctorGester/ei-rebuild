// Meshes kept on the GPU, built again only where the game says they changed. See docs/notes/renderer.md,
// "The renderer's source files".
#include "render.h"
#include "render_internal.h"

#include "../platform/log.h"

#include <cstring>
#include <unordered_map>

namespace render {

// A corner's geometry; its colours (colour, specular) are a stream of their own.
struct MeshVertex {
    Vector3 position;
    float u = 0.0f, v = 0.0f;
    Vector3 normal;   // CornerNormal
};
static_assert(sizeof(MeshVertex) == kCachedVertexBytes);

struct MeshChanges {
    std::uint32_t positions = 0, colours = 0, rebuilt = 0;
};
static std::unordered_map<const void*, MeshChanges> g_changes;

static MeshChanges ChangesOf(const void* object) {
    if (!object) return MeshChanges{};
    auto it = g_changes.find(object);
    return it == g_changes.end() ? MeshChanges{} : it->second;
}

// What a kept mesh was built from: when an array or count differs, it is built again.
struct MeshFrom {
    const void* shared = nullptr;
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
struct CachedMesh {
    MeshFrom from;
    bool valid = false;                  // built (a corner out of range: not)
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> colours;
    std::vector<std::uint32_t> indices;
    PoolPlace place;
    bool dirty = true;                               // all of it to upload
    std::uint32_t vertexFrom = 0, vertexTo = 0;
    std::uint32_t colourFrom = 0, colourTo = 0;
    bool pending = false;
    bool drawn = false;
    bool relightNext = false;                        // a draw not kept wrote the colours (NoteMeshDraw)
    std::uint32_t lastUsed = 0;
    std::uint32_t mark = 0;
    std::uint32_t version = 0;                       // CachedMeshVersion
    bool boxKept = false;                            // CachedMeshBox's box taken since the positions changed
    float box[6] = {};
};

static std::unordered_map<const void*, CachedMesh> g_meshes;
static std::unordered_map<const void*, CachedMesh> g_casterMeshes;   // KeepCasterMesh's, by part (no colours)
static std::uint32_t g_lastVersion = 0;   // the last CachedMesh::version given
static std::vector<CachedMesh*> g_pending;
static GpuPool g_pool(sizeof(MeshVertex), 1u << 16, 3u << 16, "ei-mesh");
// A mesh not drawn for this many frames is dropped (its pool room comes back at the next Restart).
constexpr std::uint32_t kMeshLife = 600;

#if defined(EI_RENDER_VALIDATION)
static bool g_cacheOn = true;
static std::uint64_t g_checks = 0, g_stale = 0;
// Every this many frames each kept mesh drawn is compared with a fresh build.
constexpr std::uint32_t kCheckEvery = 16;
static CachedMesh g_fresh;   // the fresh build
#endif

static MeshFrom FromOf(const MeshDraw& m) {
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
    f.own = ChangesOf(g_source.owner);
    f.colourWrites = ChangesOf(m.colours).colours;
    f.link = ChangesOf(g_source.shared);
    return f;
}

static MeshFrom FromOf(const ShadowMesh& m) {
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
    f.own = ChangesOf(m.owner);
    f.link = ChangesOf(m.shared);
    return f;
}

// Vertex `index` of the packets: lane index % 4 of packet index / 4.
static Vector3 PacketVertex(const VertexPacket* packets, int index) {
    const VertexPacket& packet = packets[index / 4];
    const int lane = index % 4;
    return Vector3{packet.x()[lane], packet.y()[lane], packet.z()[lane]};
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

// The triangles over `cornerCount` corners into `out`; one with a corner out of range is left out.
static void BuildTriangles(const std::uint16_t* indices, int indexCount, int cornerCount, std::vector<std::uint32_t>& out) {
    out.clear();
    for (int k = 0; k + 2 < indexCount; k += 3) {
        if (indices[k] >= cornerCount || indices[k + 1] >= cornerCount || indices[k + 2] >= cornerCount) continue;
        for (int j = 0; j < 3; ++j) out.push_back(indices[k + j]);
    }
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
    BuildTriangles(m.indices, m.indexCount, m.cornerCount, out.indices);
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
    BuildTriangles(m.indices, m.indexCount, m.cornerCount, out.indices);
    return true;
}

static void Mark(std::uint32_t i, std::uint32_t& from, std::uint32_t& to) {
    if (to <= from) {
        from = i;
        to = i + 1;
    } else {
        if (i < from) from = i;
        if (i + 1 > to) to = i + 1;
    }
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
        Mark(static_cast<std::uint32_t>(i), e.vertexFrom, e.vertexTo);
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
        Mark(static_cast<std::uint32_t>(i), e.colourFrom, e.colourTo);
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
    if (geometry) {
        e.version = ++g_lastVersion;
        e.boxKept = false;
    }
}

// The kept box against the positions' box now.
static void CheckBox(const MeshDraw& m, CachedMesh& e) {
    float fresh[6];
    if (!e.boxKept || !PositionBox(m, fresh) || std::memcmp(fresh, e.box, sizeof(fresh)) == 0) return;
    LogFirst(g_stale, 20, "render: a kept mesh's box was stale: figure mesh %p, low %g %g %g, now %g %g %g", g_source.owner,
                e.box[0], e.box[1], e.box[2], fresh[0], fresh[1], fresh[2]);
    e.boxKept = false;
}

// A kept caster against a fresh build of what the walk handed over.
static void Check(const ShadowMesh& m, CachedMesh& e) {
    ++g_checks;
    if (!Build(m, g_fresh)) return;
    if (g_fresh.vertices.size() == e.vertices.size() && g_fresh.indices == e.indices &&
        std::memcmp(g_fresh.vertices.data(), e.vertices.data(), e.vertices.size() * sizeof(MeshVertex)) == 0)
        return;
    LogFirst(g_stale, 20, "render: a caster kept on the GPU was stale: figure mesh %p, link figure %p, %d corners%s", m.owner, m.shared,
                m.cornerCount, m.moved ? ", moved positions" : "");
    e.vertices.swap(g_fresh.vertices);
    e.indices.swap(g_fresh.indices);
    e.dirty = true;
    e.version = ++g_lastVersion;
}
#endif

static bool Place(CachedMesh& e) {
    bool anew = false;
    if (!g_pool.Place(e.place, static_cast<std::uint32_t>(e.vertices.size()), static_cast<std::uint32_t>(e.indices.size()),
                      &anew))
        return false;
    if (anew) e.dirty = true;
    return true;
}

// The pool starts again with the meshes of frame `f` (and the pending ones), placed again from its start.
static void Restart(const Frame& f) {
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
    for (auto& entry : g_casterMeshes)
        if (entry.second.lastUsed == FrameNumber() && entry.second.valid) take(&entry.second);
    for (CachedMesh* e : g_pending) take(e);
    g_pool.Restart(vertices, indices);
    for (CachedMesh* e : meshes) {
        if (!e->pending) {
            e->pending = true;
            g_pending.push_back(e);
        }
    }
}

static void Write(CachedMesh& e) {
    FrameStats& stats = g_stats;
    if (e.dirty) {
        auto count = static_cast<std::uint32_t>(e.vertices.size());
        g_pool.WriteGeometry(e.place, 0, count, e.vertices.data());
        if (!e.colours.empty()) g_pool.WriteColours(e.place, 0, count, e.colours.data());   // a caster has none
        g_pool.WriteIndices(e.place, e.indices.data(), e.indices.size());
        stats.meshVertexBytes += e.vertices.size() * sizeof(MeshVertex) + e.colours.size() * sizeof(std::uint32_t);
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

CachedMesh* KeepMesh(const MeshDraw& m) {
    if (g_haveSource && g_source.moved) {
        ++g_stats.morphMeshes;
        g_stats.morphCorners += static_cast<std::uint64_t>(m.cornerCount > 0 ? m.cornerCount : 0);
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
    if (build || move) e.boxKept = false;
    if (build) {
        e.valid = Build(m, e);
        e.version = ++g_lastVersion;
        if (!e.valid) return nullptr;
        e.dirty = true;
        e.vertexFrom = e.vertexTo = e.colourFrom = e.colourTo = 0;
        reuse = FrameStats::New;
    } else {
        if (move && ReadPositions(m.cornerCount, e, [&](int i) { return CornerVertex(m, i); })) {
            reuse = FrameStats::Moved;
            e.version = ++g_lastVersion;
        }
        if (relight && ReadColours(m, e) && reuse == FrameStats::Same) reuse = FrameStats::Relit;
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
    if ((e.dirty || e.vertexTo > e.vertexFrom || e.colourTo > e.colourFrom) && !e.pending) {
        e.pending = true;
        g_pending.push_back(&e);
    }
    ++g_stats.meshReuse[reuse];
    return &e;
}

CachedMesh* KeepCasterMesh(const ShadowMesh& m) {
    if (!m.owner || !m.positions || !m.corners || !m.indices || m.cornerCount <= 0) return nullptr;
    MeshFrom now = FromOf(m);
    auto [found, made] = g_casterMeshes.try_emplace(m.owner);
    CachedMesh& e = found->second;
    const bool usable = e.valid && !e.indices.empty();
    if (!made && e.lastUsed == FrameNumber()) return usable ? &e : nullptr;   // read once a frame
    bool build = made || !e.valid || !e.from.SameArrays(now);
#if defined(EI_RENDER_VALIDATION)
    build = build || !g_cacheOn;   // off: built every frame
#endif
    if (build) {
        e.valid = Build(m, e);
        e.version = ++g_lastVersion;
        e.dirty = true;
        e.vertexFrom = e.vertexTo = e.colourFrom = e.colourTo = 0;
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
    if (!e.valid || e.indices.empty()) return nullptr;
    if (!g_pool.Placed(e.place)) e.dirty = true;
    if ((e.dirty || e.vertexTo > e.vertexFrom) && !e.pending) {
        e.pending = true;
        g_pending.push_back(&e);
    }
    return &e;
}

bool PositionBox(const MeshDraw& m, float box[6]) {
    if (!m.positions || m.positionCount <= 0) return false;
    for (int k = 0; k < 3; ++k) {
        box[k] = 1e30f;
        box[3 + k] = -1e30f;
    }
    for (int i = 0; i < m.positionCount; ++i) {
        const VertexPacket& packet = m.positions[i / 4];
        int lane = i % 4;
        for (int k = 0; k < 3; ++k) {
            float v = packet.block[k][lane];
            box[k] = v < box[k] ? v : box[k];
            box[3 + k] = v > box[3 + k] ? v : box[3 + k];
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
    auto it = g_meshes.find(g_source.owner);
    if (it != g_meshes.end()) it->second.relightNext = true;
}

const void* MeshSourceOwner() { return g_haveSource ? g_source.owner : nullptr; }

const MeshLighting* MeshSourceLighting() { return g_haveSource ? g_source.lighting : nullptr; }

Vector3 CornerNormal(const MeshDraw& m, int index) {
    if (!m.normals || !m.words || index >= m.wordCount) return Vector3{};
    const int normal = static_cast<int>(m.words[index] >> 16);
    if (normal >= m.normalCount) return Vector3{};
    const NormalPacket& packet = m.normals[normal / 4];
    const int lane = normal % 4;
    return Vector3{packet.block[0][lane], packet.block[1][lane], packet.block[2][lane]};
}

int CachedIndexCount(const CachedMesh& mesh) { return static_cast<int>(mesh.indices.size()); }

int CachedVertexCount(const CachedMesh& mesh) { return static_cast<int>(mesh.vertices.size()); }

Vector3 CachedCorner(const CachedMesh& mesh, int k) { return mesh.vertices[mesh.indices[static_cast<std::size_t>(k)]].position; }

std::uint32_t CachedMeshVersion(const CachedMesh& mesh) { return mesh.version; }

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

bool BindCachedMesh(const CachedMesh& mesh, Geometry& g, int* firstIndex) {
    if (!g_pool.Placed(mesh.place)) return false;
    g_pool.Bind(g);
    *firstIndex = static_cast<int>(mesh.place.firstIndex);
    return true;
}

void MeshCacheFrameDone() {
    std::uint32_t now = FrameNumber();
    if (now % 64 != 0) return;
    for (auto* meshes : {&g_meshes, &g_casterMeshes})
        for (auto it = meshes->begin(); it != meshes->end();) {
            if (!it->second.pending && now - it->second.lastUsed > kMeshLife) it = meshes->erase(it);
            else ++it;
        }
}

void SetMeshSource(const MeshSource* source) {
    g_haveSource = source != nullptr;
    g_source = source ? *source : MeshSource{};
}

void MeshChanged(const void* object, MeshChange change) {
    if (!object) return;
    MeshChanges& c = g_changes[object];
    switch (change) {
    case MeshChange::Positions: ++c.positions; break;
    case MeshChange::Colours: ++c.colours; break;
    default: ++c.rebuilt; break;
    }
}

void SetInterfaceDraw(bool on) { g_interface = on; }

#if defined(EI_RENDER_VALIDATION)
void SetMeshCache(bool on) {
    if (on != g_cacheOn) ei::Log("render: figure meshes %s", on ? "kept on the GPU" : "built and uploaded every frame");
    // What changed while off was not read: every kept mesh is built again at its next draw.
    if (on && !g_cacheOn)
        for (auto* meshes : {&g_meshes, &g_casterMeshes})
            for (auto& m : *meshes) m.second.valid = false;
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
