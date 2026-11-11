// The global map's cloud layer: see global_map_clouds.h. The clouds themselves are the renderer's
// (shaders/map_clouds.hlsl).
#include "game/ui/global_map_clouds.h"

#include "engine/core/random.h"

#include <cmath>

void GlobalMapClouds::Start() { seed = g_clientRandom.Next(); }

void GlobalMapClouds::Draw(const Surface* holder, float seconds) {
    time = std::fmod(time + static_cast<double>(seconds) * speed, render::kMapCloudsPeriod);
    render::DrawClouds(holder, {static_cast<float>(time), start, width, seed});
}

void GlobalMapClouds::SetRamp(int newStart, int newWidth, float newSpeed) {
    start = newStart;
    width = newWidth;
    speed = newSpeed;
}
