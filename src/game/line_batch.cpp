// The renderer's line lists (renderer_lines.h): one function appends a point, another projects a list and
// hands the strip to the device object's line draw (renderer_lines.cpp). Debug drawing: the figure parts'
// bounds boxes and the world client's line lists. See docs/notes/renderer.md.
#include "renderer_lines.h"

#include "link_figure.h"
#include "model.h"

#include <vector>

int LineBatch::AddPoint(const float* point) {
    if (count == std::ssize(packets) * 4) packets.resize(std::ssize(packets) + 1);
    auto put = [this, point](int index) {
        LinePacket& packet = packets[index >> 2];
        int slot = index & 3;
        packet.x[slot] = point[0];
        packet.y[slot] = point[1];
        packet.z[slot] = point[2];
        packet.w[slot] = 1.0f;
    };
    put(count);
    ++count;
    for (int index = count; index < std::ssize(packets) * 4; ++index) put(index);
    return count;
}

int LineBatch::Draw(CTransformPipeline* pipeline, CTextureDevice* device) {
    std::vector<ProjectedPoint>& projected = LineProjected();
    projected.resize(std::ssize(packets) * 4);
    LineOutcodes().SetSize(std::ssize(packets) * 4, -1);
    int visibility = pipeline->transform_->ProjectClipped(pipeline->Top(CTransformPipeline::kClipStack), packets.data(),
                                                         projected.data(), LineOutcodes().data, std::ssize(packets));
    if (visibility == 0) {
        const ProjectedPoints points{projected.data(), std::ssize(projected)};
        DeviceDrawLineStrip(device, &points, colour);
    }
    return 1;
}
