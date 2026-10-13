// Checks for link_mesh_draw.cpp (CLinkMesh's slots 7-14, A-S12). Main thread. All side by side:
//  - 004F3AD0 (flat colours): the original into scratch arrays, compared by content.
//  - 004F3870 (lighting) and 004F4680 (the shadow's projection): the mesh's transform object is
//    replaced by a tap for the call; ours runs first (the tap records the arguments and forwards to
//    the real object), then the original (recorded only). Compared: the arguments (the light
//    vector and the positions by content), the result and, for the lighting, the speculars.
//  - 004F3C60, 004F3FA0, 004F42D0, 004F4150 (the draws): ours draws with the device's mesh draw
//    tapped (mesh_capture.h), then the original runs with the draw skipped (it returns ours'
//    result). Compared: the draw's arguments, the positions' x, y, z rows, the result and the
//    part's screen rectangle. The original sets the device's specular state a second time (on and
//    off again), which changes nothing.
//  - 004F4850 (the shadow's rasteriser): the device's pixel converter is replaced by a tap the same
//    way; compared: the triangles handed over (count and contents) and the result.
// Scenarios: any scene with figures drawn through a link figure (units; `newgame`), the shadows of
// such figures for the last two.
#include "verify.h"

#include "mesh_capture.h"

#include "../../src/game/link_figure.h"
#include "../../src/game/texture.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using ColourArray = lib::AlignedArray<std::uint32_t>;
using OffsetArray = lib::AlignedArray<Vector3>;

std::uint32_t Word(const void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }

bool SameContents(const ColourArray& a, const ColourArray& b) {
    if (a.size != b.size) return false;
    return a.size <= 0 || std::memcmp(a.data, b.data, static_cast<std::size_t>(a.size) * 4) == 0;
}

// ---- 004F3AD0 ----

// PUSH EBP (1), MOV EBP,ESP (2), MOV EAX,[EBP+8] (3).
bool(__fastcall* s_flatColours)(CLinkMesh*, void*, const stlp::string*, std::uint32_t, std::uint32_t, ColourArray*,
                                ColourArray*);
bool __fastcall FlatColoursVerify(CLinkMesh* self, void*, const stlp::string* name, std::uint32_t colour,
                                  std::uint32_t specular, ColourArray* colours, ColourArray* speculars) {
    ColourArray expectedColours{};
    ColourArray expectedSpeculars{};
    bool expected = s_flatColours(self, nullptr, name, colour, specular, &expectedColours, &expectedSpeculars);
    bool result = self->CLinkMesh::FlatColours(name, colour, specular, colours, speculars);
    bool same = expected == result &&
                (!result || (SameContents(expectedColours, *colours) && SameContents(expectedSpeculars, *speculars)));
    ei::verify::Check("CLinkMesh::FlatColours (004F3AD0)", true, same, "%s: %d / %d, sizes %d / %d", name->data(),
                      expected ? 1 : 0, result ? 1 : 0, expectedColours.size, colours->size);
    expectedColours.FreeData();
    expectedSpeculars.FreeData();
    return result;
}

// ---- The transform object's tap ----

constexpr int kPipelineSlots = 41;
constexpr int kProjectFlatSlot = 29;
constexpr int kLightMeshSlot = 40;

struct PipelineTap {
    void** vtable;
    ITransformPipeline* real;
    bool forward;
    int calls;
    std::uint32_t arguments[10];
    std::uint32_t positionSum;
};

std::uint32_t PositionSum(const VertexPacket* packets, int blocks) {
    std::uint32_t sum = 0;
    for (int b = 0; b < blocks; ++b) {
        std::uint32_t rows[12];
        std::memcpy(rows, &packets[b], sizeof(rows));   // the w row is not always written
        for (std::uint32_t word : rows) sum = sum * 31 + word;
    }
    return sum;
}

void __fastcall TapLightMesh(PipelineTap* tap, void*, const void* vertices, const void* normals, std::uint32_t* colours,
                             const void* words, int wordCount, CLight** lights, int lightCount,
                             const std::uint32_t* vector) {
    ++tap->calls;
    const std::uint32_t arguments[10] = {Word(vertices), Word(normals), Word(colours), Word(words),
                                         static_cast<std::uint32_t>(wordCount), Word(lights),
                                         static_cast<std::uint32_t>(lightCount), vector[0], vector[1], vector[2]};
    std::memcpy(tap->arguments, arguments, sizeof(arguments));
    if (tap->forward) tap->real->LightMesh(vertices, normals, colours, words, wordCount, lights, lightCount, vector);
}

void __fastcall TapProjectFlat(PipelineTap* tap, void*, const float* matrix, const VertexPacket* vertices,
                               float* points, int blocks, float* bounds) {
    ++tap->calls;
    const std::uint32_t arguments[10] = {Word(matrix), Word(points), static_cast<std::uint32_t>(blocks), Word(bounds)};
    std::memcpy(tap->arguments, arguments, sizeof(arguments));
    tap->positionSum = PositionSum(vertices, blocks);
    if (tap->forward) tap->real->ProjectFlat(matrix, vertices, points, blocks, bounds);
}

// No other slot is called by the two functions.
void __fastcall TapUnexpected() { ei::verify::Report("CLinkMesh draw: unexpected transform slot", false, ""); }

void** PipelineTapVtable() {
    static void* table[kPipelineSlots];
    if (!table[0]) {
        for (void*& slot : table) slot = reinterpret_cast<void*>(&TapUnexpected);
        table[kProjectFlatSlot] = reinterpret_cast<void*>(&TapProjectFlat);
        table[kLightMeshSlot] = reinterpret_cast<void*>(&TapLightMesh);
    }
    return table;
}

bool SameCalls(const PipelineTap& a, const PipelineTap& b) {
    return a.calls == b.calls && a.positionSum == b.positionSum &&
           std::memcmp(a.arguments, b.arguments, sizeof(a.arguments)) == 0;
}

// Runs `ours` with the mesh's transform object tapped and forwarding, then `original` recorded
// only; the two records.
template <typename Ours, typename Original>
bool TappedPipeline(CLinkMesh* self, Ours ours, Original original) {
    ITransformPipeline* real = self->pipeline_;
    PipelineTap tap{PipelineTapVtable(), real, true, 0, {}, 0};
    self->pipeline_ = reinterpret_cast<ITransformPipeline*>(&tap);
    ours();
    PipelineTap first = tap;
    tap.forward = false;
    tap.calls = 0;
    tap.positionSum = 0;
    std::memset(tap.arguments, 0, sizeof(tap.arguments));
    original();
    self->pipeline_ = real;
    return SameCalls(first, tap);
}

// ---- 004F3870 ----

// PUSH EBP (1), MOV EBP,ESP (2), SUB ESP,0x1C (3).
bool(__fastcall* s_lightPart)(CLinkMesh*, void*, const stlp::string*, const FigureLights*, FigureMaterial*, int,
                              ColourArray*, ColourArray*);
bool __fastcall LightPartVerify(CLinkMesh* self, void*, const stlp::string* name, const FigureLights* lights,
                                FigureMaterial* material, int alpha, ColourArray* colours, ColourArray* speculars) {
    bool result = false;
    bool expected = false;
    std::vector<std::uint32_t> ourSpeculars;
    int ourSizes[2] = {};
    bool sameCalls = TappedPipeline(
        self,
        [&] {
            result = self->CLinkMesh::LightPart(name, lights, material, alpha, colours, speculars);
            if (!result) return;
            ourSizes[0] = colours->size;
            ourSizes[1] = speculars->size;
            ourSpeculars.assign(speculars->data, speculars->data + (speculars->size > 0 ? speculars->size : 0));
        },
        [&] { expected = s_lightPart(self, nullptr, name, lights, material, alpha, colours, speculars); });
    bool same = sameCalls && expected == result;
    if (same && result) {
        same = ourSizes[0] == colours->size && ourSizes[1] == speculars->size &&
               (ourSpeculars.empty() ||
                std::memcmp(ourSpeculars.data(), speculars->data, ourSpeculars.size() * 4) == 0);
    }
    ei::verify::Check("CLinkMesh::LightPart (004F3870)", true, same, "%s: %d / %d, calls same %d, sizes %d %d",
                      name->data(), expected ? 1 : 0, result ? 1 : 0, sameCalls ? 1 : 0, colours->size,
                      speculars->size);
    return result;
}

// ---- 004F4680 ----

// PUSH ECX (1), MOV EAX,[ESP+8] (4).
int(__fastcall* s_projectPart)(CLinkMesh*, void*, const stlp::string*, OffsetArray*, const float*, float*, float*);
int __fastcall ProjectPartVerify(CLinkMesh* self, void*, const stlp::string* name, OffsetArray* offsets,
                                 const float* matrix, float* points, float* bounds) {
    int result = 0;
    int expected = 0;
    bool sameCalls = TappedPipeline(
        self, [&] { result = self->CLinkMesh::ProjectPart(name, offsets, matrix, points, bounds); },
        [&] { expected = s_projectPart(self, nullptr, name, offsets, matrix, points, bounds); });
    ei::verify::Check("CLinkMesh::ProjectPart (004F4680)", true, sameCalls && expected == result,
                      "%s: %d / %d, calls same %d, %d offsets", name->data(), expected, result, sameCalls ? 1 : 0,
                      offsets->size);
    return result;
}

// ---- The draws ----

struct DrawOutcome {
    int result;
    meshcapture::Draw draw;
    bool hasHeader;
    RECT screen;
};

template <typename Run>
DrawOutcome TappedDraw(CLinkMesh* self, const stlp::string* name, meshcapture::Mode mode, int skippedResult, Run run) {
    meshcapture::State& tap = meshcapture::Tap();
    tap.mode = mode;
    tap.result = skippedResult;
    std::memset(&tap.draw, 0, sizeof(tap.draw));
    DrawOutcome outcome{};
    outcome.result = run();
    tap.mode = meshcapture::Mode::Off;
    outcome.draw = tap.draw;
    const LinkHeaderMap::Node* node = self->headers_.find(*name);
    outcome.hasHeader = node != self->headers_.end();
    if (outcome.hasHeader) outcome.screen = node->second.screen;
    return outcome;
}

const char* DrawDiff(const DrawOutcome& expected, const DrawOutcome& actual) {
    if (expected.result != actual.result) return "result";
    if (expected.draw.calls != actual.draw.calls) return "draw calls";
    if (expected.draw.textured != actual.draw.textured) return "textured";
    if (std::memcmp(expected.draw.textures, actual.draw.textures, sizeof(expected.draw.textures)) != 0) return "textures";
    if (std::memcmp(expected.draw.arguments, actual.draw.arguments, sizeof(expected.draw.arguments)) != 0)
        return "arguments";
    if (expected.draw.positionSum != actual.draw.positionSum) return "positions";
    if (expected.hasHeader != actual.hasHeader) return "header";
    if (expected.hasHeader && std::memcmp(&expected.screen, &actual.screen, sizeof(RECT)) != 0) return "screen";
    return nullptr;
}

// Ours for real, then the original with the device's draw skipped.
template <typename Ours, typename Original>
int CheckDraw(const char* what, CLinkMesh* self, const stlp::string* name, int flags, Ours ours, Original original) {
    DrawOutcome actual = TappedDraw(self, name, meshcapture::Mode::Record, 0, ours);
    DrawOutcome expected = TappedDraw(self, name, meshcapture::Mode::Skip, actual.result, original);
    const char* diff = DrawDiff(expected, actual);
    ei::verify::Check(what, true, diff == nullptr, "%s flags %x: %s (%d / %d)", name->data(), flags, diff ? diff : "",
                      expected.result, actual.result);
    return actual.result;
}

// 004F3C60: MOV EAX,[ESP+4] (4), SUB ESP,0x10 (3).
int(__fastcall* s_drawMoved)(CLinkMesh*, void*, const stlp::string*, int, ColourArray*, ColourArray*, OffsetArray*,
                             std::uint32_t, std::uint32_t);
int __fastcall DrawPartMovedVerify(CLinkMesh* self, void*, const stlp::string* name, int flags, ColourArray* colours,
                                   ColourArray* speculars, OffsetArray* offsets, std::uint32_t u, std::uint32_t v) {
    return CheckDraw(
        "CLinkMesh::DrawPartMoved (004F3C60)", self, name, flags,
        [&] { return self->CLinkMesh::DrawPartMoved(name, flags, colours, speculars, offsets, u, v); },
        [&] { return s_drawMoved(self, nullptr, name, flags, colours, speculars, offsets, u, v); });
}

// 004F3FA0: MOV EAX,[ESP+4] (4), SUB ESP,0x10 (3).
int(__fastcall* s_draw)(CLinkMesh*, void*, const stlp::string*, int, ColourArray*, ColourArray*, std::uint32_t,
                        std::uint32_t);
int __fastcall DrawPartVerify(CLinkMesh* self, void*, const stlp::string* name, int flags, ColourArray* colours,
                              ColourArray* speculars, std::uint32_t u, std::uint32_t v) {
    return CheckDraw(
        "CLinkMesh::DrawPart (004F3FA0)", self, name, flags,
        [&] { return self->CLinkMesh::DrawPart(name, flags, colours, speculars, u, v); },
        [&] { return s_draw(self, nullptr, name, flags, colours, speculars, u, v); });
}

// 004F42D0: MOV EAX,[ESP+4] (4), SUB ESP,0x10 (3).
int(__fastcall* s_drawMovedTextured)(CLinkMesh*, void*, const stlp::string*, int, CDXSurface*, CDXSurface*,
                                     ColourArray*, ColourArray*, OffsetArray*, std::uint32_t, std::uint32_t);
int __fastcall DrawPartMovedTexturedVerify(CLinkMesh* self, void*, const stlp::string* name, int flags,
                                           CDXSurface* first, CDXSurface* second, ColourArray* colours,
                                           ColourArray* speculars, OffsetArray* offsets, std::uint32_t u,
                                           std::uint32_t v) {
    return CheckDraw(
        "CLinkMesh::DrawPartMovedTextured (004F42D0)", self, name, flags,
        [&] {
            return self->CLinkMesh::DrawPartMovedTextured(name, flags, first, second, colours, speculars, offsets, u, v);
        },
        [&] {
            return s_drawMovedTextured(self, nullptr, name, flags, first, second, colours, speculars, offsets, u, v);
        });
}

// 004F4150: MOV EAX,[ESP+4] (4), SUB ESP,8 (3).
int(__fastcall* s_drawTextured)(CLinkMesh*, void*, const stlp::string*, int, CDXSurface*, CDXSurface*, ColourArray*,
                                ColourArray*, std::uint32_t, std::uint32_t);
int __fastcall DrawPartTexturedVerify(CLinkMesh* self, void*, const stlp::string* name, int flags, CDXSurface* first,
                                      CDXSurface* second, ColourArray* colours, ColourArray* speculars,
                                      std::uint32_t u, std::uint32_t v) {
    return CheckDraw(
        "CLinkMesh::DrawPartTextured (004F4150)", self, name, flags,
        [&] { return self->CLinkMesh::DrawPartTextured(name, flags, first, second, colours, speculars, u, v); },
        [&] { return s_drawTextured(self, nullptr, name, flags, first, second, colours, speculars, u, v); });
}

// ---- 004F4850 ----

constexpr int kConverterSlots = 9;
constexpr int kRasteriseSlot = 8;
constexpr std::size_t kTriangleBytes = 0x30;

struct ConverterTap {
    void** vtable;
    CPixelConverter* real;
    bool forward;
    int calls;
    std::uint32_t sum;
};

void __fastcall TapRasterise(ConverterTap* tap, void*, void* target) {
    ++tap->calls;
    std::uint32_t words[kTriangleBytes / 4];
    std::memcpy(words, target, sizeof(words));
    for (std::uint32_t word : words) tap->sum = tap->sum * 31 + word;
    if (tap->forward) tap->real->RasteriseTriangle(target);
}

void** ConverterTapVtable() {
    static void* table[kConverterSlots];
    if (!table[0]) {
        for (void*& slot : table) slot = reinterpret_cast<void*>(&TapUnexpected);
        table[kRasteriseSlot] = reinterpret_cast<void*>(&TapRasterise);
    }
    return table;
}

// PUSH EBP (1), MOV EBP,ESP (2), SUB ESP,0x1C (3).
int(__fastcall* s_rasterisePart)(CLinkMesh*, void*, const stlp::string*, int*, void*);
int __fastcall RasterisePartVerify(CLinkMesh* self, void*, const stlp::string* name, int* pixels, void* target) {
    CTextureDevice* device = self->loader_->renderer_;
    void* real = device->converter_;
    ConverterTap tap{ConverterTapVtable(), static_cast<CPixelConverter*>(real), true, 0, 0};
    device->converter_ = &tap;
    int result = self->CLinkMesh::RasterisePart(name, pixels, target);
    ConverterTap ours = tap;
    tap.forward = false;
    tap.calls = 0;
    tap.sum = 0;
    int expected = s_rasterisePart(self, nullptr, name, pixels, target);
    device->converter_ = real;
    bool same = expected == result && ours.calls == tap.calls && ours.sum == tap.sum;
    ei::verify::Check("CLinkMesh::RasterisePart (004F4850)", true, same, "%s: %d / %d, %d / %d triangles", name->data(),
                      expected, result, tap.calls, ours.calls);
    return result;
}

} // namespace

EI_VERIFY(0x004F3AD0, FlatColoursVerify, 6, s_flatColours);
EI_VERIFY(0x004F3870, LightPartVerify, 6, s_lightPart);
EI_VERIFY(0x004F3C60, DrawPartMovedVerify, 7, s_drawMoved);
EI_VERIFY(0x004F3FA0, DrawPartVerify, 7, s_draw);
EI_VERIFY(0x004F42D0, DrawPartMovedTexturedVerify, 7, s_drawMovedTextured);
EI_VERIFY(0x004F4150, DrawPartTexturedVerify, 7, s_drawTextured);
EI_VERIFY(0x004F4680, ProjectPartVerify, 5, s_projectPart);
EI_VERIFY(0x004F4850, RasterisePartVerify, 6, s_rasterisePart);
