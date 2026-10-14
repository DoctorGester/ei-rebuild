// 005A7290 (thiscall on a figure part's scene node, RET 4): the finishing call of the part draws
// (005A7640, 005A7A70). With the debug flag 0x4000 of the owning manager's context (+8) set, the
// mesh's clip box is drawn as lines through the renderer (slot 15): the corners are the bounds'
// centre plus, per axis, either the box minimum or the maximum; eight edges, two points each, in
// the original's order. See docs/notes/models.md ("Figure parts").
#include "link_figure.h"   // FigureMeshDrawer
#include "model.h"         // CSceneRenderer
#include "renderer_lines.h"   // LineBatch

#include "lib/crt.h"
#include "ei/hook.h"
#include "engine_object.h"
#include "originals_engine.h"
#include "originals_library.h"

#include <cstdint>

namespace {

// 00455940 the owning manager's context (object.cpp; the world, an engine object). 0049B140 (RET 4)
// appends a point.
CEngineObject* ContextOf(FigureMeshDrawer* node) { return static_cast<CEngineObject*>(ManagerContextOf(node)); }
__declspec(naked) void __fastcall AddPoint(LineBatch*, void*, const float*) EI_ORIGINAL(0x0049B140)

constexpr std::uint32_t kShowBounds = 0x4000;   // the context's flags_

} // namespace

#pragma float_control(precise, on, push)

void __fastcall DrawPartBounds(FigureMeshDrawer* node, void*, CSceneRenderer* renderer) {
    if (!(ContextOf(node)->flags_ & kShowBounds)) return;
    LineBatch lines{-1, {nullptr, 0, 0, 0}, 0, 0xFFFF0000u, 0};
    const ClipBounds& bounds = node->mesh_->bounds_;
    const float* origin = &bounds.center.x;
    const float* nearExtent = &bounds.boxMin.x;
    const float* farExtent = &bounds.boxMax.x;
    // corner[i]: bit 2 near x, bit 1 near y, bit 0 near z (else far).
    float corner[8][3];
    for (int i = 0; i < 8; ++i) {
        corner[i][0] = origin[0] + ((i & 4) ? nearExtent[0] : farExtent[0]);
        corner[i][1] = origin[1] + ((i & 2) ? nearExtent[1] : farExtent[1]);
        corner[i][2] = origin[2] + ((i & 1) ? nearExtent[2] : farExtent[2]);
    }
    static const int kOrder[16] = {3, 1, 0, 2, 6, 4, 0, 1, 5, 4, 5, 7, 6, 7, 3, 2};
    for (int index : kOrder) AddPoint(&lines, nullptr, corner[index]);
    renderer->DrawLineList(&lines);
    lines.packets.FreeData();
}

#pragma float_control(pop)

EI_REPLACE(0x005A7290, DrawPartBounds);

// 005A75D0 (A-W14): the line batch's destructor: spare00 -1, the points' block freed, the array
// empty. Only the unwind funclet 00726C10 of the original DrawPartBounds calls it (the rewritten
// one frees its batch itself).
void __fastcall LineBatchDestruct(void* batch) {
    auto* self = static_cast<LineBatch*>(batch);
    self->spare00 = -1;
    self->packets.RemoveAll();
}
EI_REPLACE(0x005A75D0, LineBatchDestruct);
