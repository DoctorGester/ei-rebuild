// The camera's mouse turn traced (camera_trace.h; docs/notes/ui.md, "Camera mouse turn").
#include "game/ui/camera_trace.h"

#include "engine/scene/camera.h"
#include "game/app/singletons.h"

#include "platform/log.h"
#include "platform/platform.h"
#include "lib/maths.h"

#include <algorithm>
#include <cmath>

namespace camera_trace {

namespace {

// The second's frames: their count, the shortest and longest poll intervals, the motion summed.
struct Second {
    std::uint32_t start = 0;
    int frames = 0;
    std::uint32_t shortest = 0xFFFFFFFFu;
    std::uint32_t longest = 0;
    Point motion{};
};
Second g_second;
int g_drags = 0;    // the drags taken since the last poll

float Heading() {
    const Vector3 x = g_camera.viewRotation.AxisX();
    return lib::Atan2(x.y, x.x);
}

} // namespace

bool On() {
    static const bool on = !platform::EnvironmentVariable("EI_CAMERA_TRACE").empty();
    return on;
}

void Poll(std::uint32_t now, std::uint32_t interval, Point motion, Point drag, bool held) {
    if (!On()) return;
    if (g_second.frames == 0) g_second.start = now;
    ++g_second.frames;
    g_second.shortest = std::min(g_second.shortest, interval);
    g_second.longest = std::max(g_second.longest, interval);
    g_second.motion = g_second.motion + motion;
    if (held || motion.x != 0 || motion.y != 0) {
        ei::Log("camera trace: poll (%s) %u +%u ms motion %d %d drag %d %d, %d drags taken; heading %.5f pitch %.5f, turn %.5f "
                "(target %.5f) tilt %.5f (target %.5f)",
                held ? "held" : "free", now, interval, motion.x, motion.y, drag.x, drag.y, g_drags, Heading(), g_camera.pitch,
                g_camera.turnRate, g_camera.turnTarget, g_camera.tiltRate, g_camera.tiltTarget);
    }
    g_drags = 0;
    if (now - g_second.start < 1000) return;
    ei::Log("camera trace: %d frames in %u ms (%u to %u ms apart), motion %d %d", g_second.frames, now - g_second.start,
            g_second.shortest, g_second.longest, g_second.motion.x, g_second.motion.y);
    g_second = Second{};
}

void Drag(Point drag, float layoutX, float layoutY) {
    if (!On()) return;
    ++g_drags;
    if (drag.x == 0 && drag.y == 0) return;
    ei::Log("camera trace: drag %d %d (layout %.3f %.3f) at %u: turn target %.5f tilt target %.5f", drag.x, drag.y, layoutX,
            layoutY, platform::FrameClock(), g_camera.turnTarget, g_camera.tiltTarget);
}

void MapDrag(Point drag, float layoutX, float angle) {
    if (!On()) return;
    if (drag.x == 0 && drag.y == 0) return;
    ei::Log("camera trace: global map drag %d %d (layout %.3f) at %u: angle %.5f", drag.x, drag.y, layoutX,
            platform::FrameClock(), angle);
}

} // namespace camera_trace
