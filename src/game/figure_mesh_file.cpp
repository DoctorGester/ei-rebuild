// CFigureMesh's own file format (vtable slots; no direct calls in the game): 0059F710 writes the
// mesh to an MFC CFile, 0059F8F0 reads it back. The name, then every array as a count and its
// elements, then the bounding values. See figure_mesh.h and docs/notes/world.md (seventh round).
#include "figure_mesh.h"

#include "ei/hook.h"
#include "ei/vcall.h"

namespace {

constexpr int kSlotFileRead = 13;    // CFile slot 13 (+0x34): Read(buffer, count)
constexpr int kSlotFileWrite = 14;   // CFile slot 14 (+0x38): Write(buffer, count)

// 00700E17 (RET 4). CString::GetBufferSetLength.
__declspec(naked) char* __fastcall StringBufferSetLength(void*, void*, int) EI_ORIGINAL(0x00700E17)

void Write(void* file, const void* data, int size) { ei::VCall<void>(file, kSlotFileWrite, data, static_cast<unsigned>(size)); }
void Read(void* file, void* data, int size) { ei::VCall<unsigned>(file, kSlotFileRead, data, static_cast<unsigned>(size)); }

void WriteCount(void* file, int count) { Write(file, &count, 4); }
int ReadCount(void* file) {
    int count;
    Read(file, &count, 4);
    return count;
}

char*& Name(CFigureMesh* mesh) { return *reinterpret_cast<char**>(reinterpret_cast<std::uint8_t*>(mesh) + 0x120); }

} // namespace

void CFigureMesh::SaveToFile(void* file) {
    int length = reinterpret_cast<const int*>(Name(this))[-2];
    WriteCount(file, length);
    Write(file, Name(this), length);
    WriteCount(file, buffers_.vertexCount);
    Write(file, buffers_.vertices, buffers_.vertexCount * 0x40);
    WriteCount(file, buffers_.normalCount);
    Write(file, buffers_.normals, buffers_.normalCount * 0x40);
    WriteCount(file, buffers_.texCoordCount);
    Write(file, buffers_.texCoords, buffers_.texCoordCount * 8);
    WriteCount(file, 0);
    WriteCount(file, buffers_.cornerCount);
    Write(file, buffers_.corners, buffers_.cornerCount * 6);
    WriteCount(file, buffers_.wordCount);
    Write(file, buffers_.words, buffers_.wordCount * 4);
    WriteCount(file, buffers_.indexCount);
    Write(file, buffers_.indices, buffers_.indexCount * 2);
    Write(file, &center_, 12);
    Write(file, &boxMin_, 12);
    Write(file, &boxMax_, 12);
    Write(file, &radius_, 4);
    Write(file, &unknown_158, 4);
    Write(file, &unknown_15C, 4);
}

void CFigureMesh::LoadFromFile(void* file) {
    int length = ReadCount(file);
    Read(file, StringBufferSetLength(reinterpret_cast<std::uint8_t*>(this) + 0x120, nullptr, length), length);
    // The arrays the file fills (the renderer's descriptor is not updated here).
    int count = ReadCount(file);
    vertices_.SetSize(count);
    Read(file, vertices_.data, count * 0x40);
    count = ReadCount(file);
    normals_.SetSize(count);
    Read(file, normals_.data, count * 0x40);
    count = ReadCount(file);
    texCoords_.SetSize(count);
    Read(file, texCoords_.data, count * 8);
    ReadCount(file);   // the 0 the writer puts here
    count = ReadCount(file);
    corners_.Resize(count, -1);
    Read(file, corners_.data, count * 6);
    count = ReadCount(file);
    words_.Resize(count, -1);
    Read(file, words_.data, count * 4);
    count = ReadCount(file);
    indices_.Resize(count, -1);
    Read(file, indices_.data, count * 2);
    Read(file, &center_, 12);
    Read(file, &boxMin_, 12);
    Read(file, &boxMax_, 12);
    Read(file, &radius_, 4);
    Read(file, &unknown_158, 4);
    Read(file, &unknown_15C, 4);
}

EI_REPLACE_METHOD(0x0059F710, CFigureMesh::SaveToFile);
EI_REPLACE_METHOD(0x0059F8F0, CFigureMesh::LoadFromFile);
