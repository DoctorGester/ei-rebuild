// The new renderer's vertex and index buffers: the streams a frame appends its vertices to
// (StreamBuffer, GeometryStream) and the pools that keep terrain pages and meshes on the GPU across
// frames (GpuPool; docs/notes/renderer.md section 13).
#include "../hook/log.h"
#include "render.h"
#include "render_internal.h"

#if defined(_WIN32)
#include <d3d11.h>
#include <windows.h>
#else
#include "render_native.h"
#endif

#include <algorithm>
#include <cstring>

namespace render::detail {

namespace {

// Buffers replaced or dropped during a frame: destroyed after its commit (passes drawn earlier in the
// frame may still use them).
std::vector<sg_buffer> g_retiredBuffers;

void Retire(sg_buffer& b) {
    if (b.id) g_retiredBuffers.push_back(b);
    b = sg_buffer{};
}

} // namespace

// --- Streams ---------------------------------------------------------------------------------------------

int StreamBuffer::Append(const void* data, std::size_t bytes) {
    if (bytes == 0) return 0;
    std::size_t rounded = (bytes + 3) & ~static_cast<std::size_t>(3);   // sokol appends at multiples of 4
    if (commit != CommitNumber()) {    // sokol starts appending from the start again after a commit
        commit = CommitNumber();
        used = 0;
    }
    if (!buffer.id || used + rounded > capacity) {
        // Too small for this frame's data: a larger buffer; the old one is destroyed after the
        // commit (passes drawn earlier in the frame still use it).
        Retire(buffer);
        capacity = (capacity > rounded ? capacity : rounded) * 2 + 4096;
        sg_buffer_desc d{};
        d.size = capacity;
        d.usage.vertex_buffer = !index;
        d.usage.index_buffer = index;
        d.usage.immutable = false;
        d.usage.dynamic_update = true;
        d.label = label;
        buffer = sg_make_buffer(&d);
        used = 0;
        if (sg_query_buffer_state(buffer) != SG_RESOURCESTATE_VALID) {
            if (buffer.id) sg_destroy_buffer(buffer);
            buffer = sg_buffer{};
            capacity = 0;
            return -1;
        }
    }
    sg_range range{data, bytes};
    int offset = sg_append_buffer(buffer, &range);
    used += rounded;
    return sg_query_buffer_overflow(buffer) ? -1 : offset;
}

void StreamBuffer::Destroy() {
    if (buffer.id) sg_destroy_buffer(buffer);
    buffer = sg_buffer{};
    capacity = 0;
}

void RetireBuffers() {
    for (sg_buffer b : g_retiredBuffers) sg_destroy_buffer(b);
    g_retiredBuffers.clear();
}

bool GeometryStream::Upload(const std::vector<Vertex>& vertexData, const std::vector<std::uint32_t>& indexData) {
    if (frame == FrameNumber() && vertexCount == vertexData.size() && indexCount == indexData.size())
        return vertexOffset >= 0 && indexOffset >= 0;
    frame = FrameNumber();
    vertexCount = vertexData.size();
    indexCount = indexData.size();
    vertexOffset = indexOffset = -1;
    if (vertexData.empty() || indexData.empty()) return false;
    std::size_t vertexBytes = vertexData.size() * sizeof(Vertex), indexBytes = indexData.size() * sizeof(std::uint32_t);
    vertexOffset = vertices.Append(vertexData.data(), vertexBytes);
    indexOffset = indices.Append(indexData.data(), indexBytes);
    FrameStats& stats = g_stats;
    (terrain ? stats.terrainVertexBytes : stats.meshVertexBytes) += vertexBytes;
    (terrain ? stats.terrainIndexBytes : stats.meshIndexBytes) += indexBytes;
    return vertexOffset >= 0 && indexOffset >= 0;
}

bool GeometryStream::Bind(sg_bindings& b) const {
    if (frame != FrameNumber() || vertexOffset < 0 || indexOffset < 0) return false;
    b.vertex_buffers[0] = vertices.Buffer();
    b.vertex_buffer_offsets[0] = vertexOffset;
    b.index_buffer = indices.Buffer();
    b.index_buffer_offset = indexOffset;
    return true;
}

void GeometryStream::Destroy() {
    vertices.Destroy();
    indices.Destroy();
    frame = 0xFFFFFFFFu;
    vertexOffset = indexOffset = -1;
}

// --- Pools -------------------------------------------------------------------------------------------------

#if defined(_WIN32)
bool GpuPool::Buffer::Make(std::size_t bytes, bool index, const char* label) {
    D3D11_BUFFER_DESC d{};
    d.ByteWidth = static_cast<UINT>(bytes);
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = index ? D3D11_BIND_INDEX_BUFFER : D3D11_BIND_VERTEX_BUFFER;
    ID3D11Buffer* made = nullptr;
    if (!D3DDevice() || FAILED(D3DDevice()->CreateBuffer(&d, nullptr, &made)) || !made) return false;
    sg_buffer_desc s{};
    s.size = bytes;
    s.usage.vertex_buffer = !index;
    s.usage.index_buffer = index;
    s.d3d11_buffer = made;
    s.label = label;
    buffer = sg_make_buffer(&s);
    made->Release();   // sokol holds the reference
    if (sg_query_buffer_state(buffer) != SG_RESOURCESTATE_VALID) {
        if (buffer.id) sg_destroy_buffer(buffer);
        buffer = sg_buffer{};
        return false;
    }
    d3d = made;
    return true;
}

void GpuPool::Buffer::Write(std::size_t offset, const void* data, std::size_t bytes) const {
    if (!bytes || !d3d) return;
    D3D11_BOX box{static_cast<UINT>(offset), 0, 0, static_cast<UINT>(offset + bytes), 1, 1};
    D3DContext()->UpdateSubresource(d3d, 0, &box, data, 0, 0);
}

void GpuPool::Buffer::Drop() {
    Retire(buffer);
    d3d = nullptr;
}

#else
bool GpuPool::Buffer::Make(std::size_t bytes, bool newIndex, const char*) {
    shadow.assign(bytes, 0);
    index = newIndex;
    dirty = true;
    uploaded = 0xFFFFFFFFu;
    sg_buffer_desc desc{};
    desc.size = bytes;
    desc.usage.vertex_buffer = !index;
    desc.usage.index_buffer = index;
    desc.usage.dynamic_update = true;
    buffer = sg_make_buffer(desc);
    return sg_query_buffer_state(buffer) == SG_RESOURCESTATE_VALID;
}
void GpuPool::Buffer::Write(std::size_t offset, const void* data, std::size_t bytes) const {
    if (!bytes || offset + bytes > shadow.size()) return;
    std::memcpy(shadow.data() + offset, data, bytes);
    dirty = true;
}
void GpuPool::Buffer::Flush() const {
    if (!dirty) return;
    // A later pass can update a pool already drawn in this frame: retain the earlier buffer until commit.
    if (!buffer.id || uploaded == CommitNumber()) {
        Retire(buffer);
        sg_buffer_desc desc{};
        desc.size = shadow.size();
        desc.usage.vertex_buffer = !index;
        desc.usage.index_buffer = index;
        desc.usage.dynamic_update = true;
        buffer = sg_make_buffer(desc);
    }
    sg_update_buffer(buffer, sg_range{shadow.data(), shadow.size()});
    uploaded = CommitNumber();
    dirty = false;
}
void GpuPool::Buffer::Drop() {
    Retire(buffer);
    shadow.clear();
    dirty = false;
}
#endif

bool GpuPool::Place(PoolPlace& p, std::uint32_t vertexCount, std::uint32_t indexCount, bool* anew) {
    *anew = false;
    if (p.generation == generation && p.vertexRoom >= vertexCount && p.indexRoom >= indexCount) return true;
    if (usedVertices + vertexCount > vertexCapacity || usedIndices + indexCount > indexCapacity) return false;
    p.firstVertex = usedVertices;
    p.vertexRoom = vertexCount;
    p.firstIndex = usedIndices;
    p.indexRoom = indexCount;
    p.generation = generation;
    usedVertices += vertexCount;
    usedIndices += indexCount;
    *anew = true;
    return true;
}

void GpuPool::Restart(std::uint64_t vertexCount, std::uint64_t indexCount) {
    std::uint64_t needVertices = std::max<std::uint64_t>(2 * vertexCount, leastVertices);
    std::uint64_t needIndices = std::max<std::uint64_t>(2 * indexCount, leastIndices);
    if (!geometry.buffer.id || needVertices > vertexCapacity || needIndices > indexCapacity) {
        needVertices = std::max<std::uint64_t>(needVertices, 2ull * vertexCapacity);
        needIndices = std::max<std::uint64_t>(needIndices, 2ull * indexCapacity);
        Drop();
        if (needVertices > 0x1000000 || needIndices > 0x3000000 ||
            !geometry.Make(static_cast<std::size_t>(needVertices) * geometryStride, false, label) ||
            !colours.Make(static_cast<std::size_t>(needVertices) * kColourPairBytes, false, label) ||
            !indices.Make(static_cast<std::size_t>(needIndices) * sizeof(std::uint32_t), true, label)) {
            ei::Log("render: the %s pool (%llu vertices, %llu indices) could not be made", label,
                    static_cast<unsigned long long>(needVertices), static_cast<unsigned long long>(needIndices));
            Drop();
        } else {
            vertexCapacity = static_cast<std::uint32_t>(needVertices);
            indexCapacity = static_cast<std::uint32_t>(needIndices);
        }
    }
    usedVertices = usedIndices = 0;
    ++generation;
}

void GpuPool::WriteGeometry(const PoolPlace& p, std::uint32_t first, std::uint32_t count, const void* data) const {
    geometry.Write((p.firstVertex + first) * geometryStride, data, count * geometryStride);
}

void GpuPool::WriteColours(const PoolPlace& p, std::uint32_t first, std::uint32_t count, const std::uint32_t* pairs) const {
    colours.Write((p.firstVertex + first) * kColourPairBytes, pairs, count * kColourPairBytes);
}

void GpuPool::WriteIndices(const PoolPlace& p, const std::uint32_t* relative, std::size_t count) {
    absolute.resize(count);
    for (std::size_t i = 0; i < count; ++i) absolute[i] = relative[i] + p.firstVertex;
    indices.Write(p.firstIndex * sizeof(std::uint32_t), absolute.data(), count * sizeof(std::uint32_t));
}

void GpuPool::Bind(sg_bindings& b) const {
#if !defined(_WIN32)
    geometry.Flush();
    colours.Flush();
    indices.Flush();
#endif
    b.vertex_buffers[0] = geometry.buffer;
    b.vertex_buffers[1] = colours.buffer;
    b.index_buffer = indices.buffer;
}

void GpuPool::Drop() {
    geometry.Drop();
    colours.Drop();
    indices.Drop();
    vertexCapacity = indexCapacity = 0;
    usedVertices = usedIndices = 0;
    ++generation;
}

} // namespace render::detail
