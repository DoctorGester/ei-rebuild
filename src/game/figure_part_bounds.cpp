// The finishing call of the part draws. With the debug flag 0x4000 of the owning manager's context set, the
// mesh's clip box is drawn as lines through the renderer: the corners are the bounds' centre plus, per axis,
// either the box minimum or the maximum; eight edges, two points each, in the original's order. See
// docs/notes/models.md ("Figure parts").
#include "link_figure.h"   // FigureMeshDrawer
#include "model.h"         // SceneRenderer
#include "renderer_lines.h"   // LineBatch

#include "engine_object.h"

#include <cstdint>

namespace {

// The owning manager's context (object.cpp; the world, an engine object), and appending a point.
EngineObject* ContextOf(FigureMeshDrawer* node) { return reinterpret_cast<EngineObject*>(node->ManagerContext()); }

constexpr std::uint32_t kShowBounds = 0x4000;   // the context's flags

} // namespace

void FigureMeshDrawer::DrawBounds(SceneRenderer* renderer) {
    if (!(ContextOf(this)->flags & kShowBounds)) return;
    LineBatch lines{{}, 0, 0xFFFF0000u};
    const ClipBounds& bounds = mesh->box.bounds;
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
    for (int index : kOrder) lines.AddPoint(corner[index]);
    renderer->DrawLineList(&lines);
    lib::FreeStorage(lines.packets);
}
