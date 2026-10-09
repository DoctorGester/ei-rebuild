// Verify wrapper for Terrain::AddDecalTriangles (terrain_decal.cpp): reached every frame with unit
// shadows (the shadow projector 0056C750 lays each shadow on the ground; zone1 after a new game).
#include "verify.h"

#include "../../src/game/ground_decal.h"
#include "../../src/game/terrain.h"

#include <cstring>
#include <vector>

namespace {

// 006CCF20: PUSH EBP; MOV EBP,ESP; SUB ESP,0xF4 (9 bytes). Appends triangles to the builder: the
// original first; what it appended (vertices, texture coordinates, indices) is kept, the builder's
// counts and array sizes are put back, and ours runs (the same unhooked 004D8490 adds the
// triangles); the counts and everything appended compared. The cell rectangle comes from FISTPs:
// a corner on a .5 edge may rarely give another rectangle.
void(__fastcall* s_addDecalTriangles)(Terrain*, void*, DecalBuilder*, float* const*);

struct Appended {
    int vertices = 0, indices = 0;
    std::vector<float> positions;
    std::vector<DecalTexCoord> texCoords;
    std::vector<std::uint16_t> indexList;

    void Save(const DecalBuilder* b, int fromVertex, int fromIndex) {
        vertices = b->vertexCount_;
        indices = b->indexCount_;
        positions.clear();
        texCoords.clear();
        indexList.clear();
        for (int i = fromVertex; i < vertices; ++i) {
            const VertexPacket& p = b->points_->data[i / 4];
            for (int k = 0; k < 3; ++k) positions.push_back(p.block[k][i & 3]);
            texCoords.push_back(b->texCoords_->data[i]);
        }
        for (int i = fromIndex; i < indices; ++i) indexList.push_back(b->indices_->data[i]);
    }
};

void __fastcall AddDecalTrianglesVerify(Terrain* self, void*, DecalBuilder* builder, float* const* corners) {
    int vertices = builder->vertexCount_, indices = builder->indexCount_;
    int points = builder->points_->size, texCoords = builder->texCoords_->size, indexSize = builder->indices_->size;
    s_addDecalTriangles(self, nullptr, builder, corners);
    Appended original, ours;
    original.Save(builder, vertices, indices);
    int pointsAfter = builder->points_->size, texCoordsAfter = builder->texCoords_->size;
    int indexSizeAfter = builder->indices_->size;
    builder->vertexCount_ = vertices;
    builder->indexCount_ = indices;
    builder->points_->size = points;
    builder->texCoords_->size = texCoords;
    builder->indices_->size = indexSize;
    self->AddDecalTriangles(builder, corners);
    ours.Save(builder, vertices, indices);

    ei::verify::Check("Terrain::AddDecalTriangles", original.vertices, ours.vertices, "vertex count");
    ei::verify::Check("Terrain::AddDecalTriangles", original.indices, ours.indices, "index count");
    bool sizes = pointsAfter == builder->points_->size && texCoordsAfter == builder->texCoords_->size &&
                 indexSizeAfter == builder->indices_->size;
    ei::verify::Check("Terrain::AddDecalTriangles", true, sizes, "reserved sizes");
    bool same = original.positions == ours.positions && original.indexList == ours.indexList &&
                original.texCoords.size() == ours.texCoords.size() &&
                std::memcmp(original.texCoords.data(), ours.texCoords.data(),
                            original.texCoords.size() * sizeof(DecalTexCoord)) == 0;
    ei::verify::Check("Terrain::AddDecalTriangles", true, same, "appended triangles");
}
EI_VERIFY(0x006CCF20, AddDecalTrianglesVerify, 9, s_addDecalTriangles);

} // namespace
