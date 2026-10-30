// The renderer's line lists (renderer_lines.h): one function appends a point, another projects a list and
// hands the strip to the device object's line draw (renderer_lines.cpp). Debug drawing: the figure parts'
// bounds boxes and the world client's line lists. See docs/notes/renderer.md.
#include "figure_mesh.h"   // VertexPacket
#include "model.h"
#include "renderer_lines.h"
#include "lib/containers.h"

#include <vector>

int LineBatch::AddPoint(const float* point) {
    if (count == std::ssize(packets) * 4) packets.resize(std::ssize(packets) + 1);
    auto put = [this, point](int index) {
        VertexPacket& packet = packets[index >> 2];
        int slot = index & 3;
        packet.x()[slot] = point[0];
        packet.y()[slot] = point[1];
        packet.z()[slot] = point[2];
        packet.w()[slot] = 1.0f;
    };
    put(count);
    ++count;
    for (int index = count; index < std::ssize(packets) * 4; ++index) put(index);
    return count;
}

int LineBatch::Draw(TransformPipeline* pipeline, TextureDevice* device) {
    std::vector<Vector4>& projected = g_lineProjected;
    projected.resize(std::ssize(packets) * 4);
    g_lineOutcodes.resize(std::ssize(packets) * 4);
    int visibility = pipeline->ProjectClipped(*pipeline->Top(TransformPipeline::kClipStack), packets.data(),
                                              projected.data(), g_lineOutcodes.data(), lib::Count(packets));
    if (visibility == 0) {
        const ProjectedPoints points{projected.data(), static_cast<int>(lib::Count(projected))};
        DeviceDrawLineStrip(device, &points, colour);
    }
    return 1;
}
