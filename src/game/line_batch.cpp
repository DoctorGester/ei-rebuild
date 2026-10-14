// The renderer's line lists (renderer_lines.h; A-E1): 0049B140 appends a point, 0049B780 projects
// a list and hands the strip to the device object's line draw (0049B4B0, renderer_lines.cpp). Debug
// drawing: the figure parts' bounds boxes and the world client's line lists. See
// docs/notes/renderer.md.
#include "renderer_lines.h"

#include "link_figure.h"
#include "model.h"
#include "originals_library.h"

#include "ei/hook.h"

int LineBatch::AddPoint(const float* point) {
    if (count == packets.size * 4) packets.SetSize(packets.size + 1);
    auto put = [this, point](int index) {
        LinePacket& packet = packets.data[index >> 2];
        int slot = index & 3;
        packet.x[slot] = point[0];
        packet.y[slot] = point[1];
        packet.z[slot] = point[2];
        packet.w[slot] = 1.0f;
    };
    put(count);
    ++count;
    for (int index = count; index < packets.size * 4; ++index) put(index);
    return count;
}
EI_REPLACE_METHOD(0x0049B140, LineBatch::AddPoint);

int LineBatch::Draw(CTransformPipeline* pipeline, CTextureDevice* device) {
    lib::AlignedArray<ProjectedPoint>& projected = LineProjected();
    projected.SetSizeDiscard(packets.size * 4);
    ByteArraySetSizeOriginal(&LineOutcodes(), nullptr, packets.size * 4, -1);
    int visibility = pipeline->transform_->ProjectClipped(pipeline->Top(CTransformPipeline::kClipStack), packets.data,
                                                         projected.data, LineOutcodes().data, packets.size);
    if (visibility == 0) DeviceDrawLineStrip(device, reinterpret_cast<const ProjectedPoints*>(&projected), colour);
    return 1;
}
EI_REPLACE_METHOD(0x0049B780, LineBatch::Draw);
