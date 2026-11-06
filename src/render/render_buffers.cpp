// The new renderer's vertex and index buffers: the streams a frame appends its vertices to
// (StreamBuffer, GeometryStream) and the pools that keep terrain pages and meshes on the GPU across
// frames (GpuPool; docs/notes/renderer.md section 13).
#include "../platform/log.h"
#include "render.h"
#include "render_internal.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace render {

// --- Images and samplers shared by the passes ---------------------------------------------------------------

static std::vector<gpu::Texture> g_released;

void ReleaseAfterPresent(const gpu::Texture& image) {
    if (image) g_released.push_back(image);
}

void ReleaseRetired() {
    for (gpu::Texture& t : g_released) gpu::Release(t);
    g_released.clear();
}

// --- Streams ---------------------------------------------------------------------------------------------

int StreamBuffer::Append(const void* data, std::size_t bytes) {
    if (bytes == 0) return 0;
    std::size_t rounded = (bytes + 3) & ~static_cast<std::size_t>(3);   // offsets at multiples of 4 (the indices')
    if (commit != CommitNumber()) {    // each frame appends from the start again
        commit = CommitNumber();
        used = 0;
    }
    if (!buffer || used + rounded > buffer.size) {
        // Too small for this frame's data: a larger buffer (the GPU keeps the old one while passes drawn earlier in
        // the frame still read it).
        const std::size_t capacity = (buffer.size > rounded ? buffer.size : rounded) * 2 + 4096;
        gpu::Release(buffer);
        used = 0;
        if (capacity > 0xFFFFFFFFu) return -1;
        buffer = gpu::MakeBuffer(use, static_cast<std::uint32_t>(capacity), label);
        if (!buffer) return -1;
    }
    const auto offset = static_cast<std::uint32_t>(used);
    if (!gpu::Write(buffer, offset, data, static_cast<std::uint32_t>(bytes),
                    used == 0 ? gpu::Contents::Discard : gpu::Contents::Keep))
        return -1;
    used += rounded;
    return static_cast<int>(offset);
}

void Geometry::Bind() const {
    gpu::BindVertexBuffers(0, {vertices, static_cast<std::size_t>(vertexBuffers)});
    gpu::BindIndexBuffer(indices);
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
    (terrain ? stats.counts[FrameStats::Count::TerrainVertexBytes] : stats.counts[FrameStats::Count::MeshVertexBytes]) += vertexBytes;
    (terrain ? stats.counts[FrameStats::Count::TerrainIndexBytes] : stats.counts[FrameStats::Count::MeshIndexBytes]) += indexBytes;
    return vertexOffset >= 0 && indexOffset >= 0;
}

bool GeometryStream::Bind(Geometry& g) const {
    if (frame != FrameNumber() || vertexOffset < 0 || indexOffset < 0) return false;
    g.vertices[0] = {vertices.Buffer(), static_cast<std::uint32_t>(vertexOffset)};
    g.vertexBuffers = 1;
    g.indices = {indices.Buffer(), static_cast<std::uint32_t>(indexOffset)};
    return true;
}

// --- Pools -------------------------------------------------------------------------------------------------

bool GpuPool::Buffer::Make(std::size_t bytes, bool index, const char* label) {
    if (bytes > 0xFFFFFFFFu) return false;
    buffer = gpu::MakeBuffer(index ? gpu::BufferUse::Index : gpu::BufferUse::Vertex, static_cast<std::uint32_t>(bytes), label);
    return static_cast<bool>(buffer);
}

void GpuPool::Buffer::Write(std::size_t offset, const void* data, std::size_t bytes) const {
    if (buffer)
        gpu::Write(buffer, static_cast<std::uint32_t>(offset), data, static_cast<std::uint32_t>(bytes), gpu::Contents::Keep);
}

void GpuPool::Buffer::Drop() { gpu::Release(buffer); }

bool GpuPool::Place(KeptRanges& k, std::size_t vertexCount, std::size_t indexCount) {
    PoolPlace& p = k.place;
    if (p.generation == generation && p.vertexRoom >= vertexCount && p.indexRoom >= indexCount) return true;
    if (usedVertices + vertexCount > vertexCapacity || usedIndices + indexCount > indexCapacity) return false;
    p.firstVertex = usedVertices;
    p.vertexRoom = static_cast<std::uint32_t>(vertexCount);
    p.firstIndex = usedIndices;
    p.indexRoom = static_cast<std::uint32_t>(indexCount);
    p.generation = generation;
    usedVertices += p.vertexRoom;
    usedIndices += p.indexRoom;
    k.dirty = true;
    return true;
}

void GpuPool::Restart(std::uint64_t vertexCount, std::uint64_t indexCount) {
    std::uint64_t needVertices = std::max<std::uint64_t>(2 * vertexCount, leastVertices);
    std::uint64_t needIndices = std::max<std::uint64_t>(2 * indexCount, leastIndices);
    if (!geometry.buffer || needVertices > vertexCapacity || needIndices > indexCapacity) {
        needVertices = std::max<std::uint64_t>(needVertices, 2ull * vertexCapacity);
        needIndices = std::max<std::uint64_t>(needIndices, 2ull * indexCapacity);
        Drop();
        if (needVertices > 0x1000000 || needIndices > 0x3000000 ||
            !geometry.Make(static_cast<std::size_t>(needVertices) * geometryStride, false, label) ||
            !colours.Make(static_cast<std::size_t>(needVertices) * sizeof(ColourPair), false, label) ||
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

void GpuPool::WriteIndices(const PoolPlace& p, const std::uint32_t* relative, std::size_t count) {
    absolute.resize(count);
    for (std::size_t i = 0; i < count; ++i) absolute[i] = relative[i] + p.firstVertex;
    indices.Write(p.firstIndex * sizeof(std::uint32_t), absolute.data(), count * sizeof(std::uint32_t));
}

void GpuPool::WriteKept(KeptRanges& k, const void* vertices, std::size_t vertexCount,
                        std::span<const std::uint32_t> pairs, std::span<const std::uint32_t> relative,
                        FrameStats::Count vertexBytes, FrameStats::Count indexBytes) {
    const PoolPlace& p = k.place;
    std::uint64_t& written = g_stats.counts[vertexBytes];
    if (k.dirty) {
        geometry.Write(p.firstVertex * geometryStride, vertices, vertexCount * geometryStride);
        colours.Write(p.firstVertex * sizeof(ColourPair), pairs.data(), pairs.size_bytes());
        WriteIndices(p, relative.data(), relative.size());
        written += vertexCount * geometryStride + pairs.size_bytes();
        g_stats.counts[indexBytes] += relative.size_bytes();
    } else {
        if (k.vertexTo > k.vertexFrom) {
            const std::size_t bytes = (k.vertexTo - k.vertexFrom) * geometryStride;
            geometry.Write((p.firstVertex + k.vertexFrom) * geometryStride,
                           static_cast<const std::byte*>(vertices) + k.vertexFrom * geometryStride, bytes);
            written += bytes;
        }
        if (k.colourTo > k.colourFrom) {
            const std::size_t bytes = (k.colourTo - k.colourFrom) * sizeof(ColourPair);
            colours.Write((p.firstVertex + k.colourFrom) * sizeof(ColourPair), &pairs[2 * std::size_t{k.colourFrom}],
                          bytes);
            written += bytes;
        }
    }
    k.dirty = false;
    k.vertexFrom = k.vertexTo = 0;
    k.colourFrom = k.colourTo = 0;
}

void GpuPool::Bind(Geometry& g) const {
    g.vertices[0] = {geometry.buffer, 0};
    g.vertices[1] = {colours.buffer, 0};
    g.vertexBuffers = 2;
    g.indices = {indices.buffer, 0};
}

void GpuPool::Drop() {
    geometry.Drop();
    colours.Drop();
    indices.Drop();
    vertexCapacity = indexCapacity = 0;
    usedVertices = usedIndices = 0;
    ++generation;
}

}   // namespace render
