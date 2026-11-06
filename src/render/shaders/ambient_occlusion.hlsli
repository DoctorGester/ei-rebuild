// The ambient occlusion's receivers' part (included in the terrain and mesh programs, which name its slots:
// OCCLUSION_MAP_SLOT, then OCCLUSION_DEPTH_SLOT): the half-size occlusion (render_occlusion.cpp) at a frame pixel,
// from the four texels around it weighted by how near their depth lies to the pixel's own. A pixel whose surface is
// none of theirs (drawn after them without writing the depth, or in front of what they show) takes none.
#include "bindings.hlsli"
Texture2D<float> occlusion_map : FS_TEXTURE(OCCLUSION_MAP_SLOT);
SamplerState occlusion_smp : FS_SAMPLER(OCCLUSION_MAP_SLOT);   // nearest, clamped
Texture2D<float> occlusion_depth : FS_TEXTURE(OCCLUSION_DEPTH_SLOT);
SamplerState occlusion_depth_smp : FS_SAMPLER(OCCLUSION_DEPTH_SLOT);

// How much the occlusion also darkens the lit colour (sun and point lights as well as the ambient light it already
// takes away): the user's pick from live tuning (2026-11-06). The receivers multiply by occlusion_direct().
static const float kOcclusionDirect = 0.5;
float occlusion_direct(float occlusion) { return lerp(1.0, occlusion, kOcclusionDirect); }

// The occlusion of the ambient light at frame pixel `pixel` (SV_Position.xy) of depth `z`: 1 none. `on`: the occlusion
// is taken; `depthOffset`, `depthScale`: a depth's clip w (w = depthScale / (depth - depthOffset)); `scale`: the
// half-size texels per frame pixel, `texel`: their inverse size. The receivers' blocks hold these (render_internal.h's
// OcclusionReceiver) as occlusionOn, occlusionDepthOffset, occlusionDepthScale, occlusionScale and occlusionTexel.
float ambient_occlusion(float2 pixel, float z, int on, float depthOffset, float depthScale, float2 scale, float2 texel) {
    if (on == 0) return 1.0;
    // The frame is drawn half a pixel to the right and down (Direct3D 7's pixel centres), the depth was not.
    float2 at = (pixel - 0.5) * scale;
    float2 uv = at * texel;
    float4 o = occlusion_map.GatherRed(occlusion_smp, uv);
    float4 d = occlusion_depth.GatherRed(occlusion_depth_smp, uv);
    // Gather's order: (0, 1), (1, 1), (1, 0), (0, 0) of the 2 x 2 texels around `uv`.
    float2 f = frac(at - 0.5);
    float4 near = float4((1.0 - f.x) * f.y, f.x * f.y, f.x * (1.0 - f.y), (1.0 - f.x) * (1.0 - f.y));
    float w = depthScale / (z - depthOffset);
    float4 apart = abs(depthScale / (d - depthOffset) - w) / w;
    float4 weight = near / (1.0 + apart * apart * 2500.0);
    float occlusion = dot(weight, o) / dot(weight, 1.0);
    // None of the four on the pixel's surface (the nearest more than 10 to 20% of its distance away): none.
    return lerp(occlusion, 1.0, saturate(min(min(apart.x, apart.y), min(apart.z, apart.w)) * 10.0 - 1.0));
}
