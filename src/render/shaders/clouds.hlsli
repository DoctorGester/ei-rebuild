// The global map's clouds (render_compose.cpp, render::DrawClouds), the block of both programs: texture_clouds_frame
// builds one 256 x 256 frame of grey levels (R8UI) from the random words of its four layers (global_map_clouds.cpp
// keeps them), integer for integer as the game's CPU code did; texture_clouds blends two frames and takes each
// texel's alpha from the ramp. The words are unsigned: the block holds them as ints (as sokol's uniform types) and the
// code reads them back as uints, bit for bit.
#include "rect_vs.hlsli"
#ifdef EI_FRAGMENT
struct CloudParams {
    int4 words[3];   // the frame: layer k's a, b, c are words 3k .. 3k + 2
    int4 blend;      // the blend: x the weight of `to` out of 256
    int4 ramp[64];   // the blend: the alpha (0..255) of grey level g in ramp[g / 4][g % 4]
};
ConstantBuffer<CloudParams> clouds : FS_BLOCK(0);
#endif
