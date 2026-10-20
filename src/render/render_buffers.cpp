// The new renderer's vertex and index buffers: the streams a frame appends its vertices to
// (StreamBuffer, GeometryStream) and the pools that keep terrain pages and meshes on the GPU across
// frames (GpuPool, R16c / R16d; docs/notes/renderer.md section 13).
#include "render.h"
#include "render_internal.h"

#include "../hook/log.h"

#include <windows.h>
#include <d3d11.h>

#include <algorithm>

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
    if (commit_ != CommitNumber()) {   // sokol starts appending from the start again after a commit
        commit_ = CommitNumber();
        used_ = 0;
    }
    if (!buffer_.id || used_ + rounded > capacity_) {
        // Too small for this frame's data: a larger buffer; the old one is destroyed after the
        // commit (passes drawn earlier in the frame still use it).
        Retire(buffer_);
        capacity_ = (capacity_ > rounded ? capacity_ : rounded) * 2 + 4096;
        sg_buffer_desc d{};
        d.size = capacity_;
        d.usage.vertex_buffer = !index_;
        d.usage.index_buffer = index_;
        d.usage.immutable = false;
        d.usage.dynamic_update = true;
        d.label = label_;
        buffer_ = sg_make_buffer(&d);
        used_ = 0;
        if (sg_query_buffer_state(buffer_) != SG_RESOURCESTATE_VALID) {
            if (buffer_.id) sg_destroy_buffer(buffer_);
            buffer_ = sg_buffer{};
            capacity_ = 0;
            return -1;
        }
    }
    sg_range range{data, bytes};
    int offset = sg_append_buffer(buffer_, &range);
    used_ += rounded;
    return sg_query_buffer_overflow(buffer_) ? -1 : offset;
}

void StreamBuffer::Destroy() {
    if (buffer_.id) sg_destroy_buffer(buffer_);
    buffer_ = sg_buffer{};
    capacity_ = 0;
}

void RetireBuffers() {
    for (sg_buffer b : g_retiredBuffers) sg_destroy_buffer(b);
    g_retiredBuffers.clear();
}

bool GeometryStream::Upload(const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices) {
    if (frame_ == FrameNumber() && vertexCount_ == vertices.size() && indexCount_ == indices.size())
        return vertexOffset_ >= 0 && indexOffset_ >= 0;
    frame_ = FrameNumber();
    vertexCount_ = vertices.size();
    indexCount_ = indices.size();
    vertexOffset_ = indexOffset_ = -1;
    if (vertices.empty() || indices.empty()) return false;
    std::size_t vertexBytes = vertices.size() * sizeof(Vertex), indexBytes = indices.size() * sizeof(std::uint32_t);
    vertexOffset_ = vertices_.Append(vertices.data(), vertexBytes);
    indexOffset_ = indices_.Append(indices.data(), indexBytes);
    FrameStats& stats = Stats();
    (terrain_ ? stats.terrainVertexBytes : stats.meshVertexBytes) += vertexBytes;
    (terrain_ ? stats.terrainIndexBytes : stats.meshIndexBytes) += indexBytes;
    return vertexOffset_ >= 0 && indexOffset_ >= 0;
}

bool GeometryStream::Bind(sg_bindings& b) const {
    if (frame_ != FrameNumber() || vertexOffset_ < 0 || indexOffset_ < 0) return false;
    b.vertex_buffers[0] = vertices_.Buffer();
    b.vertex_buffer_offsets[0] = vertexOffset_;
    b.index_buffer = indices_.Buffer();
    b.index_buffer_offset = indexOffset_;
    return true;
}

void GeometryStream::Destroy() {
    vertices_.Destroy();
    indices_.Destroy();
    frame_ = 0xFFFFFFFFu;
    vertexOffset_ = indexOffset_ = -1;
}

// --- Pools -------------------------------------------------------------------------------------------------

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

bool GpuPool::Place(PoolPlace& p, std::uint32_t vertices, std::uint32_t indices, bool* anew) {
    *anew = false;
    if (p.generation == generation_ && p.vertexRoom >= vertices && p.indexRoom >= indices) return true;
    if (usedVertices_ + vertices > vertexCapacity_ || usedIndices_ + indices > indexCapacity_) return false;
    p.firstVertex = usedVertices_;
    p.vertexRoom = vertices;
    p.firstIndex = usedIndices_;
    p.indexRoom = indices;
    p.generation = generation_;
    usedVertices_ += vertices;
    usedIndices_ += indices;
    *anew = true;
    return true;
}

void GpuPool::Restart(std::uint64_t vertices, std::uint64_t indices) {
    std::uint64_t needVertices = std::max<std::uint64_t>(2 * vertices, leastVertices_);
    std::uint64_t needIndices = std::max<std::uint64_t>(2 * indices, leastIndices_);
    if (!geometry_.buffer.id || needVertices > vertexCapacity_ || needIndices > indexCapacity_) {
        needVertices = std::max<std::uint64_t>(needVertices, 2ull * vertexCapacity_);
        needIndices = std::max<std::uint64_t>(needIndices, 2ull * indexCapacity_);
        Drop();
        if (needVertices > 0x1000000 || needIndices > 0x3000000 ||
            !geometry_.Make(static_cast<std::size_t>(needVertices) * geometryStride_, false, label_) ||
            !colours_.Make(static_cast<std::size_t>(needVertices) * kColourPairBytes, false, label_) ||
            !indices_.Make(static_cast<std::size_t>(needIndices) * sizeof(std::uint32_t), true, label_)) {
            ei::Log("render: the %s pool (%llu vertices, %llu indices) could not be made", label_,
                    static_cast<unsigned long long>(needVertices), static_cast<unsigned long long>(needIndices));
            Drop();
        } else {
            vertexCapacity_ = static_cast<std::uint32_t>(needVertices);
            indexCapacity_ = static_cast<std::uint32_t>(needIndices);
        }
    }
    usedVertices_ = usedIndices_ = 0;
    ++generation_;
}

void GpuPool::WriteGeometry(const PoolPlace& p, std::uint32_t first, std::uint32_t count, const void* data) const {
    geometry_.Write((p.firstVertex + first) * geometryStride_, data, count * geometryStride_);
}

void GpuPool::WriteColours(const PoolPlace& p, std::uint32_t first, std::uint32_t count, const std::uint32_t* pairs) const {
    colours_.Write((p.firstVertex + first) * kColourPairBytes, pairs, count * kColourPairBytes);
}

void GpuPool::WriteIndices(const PoolPlace& p, const std::uint32_t* relative, std::size_t count) {
    absolute_.resize(count);
    for (std::size_t i = 0; i < count; ++i) absolute_[i] = relative[i] + p.firstVertex;
    indices_.Write(p.firstIndex * sizeof(std::uint32_t), absolute_.data(), count * sizeof(std::uint32_t));
}

void GpuPool::Bind(sg_bindings& b) const {
    b.vertex_buffers[0] = geometry_.buffer;
    b.vertex_buffers[1] = colours_.buffer;
    b.index_buffer = indices_.buffer;
}

void GpuPool::Drop() {
    geometry_.Drop();
    colours_.Drop();
    indices_.Drop();
    vertexCapacity_ = indexCapacity_ = 0;
    usedVertices_ = usedIndices_ = 0;
    ++generation_;
}

} // namespace render::detail
