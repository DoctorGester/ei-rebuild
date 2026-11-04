// The screen-space ambient occlusion (render_occlusion.cpp), at half the frame's size from the depth of the world's
// ground and models drawn there first: per texel, how much of the sky above its surface the depth around it hides
// (1 open .. 0 closed), by horizons as HBAO finds them. The texel's point and the points around it are the depth
// taken back into the landscape's space; in each of four directions on the screen the steepest point within the
// radius above the surface's own plane, faded with its distance, is the horizon. The directions turn and the steps
// shift per texel by a 4 x 4 pattern fixed to the screen, which occlusion_blur.hlsl averages away.
#include "fullscreen.hlsli"
#ifdef EI_FRAGMENT
struct OcclusionParams {
    row_major float4x4 fromClip;   // the camera's clip space (x, y, depth, 1) to the landscape's
    float4 size;                   // xy: the texels, zw: their inverse
    float4 eye;                    // xyz: the camera (the landscape's space)
    float4 reach;                  // x: the radius (world units), y: 1 / its square, z: texels per world unit at a
                                   // depth of 1, w: the widest radius in texels
    float4 depthMap;               // a depth z at the clip w = y / (z - x); z: the strength, w: the angle's bias
};
ConstantBuffer<OcclusionParams> params : FS_BLOCK(0);
Texture2D<float> depth : FS_TEXTURE(0);
SamplerState point_smp : FS_SAMPLER(0);   // unused: the depth is read with Load

static const int kDirections = 4;
static const int kSteps = 4;
// A 4 x 4 ordered pattern (each value 0 .. 15 once): the directions' turn and, reordered, the steps' shift.
static const float kPattern[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

float3 point_at(int2 texel, float z) {
    float2 ndc = (float2(texel) + 0.5) * params.size.zw * float2(2.0, -2.0) + float2(-1.0, 1.0);
    float4 p = float4(ndc, z, 1.0);
    float4 q = float4(dot(params.fromClip[0], p), dot(params.fromClip[1], p), dot(params.fromClip[2], p),
                      dot(params.fromClip[3], p));
    return q.xyz / q.w;
}

float depth_at(int2 texel) { return depth.Load(int3(clamp(texel, int2(0, 0), int2(params.size.xy) - 1), 0)); }

float ps_main(float4 pos : SV_Position) : SV_Target0 {
    int2 c = int2(pos.xy);
    float z = depth_at(c);
    if (z >= 1.0) return 1.0;   // nothing drawn: the sky
    float3 p = point_at(c, z);
    // The surface's normal from the nearer neighbour on each axis (the other may lie across an edge).
    float zl = depth_at(c - int2(1, 0)), zr = depth_at(c + int2(1, 0));
    float zu = depth_at(c - int2(0, 1)), zd = depth_at(c + int2(0, 1));
    float3 dx = abs(zr - z) < abs(z - zl) ? point_at(c + int2(1, 0), zr) - p : p - point_at(c - int2(1, 0), zl);
    float3 dy = abs(zd - z) < abs(z - zu) ? point_at(c + int2(0, 1), zd) - p : p - point_at(c - int2(0, 1), zu);
    float3 n = cross(dx, dy);
    float len = length(n);
    if (len < 1e-12) return 1.0;
    n /= len;
    if (dot(n, params.eye.xyz - p) < 0.0) n = -n;
    float w = params.depthMap.y / (z - params.depthMap.x);
    float radius = min(params.reach.x * params.reach.z / w, params.reach.w);
    if (radius < 1.0) return 1.0;   // the radius under a texel: nothing to find
    int k = (c.x & 3) + (c.y & 3) * 4;
    float turn = (kPattern[k] + 0.5) / 16.0;
    float shift = (kPattern[(k * 7 + 5) & 15] + 0.5) / 16.0;
    float hidden = 0.0;
    [unroll] for (int d = 0; d < kDirections; ++d) {
        float a = (d + turn) * (6.2831853 / kDirections);
        float2 dir = float2(cos(a), sin(a));
        float horizon = 0.0;
        [unroll] for (int s = 0; s < kSteps; ++s) {
            float2 at = pos.xy + dir * max(1.0, radius * (s + shift) / kSteps);
            int2 t = int2(floor(at));
            if (any(t < 0) || any(t >= int2(params.size.xy))) break;
            float zs = depth.Load(int3(t, 0));
            if (zs >= 1.0) continue;
            float3 h = point_at(t, zs) - p;
            float h2 = dot(h, h);
            if (h2 < 1e-8) continue;
            float rise = dot(h, n) * rsqrt(h2) - params.depthMap.w;
            horizon = max(horizon, saturate(rise) * saturate(1.0 - h2 * params.reach.y));
        }
        hidden += horizon;
    }
    return saturate(1.0 - params.depthMap.z * hidden / kDirections);
}
#endif
