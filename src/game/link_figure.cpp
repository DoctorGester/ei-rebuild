// The 3D library's figure meshes and their part loaders (004F2650-004FBD80); see link_figure.h.
#include "link_figure.h"

#include "originals_script.h"
#include "texture.h"   // CTextureDevice

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/gameapi.h"

#include <cstring>
#include <new>

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

// One vertex packet from its FIG8 record at *cursor (3 blocks of 8 versions, 0x180 bytes; moved
// past it), morphed by s, t, u; w = 1.
void MorphFig8Packet(VertexPacket& packet, const char** cursor, float s, float t, float u) {
    for (int b = 0; b < 3; ++b) {
        const float* versions = reinterpret_cast<const float*>(*cursor);
        for (int k = 0; k < 4; ++k) packet.block[b][k] = Morph8(versions + k, s, t, u);
        *cursor += 0x80;
    }
    SetW(packet);
}

// The same for the older records (3 blocks of 6 versions, 0x120 bytes).
void MorphOldPacket(VertexPacket& packet, const char** cursor, float s, float t, float u) {
    for (int b = 0; b < 3; ++b) {
        const float* versions = reinterpret_cast<const float*>(*cursor);
        for (int k = 0; k < 4; ++k) packet.block[b][k] = Morph6(versions + k, s, t, u);
        *cursor += 0x60;
    }
    SetW(packet);
}

} // namespace

// ---- CLinkMesh ------------------------------------------------------------------------------------

CLinkMesh* CLinkMesh::Construct(CLinkFigureLoader* loader, const stlp::string* name, float s, float t, float u) {
    refCount_ = 0;
    ei::SetVtable(this, kRootVtableAddress);
    new (&name_) stlp::string(*name);
    headers_.Construct();
    ei::SetVtable(this, kBaseVtableAddress);
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
    ei::SetVtable(this, kVtableAddress);
    loader_ = loader;
    pipeline_ = loader->renderer_->pipeline_->transform_;
    return this;
}
EI_REPLACE_METHOD(0x004F2C00, CLinkMesh::Construct);

void CLinkMesh::Destruct() {
    ei::SetVtable(this, kVtableAddress);
    ranges_.Destroy();
    indices_.FreeData();
    words_.FreeData();
    corners_.FreeData();
    texCoords_.FreeData();
    normals_.FreeData();
    vertices_.FreeData();
    DestructBase();
}
EI_REPLACE_METHOD(0x004F2E40, CLinkMesh::Destruct);

void CLinkMesh::DestructBase() {
    ei::SetVtable(this, kBaseVtableAddress);
    headers_.Destroy();
    name_.~string();
    ei::SetVtable(this, kRootVtableAddress);
}
EI_REPLACE_METHOD(0x004F2D40, CLinkMesh::DestructBase);

CLinkMesh* CLinkMesh::DeletingDestructBase(unsigned flags) {
    DestructBase();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x004F2E00, CLinkMesh::DeletingDestructBase);

void* CLinkMesh::DeleteThis(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
void* __fastcall LinkMeshDeleteThis(CLinkMesh* self, void*, unsigned flags) { return self->CLinkMesh::DeleteThis(flags); }
EI_REPLACE(0x004F2E20, LinkMeshDeleteThis);

bool CLinkMesh::AddPart(const stlp::string* name, const VertexPacket* vertices, int vertexCount,
                        const NormalPacket* normals, int normalCount, const TexCoord* texCoords, int texCoordCount,
                        const MeshCorner* corners, int cornerCount, const std::uint32_t* words, int wordCount,
                        const std::uint16_t* indices, int indexCount) {
    if (ranges_.find(*name) != ranges_.end()) return false;

    LinkPartRanges ranges{};
    ranges.vertices = {Append(vertices_, vertices, vertexCount), vertexCount};
    ranges.normals = {Append(normals_, normals, normalCount), normalCount};
    ranges.texCoords = {Append(texCoords_, texCoords, texCoordCount), texCoordCount};

    int start = corners_.size;
    corners_.SetSize(start + cornerCount, -1);
    std::memcpy(corners_.data + start, corners, static_cast<std::size_t>(cornerCount) * sizeof(MeshCorner));
    ranges.corners = {start, cornerCount};

    // The original's out-of-line SetSize(n, -1) (00452EB0 / 0044FA90) for these two.
    ranges.words = {Append(words_, words, wordCount), wordCount};
    ranges.indices = {Append(indices_, indices, indexCount), indexCount};

    *LinkRangeMapAtOriginal(&ranges_, nullptr, name) = ranges;
    return true;
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
    if (packets) lib::AlignedArray<VertexPacket>::Free(packets);
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
    for (int i = 0; i < count; ++i) MorphOldPacket(packets[i], cursor, s, t, u);
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
    if (packets) lib::AlignedArray<VertexPacket>::Free(packets);
}
EI_REPLACE_METHOD(0x004F87D0, CLinkFigureLoader::LoadOldPart);

// ---- Morphing a loaded part again -----------------------------------------------------------------

namespace {
// 004F3800 (CLinkMesh::SetPartVertices, below), called through its address so its check sees these calls.
__declspec(naked) bool __fastcall SetPartVerticesAt(CLinkMesh*, void*, const stlp::string*, const VertexPacket*, unsigned)
    EI_ORIGINAL(0x004F3800)
} // namespace

void CLinkFigureLoader::RemorphFigPart(CLinkMesh* mesh, const stlp::string* name, const char** cursor, float s,
                                       float t, float u) {
    LinkPartHeader* header = LinkHeaderMapAtOriginal(&mesh->headers_, nullptr, name);
    FigHeader fig;
    std::memcpy(&fig, *cursor, sizeof(fig));
    *cursor += 0x168;   // the header, two dwords and the FIG8 bounds (a plain FIG is read as FIG8 too)
    int count = fig.vertexCount;
    VertexPacket* packets = AllocatePackets(count);
    for (int i = 0; i < count; ++i) MorphFig8Packet(packets[i], cursor, s, t, u);
    if (count) pipeline_->FitBounds(&header->bounds, packets, count);
    SetPartVerticesAt(mesh, nullptr, name, packets, static_cast<unsigned>(count));
    if (packets) lib::AlignedArray<VertexPacket>::Free(packets);
}
EI_REPLACE_METHOD(0x004F9FD0, CLinkFigureLoader::RemorphFigPart);

void CLinkFigureLoader::RemorphOldPart(CLinkMesh* mesh, const stlp::string* name, const char** cursor, float s,
                                       float t, float u) {
    LinkPartHeader* header = LinkHeaderMapAtOriginal(&mesh->headers_, nullptr, name);
    *cursor += 0xF8;   // two dwords and the morphed bounds
    int count = Read<int>(cursor);
    VertexPacket* packets = AllocatePackets(count);
    for (int i = 0; i < count; ++i) MorphOldPacket(packets[i], cursor, s, t, u);
    if (count) pipeline_->FitBounds(&header->bounds, packets, count);
    SetPartVerticesAt(mesh, nullptr, name, packets, static_cast<unsigned>(count));
    if (packets) lib::AlignedArray<VertexPacket>::Free(packets);
}
EI_REPLACE_METHOD(0x004FA670, CLinkFigureLoader::RemorphOldPart);

int CLinkMesh::MemoryBytes() const {
    const int parts = static_cast<int>(ranges_.size());
    const int inner = texCoords_.size + 8 * (vertices_.size + normals_.size) + 6 * parts;
    return 2 * (indices_.size + 3 * corners_.size + 2 * (words_.size + 0x2C + 2 * inner));
}
EI_REPLACE_METHOD(0x004F4B50, CLinkMesh::MemoryBytes);

bool CLinkMesh::SetPartVertices(const stlp::string* name, const VertexPacket* vertices, unsigned count) {
    const LinkRangeMap::Node* node = ranges_.find(*name);
    if (node == ranges_.end()) return false;
    const LinkPartRanges& r = node->second;
    unsigned n = static_cast<unsigned>(r.vertices.count) < count ? static_cast<unsigned>(r.vertices.count) : count;
    std::memcpy(vertices_.data + r.vertices.start, vertices, static_cast<std::size_t>(n) * sizeof(VertexPacket));
    return true;
}
EI_REPLACE_METHOD(0x004F3800, CLinkMesh::SetPartVertices);

// ---- CLinkMesh's slots ----------------------------------------------------------------------------

namespace {

// 004F7480 (thiscall on the loader, RET 8, AL): loads part `name` into `mesh` unless it has it.
__declspec(naked) bool __fastcall LoaderLoadPart(CLinkFigureLoader* /*loader*/, void*, CLinkMesh* /*mesh*/,
                                                 const stlp::string* /*name*/) EI_ORIGINAL(0x004F7480)

// Moves the elements after [start, start + count) down over them and shortens the array (the
// capacity and block stay).
template <typename T>
void CloseGap(T* data, int& size, int start, int count, int elementBytes) {
    int after = size - start - count;
    if (after != 0) {
        auto* bytes = reinterpret_cast<std::uint8_t*>(data);
        std::memmove(bytes + start * elementBytes, bytes + (start + count) * elementBytes,
                     static_cast<std::size_t>(after) * static_cast<std::size_t>(elementBytes));
    }
    size -= count;
}

constexpr int kNoRect = 2000000000;

} // namespace

void CLinkMesh::SetMorph(float s, float t, float u) {
    morphWeights_[0] = s;
    morphWeights_[1] = t;
    morphWeights_[2] = u;
}
EI_REPLACE_METHOD(0x004F2F90, CLinkMesh::SetMorph);

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
void __fastcall LinkMeshUpdateScreenRect(CLinkMesh* self) { self->CLinkMesh::UpdateScreenRect(); }
EI_REPLACE(0x004F45E0, LinkMeshUpdateScreenRect);

bool CLinkMesh::HasPart(const stlp::string* name) { return ranges_.find(*name) != ranges_.end(); }
bool __fastcall LinkMeshHasPart(CLinkMesh* self, void*, const stlp::string* name) { return self->CLinkMesh::HasPart(name); }
EI_REPLACE(0x004F3040, LinkMeshHasPart);

void CLinkMesh::CollectPartNames(stlp::vector<stlp::string>* out) {
    for (LinkRangeMap::Node* node = ranges_.begin(); node != ranges_.end(); node = LinkRangeMap::Next(node))
        out->push_back(node->first);
}
void __fastcall LinkMeshCollectPartNames(CLinkMesh* self, void*, stlp::vector<stlp::string>* out) {
    self->CLinkMesh::CollectPartNames(out);
}
EI_REPLACE(0x004F2FB0, LinkMeshCollectPartNames);

bool CLinkMesh::LoadPart(const stlp::string* name) { return LoaderLoadPart(loader_, nullptr, this, name); }
bool __fastcall LinkMeshLoadPart(CLinkMesh* self, void*, const stlp::string* name) { return self->CLinkMesh::LoadPart(name); }
EI_REPLACE(0x004F30D0, LinkMeshLoadPart);

bool CLinkMesh::RemovePart(const stlp::string* name) {
    LinkRangeMap::Node* node = ranges_.find(*name);
    if (node == ranges_.end()) return false;
    LinkPartRanges removed = node->second;
    LinkRangeMapEraseOriginal(&ranges_, nullptr, node);

    CloseGap(vertices_.data, vertices_.size, removed.vertices.start, removed.vertices.count, sizeof(VertexPacket));
    CloseGap(normals_.data, normals_.size, removed.normals.start, removed.normals.count, sizeof(NormalPacket));
    CloseGap(texCoords_.data, texCoords_.size, removed.texCoords.start, removed.texCoords.count, sizeof(TexCoord));
    CloseGap(corners_.data, corners_.size, removed.corners.start, removed.corners.count, sizeof(MeshCorner));
    CloseGap(words_.data, words_.size, removed.words.start, removed.words.count, 4);
    CloseGap(indices_.data, indices_.size, removed.indices.start, removed.indices.count, 2);

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
bool __fastcall LinkMeshRemovePart(CLinkMesh* self, void*, const stlp::string* name) { return self->CLinkMesh::RemovePart(name); }
EI_REPLACE(0x004F3530, LinkMeshRemovePart);

bool CLinkMesh::GetPartData(const stlp::string* name, VertexPacket** vertices, MeshCorner** corners,
                            std::uint16_t** indices, int* vertexCount, int* indexCount) {
    const LinkRangeMap::Node* node = ranges_.find(*name);
    if (node == ranges_.end()) return false;
    const LinkPartRanges& r = node->second;
    *vertices = vertices_.data + r.vertices.start;
    *corners = reinterpret_cast<MeshCorner*>(corners_.data + r.corners.start);
    *indices = indices_.data + r.indices.start;
    *indexCount = r.indices.count;
    *vertexCount = r.vertices.count;
    return true;
}
bool __fastcall LinkMeshGetPartData(CLinkMesh* self, void*, const stlp::string* name, VertexPacket** vertices,
                                    MeshCorner** corners, std::uint16_t** indices, int* vertexCount, int* indexCount) {
    return self->CLinkMesh::GetPartData(name, vertices, corners, indices, vertexCount, indexCount);
}
EI_REPLACE(0x004F3780, LinkMeshGetPartData);
