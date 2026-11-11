// The 3D library's figure meshes and their part loaders; see link_figure.h.
#include "engine/models/link_figure.h"

#include "engine/models/link_loader.h"
#include "render/render.h"   // MeshChanged

#include "lib/bytes.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/rect.h"

#include <cstring>
#include <new>
#include <span>
#include <vector>

namespace {


template <typename T>
T Read(const char** cursor) {
    const T value = lib::Load<T>(*cursor);
    *cursor += sizeof(T);
    return value;
}

// The `count` elements of T the file data holds at `at`, copied out (the records lie at any alignment).
template <typename T>
std::vector<T> FileArray(const char* at, int count) {
    static_assert(lib::kFormatValue<T>, "plain bytes without padding, one size in every build");
    std::vector<T> out(static_cast<std::size_t>(count));
    if (count != 0) std::memcpy(out.data(), at, out.size() * sizeof(T));
    return out;
}

// Appends `from` to an array; returns the range they take.
template <typename T>
LinkPartRanges::Range Append(std::vector<T>& array, std::span<const T> from) {
    const int start = static_cast<int>(std::ssize(array));
    array.insert(array.end(), from.begin(), from.end());
    return {start, static_cast<int>(std::ssize(from))};
}

// One component of a vertex packet block from its `Versions` versions (`block[c * 4 + k]`, c = 0..Versions - 1):
// the versions of component k gathered, then morphed by s, t, u.
template <int Versions, float (*Blend)(const float*, float, float, float)>
float MorphComponent(const float* block, int k, Morph morph) {
    float versions[Versions];
    for (int c = 0; c < Versions; ++c) versions[c] = block[c * fig::kBlockFloats + k];
    return Blend(versions, morph.s, morph.t, morph.u);
}

void SetW(VertexPacket& packet) {
    for (int k = 3; k >= 0; --k) packet.block[3][k] = 1.0f;
}

// One vertex packet from its FIG8 record at *cursor (3 blocks of 8 versions; moved
// past it), morphed by s, t, u; w = 1.
void MorphFig8Packet(VertexPacket& packet, const char** cursor, Morph morph) {
    for (int b = 0; b < 3; ++b) {
        float versions[fig::kBlockFloats * fig::kFig8Versions];
        std::memcpy(versions, *cursor, sizeof(versions));
        for (int k = 0; k < 4; ++k)
            packet.block[b][k] = MorphComponent<fig::kFig8Versions, fig::MorphCube<float>>(versions, k, morph);
        *cursor += sizeof(versions);
    }
    SetW(packet);
}

// The same for the older records (3 blocks of 6 versions).
void MorphOldPacket(VertexPacket& packet, const char** cursor, Morph morph) {
    for (int b = 0; b < 3; ++b) {
        float versions[fig::kBlockFloats * fig::kOldVersions];
        std::memcpy(versions, *cursor, sizeof(versions));
        for (int k = 0; k < 4; ++k)
            packet.block[b][k] = MorphComponent<fig::kOldVersions, fig::MorphPrism<float>>(versions, k, morph);
        *cursor += sizeof(versions);
    }
    SetW(packet);
}

} // namespace


LinkMesh::LinkMesh(LinkFigureLoader* newLoader, std::string_view newName, Morph newMorph)
    : name(newName), morph(newMorph) {
    static std::uint64_t made = 0;
    serial = ++made;
    loader = newLoader;
}

LinkMesh::~LinkMesh() {
    render::MeshChanged(this, render::MeshChange::Rebuilt);   // the parts the renderer keeps
}

bool LinkMesh::AddPart(const std::string& newName, std::span<const VertexPacket> newVertices,
                       std::span<const NormalPacket> newNormals, std::span<const TexCoord> newTexCoords,
                       std::span<const MeshCorner> newCorners, std::span<const std::uint32_t> newWords,
                       std::span<const std::uint16_t> newIndices) {
    render::MeshChanged(this, render::MeshChange::Rebuilt);   // the parts the renderer keeps
    if (ranges.find(newName) != ranges.end()) return false;

    LinkPartRanges newRanges{};
    newRanges.vertices = Append(vertices, newVertices);
    newRanges.normals = Append(normals, newNormals);
    newRanges.texCoords = Append(texCoords, newTexCoords);
    newRanges.corners = Append(corners, newCorners);
    // The out-of-line SetSize(n, -1) for these two.
    newRanges.words = Append(words, newWords);
    newRanges.indices = Append(indices, newIndices);

    ranges[newName] = newRanges;
    return true;
}

// ---- Part loaders ---------------------------------------------------------------------------------

void LinkFigureLoader::LoadFigPart(LinkMesh* mesh, const std::string& name, const char** cursor, Morph morph) {
    LinkPartHeader* header = &mesh->headers[name];
    FigHeader figHeader = Read<FigHeader>(cursor);
    header->materialValue = Read<std::uint32_t>(cursor);
    header->meshType = Read<std::uint32_t>(cursor);
    // The morphed bounds (8 or 1 versions) are not used here.
    *cursor += fig::BoundsBytes(fig::VersionsOf(figHeader.magic));

    int count = figHeader.vertexCount;
    std::vector<VertexPacket> packets(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        VertexPacket& packet = packets[i];
        if (figHeader.magic == fig::kFig8Magic) {
            MorphFig8Packet(packet, cursor, morph);
            continue;
        }
        for (int b = 0; b < 3; ++b) {
            std::memcpy(packet.block[b], *cursor, sizeof(packet.block[b]));
            *cursor += sizeof(packet.block[b]);
        }
        SetW(packet);
    }
    if (count) header->box.bounds.Fit(packets.data(), count);

    const char* normals = *cursor;
    const char* texCoords = normals + figHeader.normalCount * static_cast<int>(sizeof(NormalPacket));
    const char* indices = texCoords + figHeader.texCoordCount * static_cast<int>(sizeof(TexCoord));
    const char* corners = indices + figHeader.indexCount * 2;
    const char* words = corners + figHeader.cornerCount * static_cast<int>(sizeof(MeshCorner));
    *cursor = words + figHeader.wordCount * 4;
    mesh->AddPart(name, packets, FileArray<NormalPacket>(normals, figHeader.normalCount),
                  FileArray<TexCoord>(texCoords, figHeader.texCoordCount),
                  FileArray<MeshCorner>(corners, figHeader.cornerCount),
                  FileArray<std::uint32_t>(words, figHeader.wordCount),
                  FileArray<std::uint16_t>(indices, figHeader.indexCount));
}

void LinkFigureLoader::LoadOldPart(LinkMesh* mesh, const std::string& name, const char** cursor, Morph morph) {
    LinkPartHeader* header = &mesh->headers[name];
    header->materialValue = Read<std::uint32_t>(cursor);
    header->meshType = Read<std::uint32_t>(cursor);
    *cursor += fig::BoundsBytes(fig::kOldVersions);   // the morphed bounds
    int count = Read<int>(cursor);

    std::vector<VertexPacket> packets(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) MorphOldPacket(packets[i], cursor, morph);
    if (count) header->box.bounds.Fit(packets.data(), count);

    // Each array is a count and its elements; a block of 0x34-byte records between the texture
    // coordinates and the corners is skipped.
    int normalCount = Read<int>(cursor);
    const char* normals = *cursor;
    *cursor += normalCount * static_cast<int>(sizeof(NormalPacket));
    int texCoordCount = Read<int>(cursor);
    const char* texCoords = *cursor;
    *cursor += texCoordCount * static_cast<int>(sizeof(TexCoord));
    int skipped = Read<int>(cursor);
    *cursor += skipped * fig::kSkippedRecordBytes;
    int cornerCount = Read<int>(cursor);
    const char* corners = *cursor;
    *cursor += cornerCount * static_cast<int>(sizeof(MeshCorner));
    int wordCount = Read<int>(cursor);
    const char* words = *cursor;
    *cursor += wordCount * 4;
    int indexCount = Read<int>(cursor);
    const char* indices = *cursor;
    *cursor += indexCount * 2;
    mesh->AddPart(name, packets, FileArray<NormalPacket>(normals, normalCount),
                  FileArray<TexCoord>(texCoords, texCoordCount), FileArray<MeshCorner>(corners, cornerCount),
                  FileArray<std::uint32_t>(words, wordCount), FileArray<std::uint16_t>(indices, indexCount));
}

// ---- Morphing a loaded part again -----------------------------------------------------------------

void LinkFigureLoader::RemorphFigPart(LinkMesh* mesh, const std::string& name, const char** cursor, Morph morph) {
    LinkPartHeader* header = &mesh->headers[name];
    FigHeader figHeader = lib::Load<FigHeader>(*cursor);
    // The header, two dwords and the FIG8 bounds (a plain FIG is read as FIG8 too).
    *cursor += fig::kHeaderBytes + fig::BoundsBytes(fig::kFig8Versions);
    int count = figHeader.vertexCount;
    std::vector<VertexPacket> packets(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) MorphFig8Packet(packets[i], cursor, morph);
    if (count) header->box.bounds.Fit(packets.data(), count);
    mesh->SetPartVertices(name, packets.data(), static_cast<unsigned>(count));
}

void LinkFigureLoader::RemorphOldPart(LinkMesh* mesh, const std::string& name, const char** cursor, Morph morph) {
    LinkPartHeader* header = &mesh->headers[name];
    *cursor += fig::kMeshWordsBytes + fig::BoundsBytes(fig::kOldVersions);   // two dwords and the morphed bounds
    int count = Read<int>(cursor);
    std::vector<VertexPacket> packets(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) MorphOldPacket(packets[i], cursor, morph);
    if (count) header->box.bounds.Fit(packets.data(), count);
    mesh->SetPartVertices(name, packets.data(), static_cast<unsigned>(count));
}

bool LinkMesh::SetPartVertices(std::string_view newName, const VertexPacket* newVertices, unsigned count) {
    auto node = ranges.find(newName);
    if (node == ranges.end()) return false;
    const LinkPartRanges& r = node->second;
    unsigned n = static_cast<unsigned>(r.vertices.count) < count ? static_cast<unsigned>(r.vertices.count) : count;
    std::memcpy(vertices.data() + r.vertices.start, newVertices, static_cast<std::size_t>(n) * sizeof(VertexPacket));
    render::MeshChanged(this, render::MeshChange::Positions);   // the parts the renderer keeps
    return true;
}

// ---- LinkMesh's slots ----------------------------------------------------------------------------

bool LinkMesh::HasPart(std::string_view partName) { return ranges.find(partName) != ranges.end(); }

const LinkPartRanges* LinkMesh::PartRanges(std::string_view partName) const {
    auto node = ranges.find(partName);
    return node == ranges.end() ? nullptr : &node->second;
}

std::vector<std::string> LinkMesh::PartNames() {
    std::vector<std::string> names;
    for (const auto& part : ranges) names.push_back(part.first);
    return names;
}

bool LinkMesh::GetPartData(std::string_view partName, VertexPacket** outVertices, MeshCorner** outCorners,
                            std::uint16_t** outIndices, int* vertexCount, int* indexCount) {
    auto node = ranges.find(partName);
    if (node == ranges.end()) return false;
    const LinkPartRanges& r = node->second;
    *outVertices = vertices.data() + r.vertices.start;
    *outCorners = corners.data() + r.corners.start;
    *outIndices = indices.data() + r.indices.start;
    *indexCount = r.indices.count;
    *vertexCount = r.vertices.count;
    return true;
}
