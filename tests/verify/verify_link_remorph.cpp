// Checks for the link figure re-morphing (A-S8): 004F3800 CLinkMesh::SetPartVertices and 004F9FD0 /
// 004FA670 RemorphFigPart / RemorphOldPart (link_figure.cpp), 004F9E50 RemorphPart (link_loader.cpp).
// They overwrite a part's vertex packets (and its header's bounds) in place, so the original runs
// first on the real mesh, its outputs are kept and the old bytes put back, then ours runs and the
// two are compared (vertices and bounds nearly, the cursor exactly). RemorphPart only dispatches
// (ours only). Scenarios: `models`, `ui_hero_screen` (the loader's Remorph, slot 6 004F75E0, on a
// mesh only one holder uses).
#include "verify.h"

#include "../../src/game/link_figure.h"

#include <cstring>
#include <vector>

namespace {

bool Near(float expected, float actual) { return ei::verify::NearlyEqual<float>(expected, actual, 8, 1e-5f); }

// Part `name`'s vertex packets in the mesh (none when it is not a part).
struct PartVertices {
    VertexPacket* data = nullptr;
    int count = 0;
    PartVertices(CLinkMesh* mesh, const stlp::string* name) {
        const LinkRangeMap::Node* node = mesh->ranges_.find(*name);
        if (node == mesh->ranges_.end()) return;
        data = mesh->vertices_.data + node->second.vertices.start;
        count = node->second.vertices.count;
    }
    std::vector<VertexPacket> Save() const { return std::vector<VertexPacket>(data, data + count); }
    void Restore(const std::vector<VertexPacket>& saved) const {
        if (count) std::memcpy(data, saved.data(), saved.size() * sizeof(VertexPacket));
    }
};

// The index of the first packet of `actual` that differs from `expected`, or -1.
int FirstDiff(const std::vector<VertexPacket>& expected, const VertexPacket* actual, bool nearly) {
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const float* e = &expected[i].block[0][0];
        const float* a = &actual[i].block[0][0];
        for (int k = 0; k < 16; ++k)
            if (nearly ? !Near(e[k], a[k]) : std::memcmp(&e[k], &a[k], 4) != 0) return static_cast<int>(i);
    }
    return -1;
}

bool SameBounds(const ClipBounds& e, const ClipBounds& a) {
    if (e.flags != a.flags) return false;
    const float* ef = &e.center.x;
    const float* af = &a.center.x;
    for (int k = 0; k < 10; ++k)
        if (!Near(ef[k], af[k])) return false;
    return true;
}

// The part header's bounds, or null when the headers map has no such part yet.
ClipBounds* HeaderBounds(CLinkMesh* mesh, const stlp::string* name) {
    LinkHeaderMap::Node* node = mesh->headers_.find(*name);
    return node == mesh->headers_.end() ? nullptr : &node->second.bounds;
}

using RemorphFn = void(__fastcall*)(CLinkFigureLoader*, void*, CLinkMesh*, const stlp::string*, const char**, float,
                                    float, float);

// The original, then ours from the same state; compared as described above.
void CompareRemorph(const char* check, RemorphFn original, void (CLinkFigureLoader::*ours)(CLinkMesh*, const stlp::string*,
                    const char**, float, float, float), CLinkFigureLoader* self, CLinkMesh* mesh,
                    const stlp::string* name, const char** cursor, float s, float t, float u) {
    PartVertices part(mesh, name);
    const std::vector<VertexPacket> before = part.Save();
    ClipBounds* bounds = HeaderBounds(mesh, name);
    ClipBounds boundsBefore{};
    if (bounds) boundsBefore = *bounds;

    const char* expectedCursor = *cursor;
    original(self, nullptr, mesh, name, &expectedCursor, s, t, u);
    const std::vector<VertexPacket> expected = part.Save();
    bounds = HeaderBounds(mesh, name);   // the original's operator[] may have added it
    ClipBounds expectedBounds{};
    if (bounds) expectedBounds = *bounds;

    part.Restore(before);
    if (bounds) *bounds = boundsBefore;
    (self->*ours)(mesh, name, cursor, s, t, u);

    const char* diff = nullptr;
    int bad = -1;
    if (expectedCursor != *cursor) diff = "cursor";
    else if ((bad = FirstDiff(expected, part.data, true)) >= 0) diff = "vertex";
    else if (bounds && !SameBounds(expectedBounds, *bounds)) diff = "bounds";
    ei::verify::Check(check, true, diff == nullptr, "%s %d (%s, %d vertices)", diff ? diff : "", bad, name->data(),
                      part.count);
}

} // namespace

// 004F3800: MOV EAX,[ESP+4] (4), PUSH ESI (1).
static bool(__fastcall* s_setPartVertices)(CLinkMesh*, void*, const stlp::string*, const VertexPacket*, unsigned);
static bool __fastcall SetPartVerticesVerify(CLinkMesh* self, void*, const stlp::string* name,
                                             const VertexPacket* vertices, unsigned count) {
    PartVertices part(self, name);
    const std::vector<VertexPacket> before = part.Save();
    const bool expected = s_setPartVertices(self, nullptr, name, vertices, count);
    const std::vector<VertexPacket> after = part.Save();
    part.Restore(before);
    const bool actual = self->SetPartVertices(name, vertices, count);
    const int bad = FirstDiff(after, part.data, false);
    ei::verify::Check("CLinkMesh::SetPartVertices", true, expected == actual && bad < 0, "result %d/%d vertex %d (%u)",
                      expected, actual, bad, count);
    return actual;
}
EI_VERIFY(0x004F3800, SetPartVerticesVerify, 5, s_setPartVertices);

// 004F9FD0 / 004FA670: PUSH -1 (2), PUSH imm32 (5).
static RemorphFn s_remorphFig;
static void __fastcall RemorphFigPartVerify(CLinkFigureLoader* self, void*, CLinkMesh* mesh, const stlp::string* name,
                                            const char** cursor, float s, float t, float u) {
    CompareRemorph("CLinkFigureLoader::RemorphFigPart", s_remorphFig, &CLinkFigureLoader::RemorphFigPart, self, mesh,
                   name, cursor, s, t, u);
}
EI_VERIFY(0x004F9FD0, RemorphFigPartVerify, 7, s_remorphFig);

static RemorphFn s_remorphOld;
static void __fastcall RemorphOldPartVerify(CLinkFigureLoader* self, void*, CLinkMesh* mesh, const stlp::string* name,
                                            const char** cursor, float s, float t, float u) {
    CompareRemorph("CLinkFigureLoader::RemorphOldPart", s_remorphOld, &CLinkFigureLoader::RemorphOldPart, self, mesh,
                   name, cursor, s, t, u);
}
EI_VERIFY(0x004FA670, RemorphOldPartVerify, 7, s_remorphOld);

// 004F9E50 (ours only): true only for a part whose record was found; the part is still a part.
static bool(__fastcall* s_remorphPart)(CLinkFigureLoader*, void*, CLinkMesh*, LinkFigureSet*, const stlp::string*, float,
                                       float, float);
static bool __fastcall RemorphPartVerify(CLinkFigureLoader* self, void*, CLinkMesh* mesh, LinkFigureSet* set,
                                         const stlp::string* name, float s, float t, float u) {
    const bool had = mesh->ranges_.find(*name) != mesh->ranges_.end();
    const bool actual = self->RemorphPart(mesh, set, name, s, t, u);
    const bool has = mesh->ranges_.find(*name) != mesh->ranges_.end();
    ei::verify::Check("CLinkFigureLoader::RemorphPart", had, has, "%s (result %d)", name->data(), actual);
    return actual;
}
EI_VERIFY(0x004F9E50, RemorphPartVerify, 0, s_remorphPart);
