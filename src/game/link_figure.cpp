// The 3D library's figure meshes and their part loaders; see link_figure.h.
#include "link_figure.h"

#include "library_trees.h"
#include "model.h"
#include "texture.h"   // CTextureDevice
#include "render/render.h"   // MeshChanged

#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/stlp_tree_abi.h"

#include <cstring>
#include <new>
#include <vector>

namespace {

constexpr std::uint32_t kOne = 0x3F800000;   // 1.0f, the w row of every vertex packet

template <typename T>
T Read(const char** cursor) {
    T value;
    std::memcpy(&value, *cursor, sizeof(T));
    *cursor += sizeof(T);
    return value;
}

// The array of T the file data holds at `at` (unaligned records the loaders copy).
template <typename T>
const T* FileArray(const char* at) {
    return reinterpret_cast<const T*>(at);
}

// Appends `count` elements to an aligned array; returns where they start.
template <typename T>
int Append(lib::AlignedArray<T>& array, const T* from, int count) {
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
VertexPacket* AllocatePackets(int count) { return count ? lib::AlignedArray<VertexPacket>::Allocate(count) : nullptr; }

void SetW(VertexPacket& packet) {
    for (int k = 3; k >= 0; --k) std::memcpy(&packet.block[3][k], &kOne, 4);
}

// One vertex packet from its FIG8 record at *cursor (3 blocks of 8 versions; moved
// past it), morphed by s, t, u; w = 1.
void MorphFig8Packet(VertexPacket& packet, const char** cursor, float s, float t, float u) {
    for (int b = 0; b < 3; ++b) {
        const float* versions = FileArray<float>(*cursor);
        for (int k = 0; k < 4; ++k) packet.block[b][k] = Morph8(versions + k, s, t, u);
        *cursor += 0x80;
    }
    SetW(packet);
}

// The same for the older records (3 blocks of 6 versions).
void MorphOldPacket(VertexPacket& packet, const char** cursor, float s, float t, float u) {
    for (int b = 0; b < 3; ++b) {
        const float* versions = FileArray<float>(*cursor);
        for (int k = 0; k < 4; ++k) packet.block[b][k] = Morph6(versions + k, s, t, u);
        *cursor += 0x60;
    }
    SetW(packet);
}

} // namespace


// No references is the base's.
CLinkMeshBase::CLinkMeshBase(const std::string* name) : refCount_(0), name_(*name) { headers_.Construct(); }

// The name goes after the body (a member).
CLinkMeshBase::~CLinkMeshBase() { headers_.Destroy(); }

CLinkMesh::CLinkMesh(CLinkFigureLoader* loader, const std::string* name, float s, float t, float u)
    : CLinkMeshBase(name) {
    morphWeights_[0] = s;
    morphWeights_[1] = t;
    morphWeights_[2] = u;
    vertices_.Construct();
    normals_.Construct();
    texCoords_.Construct();
    corners_.Construct();
    words_.Construct();
    indices_.Construct();
    ranges_.Construct();
    loader_ = loader;
    pipeline_ = loader->renderer_->pipeline_->transform_;
}

CLinkMesh::~CLinkMesh() {
    render::MeshChanged(this, render::MeshChange::Rebuilt);   // the parts the new renderer keeps
    ranges_.Destroy();
    indices_.FreeData();
    words_.FreeData();
    corners_.FreeData();
    texCoords_.FreeData();
    normals_.FreeData();
    vertices_.FreeData();
}

bool CLinkMesh::AddPart(const std::string* name, const VertexPacket* vertices, int vertexCount,
                        const NormalPacket* normals, int normalCount, const TexCoord* texCoords, int texCoordCount,
                        const MeshCorner* corners, int cornerCount, const std::uint32_t* words, int wordCount,
                        const std::uint16_t* indices, int indexCount) {
    render::MeshChanged(this, render::MeshChange::Rebuilt);   // the parts the new renderer keeps
    if (ranges_.find(*name) != ranges_.end()) return false;

    LinkPartRanges ranges{};
    ranges.vertices = {Append(vertices_, vertices, vertexCount), vertexCount};
    ranges.normals = {Append(normals_, normals, normalCount), normalCount};
    ranges.texCoords = {Append(texCoords_, texCoords, texCoordCount), texCoordCount};

    int start = corners_.size;
    corners_.SetSize(start + cornerCount, -1);
    std::memcpy(corners_.data + start, corners, static_cast<std::size_t>(cornerCount) * sizeof(MeshCorner));
    ranges.corners = {start, cornerCount};

    // The out-of-line SetSize(n, -1) for these two.
    ranges.words = {Append(words_, words, wordCount), wordCount};
    ranges.indices = {Append(indices_, indices, indexCount), indexCount};

    ranges_.Subscript(*name) = ranges;
    return true;
}

// ---- Part loaders ---------------------------------------------------------------------------------

void CLinkFigureLoader::LoadFigPart(CLinkMesh* mesh, const std::string* name, const char** cursor, float s, float t,
                                    float u) {
    LinkPartHeader* header = &mesh->headers_.Subscript<lib_trees::LinkHeaderElement>(*name);
    FigHeader fig = Read<FigHeader>(cursor);
    header->materialValue = Read<std::uint32_t>(cursor);
    header->meshType = Read<std::uint32_t>(cursor);
    // The morphed bounds (8 or 1 versions) are not used here.
    *cursor += fig.magic == kFig8Magic ? 0x140 : 0x28;

    int count = fig.vertexCount;
    VertexPacket* packets = AllocatePackets(count);
    for (int i = 0; i < count; ++i) {
        VertexPacket& packet = packets[i];
        if (fig.magic == kFig8Magic) {
            MorphFig8Packet(packet, cursor, s, t, u);
            continue;
        }
        for (int b = 0; b < 3; ++b) {
            std::memcpy(packet.block[b], *cursor, 0x10);
            *cursor += 0x10;
        }
        SetW(packet);
    }
    if (count) pipeline_->FitBounds(&header->box.bounds, packets, count);

    const char* normals = *cursor;
    const char* texCoords = normals + fig.normalCount * static_cast<int>(sizeof(NormalPacket));
    const char* indices = texCoords + fig.texCoordCount * static_cast<int>(sizeof(TexCoord));
    const char* corners = indices + fig.indexCount * 2;
    const char* words = corners + fig.cornerCount * static_cast<int>(sizeof(MeshCorner));
    *cursor = words + fig.wordCount * 4;
    mesh->AddPart(name, packets, count, FileArray<NormalPacket>(normals), fig.normalCount,
                  FileArray<TexCoord>(texCoords), fig.texCoordCount, FileArray<MeshCorner>(corners), fig.cornerCount,
                  FileArray<std::uint32_t>(words), fig.wordCount, FileArray<std::uint16_t>(indices), fig.indexCount);
    if (packets) lib::AlignedArray<VertexPacket>::Free(packets);
}

void CLinkFigureLoader::LoadOldPart(CLinkMesh* mesh, const std::string* name, const char** cursor, float s, float t,
                                    float u) {
    LinkPartHeader* header = &mesh->headers_.Subscript<lib_trees::LinkHeaderElement>(*name);
    header->materialValue = Read<std::uint32_t>(cursor);
    header->meshType = Read<std::uint32_t>(cursor);
    *cursor += 0xF0;   // the morphed bounds
    int count = Read<int>(cursor);

    VertexPacket* packets = AllocatePackets(count);
    for (int i = 0; i < count; ++i) MorphOldPacket(packets[i], cursor, s, t, u);
    if (count) pipeline_->FitBounds(&header->box.bounds, packets, count);

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
    mesh->AddPart(name, packets, count, FileArray<NormalPacket>(normals), normalCount,
                  FileArray<TexCoord>(texCoords), texCoordCount, FileArray<MeshCorner>(corners), cornerCount,
                  FileArray<std::uint32_t>(words), wordCount, FileArray<std::uint16_t>(indices), indexCount);
    if (packets) lib::AlignedArray<VertexPacket>::Free(packets);
}

// ---- Morphing a loaded part again -----------------------------------------------------------------

void CLinkFigureLoader::RemorphFigPart(CLinkMesh* mesh, const std::string* name, const char** cursor, float s,
                                       float t, float u) {
    LinkPartHeader* header = &mesh->headers_.Subscript<lib_trees::LinkHeaderElement>(*name);
    FigHeader fig;
    std::memcpy(&fig, *cursor, sizeof(fig));
    *cursor += 0x168;   // the header, two dwords and the FIG8 bounds (a plain FIG is read as FIG8 too)
    int count = fig.vertexCount;
    VertexPacket* packets = AllocatePackets(count);
    for (int i = 0; i < count; ++i) MorphFig8Packet(packets[i], cursor, s, t, u);
    if (count) pipeline_->FitBounds(&header->box.bounds, packets, count);
    mesh->SetPartVertices(name, packets, static_cast<unsigned>(count));
    if (packets) lib::AlignedArray<VertexPacket>::Free(packets);
}

void CLinkFigureLoader::RemorphOldPart(CLinkMesh* mesh, const std::string* name, const char** cursor, float s,
                                       float t, float u) {
    LinkPartHeader* header = &mesh->headers_.Subscript<lib_trees::LinkHeaderElement>(*name);
    *cursor += 0xF8;   // two dwords and the morphed bounds
    int count = Read<int>(cursor);
    VertexPacket* packets = AllocatePackets(count);
    for (int i = 0; i < count; ++i) MorphOldPacket(packets[i], cursor, s, t, u);
    if (count) pipeline_->FitBounds(&header->box.bounds, packets, count);
    mesh->SetPartVertices(name, packets, static_cast<unsigned>(count));
    if (packets) lib::AlignedArray<VertexPacket>::Free(packets);
}

int CLinkMesh::MemoryBytes() const {
    const int parts = static_cast<int>(ranges_.size());
    const int inner = texCoords_.size + 8 * (vertices_.size + normals_.size) + 6 * parts;
    return 2 * (indices_.size + 3 * corners_.size + 2 * (words_.size + 0x2C + 2 * inner));
}

bool CLinkMesh::SetPartVertices(const std::string* name, const VertexPacket* vertices, unsigned count) {
    const LinkRangeMap::Node* node = ranges_.find(*name);
    if (node == ranges_.end()) return false;
    const LinkPartRanges& r = node->second;
    unsigned n = static_cast<unsigned>(r.vertices.count) < count ? static_cast<unsigned>(r.vertices.count) : count;
    std::memcpy(vertices_.data + r.vertices.start, vertices, static_cast<std::size_t>(n) * sizeof(VertexPacket));
    render::MeshChanged(this, render::MeshChange::Positions);   // the parts the new renderer keeps
    return true;
}

// ---- CLinkMesh's slots ----------------------------------------------------------------------------

namespace {

// Moves the elements after [start, start + count) down over them and shortens the array (the
// capacity and block stay).
template <typename T>
void CloseGap(T* data, int& size, int start, int count) {
    int after = size - start - count;
    if (after != 0) std::memmove(data + start, data + start + count, static_cast<std::size_t>(after) * sizeof(T));
    size -= count;
}

constexpr int kNoRect = 2000000000;

} // namespace

void CLinkMesh::SetMorph(float s, float t, float u) {
    morphWeights_[0] = s;
    morphWeights_[1] = t;
    morphWeights_[2] = u;
}

void CLinkMesh::UpdateScreenRect() {
    gameapi::SetRect(&screenRect_, kNoRect, kNoRect, -kNoRect, -kNoRect);
    for (LinkHeaderMap::Node* node = headers_.begin(); node != headers_.end(); node = LinkHeaderMap::Next(node)) {
        const RECT& part = node->second.screen;
        if (gameapi::IsRectEmpty(&part)) continue;
        if (part.left < screenRect_.left) screenRect_.left = part.left;
        if (screenRect_.right < part.right) screenRect_.right = part.right;
        if (part.top < screenRect_.top) screenRect_.top = part.top;
        if (screenRect_.bottom < part.bottom) screenRect_.bottom = part.bottom;
    }
}

bool CLinkMesh::HasPart(const std::string* name) { return ranges_.find(*name) != ranges_.end(); }

void CLinkMesh::CollectPartNames(std::vector<std::string>* out) {
    for (LinkRangeMap::Node* node = ranges_.begin(); node != ranges_.end(); node = LinkRangeMap::Next(node))
        out->push_back(node->first);
}

bool CLinkMesh::LoadPart(const std::string* name) { return loader_->LoadPartInto(this, name); }

bool CLinkMesh::RemovePart(const std::string* name) {
    LinkRangeMap::Node* node = ranges_.find(*name);
    if (node == ranges_.end()) return false;
    render::MeshChanged(this, render::MeshChange::Rebuilt);   // the parts the new renderer keeps
    LinkPartRanges removed = node->second;
    ranges_.Erase(node);

    CloseGap(vertices_.data, vertices_.size, removed.vertices.start, removed.vertices.count);
    CloseGap(normals_.data, normals_.size, removed.normals.start, removed.normals.count);
    CloseGap(texCoords_.data, texCoords_.size, removed.texCoords.start, removed.texCoords.count);
    CloseGap(corners_.data, corners_.size, removed.corners.start, removed.corners.count);
    CloseGap(words_.data, words_.size, removed.words.start, removed.words.count);
    CloseGap(indices_.data, indices_.size, removed.indices.start, removed.indices.count);

    for (LinkRangeMap::Node* n = ranges_.begin(); n != ranges_.end(); n = LinkRangeMap::Next(n)) {
        LinkPartRanges& r = n->second;
        if (!(static_cast<std::uint32_t>(r.indices.start) > static_cast<std::uint32_t>(removed.indices.start))) continue;
        r.vertices.start -= removed.vertices.start;
        r.normals.start -= removed.normals.start;
        r.texCoords.start -= removed.texCoords.start;
        r.corners.start -= removed.corners.start;
        r.words.start -= removed.words.start;
        r.indices.start -= removed.indices.start;
    }
    return true;
}

bool CLinkMesh::GetPartData(const std::string* name, VertexPacket** vertices, MeshCorner** corners,
                            std::uint16_t** indices, int* vertexCount, int* indexCount) {
    const LinkRangeMap::Node* node = ranges_.find(*name);
    if (node == ranges_.end()) return false;
    const LinkPartRanges& r = node->second;
    *vertices = vertices_.data + r.vertices.start;
    *corners = corners_.data + r.corners.start;
    *indices = indices_.data + r.indices.start;
    *indexCount = r.indices.count;
    *vertexCount = r.vertices.count;
    return true;
}
