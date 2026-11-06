// Meshes kept on the GPU, built again only where the game says they changed, one copy for the figure meshes whose
// built arrays are the same. See docs/notes/renderer.md, "The renderer's source files".
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
    std::uint32_t colour = 0, specular = 0;   // without `colours`: every corner's
    const TexCoord* uvs = nullptr;
    const MeshCorner* corners = nullptr;
    int cornerCount = 0;
    const std::uint16_t* indices = nullptr;
    int indexCount = 0;
    const NormalPacket* normals = nullptr;
    const std::uint32_t* words = nullptr;
    int wordCount = 0;
    MeshChanges own, link;   // the figure mesh's and the link figure's changes when built

    bool SameArrays(const MeshFrom& o) const {
        return shared == o.shared && positions == o.positions && positionCount == o.positionCount && colours == o.colours &&
               speculars == o.speculars && uvs == o.uvs && corners == o.corners &&
               cornerCount == o.cornerCount && indices == o.indices && indexCount == o.indexCount && normals == o.normals && words == o.words &&
               wordCount == o.wordCount && own.rebuilt == o.own.rebuilt &&
               link.rebuilt == o.link.rebuilt;
    }
    bool SamePositions(const MeshFrom& o) const {
        return own.positions == o.own.positions && link.positions == o.link.positions;
    }
    bool SameColours(const MeshFrom& o) const {
        return own.colours == o.own.colours && colour == o.colour && specular == o.specular;
    }
};

// Corners and triangles as kept, and their place in the pool: one copy for every figure mesh whose built arrays are the
// same byte for byte (the palms of one morph draw one link figure's part with one material), found by a hash of the
// arrays and compared whole. A figure mesh whose arrays change takes a copy of its own unless it is the only user.
struct GpuMesh : KeptRanges {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> colours;
    std::vector<std::uint32_t> indices;
    int users = 0;
    std::uint64_t hash = 0;
    bool listed = false;     // in its table under `hash` (its arrays unchanged since)
    bool released = false;   // in GpuMeshes::released
};

// A figure mesh's kept mesh: what it was built from, and its arrays on the GPU.
struct CachedMesh {
    MeshFrom from;
    bool valid = false;                  // built (a corner out of range: not)
    GpuMesh* gpu = nullptr;
    bool drawn = false;
    std::uint32_t version = 0;                       // CachedMeshVersion
    std::uint32_t lastUsed = 0;                      // FrameNumber() of its last draw
    bool boxKept = false;                            // CachedMeshBox's box taken since the positions changed
    float box[6] = {};
    ChangesRef own, link;                            // MeshFrom's changes as last found
};

// The GPU meshes and their table by hash. A mesh no figure mesh uses any more is let go at
// the next FrameDone once it is written (UploadKept still writes one it held); its pool room comes back at the next
// Restart.
class GpuMeshes {
public:
    GpuMesh* Make() {
        GpuMesh* g = nullptr;
        if (!unused.empty()) {
            g = unused.back();
            unused.pop_back();
        } else {
            g = &meshes.emplace_back();
        }
        *g = GpuMesh{};
        g->users = 1;
        ++live;
        return g;
    }
    void Release(GpuMesh*& g) {
        if (g && --g->users == 0 && !g->released) {
            g->released = true;
            released.push_back(g);
        }
        g = nullptr;
    }
    // `kept`'s arrays to change: its own copy (`keep`: with the arrays it shares), out of the table.
    GpuMesh& Own(CachedMesh& kept, bool keep) {
        if (kept.gpu && kept.gpu->users == 1) {
            Unlist(*kept.gpu);
            return *kept.gpu;
        }
        GpuMesh* g = Make();
        if (kept.gpu && keep) {
            g->vertices = kept.gpu->vertices;
            g->colours = kept.gpu->colours;
            g->indices = kept.gpu->indices;
        }
        Release(kept.gpu);
        kept.gpu = g;
        return *g;
    }
    // `kept` takes the GPU mesh with the same arrays as its own, or its own goes into the table.
    void Share(CachedMesh& kept) {
        GpuMesh* g = kept.gpu;
        if (!g || g->listed) return;
        const std::uint64_t hash = Hash(*g);
        for (auto [at, end] = table.equal_range(hash); at != end; ++at) {
            GpuMesh* other = at->second;
            if (other == g || !Same(*other, *g)) continue;
            ++other->users;
            Release(kept.gpu);
            kept.gpu = other;
            ++shared;
            return;
        }
        g->hash = hash;
        g->listed = true;
        table.emplace(hash, g);
    }
    void Unlist(GpuMesh& g) {
        if (!g.listed) return;
        for (auto [at, end] = table.equal_range(g.hash); at != end; ++at)
            if (at->second == &g) {
                table.erase(at);
                break;
            }
        g.listed = false;
    }
    void FrameDone() {
        std::size_t kept = 0;
        for (GpuMesh* g : released) {
            g->released = false;
            if (g->users > 0) continue;   // taken again (Share)
            if (g->pending) {
                g->released = true;
                released[kept++] = g;
                continue;
            }
            Unlist(*g);
            *g = GpuMesh{};
            unused.push_back(g);
            --live;
        }
        released.resize(kept);
    }
    // The meshes some figure mesh uses.
    std::size_t Live() const {
        std::size_t unusedYet = 0;
        for (const GpuMesh* g : released) unusedYet += g->users == 0 ? 1 : 0;
        return live - unusedYet;
    }
    std::uint64_t Shared() const { return shared; }

private:
    static std::uint64_t Hash(const GpuMesh& g) {
        std::uint64_t h = 0xCBF29CE484222325ull;
        auto add = [&h](const void* data, std::size_t bytes) {
            const auto* p = static_cast<const unsigned char*>(data);
            std::size_t k = 0;
            for (; k + 8 <= bytes; k += 8) {
                std::uint64_t word;
                std::memcpy(&word, p + k, 8);
                h = (h ^ word) * 0x100000001B3ull;
                h ^= h >> 29;
            }
            for (; k < bytes; ++k) h = (h ^ p[k]) * 0x100000001B3ull;
            h = (h ^ bytes) * 0x100000001B3ull;
        };
        add(g.vertices.data(), g.vertices.size() * sizeof(MeshVertex));
        add(g.colours.data(), g.colours.size() * sizeof(std::uint32_t));
        add(g.indices.data(), g.indices.size() * sizeof(std::uint32_t));
        return h;
    }
    static bool Same(const GpuMesh& a, const GpuMesh& b) {
        return a.vertices.size() == b.vertices.size() && a.colours == b.colours && a.indices == b.indices &&
               std::memcmp(a.vertices.data(), b.vertices.data(), a.vertices.size() * sizeof(MeshVertex)) == 0;
    }
    std::deque<GpuMesh> meshes;   // their places stay (the kept meshes and g_pending point at them)
    std::vector<GpuMesh*> unused, released;
    std::unordered_multimap<std::uint64_t, GpuMesh*> table;
    std::size_t live = 0;
    std::uint64_t shared = 0;   // figure meshes that took another's copy
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
    // Lets go of every kept mesh `drop(mesh)` says to (its GPU mesh released there).
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
static GpuMeshes g_gpuMeshes;   // their arrays on the GPU
static std::uint32_t g_lastVersion = 0;                              // the last CachedMesh::version given
static std::vector<GpuMesh*> g_pending;
static GpuPool g_pool(sizeof(MeshVertex), 1u << 16, 3u << 16, "ei-mesh");
// A mesh not drawn for this many frames is dropped (its pool room comes back at the next Restart).
constexpr std::uint32_t kMeshLife = 600;

static bool g_cacheOn = true;   // SetMeshCache
static std::uint64_t g_checks = 0, g_stale = 0;
#if defined(EI_RENDER_VALIDATION)
// Every this many frames each kept mesh drawn is compared with a fresh build.
constexpr std::uint32_t kCheckEvery = 16;
static GpuMesh g_fresh;   // the fresh build
#endif

static MeshFrom FromOf(const MeshDraw& m, CachedMesh& e) {
    MeshFrom f;
    f.shared = m.shared;
    f.positions = m.moved ? nullptr : m.positions;
    f.positionCount = m.positionCount;
    f.colours = m.colours;
    f.speculars = m.speculars;
    if (!m.colours) {
        f.colour = m.colour;
        f.specular = m.specular;
    }
    f.uvs = m.uvs;
    f.corners = m.corners;
    f.cornerCount = m.cornerCount;
    f.indices = m.indices;
    f.indexCount = m.indexCount;
    f.normals = m.normals;
    f.words = m.words;
    f.wordCount = m.wordCount;
    f.own = ChangesOf(m.owner, e.own);
    f.link = ChangesOf(m.shared, e.link);
    return f;
}

// Corner `i`'s colour pair.
static ColourPair CornerColours(const MeshDraw& m, int i) {
    if (!m.colours) return ColourPair{m.colour, m.specular};
    const int index = m.corners[i].normal;
    return ColourPair{m.colours[index], m.speculars[index]};
}

// Corner `i`'s position, texture coordinates and normal.
static MeshVertex CornerVertex(const MeshDraw& m, int i) {
    const MeshCorner& c = m.corners[i];
    return MeshVertex{PacketVertex(m.positions, c.position), m.uvs[c.texCoord].u, m.uvs[c.texCoord].v,
                      CornerNormal(m, c.normal)};
}

// The mesh's corners and triangles as the frame's stream builds them (render_mesh.cpp), in `out`;
// false when a corner's position is out of range (nothing drawn).
static bool Build(const MeshDraw& m, GpuMesh& out) {
    out.vertices.clear();
    out.colours.clear();
    for (int i = 0; i < m.cornerCount; ++i) {
        const MeshCorner& c = m.corners[i];
        if (c.position >= m.positionCount) return false;
        out.vertices.push_back(CornerVertex(m, i));
        const ColourPair pair = CornerColours(m, i);
        out.colours.push_back(pair.colour);
        out.colours.push_back(pair.specular);
    }
    out.indices.clear();
    AppendTriangles(m.indices, m.indexCount, m.cornerCount, 0, out.indices);
    return true;
}

// Whether a corner differs from `e`'s.
static bool PositionsDiffer(const MeshDraw& m, const GpuMesh& e) {
    for (int i = 0; i < m.cornerCount; ++i) {
        const MeshVertex now = CornerVertex(m, i);
        if (std::memcmp(&e.vertices[static_cast<std::size_t>(i)], &now, sizeof(MeshVertex)) != 0) return true;
    }
    return false;
}

// Reads every corner again; the changed ones are marked for upload.
static bool ReadPositions(const MeshDraw& m, GpuMesh& e) {
    bool changed = false;
    for (int i = 0; i < m.cornerCount; ++i) {
        MeshVertex now = CornerVertex(m, i);
        MeshVertex& kept = e.vertices[static_cast<std::size_t>(i)];
        if (std::memcmp(&kept, &now, sizeof(MeshVertex)) == 0) continue;
        kept = now;
        e.MarkVertex(static_cast<std::uint32_t>(i));
        changed = true;
    }
    return changed;
}

static bool ColoursDiffer(const MeshDraw& m, const GpuMesh& e) {
    for (int i = 0; i < m.cornerCount; ++i) {
        const ColourPair now = CornerColours(m, i);
        const std::uint32_t* kept = &e.colours[2 * static_cast<std::size_t>(i)];
        if (kept[0] != now.colour || kept[1] != now.specular) return true;
    }
    return false;
}

static bool ReadColours(const MeshDraw& m, GpuMesh& e) {
    bool changed = false;
    for (int i = 0; i < m.cornerCount; ++i) {
        const ColourPair now = CornerColours(m, i);
        std::uint32_t* kept = &e.colours[2 * static_cast<std::size_t>(i)];
        if (kept[0] == now.colour && kept[1] == now.specular) continue;
        kept[0] = now.colour;
        kept[1] = now.specular;
        e.MarkColour(static_cast<std::uint32_t>(i));
        changed = true;
    }
    return changed;
}

// The kept positions read again (MeshFrom says they may have moved): a GPU mesh shared with others is left to them when
// a corner moved. True when one did.
static bool Move(const MeshDraw& m, CachedMesh& kept) {
    if (kept.gpu->users == 1) {
        if (!ReadPositions(m, *kept.gpu)) return false;
        g_gpuMeshes.Unlist(*kept.gpu);
        return true;
    }
    if (!PositionsDiffer(m, *kept.gpu)) return false;
    return ReadPositions(m, g_gpuMeshes.Own(kept, true));
}

// The kept colours read again, as Move; a copy just taken goes to one lit the same when there is one.
static bool Relight(const MeshDraw& m, CachedMesh& kept) {
    if (kept.gpu->users == 1) {
        if (!ReadColours(m, *kept.gpu)) return false;
        g_gpuMeshes.Unlist(*kept.gpu);
        return true;
    }
    if (!ColoursDiffer(m, *kept.gpu)) return false;
    ReadColours(m, g_gpuMeshes.Own(kept, true));
    g_gpuMeshes.Share(kept);
    return true;
}

#if defined(EI_RENDER_VALIDATION)
// The kept mesh against a fresh build of the draw; a stale one takes the fresh build (all of it uploaded).
static void Check(const MeshDraw& m, CachedMesh& kept) {
    ++g_checks;
    if (!Build(m, g_fresh)) return;
    const GpuMesh& e = *kept.gpu;
    if (g_fresh.vertices.size() == e.vertices.size() && g_fresh.indices == e.indices && g_fresh.colours == e.colours &&
        std::memcmp(g_fresh.vertices.data(), e.vertices.data(), e.vertices.size() * sizeof(MeshVertex)) == 0)
        return;
    bool geometry = g_fresh.vertices.size() != e.vertices.size() || g_fresh.indices != e.indices ||
                    std::memcmp(g_fresh.vertices.data(), e.vertices.data(), e.vertices.size() * sizeof(MeshVertex)) != 0;
    if (++g_stale <= 20) {
        ei::Log("render: a mesh kept on the GPU was stale (%s): figure mesh %p, link figure %p, %d corners%s",
                geometry ? "positions" : "colours", static_cast<const void*>(m.owner),
                static_cast<const void*>(m.shared), m.cornerCount, m.moved ? ", moved positions" : "");
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
                        e.colours[i], g_fresh.colours[i], m.colours, FrameNumber(), kept.lastUsed);
                break;
            }
    }
    GpuMesh& own = g_gpuMeshes.Own(kept, false);
    own.vertices.swap(g_fresh.vertices);
    own.colours.swap(g_fresh.colours);
    own.indices.swap(g_fresh.indices);
    own.WriteAll();
    g_gpuMeshes.Share(kept);
    if (geometry) {
        kept.version = ++g_lastVersion;
        kept.boxKept = false;
    }
}

// The kept box against the positions' box now.
static void CheckBox(const MeshDraw& m, CachedMesh& e) {
    float fresh[6];
    if (!e.boxKept || !PositionBox(m, fresh) || std::memcmp(fresh, e.box, sizeof(fresh)) == 0) return;
    LogFirst(g_stale, 20, "render: a kept mesh's box was stale: figure mesh %p, low %g %g %g, now %g %g %g",
             static_cast<const void*>(m.owner), e.box[0], e.box[1], e.box[2], fresh[0], fresh[1], fresh[2]);
    e.boxKept = false;
}

#endif

CachedMesh* KeepMesh(const MeshDraw& m) {
    if (m.moved) {
        ++g_stats.counts[FrameStats::Count::MorphMeshes];
        g_stats.counts[FrameStats::Count::MorphCorners] += static_cast<std::uint64_t>(m.cornerCount > 0 ? m.cornerCount : 0);
    }
    if (!m.owner || !m.slots || !m.corners || !g_cacheOn) return nullptr;
    bool made = false;
    CachedMesh& e = g_meshes.Take(m.owner, m.slots->drawn, &made);
    MeshFrom now = FromOf(m, e);
    bool build = made || !e.valid || !e.gpu || !e.from.SameArrays(now);
    bool move = !build && !e.from.SamePositions(now);
    bool relight = !build && !e.from.SameColours(now);
    bool check = false;
#if defined(EI_RENDER_VALIDATION)
    check = !build && FrameNumber() % kCheckEvery == 0;
#endif
    // Drawn already this frame with the data kept (the GPU reads it at the frame's end): a draw with
    // other data goes into the frame's stream.
    // (What changed is read at the next draw: e.from stays as it is.)
    if (e.drawn && e.lastUsed == FrameNumber() && (build || move || relight || check)) return nullptr;

    FrameStats::Reuse reuse = FrameStats::Reuse::Same;
    if (build || move) e.boxKept = false;
    if (build) {
        GpuMesh& own = g_gpuMeshes.Own(e, false);
        e.valid = Build(m, own);
        e.version = ++g_lastVersion;
        if (!e.valid) return nullptr;
        own.WriteAll();
        g_gpuMeshes.Share(e);
        reuse = FrameStats::Reuse::New;
    } else {
        if (move && Move(m, e)) {
            reuse = FrameStats::Reuse::Moved;
            e.version = ++g_lastVersion;
        }
        if (relight && Relight(m, e) && reuse == FrameStats::Reuse::Same) reuse = FrameStats::Reuse::Relit;
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
    GpuMesh& g = *e.gpu;
    g.lastUsed = e.lastUsed;
    if (!g_pool.Placed(g.place)) g.dirty = true;
    if (g.HasWrites()) AddPending(g_pending, g);
    ++g_stats.meshReuse[reuse];
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


Vector3 CornerNormal(const MeshDraw& m, int index) {
    if (!m.normals || !m.words || index >= m.wordCount) return Vector3{};
    const int normal = static_cast<int>(m.words[index] >> 16);
    if (normal >= m.normalCount) return Vector3{};
    return PacketVertex(m.normals, normal);
}

int CachedIndexCount(const CachedMesh& mesh) { return static_cast<int>(mesh.gpu->indices.size()); }

Vector3 CachedCorner(const CachedMesh& mesh, int k) {
    const GpuMesh& g = *mesh.gpu;
    return g.vertices[g.indices[static_cast<std::size_t>(k)]].position;
}

std::uint32_t CachedMeshVersion(const CachedMesh& mesh) { return mesh.version; }

void UploadCachedMeshes(const Frame& f) {
    // The frame's meshes: those its batches draw.
    auto frameMeshes = [&](auto add) {
        for (const MeshBatch& b : f.meshBatches)
            if (b.cached) add(*b.cached->gpu);
    };
    UploadKept(g_pool, g_pending, frameMeshes, FrameStats::Count::MeshVertexBytes, FrameStats::Count::MeshIndexBytes);
}

bool BindCachedMesh(const CachedMesh& mesh, Geometry& g, int* firstIndex) {
    if (!mesh.gpu || !g_pool.Placed(mesh.gpu->place)) return false;
    g_pool.Bind(g);
    *firstIndex = static_cast<int>(mesh.gpu->place.firstIndex);
    return true;
}

void MeshCacheFrameDone() {
    const std::uint32_t now = FrameNumber();
    if (now % 64 != 0) return;
    g_meshes.DropIf([now](CachedMesh& mesh) {
        if (now - mesh.lastUsed <= kMeshLife) return false;
        g_gpuMeshes.Release(mesh.gpu);
        return true;
    });
    g_gpuMeshes.FrameDone();
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


void SetMeshCache(bool on) {
    if (on != g_cacheOn) ei::Log("render: figure meshes %s", on ? "kept on the GPU" : "built and uploaded every frame");
    // What changed while off was not read: every kept mesh is built again at its next draw.
    if (on && !g_cacheOn) g_meshes.ForEach([](CachedMesh& mesh) { mesh.valid = false; });
    g_cacheOn = on;
}

MeshCacheInfo MeshCacheState() {
    MeshCacheInfo info;
    info.on = g_cacheOn;
    info.meshes = g_meshes.Live();
    info.copies = g_gpuMeshes.Live();
    info.shared = g_gpuMeshes.Shared();
    info.checks = g_checks;
    info.stale = g_stale;
    return info;
}

} // namespace render
