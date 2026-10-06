// The 3D library's figure meshes and their part loaders (004F2650-004FBD80); see link_figure.h.
#include "link_figure.h"

#include "originals_script.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>

namespace {

constexpr std::uint32_t kOne = 0x3F800000;   // 1.0f, the w row of every vertex packet

template <typename T>
T Read(const char** cursor) {
    T value;
    std::memcpy(&value, *cursor, sizeof(T));
    *cursor += sizeof(T);
    return value;
}

// Appends `count` elements to an aligned array; returns where they start.
template <typename T>
int Append(AlignedArray<T>& array, const T* from, int count) {
    int start = array.size;
    array.SetSize(start + count);
    if (count) std::memcpy(array.data + start, from, static_cast<std::size_t>(count) * sizeof(T));
    return start;
}

float Lerp(float a, float b, float f) { return (b - a) * f + a; }

// A FIG8 value from its 8 versions (`v[c * 4]`, c = 0..7):
// lerp(lerp(lerp(v0, v1, s), lerp(v2, v3, s), t), lerp(lerp(v4, v5, s), lerp(v6, v7, s), t), u).
float Morph8(const float* v, float s, float t, float u) {
    float lower = Lerp(Lerp(v[0], v[4], s), Lerp(v[8], v[12], s), t);
    float upper = Lerp(Lerp(v[16], v[20], s), Lerp(v[24], v[28], s), t);
    return Lerp(lower, upper, u);
}

// The older records' value from 6 versions: lerp(lerp(lerp(v0, v1, s), v2, t), lerp(lerp(v3, v4, s), v5, t), u).
float Morph6(const float* v, float s, float t, float u) {
    float lower = Lerp(Lerp(v[0], v[4], s), v[8], t);
    float upper = Lerp(Lerp(v[12], v[16], s), v[20], t);
    return Lerp(lower, upper, u);
}

// The temporary packet array of the loaders (operator_new, 32-byte aligned, block pointer before it).
VertexPacket* AllocatePackets(int count) { return count ? AlignedArray<VertexPacket>::Allocate(count) : nullptr; }

void SetW(VertexPacket& packet) {
    for (int k = 3; k >= 0; --k) std::memcpy(&packet.block[3][k], &kOne, 4);
}

} // namespace

// ---- CLinkMesh ------------------------------------------------------------------------------------

int CLinkMesh::AddPart(const stlp::string* name, const VertexPacket* vertices, int vertexCount,
                       const NormalPacket* normals, int normalCount, const TexCoord* texCoords, int texCoordCount,
                       const MeshCorner* corners, int cornerCount, const std::uint32_t* words, int wordCount,
                       const std::uint16_t* indices, int indexCount) {
    void* found;
    LinkRangeMapFindOriginal(&ranges_, nullptr, &found, name);
    if (found != ranges_.header) return 0;

    LinkPartRanges ranges{};
    ranges.vertices = {Append(vertices_, vertices, vertexCount), vertexCount};
    ranges.normals = {Append(normals_, normals, normalCount), normalCount};
    ranges.texCoords = {Append(texCoords_, texCoords, texCoordCount), texCoordCount};

    int start = corners_.size;
    corners_.Resize(start + cornerCount, -1);
    std::memcpy(corners_.data + start * 3, corners, static_cast<std::size_t>(cornerCount) * sizeof(MeshCorner));
    ranges.corners = {start, cornerCount};

    // The original's out-of-line SetSize(n, -1) (00452EB0 / 0044FA90) for these two.
    ranges.words = {Append(words_, words, wordCount), wordCount};
    ranges.indices = {Append(indices_, indices, indexCount), indexCount};

    *LinkRangeMapAtOriginal(&ranges_, nullptr, name) = ranges;
    return 1;
}
EI_REPLACE_METHOD(0x004F30F0, CLinkMesh::AddPart);

// ---- Part loaders ---------------------------------------------------------------------------------

void CLinkFigureLoader::LoadFigPart(CLinkMesh* mesh, const stlp::string* name, const char** cursor, float s, float t,
                                    float u) {
    LinkPartHeader* header = LinkHeaderMapAtOriginal(&mesh->headers_, nullptr, name);
    FigHeader fig = Read<FigHeader>(cursor);
    header->unknown_04 = Read<std::uint32_t>(cursor);
    header->unknown_00 = Read<std::uint32_t>(cursor);
    // The morphed bounds (8 or 1 versions) are not used here.
    *cursor += fig.magic == kFig8Magic ? 0x140 : 0x28;

    int count = fig.vertexCount;
    VertexPacket* packets = AllocatePackets(count);
    for (int i = 0; i < count; ++i) {
        VertexPacket& packet = packets[i];
        for (int b = 0; b < 3; ++b) {
            if (fig.magic == kFig8Magic) {
                const float* versions = reinterpret_cast<const float*>(*cursor);
                for (int k = 0; k < 4; ++k) packet.block[b][k] = Morph8(versions + k, s, t, u);
                *cursor += 0x80;
            } else {
                std::memcpy(packet.block[b], *cursor, 0x10);
                *cursor += 0x10;
            }
        }
        SetW(packet);
    }
    if (count) pipeline_->FitBounds(&header->bounds, packets, count);

    const char* normals = *cursor;
    const char* texCoords = normals + fig.normalCount * static_cast<int>(sizeof(NormalPacket));
    const char* indices = texCoords + fig.texCoordCount * static_cast<int>(sizeof(TexCoord));
    const char* corners = indices + fig.indexCount * 2;
    const char* words = corners + fig.cornerCount * static_cast<int>(sizeof(MeshCorner));
    *cursor = words + fig.wordCount * 4;
    mesh->AddPart(name, packets, count, reinterpret_cast<const NormalPacket*>(normals), fig.normalCount,
                  reinterpret_cast<const TexCoord*>(texCoords), fig.texCoordCount,
                  reinterpret_cast<const MeshCorner*>(corners), fig.cornerCount,
                  reinterpret_cast<const std::uint32_t*>(words), fig.wordCount,
                  reinterpret_cast<const std::uint16_t*>(indices), fig.indexCount);
    if (packets) AlignedArray<VertexPacket>::Free(packets);
}
EI_REPLACE_METHOD(0x004F7F00, CLinkFigureLoader::LoadFigPart);

void CLinkFigureLoader::LoadOldPart(CLinkMesh* mesh, const stlp::string* name, const char** cursor, float s, float t,
                                    float u) {
    LinkPartHeader* header = LinkHeaderMapAtOriginal(&mesh->headers_, nullptr, name);
    header->unknown_04 = Read<std::uint32_t>(cursor);
    header->unknown_00 = Read<std::uint32_t>(cursor);
    *cursor += 0xF0;   // the morphed bounds
    int count = Read<int>(cursor);

    VertexPacket* packets = AllocatePackets(count);
    for (int i = 0; i < count; ++i) {
        VertexPacket& packet = packets[i];
        for (int b = 0; b < 3; ++b) {
            const float* versions = reinterpret_cast<const float*>(*cursor);
            for (int k = 0; k < 4; ++k) packet.block[b][k] = Morph6(versions + k, s, t, u);
            *cursor += 0x60;
        }
        SetW(packet);
    }
    if (count) pipeline_->FitBounds(&header->bounds, packets, count);

    // Each array is a count and its elements; a block of 0x34-byte records between the texture
    // coordinates and the corners is skipped.
    int normalCount = Read<int>(cursor);
    const char* normals = *cursor;
    *cursor += normalCount * static_cast<int>(sizeof(NormalPacket));
    int texCoordCount = Read<int>(cursor);
    const char* texCoords = *cursor;
    *cursor += texCoordCount * static_cast<int>(sizeof(TexCoord));
    int skipped = Read<int>(cursor);
    *cursor += skipped * 0x34;
    int cornerCount = Read<int>(cursor);
    const char* corners = *cursor;
    *cursor += cornerCount * static_cast<int>(sizeof(MeshCorner));
    int wordCount = Read<int>(cursor);
    const char* words = *cursor;
    *cursor += wordCount * 4;
    int indexCount = Read<int>(cursor);
    const char* indices = *cursor;
    *cursor += indexCount * 2;
    mesh->AddPart(name, packets, count, reinterpret_cast<const NormalPacket*>(normals), normalCount,
                  reinterpret_cast<const TexCoord*>(texCoords), texCoordCount,
                  reinterpret_cast<const MeshCorner*>(corners), cornerCount,
                  reinterpret_cast<const std::uint32_t*>(words), wordCount,
                  reinterpret_cast<const std::uint16_t*>(indices), indexCount);
    if (packets) AlignedArray<VertexPacket>::Free(packets);
}
EI_REPLACE_METHOD(0x004F87D0, CLinkFigureLoader::LoadOldPart);
